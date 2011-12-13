/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2011, Willow Garage, Inc.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author: Ioan Sucan, E. Gil Jones */

#include "planning_scene_ros/planning_scene_ros.h"

planning_scene_ros::PlanningSceneROS::PlanningSceneROS(const planning_scene::PlanningSceneConstPtr &parent) :
    planning_scene::PlanningScene(parent), nh_("~")
{
    const PlanningSceneROS *psr = dynamic_cast<const PlanningSceneROS*>(parent.get());
    if (!psr)
        ROS_FATAL("The parent planning scene must be a PlanningSceneROS for this constructor");
    else
    {
        tf_ = psr->tf_;
        robot_description_ = psr->getRobotDescription();
        default_robot_padd_ = psr->default_robot_padd_;
        default_robot_scale_ = psr->default_robot_scale_;
        default_object_padd_ = psr->default_object_padd_;
        default_attached_padd_ = psr->default_attached_padd_;
    }
}

planning_scene_ros::PlanningSceneROS::PlanningSceneROS(const std::string &robot_description, tf::Transformer *tf) :
    planning_scene::PlanningScene(), nh_("~"), tf_(tf)
{
    RobotModelLoader rml(robot_description);
    robot_description_ = rml.getRobotDescription();
    if (rml.getURDF() && rml.getSRDF())
        if (configure(rml.getURDF(), rml.getSRDF()))
        {
            configureDefaultCollisionMatrix();
            configureDefaultPadding();
            if (isConfigured())
            {
                crobot_->setPadding(default_robot_padd_);
                crobot_->setScale(default_robot_scale_);
            }
        }
}

void planning_scene_ros::PlanningSceneROS::startStateMonitor(void)
{
    if (isConfigured())
    {
        if (!csm_)
            csm_.reset(new CurrentStateMonitor(kmodel_, tf_));
        csm_->startStateMonitor();
    }
    else
        ROS_ERROR("Cannot monitor robot state because planning scene is not configured");
}

void planning_scene_ros::PlanningSceneROS::stopStateMonitor(void)
{
    if (csm_)
        csm_->stopStateMonitor();
}

void planning_scene_ros::PlanningSceneROS::useMonitoredState(void)
{
    if (csm_)
    {
        if (!csm_->haveCompleteState())
            ROS_ERROR("The complete state of the robot is not yet known");
        const std::map<std::string, double> &v = csm_->getCurrentStateValues();
        getCurrentState().setStateValues(v);
    }
    else
        ROS_ERROR("State monitor is not active. Unable to set the planning scene state");
}

void planning_scene_ros::PlanningSceneROS::configureDefaultCollisionMatrix(void)
{
    // no collisions allowed by default
    acm_->setEntry(kmodel_->getLinkModelNamesWithCollisionGeometry(),
                   kmodel_->getLinkModelNamesWithCollisionGeometry(), false);

    // allow collisions for pairs that have been disabled
    const std::vector<std::pair<std::string, std::string> >&dc = srdf_model_->getDisabledCollisions();
    for (std::size_t i = 0 ; i < dc.size() ; ++i)
        acm_->setEntry(dc[i].first, dc[i].second, true);

    // read overriding values from the param server

    // first we do default collision operations
    if (!nh_.hasParam(robot_description_ + "_planning/default_collision_operations"))
        ROS_DEBUG("No additional default collision operations specified");
    else
    {
        ROS_DEBUG("Reading additional default collision operations");

        XmlRpc::XmlRpcValue coll_ops;
        nh_.getParam(robot_description_ + "_planning/default_collision_operations", coll_ops);

        if (coll_ops.getType() != XmlRpc::XmlRpcValue::TypeArray)
        {
            ROS_WARN("default_collision_operations is not an array");
            return;
        }

        if (coll_ops.size() == 0)
        {
            ROS_WARN("No collision operations in default collision operations");
            return;
        }

        for (int i = 0 ; i < coll_ops.size() ; ++i)
        {
            if (!coll_ops[i].hasMember("object1") || !coll_ops[i].hasMember("object2") || !coll_ops[i].hasMember("operation"))
            {
                ROS_WARN("All collision operations must have two objects and an operation");
                continue;
            }
            acm_->setEntry(std::string(coll_ops[i]["object1"]), std::string(coll_ops[i]["object2"]), std::string(coll_ops[i]["operation"]) == "disable");
        }
    }
}

void planning_scene_ros::PlanningSceneROS::configureDefaultPadding(void)
{
    nh_.param(robot_description_ + "_planning/default_robot_padding", default_robot_padd_, 0.0);
    nh_.param(robot_description_ + "_planning/default_robot_scale", default_robot_scale_, 1.0);
    nh_.param(robot_description_ + "_planning/default_object_padding", default_object_padd_, 0.0);
    nh_.param(robot_description_ + "_planning/default_attached_padding", default_attached_padd_, 0.0);
}