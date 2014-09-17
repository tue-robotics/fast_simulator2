#include "laser_range_finder.h"

#include "world.h"

namespace sim
{

// ----------------------------------------------------------------------------------------------------

LaserRangeFinder::LaserRangeFinder()
{
    // TODO: read from config
    lrf_.setNumBeams(1080);
    lrf_.setAngleLimits(-2, 2);
    lrf_.setRangeLimits(0, 30);
}

// ----------------------------------------------------------------------------------------------------

LaserRangeFinder::~LaserRangeFinder()
{
}

void LaserRangeFinder::sense(const World& world, const geo::Pose3D& sensor_pose) const
{
    std::vector<double> ranges;
    for(std::map<UUID, ObjectConstPtr>::const_iterator it = world.objects.begin(); it != world.objects.end(); ++it)
    {
        const ObjectConstPtr& obj = it->second;
        geo::ShapeConstPtr shape = obj->shape();

        if (shape)
        {
            lrf_.render(*shape, sensor_pose, obj->pose(), ranges);
        }
    }
}

}