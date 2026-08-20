#!/usr/bin/env bash
# Run this script on the Go2 auxiliary Ubuntu computer, not on the laptop.
# It builds DDS Router v2.2.0 and its pinned dependencies for Ubuntu 20.04/ARM64.

set -euo pipefail

dds_router_ws="${DDS_ROUTER_WS:-${HOME}/DDS-Router}"

sudo apt-get update
sudo apt-get install -y \
  cmake \
  g++ \
  git \
  wget \
  python3-pip \
  python3-colcon-common-extensions \
  python3-vcstool \
  libasio-dev \
  libtinyxml2-dev \
  libssl-dev \
  libyaml-cpp-dev

# Ubuntu 20.04 ships CMake 3.16, while Fast-CDR v2.2.0 requires CMake >= 3.20.
# Install a known compatible version for the unitree user without replacing the
# system package.
python3 -m pip install --user --upgrade "cmake==3.27.9"
export PATH="${HOME}/.local/bin:${PATH}"

cmake --version

mkdir -p "${dds_router_ws}/src"
wget -qO "${dds_router_ws}/ddsrouter.repos" \
  https://raw.githubusercontent.com/eProsima/DDS-Router/v2.2.0/ddsrouter.repos

vcs import "${dds_router_ws}/src" < "${dds_router_ws}/ddsrouter.repos"

# Do not accidentally link against the old Fast DDS shipped with ROS 2 Foxy.
unset AMENT_PREFIX_PATH CMAKE_PREFIX_PATH COLCON_PREFIX_PATH ROS_DISTRO ROS_VERSION
unset LD_LIBRARY_PATH PYTHONPATH

cd "${dds_router_ws}"
colcon build \
  --merge-install \
  --parallel-workers 2 \
  --cmake-clean-cache \
  --cmake-args -DBUILD_TESTS=OFF

echo "[ddsrouter] Build complete: ${dds_router_ws}"
echo "[ddsrouter] Next: source ${dds_router_ws}/install/setup.bash"
