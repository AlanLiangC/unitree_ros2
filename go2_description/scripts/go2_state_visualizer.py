#!/usr/bin/python3
"""Publish a read-only RViz model state from the Go2 state topics."""

from math import isfinite, sqrt

import rclpy
from geometry_msgs.msg import TransformStamped
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import JointState
from tf2_ros import TransformBroadcaster
from unitree_go.msg import LowState, SportModeState


# Unitree LowState motor order, as defined by motor_crc.h: FR, FL, RR, RL.
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

STANDING_POSITION = [0.0, 0.67, -1.3] * 4


def normalized_wxyz(values):
    """Return a finite normalized wxyz quaternion, or identity if invalid."""
    quaternion = [float(value) for value in values]
    if len(quaternion) != 4 or not all(isfinite(value) for value in quaternion):
        return [1.0, 0.0, 0.0, 0.0]
    norm = sqrt(sum(value * value for value in quaternion))
    if norm < 1e-8:
        return [1.0, 0.0, 0.0, 0.0]
    return [value / norm for value in quaternion]


class Go2StateVisualizer(Node):
    """Convert Unitree state messages to JointState and an odom -> base TF."""

    def __init__(self) -> None:
        super().__init__("go2_state_visualizer")

        self.declare_parameter("publish_rate", 30.0)
        self.declare_parameter("odom_frame", "odom")
        self.declare_parameter("base_frame", "base")

        sensor_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=5,
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
        )
        self.joint_publisher = self.create_publisher(
            JointState, "/go2_viz/joint_states", 5
        )
        self.tf_broadcaster = TransformBroadcaster(self)

        self.joint_position = list(STANDING_POSITION)
        self.joint_velocity = [0.0] * len(JOINT_NAMES)
        self.joint_effort = [0.0] * len(JOINT_NAMES)
        self.position = [0.0, 0.0, 0.32]
        self.quaternion_wxyz = [1.0, 0.0, 0.0, 0.0]
        self.received_lowstate = False
        self.received_pose = False

        for topic in ("/lowstate", "/lf/lowstate"):
            self.create_subscription(LowState, topic, self.lowstate_callback, sensor_qos)
        for topic in ("/sportmodestate", "/lf/sportmodestate"):
            self.create_subscription(
                SportModeState, topic, self.sportmode_callback, sensor_qos
            )

        publish_rate = max(
            1.0, float(self.get_parameter("publish_rate").get_parameter_value().double_value)
        )
        self.timer = self.create_timer(1.0 / publish_rate, self.publish_state)
        self.get_logger().info(
            "waiting for /lowstate joints and /sportmodestate body pose "
            "(Best Effort QoS)"
        )

    def lowstate_callback(self, message: LowState) -> None:
        if len(message.motor_state) < len(JOINT_NAMES):
            self.get_logger().warning(
                f"LowState contains only {len(message.motor_state)} motors; expected 12",
                throttle_duration_sec=5.0,
            )
            return

        positions = [float(state.q) for state in message.motor_state[:12]]
        velocities = [float(state.dq) for state in message.motor_state[:12]]
        efforts = [float(state.tau_est) for state in message.motor_state[:12]]
        if not all(isfinite(value) for value in positions):
            self.get_logger().warning(
                "ignoring LowState with non-finite joint positions",
                throttle_duration_sec=5.0,
            )
            return

        self.joint_position = positions
        self.joint_velocity = velocities
        self.joint_effort = efforts
        if not self.received_lowstate:
            self.received_lowstate = True
            self.get_logger().info("receiving real Go2 joint positions from /lowstate")

    def sportmode_callback(self, message: SportModeState) -> None:
        position = [float(value) for value in message.position]
        if len(position) != 3 or not all(isfinite(value) for value in position):
            self.get_logger().warning(
                "ignoring SportModeState with invalid position",
                throttle_duration_sec=5.0,
            )
            return

        self.position = position
        # Unitree IMUState.quaternion is ordered [w, x, y, z].
        self.quaternion_wxyz = normalized_wxyz(message.imu_state.quaternion)
        # Unitree firmware and the PC can use different clock epochs.  A robot
        # timestamp on a PC-side TF is then rejected by RViz as TF_OLD_DATA even
        # though the state packet has just arrived.  This bridge visualizes the
        # latest received state, so stamp it in the local ROS clock domain.
        self.publish_transform(self.get_clock().now().to_msg())
        if not self.received_pose:
            self.received_pose = True
            self.get_logger().info("receiving real Go2 body pose from /sportmodestate")

    def publish_state(self) -> None:
        stamp = self.get_clock().now().to_msg()

        joints = JointState()
        joints.header.stamp = stamp
        joints.name = JOINT_NAMES
        joints.position = self.joint_position
        joints.velocity = self.joint_velocity
        joints.effort = self.joint_effort
        self.joint_publisher.publish(joints)
        # Refresh the body transform with the same local-clock stamp as the
        # joints.  This keeps RobotModel visible between state packets and also
        # shows the neutral fallback pose while waiting for the first packet.
        self.publish_transform(stamp)

    def publish_transform(self, stamp) -> None:
        transform = TransformStamped()
        # Unitree's TimeSpec has the same fields as builtin_interfaces/Time,
        # but ROS message setters reject assigning it as a different type.
        transform.header.stamp.sec = int(stamp.sec)
        transform.header.stamp.nanosec = int(stamp.nanosec)
        transform.header.frame_id = str(self.get_parameter("odom_frame").value)
        transform.child_frame_id = str(self.get_parameter("base_frame").value)
        transform.transform.translation.x = self.position[0]
        transform.transform.translation.y = self.position[1]
        transform.transform.translation.z = self.position[2]
        qw, qx, qy, qz = self.quaternion_wxyz
        transform.transform.rotation.x = qx
        transform.transform.rotation.y = qy
        transform.transform.rotation.z = qz
        transform.transform.rotation.w = qw
        self.tf_broadcaster.sendTransform(transform)


def main(args=None) -> None:
    rclpy.init(args=args)
    node = Go2StateVisualizer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    except Exception:
        # ROS launch may invalidate the shared context before spin() returns.
        # Treat that shutdown race as a clean exit, but preserve real runtime
        # failures while the context is still healthy.
        if rclpy.ok():
            raise
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
