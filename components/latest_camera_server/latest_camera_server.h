#pragma once

#ifdef USE_ESP32

#include <atomic>
#include <cinttypes>
#include <memory>
#include <lwip/sockets.h>

#include "esphome/components/camera/camera.h"
#include "esphome/core/component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace esphome::latest_camera_server {

class LatestCameraServer final : public Component, public camera::CameraListener {
 public:
  explicit LatestCameraServer(uint16_t port) : port_(port) {}

  void setup() override;
  void loop() override {}
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void on_camera_image(const std::shared_ptr<camera::CameraImage> &image) override;

 protected:
  static void task_entry_(void *param);
  void task_loop_();
  void accept_client_();
  void handle_client_(int client_fd, const struct sockaddr_in &peer);
  void close_active_stream_(bool stop_camera);
  void stream_disconnected_();
  void send_page_(int fd);
  void send_status_(int fd, const char *token);
  bool send_all_(int fd, const void *data, size_t length);
  bool send_stream_headers_(int fd);
  bool send_latest_frame_();
  bool peer_allowed_(const struct sockaddr_in &peer) const;

  uint16_t port_;
  int listen_fd_{-1};
  int active_stream_fd_{-1};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> stream_running_{false};
  SemaphoreHandle_t image_mutex_{nullptr};
  std::shared_ptr<camera::CameraImage> latest_image_;
  char active_token_[64]{};
  uint32_t image_generation_{0};
  uint32_t last_sent_generation_{0xFFFFFFFFU};
  uint32_t frames_sent_{0};
};

}  // namespace esphome::latest_camera_server

#endif  // USE_ESP32
