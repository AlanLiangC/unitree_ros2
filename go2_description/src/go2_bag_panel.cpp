#include "go2_description/go2_bag_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <signal.h>

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDockWidget>
#include <QMainWindow>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <pluginlib/class_list_macros.hpp>
#include <rviz_common/display_context.hpp>

namespace fs = std::filesystem;

namespace go2_description
{
namespace
{

constexpr auto kRgbTopic = "/camera/color/image_raw";
constexpr auto kRgbInfoTopic = "/camera/color/camera_info";
constexpr auto kDepthTopic = "/camera/aligned_depth_to_color/image_raw";
constexpr auto kDepthInfoTopic = "/camera/aligned_depth_to_color/camera_info";
constexpr auto kCloudTopic = "/utlidar/cloud";
constexpr auto kPoseTopic = "/sportmodestate";
constexpr auto kFallbackPoseTopic = "/lf/sportmodestate";

QString humanBytes(std::uintmax_t bytes)
{
  static const char * suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(bytes);
  std::size_t index = 0;
  while (value >= 1024.0 && index + 1 < std::size(suffixes)) {
    value /= 1024.0;
    ++index;
  }
  return QString("%1 %2").arg(value, 0, index == 0 ? 'f' : 'f', index == 0 ? 0 : 1).arg(suffixes[index]);
}

std::uintmax_t directoryBytes(const QString & path)
{
  std::error_code error;
  std::uintmax_t total = 0;
  const fs::path root(path.toStdString());
  if (!fs::exists(root, error)) {
    return 0;
  }
  for (fs::recursive_directory_iterator it(root, error), end; it != end && !error; it.increment(error)) {
    if (it->is_regular_file(error)) {
      total += it->file_size(error);
    }
  }
  return total;
}

float readF32(const std::uint8_t * data, bool big_endian)
{
  std::uint8_t ordered[4];
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
  if (big_endian) {
    std::reverse_copy(data, data + 4, ordered);
  } else {
    std::copy(data, data + 4, ordered);
  }
#else
  if (big_endian) {
    std::copy(data, data + 4, ordered);
  } else {
    std::reverse_copy(data, data + 4, ordered);
  }
#endif
  float value = 0.0F;
  std::memcpy(&value, ordered, sizeof(value));
  return value;
}

QImage drawPointCloud(const sensor_msgs::msg::PointCloud2 & msg)
{
  constexpr int width = 640;
  constexpr int height = 360;
  QImage result(width, height, QImage::Format_RGB888);
  result.fill(QColor(5, 9, 14));
  QPainter painter(&result);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(QPen(QColor(25, 42, 53), 1));
  for (int x = 0; x <= width; x += 40) {
    painter.drawLine(x, 0, x, height);
  }
  for (int y = 0; y <= height; y += 40) {
    painter.drawLine(0, y, width, y);
  }
  painter.setPen(QPen(QColor(75, 95, 108), 1));
  painter.drawLine(width / 2, 0, width / 2, height);
  painter.drawLine(0, height / 2, width, height / 2);

  if (msg.point_step == 0 || msg.data.empty()) {
    return result;
  }
  int x_offset = -1;
  int y_offset = -1;
  int z_offset = -1;
  for (const auto & field : msg.fields) {
    if (field.name == "x") {x_offset = static_cast<int>(field.offset);}
    if (field.name == "y") {y_offset = static_cast<int>(field.offset);}
    if (field.name == "z") {z_offset = static_cast<int>(field.offset);}
  }
  if (x_offset < 0 || y_offset < 0 || z_offset < 0 ||
    static_cast<std::uint32_t>(std::max({x_offset, y_offset, z_offset}) + 4) > msg.point_step)
  {
    return {};
  }

  const std::size_t count = std::min<std::size_t>(
    static_cast<std::size_t>(msg.width) * msg.height, msg.data.size() / msg.point_step);
  const std::size_t stride = std::max<std::size_t>(1, count / 12000);
  constexpr float range_m = 8.0F;
  const float scale = std::min(width, height) / (2.0F * range_m);
  for (std::size_t index = 0; index < count; index += stride) {
    const auto * point = msg.data.data() + index * msg.point_step;
    const float x = readF32(point + x_offset, msg.is_bigendian != 0);
    const float y = readF32(point + y_offset, msg.is_bigendian != 0);
    const float z = readF32(point + z_offset, msg.is_bigendian != 0);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
      std::abs(x) > range_m || std::abs(y) > range_m)
    {
      continue;
    }
    const int px = static_cast<int>(width * 0.5F + y * scale);
    const int py = static_cast<int>(height * 0.5F - x * scale);
    const float height_color = std::clamp((z + 1.0F) / 3.0F, 0.0F, 1.0F);
    painter.setPen(QColor::fromHsvF(0.55 - 0.45 * height_color, 0.85, 1.0, 0.9));
    painter.drawPoint(px, py);
  }
  painter.setPen(QColor(82, 236, 216));
  painter.drawEllipse(QPoint(width / 2, height / 2), 4, 4);
  return result;
}

QImage decodeRgb(const sensor_msgs::msg::Image & msg)
{
  const int width = static_cast<int>(msg.width);
  const int height = static_cast<int>(msg.height);
  if (width <= 0 || height <= 0 || msg.data.empty()) {
    return {};
  }
  const QString encoding = QString::fromStdString(msg.encoding).toLower();
  QImage result;
  if (encoding == "rgb8" && msg.step >= static_cast<std::uint32_t>(width * 3)) {
    result = QImage(msg.data.data(), width, height, static_cast<int>(msg.step), QImage::Format_RGB888).copy();
  } else if (encoding == "bgr8" && msg.step >= static_cast<std::uint32_t>(width * 3)) {
    result = QImage(msg.data.data(), width, height, static_cast<int>(msg.step), QImage::Format_BGR888).copy();
  } else if (encoding == "rgba8" && msg.step >= static_cast<std::uint32_t>(width * 4)) {
    result = QImage(msg.data.data(), width, height, static_cast<int>(msg.step), QImage::Format_RGBA8888).copy();
  } else if (encoding == "bgra8" && msg.step >= static_cast<std::uint32_t>(width * 4)) {
    result = QImage(msg.data.data(), width, height, static_cast<int>(msg.step), QImage::Format_ARGB32).rgbSwapped();
  } else if (encoding == "mono8" && msg.step >= static_cast<std::uint32_t>(width)) {
    result = QImage(msg.data.data(), width, height, static_cast<int>(msg.step), QImage::Format_Grayscale8).copy();
  }
  return result;
}

}  // namespace

Go2BagPanel::Go2BagPanel(QWidget * parent)
: rviz_common::Panel(parent), recorder_(new QProcess(this))
{
  auto * content = new QWidget(this);
  auto * layout = new QVBoxLayout(content);
  layout->setContentsMargins(6, 6, 6, 6);

  auto make_view = [layout](const QString & title, QLabel ** target) {
      auto * group = new QGroupBox(title);
      auto * group_layout = new QVBoxLayout(group);
      *target = new QLabel("等待传感器数据");
      (*target)->setAlignment(Qt::AlignCenter);
      (*target)->setMinimumSize(320, 180);
      (*target)->setStyleSheet("QLabel { background:#080b10; color:#7f8c98; border:1px solid #26313b; }");
      group_layout->addWidget(*target);
      layout->addWidget(group);
    };
  make_view("RGB · /camera/color/image_raw", &rgb_view_);
  make_view("LiDAR top view · /utlidar/cloud", &cloud_view_);

  sensor_status_ = new QLabel("RGB 0 · Depth 0 · Cloud 0");
  layout->addWidget(sensor_status_);

  auto * recorder_group = new QGroupBox("ROS bag recorder · 原始频率");
  auto * recorder_layout = new QVBoxLayout(recorder_group);
  auto * form = new QFormLayout();
  const QString al_root = qEnvironmentVariable("AL_FARM_ROOT");
  const QString default_root = al_root.isEmpty() ?
    QDir::homePath() + "/AL_FARM/recordings/rosbags" : al_root + "/recordings/rosbags";
  output_root_ = new QLineEdit(default_root);
  auto * output_row = new QHBoxLayout();
  output_row->addWidget(output_root_);
  auto * browse = new QPushButton("浏览");
  output_row->addWidget(browse);
  form->addRow("保存目录", output_row);
  scene_name_ = new QLineEdit("go2_bag_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
  form->addRow("场景名称", scene_name_);
  recorder_layout->addLayout(form);
  auto * buttons = new QHBoxLayout();
  start_button_ = new QPushButton("● 开始录包");
  start_button_->setStyleSheet("QPushButton { background:#1f8f5f; color:white; font-weight:600; padding:7px; }");
  stop_button_ = new QPushButton("■ 结束录包");
  stop_button_->setEnabled(false);
  stop_button_->setStyleSheet("QPushButton { background:#a33b3b; color:white; font-weight:600; padding:7px; }");
  buttons->addWidget(start_button_);
  buttons->addWidget(stop_button_);
  recorder_layout->addLayout(buttons);
  recording_status_ = new QLabel("READY · 将录制 RGB、aligned depth、CameraInfo、LiDAR、SportModeState");
  recording_status_->setWordWrap(true);
  bag_path_label_ = new QLabel();
  bag_path_label_->setWordWrap(true);
  bag_path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  recorder_layout->addWidget(recording_status_);
  recorder_layout->addWidget(bag_path_label_);
  // Keep recording controls above the image previews. On laptop-sized RViz
  // windows the panel scrolls, and controls below two 16:9 previews are easy
  // to miss even though the plugin loaded correctly.
  layout->insertWidget(0, recorder_group);
  layout->addStretch(1);

  auto * scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setWidget(content);
  auto * outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(scroll);

  connect(browse, &QPushButton::clicked, this, &Go2BagPanel::chooseOutputRoot);
  connect(start_button_, &QPushButton::clicked, this, &Go2BagPanel::startRecording);
  connect(stop_button_, &QPushButton::clicked, this, &Go2BagPanel::stopRecording);
  connect(this, &Go2BagPanel::rgbReady, this, &Go2BagPanel::showRgb, Qt::QueuedConnection);
  connect(this, &Go2BagPanel::cloudReady, this, &Go2BagPanel::showCloud, Qt::QueuedConnection);
  connect(recorder_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
    this, &Go2BagPanel::recordingFinished);

  status_timer_ = new QTimer(this);
  status_timer_->setInterval(500);
  connect(status_timer_, &QTimer::timeout, this, &Go2BagPanel::refreshRecordingStatus);
  status_timer_->start();
}

Go2BagPanel::~Go2BagPanel()
{
  terminateRecorder(true);
}

void Go2BagPanel::onInitialize()
{
  const auto abstraction = getDisplayContext()->getRosNodeAbstraction().lock();
  if (!abstraction) {
    recording_status_->setText("ERROR · 无法访问 RViz ROS node");
    start_button_->setEnabled(false);
    return;
  }
  node_ = abstraction->get_raw_node();
  auto qos = rclcpp::SensorDataQoS().keep_last(1);
  rgb_subscription_ = node_->create_subscription<sensor_msgs::msg::Image>(
    kRgbTopic, qos, [this](sensor_msgs::msg::Image::ConstSharedPtr msg) {handleRgb(std::move(msg));});
  depth_subscription_ = node_->create_subscription<sensor_msgs::msg::Image>(
    kDepthTopic, qos, [this](sensor_msgs::msg::Image::ConstSharedPtr msg) {handleDepth(std::move(msg));});
  cloud_subscription_ = node_->create_subscription<sensor_msgs::msg::PointCloud2>(
    kCloudTopic, qos,
    [this](sensor_msgs::msg::PointCloud2::ConstSharedPtr msg) {handleCloud(std::move(msg));});

  QWidget * ancestor = parentWidget();
  while (ancestor != nullptr && qobject_cast<QDockWidget *>(ancestor) == nullptr) {
    ancestor = ancestor->parentWidget();
  }
  auto * dock = qobject_cast<QDockWidget *>(ancestor);
  if (dock != nullptr) {
    QWidget * window = dock->parentWidget();
    while (window != nullptr && qobject_cast<QMainWindow *>(window) == nullptr) {
      window = window->parentWidget();
    }
    if (auto * main_window = qobject_cast<QMainWindow *>(window)) {
      main_window->addDockWidget(Qt::RightDockWidgetArea, dock);
      dock->setMinimumWidth(390);
      dock->resize(430, dock->height());
      // RViz can otherwise restore a tiny 400x287 window after the panel set
      // changes. Keep the first-run layout usable while respecting an already
      // larger/maximized user window.
      if (main_window->width() < 1200 || main_window->height() < 700) {
        main_window->resize(1720, 920);
      }
      dock->show();
      main_window->show();
    }
  }
}

void Go2BagPanel::handleRgb(sensor_msgs::msg::Image::ConstSharedPtr msg)
{
  ++rgb_frames_;
  const auto now = std::chrono::steady_clock::now();
  if (last_rgb_conversion_.time_since_epoch().count() != 0 && now - last_rgb_conversion_ < std::chrono::milliseconds(100)) {
    return;
  }
  last_rgb_conversion_ = now;
  const auto image = decodeRgb(*msg);
  if (!image.isNull()) {
    Q_EMIT rgbReady(image);
  }
}

void Go2BagPanel::handleDepth(sensor_msgs::msg::Image::ConstSharedPtr msg)
{
  static_cast<void>(msg);
  ++depth_frames_;
}

void Go2BagPanel::handleCloud(sensor_msgs::msg::PointCloud2::ConstSharedPtr msg)
{
  ++cloud_frames_;
  const auto now = std::chrono::steady_clock::now();
  if (last_cloud_conversion_.time_since_epoch().count() != 0 &&
    now - last_cloud_conversion_ < std::chrono::milliseconds(100))
  {
    return;
  }
  last_cloud_conversion_ = now;
  const auto image = drawPointCloud(*msg);
  if (!image.isNull()) {
    Q_EMIT cloudReady(image);
  }
}

void Go2BagPanel::showRgb(const QImage & image) {updatePreview(rgb_view_, image);}
void Go2BagPanel::showCloud(const QImage & image) {updatePreview(cloud_view_, image);}

void Go2BagPanel::updatePreview(QLabel * label, const QImage & image)
{
  label->setPixmap(QPixmap::fromImage(image).scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  sensor_status_->setText(
    QString("RGB %1 · Depth %2 · Cloud %3")
    .arg(rgb_frames_.load()).arg(depth_frames_.load()).arg(cloud_frames_.load()));
}

void Go2BagPanel::chooseOutputRoot()
{
  const QString selected = QFileDialog::getExistingDirectory(this, "选择 rosbag 保存目录", output_root_->text());
  if (!selected.isEmpty()) {
    output_root_->setText(selected);
    Q_EMIT configChanged();
  }
}

QString Go2BagPanel::sanitizedSceneName() const
{
  QString value = scene_name_->text().trimmed();
  value.replace(QRegularExpression("[^A-Za-z0-9_.-]+"), "_");
  value.remove(QRegularExpression("^[._-]+|[._-]+$"));
  return value.isEmpty() ? "go2_bag" : value.left(80);
}

QString Go2BagPanel::nextBagPath() const
{
  QDir root(output_root_->text().trimmed());
  QString candidate = root.filePath(sanitizedSceneName());
  if (!QFileInfo::exists(candidate)) {
    return QDir::cleanPath(candidate);
  }
  return QDir::cleanPath(candidate + "_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
}

void Go2BagPanel::startRecording()
{
  if (recorder_->state() != QProcess::NotRunning) {
    return;
  }
  QDir root(output_root_->text().trimmed());
  if (!root.exists() && !root.mkpath(".")) {
    setRecordingUi(false, "ERROR · 无法创建保存目录");
    return;
  }
  active_bag_path_ = nextBagPath();
  const QStringList topics = {
    kRgbTopic, kRgbInfoTopic, kDepthTopic, kDepthInfoTopic,
    kCloudTopic, kPoseTopic, kFallbackPoseTopic};
  QStringList arguments = {
    "/opt/ros/humble/bin/ros2", "bag", "record", "--storage", "sqlite3", "--output", active_bag_path_};
  arguments.append(topics);
  recorder_->setProcessChannelMode(QProcess::MergedChannels);
  recorder_->start("/usr/bin/setsid", arguments);
  if (!recorder_->waitForStarted(5000)) {
    setRecordingUi(false, "ERROR · ros2 bag record 启动失败：" + recorder_->errorString());
    return;
  }
  recording_started_ = std::chrono::steady_clock::now();
  bag_path_label_->setText("输出：" + active_bag_path_);
  setRecordingUi(true, "RECORDING · 正在写入全部原始传感器消息");
}

void Go2BagPanel::stopRecording()
{
  if (recorder_->state() == QProcess::NotRunning) {
    return;
  }
  recording_status_->setText("FINALIZING · 正在写入 metadata.yaml，请稍候…");
  stop_button_->setEnabled(false);
  terminateRecorder(false);
}

void Go2BagPanel::terminateRecorder(bool force)
{
  if (!recorder_ || recorder_->state() == QProcess::NotRunning) {
    return;
  }
  const qint64 pid = recorder_->processId();
  if (pid > 0) {
    ::kill(-static_cast<pid_t>(pid), force ? SIGTERM : SIGINT);
  }
  if (!recorder_->waitForFinished(force ? 1500 : 10000)) {
    if (pid > 0) {
      ::kill(-static_cast<pid_t>(pid), SIGKILL);
    }
    recorder_->kill();
    recorder_->waitForFinished(1500);
  }
}

void Go2BagPanel::recordingFinished(int exit_code, QProcess::ExitStatus status)
{
  const bool metadata_ready = QFileInfo::exists(QDir(active_bag_path_).filePath("metadata.yaml"));
  const QString message = metadata_ready ?
    QString("SAVED · rosbag 已安全结束（%1）").arg(humanBytes(directoryBytes(active_bag_path_))) :
    QString("STOPPED · metadata.yaml 未生成，请检查日志（exit %1 / %2）")
    .arg(exit_code).arg(status == QProcess::NormalExit ? "normal" : "crashed");
  setRecordingUi(false, message);
}

void Go2BagPanel::refreshRecordingStatus()
{
  sensor_status_->setText(
    QString("RGB %1 · Depth %2 · Cloud %3")
    .arg(rgb_frames_.load()).arg(depth_frames_.load()).arg(cloud_frames_.load()));
  if (recorder_->state() == QProcess::NotRunning) {
    return;
  }
  const double elapsed = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - recording_started_).count();
  recording_status_->setText(
    QString("● RECORDING · %1 s · %2").arg(elapsed, 0, 'f', 1).arg(humanBytes(directoryBytes(active_bag_path_))));
}

void Go2BagPanel::setRecordingUi(bool recording, const QString & message)
{
  start_button_->setEnabled(!recording);
  stop_button_->setEnabled(recording);
  output_root_->setEnabled(!recording);
  scene_name_->setEnabled(!recording);
  recording_status_->setText(message);
}

}  // namespace go2_description

PLUGINLIB_EXPORT_CLASS(go2_description::Go2BagPanel, rviz_common::Panel)
