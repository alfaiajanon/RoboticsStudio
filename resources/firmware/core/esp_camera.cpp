#include "Arduino.h"
#include "esp_camera.h"

// One camera, as on an ESP32-CAM. fb_count frame buffers, handed out by
// esp_camera_fb_get() and given back by esp_camera_fb_return().
static const int kMaxBuffers = 4;
static int cameraPin = -1;
static pixformat_t cameraFormat = PIXFORMAT_GRAYSCALE;
static size_t frameBytes = 0;
static camera_fb_t buffers[kMaxBuffers];
static bool inUse[kMaxBuffers];
static int bufferCount = 0;

static bool frameSize(framesize_t size, size_t* w, size_t* h) {
    static const uint16_t sizes[][2] = {{96, 96}, {160, 120}, {128, 128}, {176, 144}, {240, 176}, {240, 240},
                                        {320, 240}, {320, 320}, {400, 296}, {480, 320}, {640, 480}, {80, 60}};
    if (size < 0 || size >= FRAMESIZE_INVALID) return false;
    *w = sizes[size][0];
    *h = sizes[size][1];
    return true;
}

static int noEffect(sensor_t*, int) { return 0; }
static sensor_t sensor = {noEffect, noEffect, noEffect, noEffect, noEffect};

esp_err_t esp_camera_init(const camera_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    esp_camera_deinit();
    size_t w = 0, h = 0;
    if (!frameSize(config->frame_size, &w, &h)) return ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE;
    const esp_err_t err = rs_camera_init(config->pin_sccb_sda, config->pixel_format, int32_t(w), int32_t(h));
    if (err != ESP_OK) return err;
    cameraPin = config->pin_sccb_sda;
    cameraFormat = config->pixel_format;
    frameBytes = w * h * (cameraFormat == PIXFORMAT_GRAYSCALE ? 1 : 2);
    bufferCount = config->fb_count < 1 ? 1 : (config->fb_count > kMaxBuffers ? kMaxBuffers : int(config->fb_count));
    for (int i = 0; i < bufferCount; i++) {
        buffers[i].buf = static_cast<uint8_t*>(malloc(frameBytes));
        if (!buffers[i].buf) { esp_camera_deinit(); return ESP_ERR_NO_MEM; }
        inUse[i] = false;
    }
    return ESP_OK;
}

esp_err_t esp_camera_deinit(void) {
    for (int i = 0; i < bufferCount; i++) { free(buffers[i].buf); buffers[i].buf = nullptr; inUse[i] = false; }
    bufferCount = 0;
    cameraPin = -1;
    return ESP_OK;
}

camera_fb_t* esp_camera_fb_get(void) {
    if (cameraPin < 0) return nullptr;
    int free_ = -1;
    for (int i = 0; i < bufferCount; i++) if (!inUse[i]) { free_ = i; break; }
    if (free_ < 0) return nullptr;   // every buffer is still out: return them with esp_camera_fb_return()
    uint32_t meta[6] = {0, 0, 0, 0, 0, 0};
    const int32_t n = rs_camera_frame(cameraPin, buffers[free_].buf, uint32_t(frameBytes), meta);
    if (n <= 0) return nullptr;      // no frame in time
    camera_fb_t* fb = &buffers[free_];
    fb->len = size_t(n);
    fb->width = meta[0];
    fb->height = meta[1];
    fb->format = pixformat_t(meta[2]);
    const uint64_t us = (uint64_t(meta[5]) << 32) | meta[4];
    fb->timestamp.tv_sec = long(us / 1000000);
    fb->timestamp.tv_usec = long(us % 1000000);
    inUse[free_] = true;
    return fb;
}

void esp_camera_fb_return(camera_fb_t* fb) {
    for (int i = 0; i < bufferCount; i++) if (fb == &buffers[i]) inUse[i] = false;
}

sensor_t* esp_camera_sensor_get(void) {
    return cameraPin >= 0 ? &sensor : nullptr;
}
