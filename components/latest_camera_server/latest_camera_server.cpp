#ifdef USE_ESP32

#include "latest_camera_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <lwip/inet.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::latest_camera_server {

static constexpr const char *TAG = "latest_camera_server";
static constexpr uint32_t TAILSCALE_NET = 0x64400000U;  // 100.64.0.0/10
static constexpr uint32_t TAILSCALE_MASK = 0xFFC00000U;
static constexpr char VIEWER_PAGE[] = R"HTML(<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>XIAO Camera</title><style>
*{box-sizing:border-box}body{margin:0;background:#111827;color:#f9fafb;font:16px system-ui,sans-serif;min-height:100vh;display:grid;place-items:center}
main{width:min(100%,1100px);padding:20px;text-align:center}h1{font-size:clamp(1.4rem,4vw,2rem)}p{line-height:1.7;color:#cbd5e1}
img{display:block;width:100%;height:auto;max-height:72vh;object-fit:contain;background:#000}.metrics{display:flex;justify-content:center;gap:10px;flex-wrap:wrap;margin:14px 0 18px}.metric{min-width:145px;padding:9px 16px;border:1px solid #334155;border-radius:10px;background:#1f2937}.metric strong,.metric span{display:block}.metric strong{font-size:1.2rem;font-variant-numeric:tabular-nums}.metric span{font-size:.8rem;color:#cbd5e1}a{display:inline-block;margin-top:16px;padding:12px 20px;border-radius:8px;background:#2563eb;color:white;text-decoration:none}
</style></head><body><main id="app"></main><script>
const app=document.getElementById('app');
const query=new URLSearchParams(location.search);
if(query.get('view')==='disconnected'){
  const reason=query.get('reason');
  app.innerHTML='<h1>映像の接続が終了しました</h1>'+
    (reason==='another_viewer'
      ? '<p>別の端末から新しい視聴要求が届いたため、最新の端末へ接続を切り替えました。</p>'
      : '<p>カメラとの通信が途切れました。Wi-FiとTailscaleの接続を確認してください。</p>')+
    '<p>このカメラは同時に1台だけ視聴できます。もう一度見る場合は再接続してください。</p><a href="/">再接続</a>';
}else{
  const token=Date.now().toString(36)+'-'+Math.random().toString(36).slice(2);
  app.innerHTML='<h1>XIAO Camera</h1><p>別の端末が接続すると、この画面は終了します。</p>'+ 
    '<div class="metrics" aria-live="polite">'+
      '<div class="metric"><strong id="fps">--</strong><span>受信 FPS</span></div>'+
      '<div class="metric"><strong id="rtt">--</strong><span>通信応答（往復）</span></div></div>';
  const fpsDisplay=document.getElementById('fps');
  const rttDisplay=document.getElementById('rtt');
  const image=new Image(); image.alt='XIAO ESP32-S3 camera live view'; app.append(image);
  image.onerror=()=>location.replace('/?view=disconnected&reason=connection');
  image.src='/stream?token='+encodeURIComponent(token);
  let previousFrames=null;
  let previousSampleAt=0;
  const check=async()=>{
    try{
      const startedAt=performance.now();
      const response=await fetch('/status?token='+encodeURIComponent(token)+'&t='+Date.now(),{cache:'no-store'});
      const status=await response.json();
      const sampledAt=performance.now();
      rttDisplay.textContent=Math.round(sampledAt-startedAt)+' ms';
      if(!status.active) location.replace('/?view=disconnected&reason='+encodeURIComponent(status.reason));
      if(previousFrames!==null&&sampledAt>previousSampleAt){
        const frameDelta=Math.max(0,status.frames-previousFrames);
        fpsDisplay.textContent=(frameDelta*1000/(sampledAt-previousSampleAt)).toFixed(1);
      }
      previousFrames=status.frames;
      previousSampleAt=sampledAt;
    }catch(_){ location.replace('/?view=disconnected&reason=connection'); }
  };
  setInterval(check,700);
}
</script></body></html>)HTML";

static void set_socket_timeouts(int fd) {
  timeval timeout{};
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  timeout.tv_sec = 0;
  timeout.tv_usec = 400000;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

void LatestCameraServer::setup() {
  auto *camera = camera::Camera::instance();
  if (camera == nullptr || camera->is_failed()) {
    this->mark_failed(LOG_STR("Camera is not available"));
    return;
  }

  this->image_mutex_ = xSemaphoreCreateMutex();
  if (this->image_mutex_ == nullptr) {
    this->mark_failed(LOG_STR("Failed to create image mutex"));
    return;
  }
  camera->add_listener(this);

  this->listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (this->listen_fd_ < 0) {
    this->mark_failed(LOG_STR("Failed to create listener socket"));
    return;
  }

  int reuse = 1;
  setsockopt(this->listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  struct sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(this->port_);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(this->listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      listen(this->listen_fd_, 4) != 0) {
    close(this->listen_fd_);
    this->listen_fd_ = -1;
    this->mark_failed(LOG_STR("Failed to bind or listen"));
    return;
  }

  int flags = fcntl(this->listen_fd_, F_GETFL, 0);
  if (flags >= 0)
    fcntl(this->listen_fd_, F_SETFL, flags | O_NONBLOCK);

  if (xTaskCreate(&LatestCameraServer::task_entry_, "camera_http", 8192, this, 2, nullptr) != pdPASS) {
    close(this->listen_fd_);
    this->listen_fd_ = -1;
    this->mark_failed(LOG_STR("Failed to start HTTP task"));
  }
}

void LatestCameraServer::on_shutdown() {
  this->stopping_.store(true);
  if (this->active_stream_fd_ >= 0)
    shutdown(this->active_stream_fd_, SHUT_RDWR);
  if (this->listen_fd_ >= 0)
    shutdown(this->listen_fd_, SHUT_RDWR);
}

void LatestCameraServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Latest camera viewer:\n  Port: %u\n  Allowed peers: 100.64.0.0/10\n  Active viewers: 1 (newest wins)",
                this->port_);
  if (this->is_failed())
    ESP_LOGE(TAG, "  Setup failed");
}

float LatestCameraServer::get_setup_priority() const { return setup_priority::LATE; }

void LatestCameraServer::on_camera_image(const std::shared_ptr<camera::CameraImage> &image) {
  if (!this->stream_running_.load() || image == nullptr || !image->was_requested_by(camera::WEB_REQUESTER))
    return;

  if (xSemaphoreTake(this->image_mutex_, portMAX_DELAY) == pdTRUE) {
    this->latest_image_ = image;
    this->image_generation_++;
    xSemaphoreGive(this->image_mutex_);
  }
}

void LatestCameraServer::task_entry_(void *param) {
  static_cast<LatestCameraServer *>(param)->task_loop_();
  vTaskDelete(nullptr);
}

void LatestCameraServer::task_loop_() {
  while (!this->stopping_.load()) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = this->listen_fd_;
    FD_SET(this->listen_fd_, &read_fds);
    const int active_fd = this->active_stream_fd_;
    if (active_fd >= 0) {
      FD_SET(active_fd, &read_fds);
      if (active_fd > max_fd)
        max_fd = active_fd;
    }

    timeval timeout{};
    timeout.tv_usec = 20000;
    const int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ready < 0 && errno != EINTR) {
      ESP_LOGW(TAG, "select failed: errno=%d", errno);
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (active_fd >= 0 && ready > 0 && FD_ISSET(active_fd, &read_fds)) {
      char incoming[16];
      const ssize_t received = recv(active_fd, incoming, sizeof(incoming), MSG_DONTWAIT);
      if (received == 0 || (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        this->stream_disconnected_();
      }
    }

    if (this->listen_fd_ >= 0 && ready > 0 && FD_ISSET(this->listen_fd_, &read_fds))
      this->accept_client_();

    if (this->active_stream_fd_ >= 0)
      this->send_latest_frame_();
  }

  this->close_active_stream_(true);
  if (this->listen_fd_ >= 0) {
    close(this->listen_fd_);
    this->listen_fd_ = -1;
  }
}

bool LatestCameraServer::peer_allowed_(const struct sockaddr_in &peer) const {
  const uint32_t address = ntohl(peer.sin_addr.s_addr);
  return (address & TAILSCALE_MASK) == TAILSCALE_NET;
}

void LatestCameraServer::accept_client_() {
  struct sockaddr_in peer{};
  socklen_t peer_len = sizeof(peer);
  const int client_fd = accept(this->listen_fd_, reinterpret_cast<sockaddr *>(&peer), &peer_len);
  if (client_fd < 0)
    return;

  set_socket_timeouts(client_fd);
  if (!this->peer_allowed_(peer)) {
    static constexpr char BODY[] = "Tailnet connection required.\n";
    char header[128];
    const int length = snprintf(header, sizeof(header),
                                "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n"
                                "Content-Length: %u\r\n\r\n",
                                static_cast<unsigned>(sizeof(BODY) - 1));
    if (length > 0 && this->send_all_(client_fd, header, static_cast<size_t>(length)))
      this->send_all_(client_fd, BODY, sizeof(BODY) - 1);
    close(client_fd);
    return;
  }
  this->handle_client_(client_fd, peer);
}

void LatestCameraServer::handle_client_(int client_fd, const struct sockaddr_in &peer) {
  char request[512]{};
  size_t used = 0;
  while (used < sizeof(request) - 1) {
    const ssize_t received = recv(client_fd, request + used, sizeof(request) - used - 1, 0);
    if (received <= 0) {
      close(client_fd);
      return;
    }
    used += static_cast<size_t>(received);
    if (strstr(request, "\r\n\r\n") != nullptr)
      break;
  }

  char method[8]{};
  char target[192]{};
  if (sscanf(request, "%7s %191s", method, target) != 2 || strcmp(method, "GET") != 0) {
    static constexpr char RESPONSE[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    this->send_all_(client_fd, RESPONSE, sizeof(RESPONSE) - 1);
    close(client_fd);
    return;
  }

  if (strncmp(target, "/status", 7) == 0) {
    char token[64]{};
    const char *query = strchr(target, '?');
    if (query != nullptr && strncmp(query + 1, "token=", 6) == 0) {
      size_t token_len = 0;
      const char *value = query + 7;
      while (value[token_len] != '\0' && value[token_len] != '&' && token_len < sizeof(token) - 1) {
        token[token_len] = value[token_len];
        token_len++;
      }
    }
    this->send_status_(client_fd, token);
    close(client_fd);
    return;
  }

  if (strncmp(target, "/stream", 7) == 0) {
    char token[64]{};
    const char *query = strchr(target, '?');
    if (query != nullptr && strncmp(query + 1, "token=", 6) == 0) {
      size_t token_len = 0;
      const char *value = query + 7;
      while (value[token_len] != '\0' && value[token_len] != '&' && token_len < sizeof(token) - 1) {
        token[token_len] = value[token_len];
        token_len++;
      }
    }
    if (token[0] == '\0') {
      static constexpr char RESPONSE[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
      this->send_all_(client_fd, RESPONSE, sizeof(RESPONSE) - 1);
      close(client_fd);
      return;
    }

    if (this->active_stream_fd_ >= 0) {
      ESP_LOGI(TAG, "New viewer replaced the previous viewer");
      this->close_active_stream_(false);
    }
    if (!this->send_stream_headers_(client_fd)) {
      close(client_fd);
      this->close_active_stream_(true);
      return;
    }
    strlcpy(this->active_token_, token, sizeof(this->active_token_));
    this->active_stream_fd_ = client_fd;
    this->last_sent_generation_ = 0xFFFFFFFFU;
    this->frames_sent_ = 0;
    if (!this->stream_running_.exchange(true))
      camera::Camera::instance()->start_stream(camera::WEB_REQUESTER);
    ESP_LOGI(TAG, "Viewer connected from %s", inet_ntoa(peer.sin_addr));
    return;
  }

  if (strcmp(target, "/") == 0 || strncmp(target, "/?", 2) == 0) {
    this->send_page_(client_fd);
    close(client_fd);
    return;
  }

  static constexpr char RESPONSE[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
  this->send_all_(client_fd, RESPONSE, sizeof(RESPONSE) - 1);
  close(client_fd);
}

void LatestCameraServer::send_page_(int fd) {
  char header[160];
  const int length = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                              "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",
                              static_cast<unsigned>(sizeof(VIEWER_PAGE) - 1));
  if (length > 0 && this->send_all_(fd, header, static_cast<size_t>(length)))
    this->send_all_(fd, VIEWER_PAGE, sizeof(VIEWER_PAGE) - 1);
}

void LatestCameraServer::send_status_(int fd, const char *token) {
  const bool active = token[0] != '\0' && this->active_stream_fd_ >= 0 && strcmp(token, this->active_token_) == 0;
  const char *reason = active ? "" : this->active_stream_fd_ >= 0 ? "another_viewer" : "connection";
  char body[128];
  const int body_len = snprintf(body, sizeof(body), "{\"active\":%s,\"reason\":\"%s\",\"frames\":%" PRIu32 "}",
                                active ? "true" : "false", reason, this->frames_sent_);
  if (body_len <= 0 || static_cast<size_t>(body_len) >= sizeof(body))
    return;
  char header[160];
  const int length = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                              "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",
                              static_cast<unsigned>(body_len));
  if (length > 0 && this->send_all_(fd, header, static_cast<size_t>(length)))
    this->send_all_(fd, body, static_cast<size_t>(body_len));
}

bool LatestCameraServer::send_all_(int fd, const void *data, size_t length) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  while (length > 0) {
    const ssize_t sent = send(fd, bytes, length, 0);
    if (sent < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (sent == 0)
      return false;
    bytes += sent;
    length -= static_cast<size_t>(sent);
  }
  return true;
}

bool LatestCameraServer::send_stream_headers_(int fd) {
  static constexpr char HEADER[] =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=xiao-frame\r\n"
      "Cache-Control: no-store\r\n"
      "Connection: close\r\n\r\n";
  return this->send_all_(fd, HEADER, sizeof(HEADER) - 1);
}

bool LatestCameraServer::send_latest_frame_() {
  std::shared_ptr<camera::CameraImage> image;
  uint32_t generation;
  if (xSemaphoreTake(this->image_mutex_, pdMS_TO_TICKS(20)) != pdTRUE)
    return true;
  generation = this->image_generation_;
  if (generation != this->last_sent_generation_)
    image = this->latest_image_;
  xSemaphoreGive(this->image_mutex_);

  if (image == nullptr)
    return true;

  char header[96];
  const int header_len = snprintf(header, sizeof(header),
                                  "--xiao-frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                  static_cast<unsigned>(image->get_data_length()));
  const int fd = this->active_stream_fd_;
  if (header_len <= 0 || !this->send_all_(fd, header, static_cast<size_t>(header_len)) ||
      !this->send_all_(fd, image->get_data_buffer(), image->get_data_length()) ||
      !this->send_all_(fd, "\r\n", 2)) {
    this->stream_disconnected_();
    return false;
  }
  this->last_sent_generation_ = generation;
  this->frames_sent_++;
  if (xSemaphoreTake(this->image_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (this->image_generation_ == generation)
      this->latest_image_.reset();
    xSemaphoreGive(this->image_mutex_);
  }
  return true;
}

void LatestCameraServer::close_active_stream_(bool stop_camera) {
  if (this->active_stream_fd_ >= 0) {
    shutdown(this->active_stream_fd_, SHUT_RDWR);
    close(this->active_stream_fd_);
    this->active_stream_fd_ = -1;
  }
  if (stop_camera && this->stream_running_.exchange(false)) {
    auto *camera = camera::Camera::instance();
    if (camera != nullptr)
      camera->stop_stream(camera::WEB_REQUESTER);
    if (this->image_mutex_ != nullptr && xSemaphoreTake(this->image_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
      this->latest_image_.reset();
      xSemaphoreGive(this->image_mutex_);
    }
  }
  if (stop_camera)
    this->active_token_[0] = '\0';
}

void LatestCameraServer::stream_disconnected_() { this->close_active_stream_(true); }

}  // namespace esphome::latest_camera_server

#endif  // USE_ESP32
