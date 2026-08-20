"""Show bridged Go2 sensors together with a local-only Go2 URDF."""

from pathlib import Path

from ament_index_python.packages import (
    get_package_prefix,
    get_package_share_directory,
)
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.conditions import IfCondition
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

    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "start_rviz",
                default_value="true",
                description="Start RViz together with the local model publishers",
            ),
            DeclareLaunchArgument(
                "start_image_view",
                default_value="true",
                description="Open the bridged Go2 camera in rqt_image_view",
            ),
            Node(
                package="go2_description",
                executable="go2_standing_joint_state.py",
                name="go2_standing_pose",
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
            # Unitree documents the cloud frame as utlidar_lidar, but the
            # bridge does not provide its calibrated transform. Identity is
            # deliberately used only for visualization.
            Node(
                package="tf2_ros",
                executable="static_transform_publisher",
                name="go2_visual_lidar_tf",
                arguments=[
                    "--x",
                    "0",
                    "--y",
                    "0",
                    "--z",
                    "0",
                    "--roll",
                    "0",
                    "--pitch",
                    "0",
                    "--yaw",
                    "0",
                    "--frame-id",
                    "base",
                    "--child-frame-id",
                    "utlidar_lidar",
                ],
                remappings=private_tf,
                output="screen",
            ),
            Node(
                package="rviz2",
                executable="rviz2",
                name="go2_rviz",
                arguments=["-d", rviz_config],
                remappings=private_tf,
                condition=IfCondition(LaunchConfiguration("start_rviz")),
                output="screen",
            ),
            # Humble's RViz Image display crashes this host's OGRE/X11 render
            # window when combined with the 3-D view. Use the ROS-native image
            # viewer so the camera is still available from the same command.
            ExecuteProcess(
                cmd=[
                    "/usr/bin/python3",
                    str(image_view),
                    "/camera/color/image_raw",
                ],
                name="go2_camera_view",
                condition=IfCondition(LaunchConfiguration("start_image_view")),
                output="screen",
            ),
        ]
    )
