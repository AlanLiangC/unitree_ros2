"""Show bridged Go2 sensors together with a read-only live Go2 URDF."""

from pathlib import Path

from ament_index_python.packages import (
    get_package_prefix,
    get_package_share_directory,
)
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, ExecuteProcess, RegisterEventHandler
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    share = Path(get_package_share_directory("go2_description"))
    robot_description = (share / "urdf" / "go2_description.urdf").read_text()
    rviz_config = str(share / "rviz" / "go2_lidar.rviz")
    image_view = (
        Path(get_package_prefix("rqt_image_view"))
        / "lib"
        / "rqt_image_view"
        / "rqt_image_view"
    )

    private_tf = [
        ("/tf", "/go2_viz/tf"),
        ("/tf_static", "/go2_viz/tf_static"),
    ]

    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        name="go2_rviz",
        arguments=["-d", rviz_config],
        remappings=private_tf,
        condition=IfCondition(LaunchConfiguration("start_rviz")),
        output="screen",
    )

    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "start_rviz",
                default_value="true",
                description="Start RViz together with the local model publishers",
            ),
            DeclareLaunchArgument(
                "start_image_view",
                default_value="false",
                description="Open an additional standalone color rqt_image_view (the RViz panel already shows it)",
            ),
            DeclareLaunchArgument(
                "start_depth_view",
                default_value="false",
                description="Open an additional standalone depth rqt_image_view (the RViz panel already shows it)",
            ),
            DeclareLaunchArgument(
                "color_topic",
                default_value="/camera/color/image_raw",
                description="Color image topic shown by rqt_image_view",
            ),
            DeclareLaunchArgument(
                "aligned_depth_topic",
                default_value="/camera/aligned_depth_to_color/image_raw",
                description="Color-aligned depth image topic shown by rqt_image_view",
            ),
            DeclareLaunchArgument(
                "lidar_x",
                default_value="0.28945",
                description="Calibrated base -> utlidar_lidar X translation (metres)",
            ),
            DeclareLaunchArgument(
                "lidar_y",
                default_value="0.0",
                description="Calibrated base -> utlidar_lidar Y translation (metres)",
            ),
            DeclareLaunchArgument(
                "lidar_z",
                default_value="-0.046825",
                description="Calibrated base -> utlidar_lidar Z translation (metres)",
            ),
            DeclareLaunchArgument(
                "lidar_roll",
                default_value="3.3231069",
                description="Calibrated firmware-cloud roll in the Go2 base frame",
            ),
            DeclareLaunchArgument(
                "lidar_pitch",
                default_value="-0.1396263",
                description="Calibrated firmware-cloud pitch in the Go2 base frame",
            ),
            DeclareLaunchArgument(
                "lidar_yaw",
                default_value="-1.1170107",
                description="Calibrated firmware-cloud yaw in the Go2 base frame",
            ),
            # Convert the read-only Go2 LowState/SportModeState streams into
            # the private JointState + odom -> base TF used by this RViz view.
            Node(
                package="go2_description",
                executable="go2_state_visualizer.py",
                name="go2_state_visualizer",
                remappings=private_tf,
                output="screen",
            ),
            Node(
                package="robot_state_publisher",
                executable="robot_state_publisher",
                name="go2_visual_robot_state_publisher",
                parameters=[
                    {
                        "robot_description": robot_description,
                        "publish_frequency": 30.0,
                    }
                ],
                remappings=private_tf
                + [("/joint_states", "/go2_viz/joint_states")],
                output="screen",
            ),
            # The firmware PointCloud2 frame is not the physical `radar` frame
            # from the mesh URDF: its axes are Z-down and yaw-rotated.  Publish
            # an independently calibrated base -> cloud transform.  Keeping it
            # separate avoids applying the URDF mesh pitch to an already
            # processed firmware point cloud.
            Node(
                package="tf2_ros",
                executable="static_transform_publisher",
                name="go2_visual_lidar_tf",
                arguments=[
                    "--x",
                    LaunchConfiguration("lidar_x"),
                    "--y",
                    LaunchConfiguration("lidar_y"),
                    "--z",
                    LaunchConfiguration("lidar_z"),
                    "--roll",
                    LaunchConfiguration("lidar_roll"),
                    "--pitch",
                    LaunchConfiguration("lidar_pitch"),
                    "--yaw",
                    LaunchConfiguration("lidar_yaw"),
                    "--frame-id",
                    "base",
                    "--child-frame-id",
                    "utlidar_lidar",
                ],
                remappings=private_tf,
                output="screen",
            ),
            rviz_node,
            RegisterEventHandler(
                OnProcessExit(
                    target_action=rviz_node,
                    on_exit=[EmitEvent(event=Shutdown(reason="RViz window closed"))],
                )
            ),
            # Optional compatibility windows. RGB/depth are normally rendered
            # by Go2BagPanel inside RViz, avoiding the unstable OGRE Image display.
            ExecuteProcess(
                cmd=[
                    "/usr/bin/python3",
                    str(image_view),
                    LaunchConfiguration("color_topic"),
                ],
                name="go2_color_view",
                condition=IfCondition(LaunchConfiguration("start_image_view")),
                output="screen",
            ),
            ExecuteProcess(
                cmd=[
                    "/usr/bin/python3",
                    str(image_view),
                    LaunchConfiguration("aligned_depth_topic"),
                ],
                name="go2_aligned_depth_view",
                condition=IfCondition(LaunchConfiguration("start_depth_view")),
                output="screen",
            ),
        ]
    )
