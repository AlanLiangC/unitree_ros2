#pragma once

#include <chrono>
#include <atomic>
#include <memory>

#include <QImage>
#include <QProcess>
#include <QString>

#include <rclcpp/rclcpp.hpp>
#include <rviz_common/panel.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace go2_description
{

class Go2BagPanel final : public rviz_common::Panel
{
  Q_OBJECT

public:
  explicit Go2BagPanel(QWidget * parent = nullptr);
  ~Go2BagPanel() override;
  void onInitialize() override;

Q_SIGNALS:
  void rgbReady(const QImage & image);
  void cloudReady(const QImage & image);

private Q_SLOTS:
  void chooseOutputRoot();
  void startRecording();
  void stopRecording();
  void refreshRecordingStatus();
  void showRgb(const QImage & image);
  void showCloud(const QImage & image);
  void recordingFinished(int exit_code, QProcess::ExitStatus status);

private:
  void handleRgb(sensor_msgs::msg::Image::ConstSharedPtr msg);
  void handleDepth(sensor_msgs::msg::Image::ConstSharedPtr msg);
  void handleCloud(sensor_msgs::msg::PointCloud2::ConstSharedPtr msg);
  void updatePreview(QLabel * label, const QImage & image);
  QString sanitizedSceneName() const;
  QString nextBagPath() const;
  void setRecordingUi(bool recording, const QString & message);
  void terminateRecorder(bool force);

  rclcpp::Node::SharedPtr node_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr rgb_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr depth_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_subscription_;
  QLabel * rgb_view_{nullptr};
  QLabel * cloud_view_{nullptr};
  QLabel * sensor_status_{nullptr};
  QLabel * recording_status_{nullptr};
  QLabel * bag_path_label_{nullptr};
  QLineEdit * output_root_{nullptr};
  QLineEdit * scene_name_{nullptr};
  QPushButton * start_button_{nullptr};
  QPushButton * stop_button_{nullptr};
  QTimer * status_timer_{nullptr};
  QProcess * recorder_{nullptr};
  QString active_bag_path_;
  std::chrono::steady_clock::time_point recording_started_;
  std::chrono::steady_clock::time_point last_rgb_conversion_{};
  std::chrono::steady_clock::time_point last_cloud_conversion_{};
  std::atomic<std::size_t> rgb_frames_{0};
  std::atomic<std::size_t> depth_frames_{0};
  std::atomic<std::size_t> cloud_frames_{0};
};

}  // namespace go2_description
