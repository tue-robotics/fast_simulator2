#include "base_controller.h"

#include <tue/simulator/world.h>
#include <tue/simulator/object.h>
#include <tue/simulator/update_request.h>

// ----------------------------------------------------------------------------------------------------

BaseController::BaseController()
{
}

// ----------------------------------------------------------------------------------------------------

void BaseController::configure(tue::Configuration config)
{

}

// ----------------------------------------------------------------------------------------------------

void BaseController::process(const sim::World& world, const sim::LUId& obj_id, double dt, sim::UpdateRequest& req)
{
    std::cout << "BaseController::process (" << obj_id.id << ")" << std::endl;

    geo::Pose3D t;
    if (world.getTransform(obj_id, world.rootId(), t))
    {
        std::cout << t << std::endl;
    }

//    req.addTransform(world.rootId(), obj_id, geo::Pose3D(2, 0, 0));
}

SIM_REGISTER_PLUGIN(BaseController)
