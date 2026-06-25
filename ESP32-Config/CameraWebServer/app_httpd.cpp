#include "Arduino.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "board_config.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

httpd_handle_t stream_httpd = NULL;

static int ws_client_fd = -1;
static TaskHandle_t ws_task_handle = NULL;

typedef struct {
  httpd_handle_t hd;
  int fd;
} ws_task_arg_t;

static void ws_stream_task(void *pvParameters) {
  ws_task_arg_t *arg = (ws_task_arg_t *)pvParameters;
  httpd_handle_t hd = arg->hd;
  int fd = arg->fd;
  free(arg);

  log_i("WS stream task started for fd=%d", fd);

  int64_t last_frame = esp_timer_get_time();

  while (ws_client_fd == fd) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      log_e("Camera capture failed");
      vTaskDelay(50 / portTICK_PERIOD_MS);
      continue;
    }

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    bool needs_free = false;

    if (fb->format == PIXFORMAT_JPEG) {
      jpg_buf = fb->buf;
      jpg_len = fb->len;
    } else {
      if (!frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
        log_e("JPEG conversion failed");
        esp_camera_fb_return(fb);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        continue;
      }
      needs_free = true;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = jpg_buf;
    ws_pkt.len = jpg_len;
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    ws_pkt.final = true;

    esp_err_t res = httpd_ws_send_frame_async(hd, fd, &ws_pkt);

    if (needs_free) {
      free(jpg_buf);
    }
    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      log_e("WS send failed: 0x%x", res);
      break;
    }

    int64_t now = esp_timer_get_time();
    int64_t delta_ms = (now - last_frame) / 1000;
    last_frame = now;
    log_i("WS frame: %u B (%.1f fps)", (uint32_t)jpg_len, delta_ms > 0 ? 1000.0 / delta_ms : 0.0);
  }

  ws_client_fd = -1;
  ws_task_handle = NULL;

  log_i("WS stream task ended");
  vTaskDelete(NULL);
}

static esp_err_t ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    int new_fd = httpd_req_to_sockfd(req);
    log_i("WS client connected: fd=%d", new_fd);

    if (ws_client_fd != -1 && ws_client_fd != new_fd) {
      log_w("Replacing existing WS client fd=%d with fd=%d", ws_client_fd, new_fd);
    }
    ws_client_fd = new_fd;

    if (ws_task_handle == NULL) {
      ws_task_arg_t *arg = (ws_task_arg_t *)malloc(sizeof(ws_task_arg_t));
      if (!arg) {
        return ESP_ERR_NO_MEM;
      }
      arg->hd = req->handle;
      arg->fd = new_fd;
      BaseType_t ok = xTaskCreatePinnedToCore(ws_stream_task, "ws_stream", 8192, arg, 5, &ws_task_handle, 1);
      if (ok != pdPASS) {
        free(arg);
        ws_client_fd = -1;
        return ESP_FAIL;
      }
    }
    return ESP_OK;
  }

  // Drain any inbound frames; this firmware does not accept WS commands
  // Does not really do anything is only her to empty potential packages from the Rapsberry like pings

  httpd_ws_frame_t ws_pkt;
  uint8_t scratch[128];
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.payload = scratch;
  return httpd_ws_recv_frame(req, &ws_pkt, sizeof(scratch));
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 5;

  httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
  };

  config.server_port += 1;
  config.ctrl_port += 1;
  log_i("Starting WebSocket stream server on port: '%u'", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &ws_uri);
  }
}
