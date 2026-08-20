# Go2 description for local ROS 2 visualization

This package wraps Unitree's official Go2 URDF and DAE meshes for ROS 2
Humble. The upstream assets were copied from
`unitreerobotics/unitree_ros/robots/go2_description` at commit
`daadf41ee9afce8f90fdc09a98506012691fa122` and remain under Unitree's
BSD-3-Clause license (see `LICENSE` and `README.upstream.md`).

The launch file publishes only local visualization topics under `/go2_viz`.
It does not subscribe to or publish any Go2 command topic.
