#ifndef ED_SIMULATOR_LASER_RANGE_FINDER_H_
#define ED_SIMULATOR_LASER_RANGE_FINDER_H_

#include "object.h"

namespace sim
{

class LaserRangeFinder : public Object
{

public:

    LaserRangeFinder();

    virtual ~LaserRangeFinder();

    void sense(const World& world, const geo::Pose3D& sensor_pose) const;

};

}

#endif
