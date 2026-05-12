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

#if !defined(CONFIG_HTTPD_WS_SUPPORT)
#error "WebSocket support not enabled. ESP-IDF >= 4.4 with CONFIG_HTTPD_WS_SUPPORT=y is required."
#endif

#if defined(LED_GPIO_NUM)
#define CONFIG_LED_MAX_INTENSITY 255
int led_duty = 0;
bool isStreaming = false;
#endif

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static int ws_client_fd = -1;
static TaskHandle_t ws_task_handle = NULL;

typedef struct {
  httpd_handle_t hd;
  int fd;
} ws_task_arg_t;

#if defined(LED_GPIO_NUM)
void enable_led(bool en) {
  int duty = en ? led_duty : 0;
  if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY)) {
    duty = CONFIG_LED_MAX_INTENSITY;
  }
  ledcWrite(LED_GPIO_NUM, duty);
  log_i("Set LED intensity to %d", duty);
}
#endif

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;

#if defined(LED_GPIO_NUM)
  enable_led(true);
  vTaskDelay(150 / portTICK_PERIOD_MS);
  fb = esp_camera_fb_get();
  enable_led(false);
#else
  fb = esp_camera_fb_get();
#endif

  if (!fb) {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%" PRIu32 ".%06" PRIu32, (uint32_t)fb->timestamp.tv_sec, (uint32_t)fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

  if (fb->format == PIXFORMAT_JPEG) {
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  } else {
    jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
  }
  esp_camera_fb_return(fb);
  return res;
}

static void ws_stream_task(void *pvParameters) {
  ws_task_arg_t *arg = (ws_task_arg_t *)pvParameters;
  httpd_handle_t hd = arg->hd;
  int fd = arg->fd;
  free(arg);

  log_i("WS stream task started for fd=%d", fd);

#if defined(LED_GPIO_NUM)
  isStreaming = true;
  enable_led(true);
#endif

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

#if defined(LED_GPIO_NUM)
  isStreaming = false;
  enable_led(false);
#endif

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

  // Drain any inbound frames; this firmware does not accept WS commands.
  httpd_ws_frame_t ws_pkt;
  uint8_t scratch[128];
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.payload = scratch;
  return httpd_ws_recv_frame(req, &ws_pkt, sizeof(scratch));
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf) {
  char *buf = NULL;
  size_t buf_len = 0;

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      *obuf = buf;
      return ESP_OK;
    }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf = NULL;
  char variable[32];
  char value[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int val = atoi(value);
  log_i("%s = %d", variable, val);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;

  if (!strcmp(variable, "framesize")) {
    if (s->pixformat == PIXFORMAT_JPEG) {
      res = s->set_framesize(s, (framesize_t)val);
    }
  } else if (!strcmp(variable, "quality")) {
    res = s->set_quality(s, val);
  } else if (!strcmp(variable, "contrast")) {
    res = s->set_contrast(s, val);
  } else if (!strcmp(variable, "brightness")) {
    res = s->set_brightness(s, val);
  } else if (!strcmp(variable, "saturation")) {
    res = s->set_saturation(s, val);
  } else if (!strcmp(variable, "gainceiling")) {
    res = s->set_gainceiling(s, (gainceiling_t)val);
  } else if (!strcmp(variable, "awb")) {
    res = s->set_whitebal(s, val);
  } else if (!strcmp(variable, "agc")) {
    res = s->set_gain_ctrl(s, val);
  } else if (!strcmp(variable, "aec")) {
    res = s->set_exposure_ctrl(s, val);
  } else if (!strcmp(variable, "hmirror")) {
    res = s->set_hmirror(s, val);
  } else if (!strcmp(variable, "vflip")) {
    res = s->set_vflip(s, val);
  } else if (!strcmp(variable, "awb_gain")) {
    res = s->set_awb_gain(s, val);
  } else if (!strcmp(variable, "agc_gain")) {
    res = s->set_agc_gain(s, val);
  } else if (!strcmp(variable, "aec_value")) {
    res = s->set_aec_value(s, val);
  } else if (!strcmp(variable, "aec2")) {
    res = s->set_aec2(s, val);
  } else if (!strcmp(variable, "dcw")) {
    res = s->set_dcw(s, val);
  } else if (!strcmp(variable, "bpc")) {
    res = s->set_bpc(s, val);
  } else if (!strcmp(variable, "wpc")) {
    res = s->set_wpc(s, val);
  } else if (!strcmp(variable, "raw_gma")) {
    res = s->set_raw_gma(s, val);
  } else if (!strcmp(variable, "lenc")) {
    res = s->set_lenc(s, val);
  } else if (!strcmp(variable, "special_effect")) {
    res = s->set_special_effect(s, val);
  } else if (!strcmp(variable, "wb_mode")) {
    res = s->set_wb_mode(s, val);
  } else if (!strcmp(variable, "ae_level")) {
    res = s->set_ae_level(s, val);
  }
#if defined(LED_GPIO_NUM)
  else if (!strcmp(variable, "led_intensity")) {
    led_duty = val;
    if (isStreaming) {
      enable_led(true);
    }
  }
#endif
  else {
    log_i("Unknown command: %s", variable);
    res = -1;
  }

  if (res < 0) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  char *end = json_response + sizeof(json_response);
  *p++ = '{';

  p += snprintf(p, end - p, "\"framesize\":%u,", s->status.framesize);
  p += snprintf(p, end - p, "\"quality\":%u,", s->status.quality);
  p += snprintf(p, end - p, "\"brightness\":%d,", s->status.brightness);
  p += snprintf(p, end - p, "\"contrast\":%d,", s->status.contrast);
  p += snprintf(p, end - p, "\"saturation\":%d,", s->status.saturation);
  p += snprintf(p, end - p, "\"awb\":%u,", s->status.awb);
  p += snprintf(p, end - p, "\"awb_gain\":%u,", s->status.awb_gain);
  p += snprintf(p, end - p, "\"aec\":%u,", s->status.aec);
  p += snprintf(p, end - p, "\"aec2\":%u,", s->status.aec2);
  p += snprintf(p, end - p, "\"ae_level\":%d,", s->status.ae_level);
  p += snprintf(p, end - p, "\"aec_value\":%u,", s->status.aec_value);
  p += snprintf(p, end - p, "\"agc\":%u,", s->status.agc);
  p += snprintf(p, end - p, "\"agc_gain\":%u,", s->status.agc_gain);
  p += snprintf(p, end - p, "\"hmirror\":%u,", s->status.hmirror);
  p += snprintf(p, end - p, "\"vflip\":%u,", s->status.vflip);
  p += snprintf(p, end - p, "\"ws_connected\":%s,", ws_client_fd >= 0 ? "true" : "false");
#if defined(LED_GPIO_NUM)
  p += snprintf(p, end - p, "\"led_intensity\":%u", led_duty);
#else
  p += snprintf(p, end - p, "\"led_intensity\":%d", -1);
#endif
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 5;

  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
  };

  httpd_uri_t cmd_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
  };

  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
  };

  httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
  };

  log_i("Starting control server on port: '%u'", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  log_i("Starting WebSocket stream server on port: '%u'", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &ws_uri);
  }
}

void setupLedFlash() {
#if defined(LED_GPIO_NUM)
  ledcAttach(LED_GPIO_NUM, 5000, 8);
#else
  log_i("LED flash is disabled -> LED_GPIO_NUM undefined");
#endif
}
