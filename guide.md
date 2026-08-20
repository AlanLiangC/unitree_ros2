# Go2 无线 DDS Router、README 测试与 RViz2 指南

本文是本仓库无线 DDS 路线的唯一主文档，已经合并原 `guide.md` 与 `connect.md`。它适用于：

- 本机 Ubuntu 22.04、ROS 2 Humble；
- Go2 Ubuntu 通过内部网口访问机器人 DDS，再通过 WLAN 与本机通信；
- DDS Router 已经按照昨天的步骤安装到 Go2 Ubuntu；
- 本机 `cyclonedds_ws` 和 `example` 已经编译。

同事部署的“Go2 本地订阅 ROS，再通过 HTTP 与工作站通信”是另一条独立路线，见 [xnavdp_guide.md](xnavdp_guide.md)。

> 本指南只桥接和读取传感器、TF 与状态话题。不要用 `low_level_ctrl`、`go2_sport_client`、`go2_stand_example` 做连通性测试，它们可能让机器人运动。

## 1. 当前网络与角色

| 设备 | 当前 IPv4 | MAC | 用途 |
| --- | --- | --- | --- |
| Go2 Ubuntu | `192.168.8.252` | `6c:1f:f7:e8:25:68` | 运行 DDS Router |
| Unitree 本体 | `192.168.8.254` | `94:ba:06:fc:40:38` | 机器人无线接口 |
| Liang 本机 | `192.168.8.253/24` | `c0:bf:be:47:c9:e2` | ROS 2 Humble、RViz2 |

这些地址由 DHCP 分配，重启 Wi-Fi 后可能互换。`init_env.sh` 会探测 `.252`～`.254` 并按 MAC 识别角色；DDS Router 配置则必须把 `go2_wifi` 明确绑定到 Ubuntu 的当前 WLAN 地址，否则多网卡环境可能发布本机无法访问的 locator。长期使用时建议在路由器中为三个 MAC 设置地址保留。

检查网络：

```bash
ip -br -4 address show wlp4s0
ping -c 3 192.168.8.252
ping -c 3 192.168.8.254
ip neigh show dev wlp4s0
```

## 2. 为什么需要 DDS Router

```text
Unitree 主控制器（192.168.123.161，DDS Domain 0）
                         │
Go2 Ubuntu eth10（192.168.123.18）
                         │
        DDS Router：Domain 0 → Domain 42
                         │
Go2 Ubuntu WLAN（当前 192.168.8.252）
                         │
Liang wlp4s0（当前 192.168.8.253，Domain 42）
```

能 ping 通 Go2 的 WLAN 地址，只表示 IP 层连通。普通 Linux 路由不会正确转发 DDS discovery 中携带的地址，因此本机直接使用 Domain 0 时仍只能看到 `/parameter_events` 和 `/rosout`。

DDS Router 在 Go2 Ubuntu 上同时加入内部 Domain 0 与无线 Domain 42，把允许的话题复制到本机可发现的 DDS 网络。

## 3. 使用统一的只读桥接配置

本仓库的 `ddsrouter/go2-wifi-bridge.yaml` 现在统一放行：

- `/camera/color/image_raw`、`/camera/color/camera_info`；
- `/utlidar/cloud`；
- `/tf`、`/tf_static`；
- `/sportmodestate`、`/lf/sportmodestate`；
- `/lowstate`、`/lf/lowstate`；
- `/wirelesscontroller`。

它没有放行：

- `/api/sport/request`；
- `/api/motion_switcher/request`；
- `/lowcmd` 或其他控制话题。

为了不覆盖昨天或同事留下的配置，把它复制为一个新文件：

```bash
cd /home/alan/AlanLiang/Projects/pure_projects/unitree_ros2
source ./init_env.sh

scp ddsrouter/go2-wifi-bridge.yaml \
  "unitree@${UNITREE_UBUNTU_IP}:~/go2-readonly-bridge.yaml"
```

这一步只新增 `~/go2-readonly-bridge.yaml`，不会覆盖昨天的 `~/go2-wifi-bridge.yaml` 或同事的导航文件。

## 4. Go2 端检查与启动

登录 Go2 Ubuntu：

```bash
ssh "unitree@${UNITREE_UBUNTU_IP}"
```

先只读检查，不要安装或更新任何依赖：

```bash
ip -br -4 address
test -x ~/DDS-Router/install/bin/ddsrouter && echo "DDS Router installed"
ls -l ~/go2-readonly-bridge.yaml
pgrep -af ddsrouter || true
```

预期接口为：

- `eth10 / 192.168.123.18`：对应 `go2_internal`；
- `wlan0 / 192.168.8.252`：对应 `go2_wifi`。

两者都必须与 YAML 的 `whitelist-interfaces` 一致。特别是无线 participant 不能留空：实测留空时 DDS Router 虽监听 `0.0.0.0:17900/17910/17911`，但 Liang 本机无法发现任何桥接话题。若 DHCP 改变 Ubuntu WLAN 地址，先更新 YAML 再启动 Router。

如果 `pgrep` 已显示 DDS Router：

- 不要直接 `pkill`；
- 如果它是你昨天在前台启动的，回到原终端用 `Ctrl+C` 停止后换统一配置；
- 如果它由 systemd 或同事的脚本管理，先确认来源，不要动它。

同时检查 `pgrep -af go2_xnavdp_client`。DDS Router 与同事客户端可以在技术上共存，但都会读取高带宽传感器并占用 Go2/Wi-Fi 资源；未经同事确认，不要在其导航实验运行时再启动相机和点云桥接。

在一个独立终端启动统一桥接，并保持该进程运行：

```bash
source ~/DDS-Router/install/setup.bash
~/DDS-Router/install/bin/ddsrouter \
  -c ~/go2-readonly-bridge.yaml
```

该终端只加载 DDS Router 自己的 Fast DDS 环境。不要在同一个终端再 source Go2 的 ROS/CycloneDDS workspace。

如果 `~/DDS-Router/install/bin/ddsrouter` 不存在，再参考 `ddsrouter/install_go2_ddsrouter.sh`；既然昨天已经安装成功，正常情况下不要重复运行安装脚本。

## 5. 本机每次初始化

本机新开 Bash 或 zsh 终端：

```bash
cd /home/alan/AlanLiang/Projects/pure_projects/unitree_ros2
source ./init_env.sh
```

预期输出类似：

```text
[unitree] 本机网卡  : wlp4s0 (192.168.8.253)
[unitree] Go2 Ubuntu: 192.168.8.252 (可达)
[unitree] Unitree   : 192.168.8.254 (可达)
[unitree] DDS       : bridge, domain 42, peer 192.168.8.252
```

脚本会加载 Humble、`cyclonedds_ws`、CycloneDDS 和已编译的 example overlay，并按 MAC 刷新 DHCP 地址。它只修改当前终端环境，不修改 Go2。

## 6. 判断桥接是否成功

```bash
ROS2CLI_NO_DAEMON=1 ros2 topic list --spin-time 10 -t
```

成功时应至少出现部分下列话题：

```text
/camera/color/image_raw
/utlidar/cloud
/sportmodestate
/lowstate
/tf
```

有些固件使用 `/lf/sportmodestate`、`/lf/lowstate`。如果只有：

```text
/parameter_events
/rosout
```

则 DDS 没有桥接成功。优先检查 Go2 端 DDS Router 是否仍在运行，以及本机输出是否为 Domain 42、peer 是否为当前 Ubuntu 地址。

## 7. 完成 README 的只读测试

### 7.1 运动状态

根据实际存在的话题选择一个：

```bash
ros2 topic echo /sportmodestate --once \
  --qos-reliability best_effort

ros2 topic echo /lf/sportmodestate --once \
  --qos-reliability best_effort
```

收到 `position`、`velocity`、`body_height`、`gait_type` 等字段，即表示状态链路正常。

### 7.2 低层状态与遥控器

```bash
ros2 topic echo /lowstate --once \
  --qos-reliability best_effort

ros2 topic echo /wirelesscontroller --once \
  --qos-reliability best_effort
```

如果只有 `/lf/lowstate`，替换话题名。固件未发布某一个可选话题不代表整个桥接失败。

### 7.3 运行已编译的只读示例

本仓库把示例程序安装在包前缀根目录，使用实际路径：

```bash
"$UNITREE_ROS2_ROOT/example/install/unitree_ros2_example/read_motion_state"
"$UNITREE_ROS2_ROOT/example/install/unitree_ros2_example/read_low_state"
"$UNITREE_ROS2_ROOT/example/install/unitree_ros2_example/read_wireless_controller"
```

每个程序都会持续运行，用 `Ctrl+C` 停止。不要运行同目录中的控制示例。

如需重新编译：

```bash
cd "$UNITREE_ROS2_ROOT/example"
colcon build \
  --symlink-install \
  --cmake-clean-cache \
  --cmake-args \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DPYTHON_EXECUTABLE=/usr/bin/python3 \
  -DBUILD_TESTING=OFF
```

显式使用 `/usr/bin/python3` 是为了避开本机 Conda Python 缺少 ROS `catkin_pkg` 的问题。

## 8. 显示相机

最直接的方式：

```bash
ros2 run rqt_image_view rqt_image_view
```

选择 `/camera/color/image_raw`。RViz2 中添加 `Image` 时使用：

```text
Topic: /camera/color/image_raw
Transport Hint: raw
Reliability Policy: Best Effort
```

## 9. 显示点云、相机与 Go2 URDF

仓库内的 `go2_description` 是 Unitree 官方 Go2 URDF/DAE 模型的 ROS 2
Humble 包装。首次使用或模型包有更新时，在本机编译一次：

```bash
cd "$UNITREE_ROS2_ROOT"
/usr/bin/python3 -m colcon build \
  --symlink-install \
  --packages-select go2_description \
  --cmake-args \
    -DPython3_EXECUTABLE=/usr/bin/python3 \
    -DPYTHON_EXECUTABLE=/usr/bin/python3
source ./init_env.sh
```

之后一个命令即可同时启动本地站立模型、显示用 TF 和 RViz：

```bash
ros2 launch go2_description go2_visualization.launch.py
```

RViz 配置会显示网格、官方 Go2 模型、完整模型 TF/坐标轴和雷达点云强度；
同一个 launch 会用 `rqt_image_view` 打开相机原始图像。当前 Humble/OGRE
组合在 RViz 的 3D 视图旁同时创建 `Image` 渲染窗口会崩溃，因此相机没有
嵌进 RViz 主窗口。若本次不需要相机窗口，可加 `start_image_view:=false`。
`LowState`、`SportModeState` 和手柄状态是自定义消息，RViz2 内置插件不能
直接将它们画出来，仍按前文用 `ros2 topic echo` 查看。

核心参数：

```text
Fixed Frame: base
PointCloud2 Topic: /utlidar/cloud
Reliability: Best Effort
Durability: Volatile
```

启动文件把模型 TF 重映射到 `/go2_viz/tf` 和 `/go2_viz/tf_static`，不会通过
DDS Router 的 `/tf` 白名单反向送进 Go2。`base -> utlidar_lidar` 使用零变换，
只为显示建立坐标关系，不是真实雷达外参；没有标定前，不能把模型、图像和
点云当作已经精确对齐。

## 10. 带宽与故障排查

### 有话题名称但没有数据

```bash
ros2 topic info -v /sportmodestate
timeout 10 ros2 topic hz /sportmodestate
timeout 10 ros2 topic hz /camera/color/image_raw
timeout 10 ros2 topic hz /utlidar/cloud
```

Humble 的 `ros2 topic hz` 不能指定 QoS。如果它收不到 Best Effort 数据，以带 `--qos-reliability best_effort` 的 `ros2 topic echo` 为准。

### 相机或点云卡顿

原始图像和点云带宽较大。先在 RViz 中只启用一个 display。若只测试状态，可在 Go2 上改用 `go2-read-state-bridge.yaml`，它不转发相机和点云。

### ROS 2 daemon 缓存旧环境

重新执行：

```bash
source ./init_env.sh
ROS2CLI_NO_DAEMON=1 ros2 topic list --spin-time 10
```

### 防火墙与客户端隔离

检查：

```bash
sudo ufw status
```

不要永久关闭整机防火墙。应只为可信的 `192.168.8.0/24` 放行所需 DDS 流量，并确认 Wi-Fi 没有启用客户端隔离。

## 11. 有线兜底

如果不使用无线桥接，可按上游 README 用网线连接内部网络，将有线网卡设为 `192.168.123.99/24`，然后：

```bash
UNITREE_NET_IFACE=enp3s0 \
UNITREE_LOCAL_IP=192.168.123.99 \
UNITREE_GO2_IP=192.168.123.161 \
UNITREE_DDS_MODE=direct \
source ./init_env.sh
```

`enp3s0` 应替换为 `ip -br link` 显示的实际有线网卡。脚本会因有线网卡 MAC 与 WLAN 记录不同而警告，但不会阻止初始化。

## 12. 最短日常清单

Go2 Ubuntu：

```bash
source ~/DDS-Router/install/setup.bash
~/DDS-Router/install/bin/ddsrouter \
  -c ~/go2-readonly-bridge.yaml
```

本机：

```bash
cd /home/alan/AlanLiang/Projects/pure_projects/unitree_ros2
source ./init_env.sh
ROS2CLI_NO_DAEMON=1 ros2 topic list --spin-time 10
ros2 launch go2_description go2_visualization.launch.py
```
