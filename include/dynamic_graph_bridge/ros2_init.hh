#ifndef ROS2_INIT_HH
#define ROS2_INIT_HH
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/node.hpp"

namespace dynamicgraph {
  rclcpp::Node::SharedPtr rosInit();
  rclcpp::executors::MultiThreadedExecutor::SharedPtr rosInitGetExecutor();

}  // end of namespace dynamicgraph.

#endif  //! ROS_INIT_HH
