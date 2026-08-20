#!/usr/bin/python3
"""Publish a local-only, fixed Go2 standing pose for RViz."""

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import JointState


JOINT_NAMES = [
    "FR_hip_joint",
    "FR_thigh_joint",
    "FR_calf_joint",
    "FL_hip_joint",
    "FL_thigh_joint",
    "FL_calf_joint",
    "RR_hip_joint",
    "RR_thigh_joint",
    "RR_calf_joint",
    "RL_hip_joint",
    "RL_thigh_joint",
    "RL_calf_joint",
]

# The standing target from example/src/src/go2/go2_stand_example.cpp. These
# values animate only the local URDF; they are never sent as motor commands.
STANDING_POSITION = [0.0, 0.67, -1.3] * 4


class StandingPosePublisher(Node):
    def __init__(self) -> None:
        super().__init__("go2_standing_pose")
        self.publisher = self.create_publisher(
            JointState, "/go2_viz/joint_states", 1
        )
        self.timer = self.create_timer(0.1, self.publish_pose)

    def publish_pose(self) -> None:
        message = JointState()
        message.header.stamp = self.get_clock().now().to_msg()
        message.name = JOINT_NAMES
        message.position = STANDING_POSITION
        self.publisher.publish(message)


def main(args=None) -> None:
    rclpy.init(args=args)
    node = StandingPosePublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
