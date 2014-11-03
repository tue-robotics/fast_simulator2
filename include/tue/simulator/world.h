#ifndef ED_SIMULATOR_WORLD_H_
#define ED_SIMULATOR_WORLD_H_

#include "object.h"
#include "id_map.h"

namespace sim
{

class World
{

public:

    World();

    virtual ~World();

    inline LUId addObject(const ObjectConstPtr& obj)
    {
        LUId id(obj->id());
        objects_.insert(id, obj);
        return id;
    }

    inline bool removeObject(const LUId& id) { return objects_.remove(id); }

    inline ObjectConstPtr object(const LUId& id) const { return objects_.get(id); }

    inline const std::vector<ObjectConstPtr>& objects() const { return objects_.getAll(); }

    LUId addTransform(const TransformConstPtr& t);

    void updateTransform(const LUId& id, const geo::Pose3D& pose);

    bool getTransform(const LUId& source, const LUId& target, geo::Pose3D& t) const;

private:

    IDMap<ObjectConstPtr> objects_;

    IDMap<TransformConstPtr> transforms_;

};

}

#endif
