#ifndef FAST_SIMULATOR2_PLUGIN_CONTAINER_H_
#define FAST_SIMULATOR2_PLUGIN_CONTAINER_H_

#include "fast_simulator2/types.h"
#include <boost/thread.hpp>
#include <tue/config/configuration.h>
#include "fast_simulator2/plugin.h"

#include <ed/types.h>

namespace class_loader { class ClassLoader; }

namespace sim
{

class PluginContainer
{

public:

    PluginContainer();

    virtual ~PluginContainer();

    PluginPtr loadPlugin(const std::string plugin_name, const std::string& lib_filename,
                    tue::Configuration config, std::string& error);

    PluginPtr plugin() const { return plugin_; }

    void runThreaded();

    void stop();

    const std::string& name() const { return plugin_->name(); }

    ed::UpdateRequestConstPtr updateRequest() const
    {
        boost::lock_guard<boost::mutex> lg(mutex_update_request_);
        return update_request_;
    }

    void clearUpdateRequest()
    {
        boost::lock_guard<boost::mutex> lg(mutex_update_request_);
        update_request_.reset();
    }

    void setWorld(const ed::WorldModelConstPtr& world)
    {
        boost::lock_guard<boost::mutex> lg(mutex_world_);
        world_new_ = world;
    }

    void setLoopFrequency(double freq) { loop_frequency_ = freq; }

protected:

    class_loader::ClassLoader*  class_loader_;

    PluginPtr plugin_;

    bool stop_;

    // 1.0 / cycle frequency
    double cycle_duration_;

    double loop_frequency_;

    mutable boost::mutex mutex_update_request_;

    ed::UpdateRequestPtr update_request_;

    boost::shared_ptr<boost::thread> thread_;

    bool step_finished_;

    double t_last_update_;

    mutable boost::mutex mutex_world_;

    ed::WorldModelConstPtr world_new_;

    ed::WorldModelConstPtr world_current_;

    // The object this plugin is attached to. Empty is not attached.
    LUId object_id_;

    void step();

    void run();

};

} // end namespace sim

#endif
