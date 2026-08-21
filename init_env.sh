#!/usr/bin/env bash
# Go2 / ROS 2 Humble environment for this repository.
#
# This file must be sourced so that its exports remain in the current shell:
#   source ./init_env.sh
#
# Defaults describe the current Wi-Fi network. Addresses are refreshed from
# the neighbor table by MAC whenever possible. They can also be overridden:
#   UNITREE_DDS_MODE=direct UNITREE_GO2_IP=192.168.123.161 source ./init_env.sh

if [[ -n "${ZSH_VERSION:-}" ]]; then
  # `%N` is the currently sourced file in zsh. Bash does not evaluate this
  # expansion because it is inside the zsh-only branch.
  _unitree_script_path="${(%):-%N}"
  _unitree_setup_suffix="zsh"
elif [[ -n "${BASH_VERSION:-}" ]]; then
  _unitree_script_path="${BASH_SOURCE[0]}"
  _unitree_setup_suffix="bash"
  if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "[unitree] 请使用 source ./init_env.sh；直接执行脚本无法保留环境变量。" >&2
    exit 2
  fi
else
  echo "[unitree] 错误：init_env.sh 仅支持 Bash 或 zsh。" >&2
  return 1 2>/dev/null || exit 1
fi

_unitree_fail() {
  echo "[unitree] 错误：$*" >&2
  return 1
}

# Resolve paths from this file rather than from the caller's working directory.
_unitree_root="$(cd -- "$(dirname -- "${_unitree_script_path}")" && pwd)" || return 1
export UNITREE_ROS2_ROOT="${UNITREE_ROS2_ROOT:-${_unitree_root}}"
export UNITREE_ROS_DISTRO="${UNITREE_ROS_DISTRO:-humble}"
export UNITREE_UBUNTU_IP="${UNITREE_UBUNTU_IP:-192.168.8.252}"
export UNITREE_GO2_IP="${UNITREE_GO2_IP:-192.168.8.254}"
export UNITREE_LOCAL_IP="${UNITREE_LOCAL_IP:-192.168.8.253}"
export UNITREE_UBUNTU_MAC="${UNITREE_UBUNTU_MAC:-6c:1f:f7:e8:25:68}"
export UNITREE_GO2_MAC="${UNITREE_GO2_MAC:-94:ba:06:fc:40:38}"
export UNITREE_LOCAL_MAC="${UNITREE_LOCAL_MAC:-c0:bf:be:47:c9:e2}"
export UNITREE_DDS_MODE="${UNITREE_DDS_MODE:-bridge}"
if [[ -n "${UNITREE_DDS_DOMAIN_OVERRIDE:-}" ]]; then
  UNITREE_DDS_DOMAIN="${UNITREE_DDS_DOMAIN_OVERRIDE}"
elif [[ "${UNITREE_DDS_MODE}" == "bridge" ]]; then
  UNITREE_DDS_DOMAIN="42"
else
  UNITREE_DDS_DOMAIN="0"
fi
export UNITREE_DDS_DOMAIN

_unitree_ros_setup="/opt/ros/${UNITREE_ROS_DISTRO}/setup.${_unitree_setup_suffix}"
_unitree_ws_setup="${UNITREE_ROS2_ROOT}/cyclonedds_ws/install/setup.${_unitree_setup_suffix}"
_unitree_root_overlay="${UNITREE_ROS2_ROOT}/install/local_setup.${_unitree_setup_suffix}"

[[ -r "${_unitree_ros_setup}" ]] || {
  _unitree_fail "找不到 ${_unitree_ros_setup}"
  unset -f _unitree_fail
  return 1
}
[[ -r "${_unitree_ws_setup}" ]] || {
  _unitree_fail "找不到 ${_unitree_ws_setup}，请先编译 cyclonedds_ws"
  unset -f _unitree_fail
  return 1
}

# Prefer the interface with this laptop's stable MAC. Fall back to its expected
# address and finally to the route selected for the Go2 Ubuntu system.
if [[ -z "${UNITREE_NET_IFACE:-}" ]]; then
  _unitree_expected_mac="$(printf '%s' "${UNITREE_LOCAL_MAC}" | tr '[:upper:]' '[:lower:]')"
  for _unitree_address_file in /sys/class/net/*/address; do
    _unitree_candidate_mac="$(tr '[:upper:]' '[:lower:]' < "${_unitree_address_file}" 2>/dev/null)"
    if [[ "${_unitree_candidate_mac}" == "${_unitree_expected_mac}" ]]; then
      UNITREE_NET_IFACE="$(basename -- "$(dirname -- "${_unitree_address_file}")")"
      break
    fi
  done
fi
if [[ -z "${UNITREE_NET_IFACE:-}" ]]; then
  UNITREE_NET_IFACE="$(
    ip -4 -o address show 2>/dev/null |
      awk -v address="${UNITREE_LOCAL_IP}" '$4 ~ ("^" address "/") {print $2; exit}'
  )"
  if [[ -z "${UNITREE_NET_IFACE}" ]]; then
    UNITREE_NET_IFACE="$(
      ip -4 route get "${UNITREE_UBUNTU_IP}" 2>/dev/null |
        awk '{for (i = 1; i <= NF; ++i) if ($i == "dev") {print $(i + 1); exit}}'
    )"
  fi
fi
export UNITREE_NET_IFACE

[[ -n "${UNITREE_NET_IFACE}" && -d "/sys/class/net/${UNITREE_NET_IFACE}" ]] || {
  _unitree_fail "无法找到连接 Go2 的网卡；可先设置 UNITREE_NET_IFACE=wlp4s0"
  unset -f _unitree_fail
  return 1
}

_unitree_actual_ip="$(
  ip -4 -o address show dev "${UNITREE_NET_IFACE}" 2>/dev/null |
    awk 'NR == 1 {sub(/\/.*/, "", $4); print $4}'
)"
if [[ -n "${_unitree_actual_ip}" ]]; then
  export UNITREE_LOCAL_IP="${_unitree_actual_ip}"
fi

# The current router allocates its three high addresses to Ubuntu, Unitree and
# Liang in varying order. Touch those addresses to refresh `ip neigh`, then map
# the two remote roles back to their stable MAC addresses.
_unitree_subnet_prefix="${_unitree_actual_ip%.*}"
if [[ -n "${_unitree_subnet_prefix}" && "${_unitree_subnet_prefix}" != "${_unitree_actual_ip}" ]]; then
  for _unitree_host in 252 253 254; do
    _unitree_candidate_ip="${_unitree_subnet_prefix}.${_unitree_host}"
    if [[ "${_unitree_candidate_ip}" != "${_unitree_actual_ip}" ]]; then
      ping -I "${UNITREE_NET_IFACE}" -c 1 -W 1 "${_unitree_candidate_ip}" >/dev/null 2>&1 || true
    fi
  done
fi

_unitree_find_ip_by_mac() {
  ip neigh show dev "${UNITREE_NET_IFACE}" 2>/dev/null |
    awk -v mac="$1" '{for (i = 1; i <= NF; ++i) if (tolower($i) == tolower(mac)) {print $1; exit}}'
}

_unitree_detected_ubuntu_ip="$(_unitree_find_ip_by_mac "${UNITREE_UBUNTU_MAC}")"
_unitree_detected_go2_ip="$(_unitree_find_ip_by_mac "${UNITREE_GO2_MAC}")"
if [[ "${UNITREE_DDS_MODE}" == "bridge" && -n "${_unitree_detected_ubuntu_ip}" ]]; then
  export UNITREE_UBUNTU_IP="${_unitree_detected_ubuntu_ip}"
fi
if [[ "${UNITREE_DDS_MODE}" == "bridge" && -n "${_unitree_detected_go2_ip}" ]]; then
  export UNITREE_GO2_IP="${_unitree_detected_go2_ip}"
fi

if [[ -n "${UNITREE_DDS_PEER_IP_OVERRIDE:-}" ]]; then
  UNITREE_DDS_PEER_IP="${UNITREE_DDS_PEER_IP_OVERRIDE}"
elif [[ "${UNITREE_DDS_MODE}" == "bridge" ]]; then
  UNITREE_DDS_PEER_IP="${UNITREE_UBUNTU_IP}"
else
  UNITREE_DDS_PEER_IP="${UNITREE_GO2_IP}"
fi
export UNITREE_DDS_PEER_IP

# Source the base distribution first.
source "${_unitree_ros_setup}" || return 1

# Packages built from the repository root (for example go2_description) are
# installed under UNITREE_ROS2_ROOT/install, outside cyclonedds_ws. Load that
# prefix as an optional overlay so a successful root-level colcon build is
# immediately visible to ros2 launch. Use local_setup here because the base and
# message-workspace underlays are selected explicitly; setup may replay stale
# underlays captured when this overlay was built.
if [[ -r "${_unitree_root_overlay}" ]]; then
  source "${_unitree_root_overlay}" || return 1
fi

# Load the dedicated Unitree message workspace last so its current interface
# packages take precedence over any old copies left in the root install tree.
source "${_unitree_ws_setup}" || return 1

export RMW_IMPLEMENTATION="rmw_cyclonedds_cpp"
export ROS_DOMAIN_ID="${UNITREE_DDS_DOMAIN}"
export ROS_LOCALHOST_ONLY="0"
export CYCLONEDDS_URI="<CycloneDDS><Domain Id=\"any\"><General><Interfaces><NetworkInterface name=\"${UNITREE_NET_IFACE}\" priority=\"default\" multicast=\"default\" /></Interfaces></General><Discovery><Peers><Peer Address=\"${UNITREE_DDS_PEER_IP}\" /></Peers></Discovery></Domain></CycloneDDS>"

# If the examples have already been built, load their package environment.
# This repository installs the binaries at the prefix root, so connect.md uses
# their explicit paths rather than `ros2 run`.
if [[ -r "${UNITREE_ROS2_ROOT}/example/install/setup.${_unitree_setup_suffix}" ]]; then
  source "${UNITREE_ROS2_ROOT}/example/install/setup.${_unitree_setup_suffix}" || return 1
fi

if [[ -n "${CONDA_PREFIX:-}" ]]; then
  echo "[unitree] 提示：当前 Conda 环境为 ${CONDA_PREFIX}。" >&2
  echo "[unitree] 编译 ROS 工作区时请按 connect.md 显式使用 /usr/bin/python3。" >&2
fi

# The daemon retains the RMW/domain/interface of the shell that started it.
# Restarting discovery after this script avoids stale results from another ROS
# environment. A new daemon will be started lazily by the next normal CLI call.
timeout 3 ros2 daemon stop >/dev/null 2>&1 || true

_unitree_actual_mac="$(tr '[:upper:]' '[:lower:]' < "/sys/class/net/${UNITREE_NET_IFACE}/address" 2>/dev/null)"

if [[ "${_unitree_actual_ip}" != "${UNITREE_LOCAL_IP}" ]]; then
  echo "[unitree] 警告：${UNITREE_NET_IFACE} 的 IPv4 是 ${_unitree_actual_ip:-无}，预期 ${UNITREE_LOCAL_IP}。" >&2
fi
_unitree_expected_mac="$(printf '%s' "${UNITREE_LOCAL_MAC}" | tr '[:upper:]' '[:lower:]')"
if [[ "${_unitree_actual_mac}" != "${_unitree_expected_mac}" ]]; then
  echo "[unitree] 警告：${UNITREE_NET_IFACE} 的 MAC 是 ${_unitree_actual_mac:-未知}，预期 ${UNITREE_LOCAL_MAC}。" >&2
fi

if ping -I "${UNITREE_NET_IFACE}" -c 1 -W 1 "${UNITREE_UBUNTU_IP}" >/dev/null 2>&1; then
  _unitree_ubuntu_reachability="可达"
else
  _unitree_ubuntu_reachability="不可达"
  echo "[unitree] 警告：无法 ping 通 Go2 Ubuntu ${UNITREE_UBUNTU_IP}。" >&2
fi
if ping -I "${UNITREE_NET_IFACE}" -c 1 -W 1 "${UNITREE_GO2_IP}" >/dev/null 2>&1; then
  _unitree_go2_reachability="可达"
else
  _unitree_go2_reachability="不可达"
  echo "[unitree] 警告：无法 ping 通 Unitree ${UNITREE_GO2_IP}。" >&2
fi

echo "[unitree] Go2 ROS 2 环境已加载"
echo "[unitree] ROS       : ${UNITREE_ROS_DISTRO} / ${RMW_IMPLEMENTATION}"
echo "[unitree] 工作区    : ${UNITREE_ROS2_ROOT}/cyclonedds_ws"
echo "[unitree] 本机网卡  : ${UNITREE_NET_IFACE} (${_unitree_actual_ip:-无 IPv4})"
echo "[unitree] Go2 Ubuntu: ${UNITREE_UBUNTU_IP} (${_unitree_ubuntu_reachability})"
echo "[unitree] Unitree   : ${UNITREE_GO2_IP} (${_unitree_go2_reachability})"
echo "[unitree] DDS       : ${UNITREE_DDS_MODE}, domain ${ROS_DOMAIN_ID}, peer ${UNITREE_DDS_PEER_IP}"
echo "[unitree] 测试命令  : ROS2CLI_NO_DAEMON=1 ros2 topic list --spin-time 10"

unset _unitree_root _unitree_ros_setup _unitree_ws_setup _unitree_root_overlay
unset _unitree_actual_ip _unitree_actual_mac _unitree_expected_mac
unset _unitree_ubuntu_reachability _unitree_go2_reachability
unset _unitree_detected_ubuntu_ip _unitree_detected_go2_ip
unset _unitree_subnet_prefix _unitree_host _unitree_candidate_ip
unset _unitree_address_file _unitree_candidate_mac
unset _unitree_script_path _unitree_setup_suffix
unset -f _unitree_fail _unitree_find_ip_by_mac
