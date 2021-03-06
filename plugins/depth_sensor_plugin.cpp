#include "depth_sensor_plugin.h"

#include <geolib/Shape.h>

#include <opencv2/highgui/highgui.hpp>

#include <rgbd/Image.h>
#include <rgbd/serialization.h>
#include <rgbd/RGBDMsg.h>
#include <tue/serialization/conversions.h>

// ROS
#include <rgbd/ros/conversions.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <ros/node_handle.h>

#include <ed/world_model.h>
#include <ed/uuid.h>
#include <ed/entity.h>

// ----------------------------------------------------------------------------------------------------

class DepthSensorRenderResult : public geo::RenderResult {

public:

    DepthSensorRenderResult(cv::Mat& z_buffer, int width, int height)
                : geo::RenderResult(width, height), z_buffer_(z_buffer)
    {
    }

    void renderPixel(int x, int y, float depth, int i_triangle)
    {
        float old_depth = z_buffer_.at<float>(y, x);
        if (old_depth == 0 || depth < old_depth)
        {
            z_buffer_.at<float>(y, x) = depth;
        }
    }

protected:

    cv::Mat& z_buffer_;

};

// ----------------------------------------------------------------------------------------------------

DepthSensorPlugin::DepthSensorPlugin() : render_rgb_(false), render_depth_(false)
{
}

// ----------------------------------------------------------------------------------------------------

void DepthSensorPlugin::configure(tue::Configuration config, const sim::LUId& obj_id)
{
    if (config.readGroup("rgb"))
    {
        config.value("width", rgb_width_);
        config.value("height", rgb_height_);

        render_rgb_ = true;

        config.endGroup();
    }

    if (config.readGroup("depth"))
    {
        config.value("width", depth_width_);
        config.value("height", depth_height_);

        double fx, fy;
        config.value("fx", fx);
        config.value("fy", fy);

        depth_rasterizer_.setOpticalTranslation(0, 0);
        depth_rasterizer_.setOpticalCenter(((double)depth_width_ + 1) / 2, ((double)depth_height_ + 1) / 2);
        depth_rasterizer_.setFocalLengths(fx, fy);

        render_depth_ = true;

        config.endGroup();
    }

    if (!ros::isInitialized())
         ros::init(ros::M_string(), "simulator", ros::init_options::NoSigintHandler);

    ros::NodeHandle nh;

    if (config.readArray("topics"))
    {
        while(config.nextArrayItem())
        {
            std::string rgbd_topic;
            if (config.value("rgbd", rgbd_topic, tue::OPTIONAL))
                pubs_rgbd_.push_back(nh.advertise<rgbd::RGBDMsg>(rgbd_topic, 10));

            std::string depth_topic;
            if (config.value("depth", depth_topic, tue::OPTIONAL))
                pubs_depth_.push_back(nh.advertise<sensor_msgs::Image>(depth_topic, 10));

            std::string depth_info_topic;
            if (config.value("depth_info", depth_info_topic, tue::OPTIONAL))
                pubs_cam_info_depth_.push_back(nh.advertise<sensor_msgs::CameraInfo>(depth_info_topic, 10));

            std::string rgb_topic;
            if (config.value("rgb", rgb_topic, tue::OPTIONAL))
                pubs_rgb_.push_back(nh.advertise<sensor_msgs::Image>(rgb_topic, 10));

            std::string rgb_info_topic;
            if (config.value("rgb_info", rgb_info_topic, tue::OPTIONAL))
                pubs_cam_info_rgb_.push_back(nh.advertise<sensor_msgs::CameraInfo>(rgb_info_topic, 10));
        }

        config.endArray();
    }

    config.value("frame_id", rgb_frame_id_);
    depth_frame_id_ = rgb_frame_id_;
}

// ----------------------------------------------------------------------------------------------------

void DepthSensorPlugin::process(const ed::WorldModel& world, const sim::LUId& obj_id, double dt, ed::UpdateRequest& req)
{
    // Get ROS current time
    ros::Time time = ros::Time::now();

    geo::Pose3D camera_pose;
    if (!world.calculateTransform("world", obj_id.id, time.toSec(), camera_pose))
        return;

    cv::Mat depth_image;
    cv::Mat rgb_image;

    if (render_rgb_)
    {
        rgb_image = cv::Mat(rgb_height_, rgb_width_, CV_8UC3, cv::Scalar(255, 255, 255));
    }

    if (render_depth_)
    {
        // Calculate inverse camera pose, including correction for geolib frame
        geo::Pose3D camera_pose_inv = geo::Pose3D(0, 0, 0, 3.1415, 0, 0) * camera_pose.inverse();

        depth_image = cv::Mat(depth_height_, depth_width_, CV_32FC1, 0.0);

        DepthSensorRenderResult res(depth_image, depth_width_, depth_height_);

        for(ed::WorldModel::const_iterator it = world.begin(); it != world.end(); ++it)
        {
            const ed::EntityConstPtr& e = *it;

            if (e->shape())
            {
                // Correction for geolib frame
                geo::Pose3D rel_pose = camera_pose_inv * e->pose();

                // Set render options
                geo::RenderOptions opt;
                opt.setMesh(e->shape()->getMesh(), rel_pose);

                // Render
                depth_rasterizer_.render(opt, res);
            }
        }
    }

    if (!pubs_depth_.empty())
    {
        // Convert depth image to ROS message
        sensor_msgs::Image depth_image_msg;
        rgbd::convert(depth_image, depth_image_msg);
        depth_image_msg.header.stamp = time;
        depth_image_msg.header.frame_id = depth_frame_id_;

        // Publish image
        for(std::vector<ros::Publisher>::const_iterator it = pubs_depth_.begin(); it != pubs_depth_.end(); ++it)
            it->publish(depth_image_msg);
    }

    if (!pubs_cam_info_depth_.empty())
    {
        // Convert camera info to ROS message
        sensor_msgs::CameraInfo cam_info_depth;
        rgbd::convert(depth_rasterizer_, cam_info_depth);
        cam_info_depth.header.stamp = time;
        cam_info_depth.header.frame_id = depth_frame_id_;

        // Publish camera info
        for(std::vector<ros::Publisher>::const_iterator it = pubs_cam_info_depth_.begin(); it != pubs_cam_info_depth_.end(); ++it)
            it->publish(cam_info_depth);
    }

    if (!pubs_rgb_.empty())
    {
        // Convert rgb image to ROS message
        sensor_msgs::Image rgb_image_msg;
        rgbd::convert(rgb_image, rgb_image_msg);
        rgb_image_msg.header.stamp = time;
        rgb_image_msg.header.frame_id = rgb_frame_id_;

        // Publish image
        for(std::vector<ros::Publisher>::const_iterator it = pubs_rgb_.begin(); it != pubs_rgb_.end(); ++it)
            it->publish(rgb_image_msg);
    }

    if (!pubs_cam_info_rgb_.empty())
    {
        // Convert camera info to ROS message
        sensor_msgs::CameraInfo cam_info_rgb;
        rgbd::convert(depth_rasterizer_, cam_info_rgb);
        cam_info_rgb.header.stamp = time;
        cam_info_rgb.header.frame_id = rgb_frame_id_;

        // Publish camera info
        for(std::vector<ros::Publisher>::const_iterator it = pubs_cam_info_rgb_.begin(); it != pubs_cam_info_rgb_.end(); ++it)
            it->publish(cam_info_rgb);
    }

    if (!pubs_rgbd_.empty())
    {
        rgbd::Image image(rgb_image, depth_image, depth_rasterizer_, rgb_frame_id_, time.toSec());

        rgbd::RGBDMsg msg;
        msg.version = 2;

        // Set RGB storage type
        rgbd::RGBStorageType rgb_type = rgbd::RGB_STORAGE_JPG;
        if (!rgb_image.data)
            rgb_type = rgbd::RGB_STORAGE_NONE;

        rgbd::DepthStorageType depth_type = rgbd::DEPTH_STORAGE_PNG;
        if (!depth_image.data)
            depth_type = rgbd::DEPTH_STORAGE_NONE;

        std::stringstream stream;
        tue::serialization::OutputArchive a(stream);
        rgbd::serialize(image, a, rgb_type, depth_type);
        tue::serialization::convert(stream, msg.rgb);

        for(std::vector<ros::Publisher>::const_iterator it = pubs_rgbd_.begin(); it != pubs_rgbd_.end(); ++it)
            it->publish(msg);
    }
}

SIM_REGISTER_PLUGIN(DepthSensorPlugin)
