cmake_minimum_required(VERSION 3.8)
project(go2_description)

find_package(ament_cmake REQUIRED)

install(
  DIRECTORY dae launch urdf
  DESTINATION share/${PROJECT_NAME}
)

# Keep rviz/go2_lidar.rviz as the single source of truth requested by this
# workspace, while also installing it where the launch file can find it.
install(
  FILES ../rviz/go2_lidar.rviz
  DESTINATION share/${PROJECT_NAME}/rviz
)

install(
  PROGRAMS scripts/go2_standing_joint_state.py
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
