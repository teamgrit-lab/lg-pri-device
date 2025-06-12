cmake_minimum_required(VERSION 3.8)
project(lg_robot)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(PkgConfig REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
# find_package(gripper_interfaces REQUIRED)
find_package(std_msgs REQUIRED)


find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "srv/GripperCommand.srv"
  "msg/Point2D.msg"
  "msg/Vector2.msg"
  "msg/Pose2D.msg"
  "msg/BoundingBox2D.msg"
  "msg/BoundingBox3D.msg"
  "msg/Mask.msg"
  "msg/KeyPoint2D.msg"
  "msg/KeyPoint2DArray.msg"
  "msg/KeyPoint3D.msg"
  "msg/KeyPoint3DArray.msg"
  "msg/Detection.msg"
  "msg/DetectionArray.msg"
  DEPENDENCIES std_msgs geometry_msgs
) 

# Add this line to get the typesupport target
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")

pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)

add_executable(left_arm_node src/left_arm.cpp)
# Update to include the typesupport target for left_arm_node as well
target_link_libraries(left_arm_node 
  nlohmann_json::nlohmann_json 
  Boost::system 
  Boost::thread 
  pthread
  "${cpp_typesupport_target}"
)
ament_target_dependencies(left_arm_node
  rclcpp
  geometry_msgs
  nlohmann_json
  tf2
  tf2_ros
  tf2_geometry_msgs
  # gripper_interfaces
)
install(TARGETS left_arm_node
  DESTINATION lib/${PROJECT_NAME}
)

add_executable(right_arm_node src/right_arm.cpp)
target_link_libraries(right_arm_node 
  nlohmann_json::nlohmann_json 
  Boost::system 
  Boost::thread 
  pthread
  "${cpp_typesupport_target}"
)
ament_target_dependencies(right_arm_node
  rclcpp
  geometry_msgs
  nlohmann_json
  tf2
  tf2_ros
  tf2_geometry_msgs
  # gripper_interfaces
)
install(TARGETS right_arm_node
  DESTINATION lib/${PROJECT_NAME}
)

add_executable(rgbd_camera_image_node src/rgbd_camera_image.cpp)
target_include_directories(rgbd_camera_image_node PUBLIC
  ${GSTREAMER_INCLUDE_DIRS}
  ${GSTREAMER_APP_INCLUDE_DIRS})
target_link_libraries(rgbd_camera_image_node Boost::system Boost::thread pthread ${GSTREAMER_LIBRARIES} ${GSTREAMER_APP_LIBRARIES})
ament_target_dependencies(rgbd_camera_image_node
  rclcpp
  sensor_msgs
)
install(TARGETS rgbd_camera_image_node
  DESTINATION lib/${PROJECT_NAME}
)

add_executable(ai_bounding_box_publisher src/ai_bounding_box_publisher.cpp)
target_link_libraries(ai_bounding_box_publisher 
  nlohmann_json::nlohmann_json 
  Boost::system 
  Boost::thread 
  pthread
  "${cpp_typesupport_target}"
)
ament_target_dependencies(ai_bounding_box_publisher
  rclcpp
  nlohmann_json
)
install(TARGETS ai_bounding_box_publisher
  DESTINATION lib/${PROJECT_NAME}
)

# add_executable(service_test src/service_test.cpp) # Your service server node
# target_link_libraries(service_test 
#   "${cpp_typesupport_target}"
# )
# ament_target_dependencies(service_test
#   rclcpp
# )
# install(TARGETS service_test
#   DESTINATION lib/${PROJECT_NAME}
# )



ament_package()
