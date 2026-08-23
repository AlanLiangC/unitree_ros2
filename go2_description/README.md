# Go2 description for local ROS 2 visualization

This package wraps Unitree's official Go2 URDF and DAE meshes for ROS 2
Humble. The upstream assets were copied from
`unitreerobotics/unitree_ros/robots/go2_description` at commit
`daadf41ee9afce8f90fdc09a98506012691fa122` and remain under Unitree's
BSD-3-Clause license (see `LICENSE` and `README.upstream.md`).

The launch file publishes only local visualization topics under `/go2_viz`.
It does not subscribe to or publish any Go2 command topic.

After the RGB-D DDS bridge is running, launch the model, LiDAR, RGB image and
color-aligned depth image together:

```bash
ros2 launch go2_description go2_visualization.launch.py
```

The RGB and aligned-depth streams are opened in separate `rqt_image_view`
windows because this Humble/OGRE host is unstable when RViz embeds an Image
display beside its 3-D view. The relevant launch arguments are:

```text
start_image_view:=true
start_depth_view:=true
color_topic:=/camera/color/image_raw
aligned_depth_topic:=/camera/aligned_depth_to_color/image_raw
```

The bridged cloud frame `utlidar_lidar` is attached to the official URDF
`radar` link. Consequently it inherits Unitree's `base -> radar` mount pose
instead of the incorrect identity transform previously used by this wrapper.
Do not use RViz's global **Invert Z Axis** option: it flips the robot and every
other display as well as the cloud.
