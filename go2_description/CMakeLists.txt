cmake_minimum_required(VERSION 3.8)
project(go2_description)

find_package(ament_cmake REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rviz_common REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(go2_bag_panel SHARED
  include/go2_description/go2_bag_panel.hpp
  src/go2_bag_panel.cpp
)
target_include_directories(go2_bag_panel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(go2_bag_panel Qt5::Widgets)
ament_target_dependencies(go2_bag_panel pluginlib rclcpp rviz_common sensor_msgs)
pluginlib_export_plugin_description_file(rviz_common plugin_description.xml)

install(
  DIRECTORY dae launch urdf
  DESTINATION share/${PROJECT_NAME}
)

install(DIRECTORY include/ DESTINATION include)
install(FILES plugin_description.xml DESTINATION share/${PROJECT_NAME})
install(TARGETS go2_bag_panel
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# Keep rviz/go2_lidar.rviz as the single source of truth requested by this
# workspace, while also installing it where the launch file can find it.
install(
  FILES ../rviz/go2_lidar.rviz
  DESTINATION share/${PROJECT_NAME}/rviz
)

install(
  PROGRAMS scripts/go2_state_visualizer.py
  DESTINATION lib/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_dependencies(pluginlib rclcpp rviz_common sensor_msgs)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_package()
