// Camera frames with the esp32-camera driver, as on an ESP32-CAM. Name the
// camera part "Camera". Each frame arrives after the camera's latency, with
// its noise; a frame can be dropped. Prints the mean brightness of the image
// and of its left and right halves (a simple "where is the light" sensor).
#include "esp_camera.h"
#include "robot_pins.h"

void setup() {
  Serial.begin(115200);
  camera_config_t config = {};
  config.pin_pwdn = CAMERA_PWDN;
  config.pin_reset = CAMERA_RESET;
  config.pin_xclk = CAMERA_XCLK;
  config.pin_sccb_sda = CAMERA_SDA;
  config.pin_sccb_scl = CAMERA_SCL;
  config.pin_d7 = CAMERA_D7; config.pin_d6 = CAMERA_D6; config.pin_d5 = CAMERA_D5; config.pin_d4 = CAMERA_D4;
  config.pin_d3 = CAMERA_D3; config.pin_d2 = CAMERA_D2; config.pin_d1 = CAMERA_D1; config.pin_d0 = CAMERA_D0;
  config.pin_vsync = CAMERA_VSYNC;
  config.pin_href = CAMERA_HREF;
  config.pin_pclk = CAMERA_PCLK;
  config.xclk_freq_hz = 20000000;
  config.ledc_timer = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_QQVGA;   // 160x120
  config.fb_count = 1;
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) Serial.printf("camera init failed: 0x%x\n", err);
}

void loop() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { Serial.println("no frame"); return; }
  unsigned long left = 0, right = 0;
  for (size_t y = 0; y < fb->height; y++)
    for (size_t x = 0; x < fb->width; x++) {
      uint8_t v = fb->buf[y * fb->width + x];
      if (x < fb->width / 2) left += v; else right += v;
    }
  const unsigned long half = fb->width / 2 * fb->height;
  Serial.printf("mean %lu  left %lu  right %lu\n", (left + right) / (2 * half), left / half, right / half);
  esp_camera_fb_return(fb);
}
