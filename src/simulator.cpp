#include "fast_simulator2/simulator.h"

// Plugin loading
#include "fast_simulator2/plugin.h"
#include "plugin_container.h"
#include <tue/filesystem/path.h>

// Object creation
#include <tue/config/loaders/yaml.h>
#include <ed/update_request.h>
#include <ed/relation.h>

#include <ed/world_model.h>
#include <ed/relations/transform_cache.h>

// Loading model files
#include <ros/package.h>
#include <fstream>

namespace sim
{

// ----------------------------------------------------------------------------------------------------

Simulator::Simulator() : world_(new ed::WorldModel())
{
    model_path_ = ros::package::getPath("fast_simulator2") + "/models";
}

// ----------------------------------------------------------------------------------------------------

Simulator::~Simulator()
{
    // Stop all plugins
    for(std::map<std::string, PluginContainerPtr>::iterator it = plugin_containers_.begin(); it != plugin_containers_.end(); ++it)
    {
        it->second.reset();
    }
}

// ----------------------------------------------------------------------------------------------------

tue::config::DataPointer Simulator::loadModelData(const std::string& type)
{
    std::map<std::string, std::string>::const_iterator it = models_.find(type);

    if (it != models_.end())
    {
        tue::Configuration cfg;
        tue::config::loadFromYAMLString(it->second, cfg);
        return cfg.data();
    }

    // Try loading model file
    tue::filesystem::Path model_dir(model_path_ + "/" + type + ".yaml");
    if (model_dir.exists())
    {
        std::ifstream f(model_dir.string().c_str());

        if (!f.is_open())
            return tue::config::DataPointer();

        std::stringstream buffer;
        buffer << f.rdbuf();

        std::string data_str = buffer.str();

        tue::Configuration cfg;
        tue::config::loadFromYAMLString(data_str, cfg);

        models_[type] = data_str;

        return cfg.data();
    }

    return tue::config::DataPointer();
}

// ----------------------------------------------------------------------------------------------------

void Simulator::createObject(LUId parent_id, tue::Configuration config, ed::UpdateRequest& req)
{
    // Check for the 'enabled' field. If it exists and the value is 0, omit this object. This allows
    // the user to easily enable and disable certain objects with one single flag.
    int enabled;
    if (config.value("enabled", enabled, tue::OPTIONAL) && !enabled)
        return;   

    std::string type;
    if (config.value("type", type, tue::OPTIONAL))
    {
        tue::config::DataPointer model_data = loadModelData(type);
        if (!model_data.empty())
        {
            config.data().add(model_data);
        }
        else
        {
            // Try to load ED model
            std::stringstream error;
            if (!model_loader_.create(config.data(), req, error))
            {
                config.addError("While loading object type '" + type + "': " + error.str());
                return;
            }
        }
    }

    std::string id;
    if (!config.value("id", id))
        return;


    if (type.empty())
        type == "_UNKNOWN_";
    req.setType(id, type);

    // Optionally set another parent
    if (config.value("parent", parent_id.id, tue::OPTIONAL))
        req.setType(parent_id.id, "_UNKNOWN_");

    geo::Pose3D pose = geo::Pose3D::identity();
    if (config.readGroup("pose", tue::REQUIRED))
    {
        config.value("x", pose.t.x);
        config.value("y", pose.t.y);
        config.value("z", pose.t.z);

        double roll = 0, pitch = 0, yaw = 0;
        config.value("roll", roll, tue::OPTIONAL);
        config.value("pitch", pitch, tue::OPTIONAL);
        config.value("yaw", yaw, tue::OPTIONAL);
        pose.R.setRPY(roll, pitch, yaw);

        config.endGroup();
    }
    else
        return;

    // Add relation from parent to child
    boost::shared_ptr<ed::TransformCache> t1(new ed::TransformCache());
    t1->insert(ed::Time(-1), pose);  // TODO: choose proper time
    req.setRelation(parent_id.id, id, t1);

    tue::Configuration params;
    if (config.readGroup("properties"))
    {
        params = config;
        config.endGroup();
    }

    if (config.readArray("plugins"))
    {
        while (config.nextArrayItem())
        {
            std::string lib_filename;
            if (config.value("lib", lib_filename))
            {
                tue::Configuration plugin_cfg;
                plugin_cfg.setValue("_object", id);
                plugin_cfg.data().add(params.data());

                std::string load_error;
                loadPlugin(id + "-" + lib_filename, lib_filename, plugin_cfg, load_error);

                if (!load_error.empty())
                {
                    config.addError(load_error);
                }
            }
        }
        config.endArray();
    }

    //  Composition
    if (config.readArray("objects"))
    {
        while(config.nextArrayItem())
        {
            createObject(id, config, req);

        }
        config.endArray();
    }
}

// ----------------------------------------------------------------------------------------------------

void Simulator::configure(tue::Configuration config)
{
    ed::UpdateRequest req;

    if (config.readArray("models"))
    {
        while (config.nextArrayItem())
        {
            std::string name;
            if (config.value("name", name))
            {
                models_[name] = config.toYAMLString();
            }
        }
        config.endArray();
    }

    // set world root
    req.setType("world", "root");

    if (config.readArray("objects"))
    {
        while (config.nextArrayItem())
        {
            createObject(LUId("world"), config, req);
        }

        config.endArray();
    }

    if (!req.empty())
    {
        ed::WorldModelPtr world_updated = boost::make_shared<ed::WorldModel>(*world_);   // Create a world copy
        world_updated->update(req);
        world_ = world_updated;
    }
}

// ----------------------------------------------------------------------------------------------------

void Simulator::step(double dt)
{
    ed::WorldModelPtr world_updated;

    // collect all update requests
    std::vector<PluginContainerPtr> plugins_with_requests;
    for(std::map<std::string, PluginContainerPtr>::iterator it = plugin_containers_.begin(); it != plugin_containers_.end(); ++it)
    {
        const PluginContainerPtr& c = it->second;

        if (c->updateRequest())
        {
            if (!world_updated)
                world_updated = boost::make_shared<ed::WorldModel>(*world_);   // Create a world copy

            const ed::UpdateRequestConstPtr& req = c->updateRequest();
            if (req)
            {
                world_updated->update(*req);
                plugins_with_requests.push_back(c);
            }
        }
    }

    if (world_updated)
        world_ = world_updated; // Swap to updated world (if something changed)

    // Set the new (updated) world
    for(std::map<std::string, PluginContainerPtr>::iterator it = plugin_containers_.begin(); it != plugin_containers_.end(); ++it)
    {
        const PluginContainerPtr& c = it->second;
        c->setWorld(world_);
    }

    // Clear the requests of all plugins that had requests (which flags them to continue processing)
    for(std::vector<PluginContainerPtr>::iterator it = plugins_with_requests.begin(); it != plugins_with_requests.end(); ++it)
    {
        PluginContainerPtr c = *it;
        c->clearUpdateRequest();
    }
}

// ----------------------------------------------------------------------------------------------------

std::string Simulator::getFullLibraryPath(const std::string& lib)
{
    if (!lib.empty() && lib[0] == '/')
    {
        if (!tue::filesystem::Path(lib).exists())
            return "";

        return lib;
    }

    for(std::vector<std::string>::const_iterator it = plugin_paths_.begin(); it != plugin_paths_.end(); ++it)
    {
        std::string lib_file_test = *it + "/" + lib;
        if (tue::filesystem::Path(lib_file_test).exists())
            return lib_file_test;
    }

    return "";
}

// ----------------------------------------------------------------------------------------------------

PluginContainerPtr Simulator::loadPlugin(const std::string plugin_name, const std::string& lib_filename,
                                         tue::Configuration config, std::string& error)
{
    if (lib_filename.empty())
    {
        error += "Empty library file given.";
        return PluginContainerPtr();
    }

    std::string full_lib_file = getFullLibraryPath(lib_filename);

    if (full_lib_file.empty())
    {
        error += "Could not find library '" + lib_filename + "'.";
        return PluginContainerPtr();
    }

    PluginContainerPtr container(new PluginContainer());
    if (container->loadPlugin(plugin_name, full_lib_file, config, error))
    {
        plugin_containers_[plugin_name] = container;
        container->runThreaded();
        return container;
    }

    return PluginContainerPtr();
}

}
