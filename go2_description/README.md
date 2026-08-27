# Go2 description for local ROS 2 visualization

This package wraps Unitree's official Go2 URDF and DAE meshes for ROS 2
Humble. The upstream assets were copied from
`unitreerobotics/unitree_ros/robots/go2_description` at commit
`daadf41ee9afce8f90fdc09a98506012691fa122` and remain under Unitree's
BSD-3-Clause license (see `LICENSE` and `README.upstream.md`).

The launch file subscribes read-only to `/lowstate` and `/sportmodestate`, then
publishes the converted joint state and visualization TF only under `/go2_viz`.
It never subscribes to or publishes any Go2 command topic. RViz uses `odom` as
its fixed frame, so both leg motion and the body pose are shown live. The
`/lf/lowstate` and `/lf/sportmodestate` topics are automatic fallbacks.

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

The bridged cloud frame `utlidar_lidar` is independently calibrated against
the Go2 base frame. It must not inherit the official URDF `radar` mesh
rotation: the firmware cloud uses different, Z-down and yaw-rotated axes.
Defaults fitted from the included Go2 recordings are `xyz=(0.28945, 0,
-0.046825)` and `rpy=(3.3231069, -0.1396263, -1.1170107)` radians. They can be
overridden with the `lidar_x`, `lidar_y`, `lidar_z`, `lidar_roll`,
`lidar_pitch`, and `lidar_yaw` launch arguments. Do not use RViz's global
**Invert Z Axis** option: it flips the robot and every other display too.
