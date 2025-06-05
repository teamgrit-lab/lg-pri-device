cmake_minimum_required(VERSION 3.8)
project(LG_robot)

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


pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)

add_executable(left_arm_node src/left_arm.cpp)
target_link_libraries(left_arm_node nlohmann_json::nlohmann_json Boost::system Boost::thread pthread)
ament_target_dependencies(left_arm_node
  rclcpp
  geometry_msgs
  nlohmann_json
  tf2
  tf2_ros
  tf2_geometry_msgs
)
install(TARGETS left_arm_node
  DESTINATION lib/${PROJECT_NAME}
)

add_executable(right_arm_node src/right_arm.cpp)
target_link_libraries(right_arm_node nlohmann_json::nlohmann_json Boost::system Boost::thread pthread)
ament_target_dependencies(right_arm_node
  rclcpp
  geometry_msgs
  nlohmann_json
  tf2
  tf2_ros
  tf2_geometry_msgs
)
install(TARGETS right_arm_node
  DESTINATION lib/${PROJECT_NAME}
)

# add_executable(head_camera_node src/head_camera.cpp)
# target_link_libraries(head_camera_node Boost::system Boost::thread pthread /usr/local/lib/libdraco.a)
# ament_target_dependencies(head_camera_node
#   rclcpp
#   sensor_msgs
# )
# install(TARGETS head_camera_node
#   DESTINATION lib/${PROJECT_NAME}
# )

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

ament_package()
