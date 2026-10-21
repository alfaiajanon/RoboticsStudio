/*
 * esp_camera.h for RoboticsStudio: a compatible subset of Espressif's
 * esp32-camera driver (our own implementation), so camera sketches written
 * for an ESP32-CAM build and run here.
 *
 * The camera is found through config.pin_sccb_sda (its SCCB data pin: a name
 * in robot_pins.h at the Prototype level, the board's GPIO number at the Real level). frame_size must match the part's resolution and
 * pixel_format must be PIXFORMAT_GRAYSCALE or PIXFORMAT_RGB565 (RGB565 bytes
 * high byte first, as the ESP32 driver delivers them); otherwise
 * esp_camera_init() fails like the real driver. Frames come with the part's
 * latency, noise and drops. esp_camera_fb_get() waits (in simulated time) for
 * the next frame.
 *
 * RoboticsStudio-only, does NOT exist on a real ESP32: PIXFORMAT_DEPTH16
 * (depth cameras: millimetres, 0 = no return) and FRAMESIZE_RS_80X60.
 * Sensor settings (esp_camera_sensor_get()->set_vflip(...), ...) are
 * accepted and have no effect.
 */
#ifndef ESP_CAMERA_H
#define ESP_CAMERA_H

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

typedef int esp_err_t;
#define ESP_OK                                 0
#define ESP_FAIL                               -1
#define ESP_ERR_NO_MEM                         0x101
#define ESP_ERR_INVALID_ARG                    0x102
#define ESP_ERR_CAMERA_NOT_DETECTED            0x20001
#define ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE 0x20002
#define ESP_ERR_CAMERA_NOT_SUPPORTED           0x20004

typedef enum {
    PIXFORMAT_RGB565,
    PIXFORMAT_YUV422,
    PIXFORMAT_YUV420,
    PIXFORMAT_GRAYSCALE,
    PIXFORMAT_JPEG,
    PIXFORMAT_RGB888,
    PIXFORMAT_RAW,
    PIXFORMAT_RGB444,
    PIXFORMAT_RGB555,
    PIXFORMAT_DEPTH16,      // RoboticsStudio only
} pixformat_t;

typedef enum {
    FRAMESIZE_96X96,        // 96x96
    FRAMESIZE_QQVGA,        // 160x120
    FRAMESIZE_128X128,      // 128x128
    FRAMESIZE_QCIF,         // 176x144
    FRAMESIZE_HQVGA,        // 240x176
    FRAMESIZE_240X240,      // 240x240
    FRAMESIZE_QVGA,         // 320x240
    FRAMESIZE_320X320,      // 320x320
    FRAMESIZE_CIF,          // 400x296
    FRAMESIZE_HVGA,         // 480x320
    FRAMESIZE_VGA,          // 640x480
    FRAMESIZE_RS_80X60,     // 80x60, RoboticsStudio only
    FRAMESIZE_INVALID
} framesize_t;

typedef enum { LEDC_TIMER_0, LEDC_TIMER_1, LEDC_TIMER_2, LEDC_TIMER_3 } ledc_timer_t;
typedef enum { LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3,
               LEDC_CHANNEL_4, LEDC_CHANNEL_5, LEDC_CHANNEL_6, LEDC_CHANNEL_7 } ledc_channel_t;
typedef enum { CAMERA_FB_IN_PSRAM, CAMERA_FB_IN_DRAM } camera_fb_location_t;
typedef enum { CAMERA_GRAB_WHEN_EMPTY, CAMERA_GRAB_LATEST } camera_grab_mode_t;

typedef struct {
    int pin_pwdn;
    int pin_reset;
    int pin_xclk;
    union { int pin_sccb_sda; int pin_sscb_sda; };
    union { int pin_sccb_scl; int pin_sscb_scl; };
    int pin_d7, pin_d6, pin_d5, pin_d4, pin_d3, pin_d2, pin_d1, pin_d0;
    int pin_vsync;
    int pin_href;
    int pin_pclk;
    int xclk_freq_hz;
    ledc_timer_t ledc_timer;
    ledc_channel_t ledc_channel;
    pixformat_t pixel_format;
    framesize_t frame_size;
    int jpeg_quality;
    size_t fb_count;
    camera_fb_location_t fb_location;
    camera_grab_mode_t grab_mode;
    int sccb_i2c_port;
} camera_config_t;

typedef struct {
    uint8_t* buf;
    size_t len;
    size_t width;
    size_t height;
    pixformat_t format;
    struct timeval timestamp;
} camera_fb_t;

typedef struct _sensor sensor_t;
struct _sensor {
    int (*set_vflip)(sensor_t* sensor, int enable);
    int (*set_hmirror)(sensor_t* sensor, int enable);
    int (*set_brightness)(sensor_t* sensor, int level);
    int (*set_contrast)(sensor_t* sensor, int level);
    int (*set_saturation)(sensor_t* sensor, int level);
};

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_camera_init(const camera_config_t* config);
esp_err_t esp_camera_deinit(void);
camera_fb_t* esp_camera_fb_get(void);
void esp_camera_fb_return(camera_fb_t* fb);
sensor_t* esp_camera_sensor_get(void);

#ifdef __cplusplus
}
#endif

#endif   // ESP_CAMERA_H
