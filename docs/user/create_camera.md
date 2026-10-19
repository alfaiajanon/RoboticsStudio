# Creating a camera (RGB or depth)

A camera module like the OV7670 or ESP32-CAM, or a cheap depth sensor: it renders the scene from the part's lens and hands frames to the firmware, with latency, noise and dropped frames. Read [component_basics.md](component_basics.md) first. References: `models/camera/cam_ov7670`, `models/camera/depth_cam_80x60`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the PCB plus a lens cylinder, about 8 g |
| site `lens_tip` | at the front of the lens. **The camera looks along the site's −Z axis, with +Y as image up** |
| connectors | `back` (behind the board, Z out of the back: the lens faces away from what it's mounted on), and optionally `bottom_edge` (on the board's lower edge, so it stands upright with the lens horizontal) |

Turn on **Cameras** in the editor preview: it draws the real view cone from the site, with a small triangle on the top edge. If it points the wrong way, rotate the site.

## Device

```json
"cameras": [{ "id": "cam", "type": "rgb", "target": {"kind": "site", "id": "lens_tip"},
              "resolution": [160, 120], "fovy": 45 }]
```

| Field | Effect |
|---|---|
| `type` | `rgb` (3 bytes per pixel) or `depth` (float meters along the view direction, 0 = no return) |
| `resolution` | pixels. Keep it small: 160×120 (QQVGA) is realistic for cheap modules and fast to process in firmware. Large sizes cost GPU time and memory (a real ESP32 has about 300 KB of RAM without PSRAM) |
| `fovy` | **vertical** field of view in degrees. OV7670 modules ≈ 25–50 depending on the lens; wide-angle ≈ 90 |

## Interface

One output per camera, target kind `camera`, any name (for example `image` or `depth`). Several cameras in one part (RGB-D) means several outputs.

## Pins

`sda` (`i2c_sda`) and `scl` (`i2c_scl`): the SCCB configuration bus, through which firmware finds the camera (`config.pin_sccb_sda`). The OV7670 also lists its parallel-interface pins (`xclk`, `pclk`, `vsync`, `href`, `d0`…`d7`, `reset`, `pwdn`, all `digital`), so a sketch can fill in a full ESP32-CAM camera config with the same names on both.

## Emulator

`"type": "camera_emulator"`

| Parameter | Default | Effect |
|---|---|---|
| `fps` | 15 | frames per **simulated** second |
| `latency_ms` | 50 | delay from capture until the firmware can get the frame (sim time). Real modules: 30–150 ms |
| `drop_probability` | 0 | chance each frame is lost (0..1). 0.01–0.05 feels like a flaky cable. Drawn from the project's Random Seed, so the same frames drop on every run |
| `noise_stddev` | 0 | rgb: per-pixel noise on a 0–255 scale. 3–8 for cheap sensors |
| `color_gain` | `[1,1,1]` | rgb: per-channel gain. For example `[1.05, 1, 0.9]` gives the warm cast of an OV7670 |
| `bits_per_channel` | 8 | rgb: quantisation. 5–6 gives the banding of RGB565 modules |
| `depth_noise_stddev` | 0 | depth: noise in meters (≈ 1 % of range for cheap ToF) |
| `depth_min` / `depth_max` | 0 / 0 | depth: outside this range reads 0, like a real sensor. 0 = no limit |

## Firmware: esp32-camera

From firmware the camera works like on an ESP32-CAM, with Espressif's camera driver API (RoboticsStudio ships a compatible subset): `esp_camera_init(&config)`, then `esp_camera_fb_get()` / `esp_camera_fb_return()`. See `examples/firmware/esp32_camera` and [firmware.md](firmware.md).

The library also has the **ESP32-CAM board** itself (`models/board/esp32_cam`): a board with its OV2640 camera built in, wired on the PCB. At the Real level its camera pins are the board's own (`ESP32_CAM_SDA` = 26, `ESP32_CAM_D0` = 5, ...; the AI-Thinker pinout, no reset pin) and need no wires; its camera device and emulator are the same as this guide's, in the board's `.rsdef`. `demo/Robot arm (ESP32-CAM)` uses it. See [wiring.md](wiring.md).

```cpp
#include "esp_camera.h"
#include "robot_pins.h"

void setup() {
  camera_config_t config = {};
  config.pin_sccb_sda = CAMERA_SDA;            // finds the part named "Camera"
  config.pin_sccb_scl = CAMERA_SCL;
  // ... the data pins (CAMERA_D0..D7, VSYNC, HREF, PCLK, XCLK, PWDN, RESET), as on a real board
  config.pixel_format = PIXFORMAT_GRAYSCALE;   // or PIXFORMAT_RGB565
  config.frame_size = FRAMESIZE_QQVGA;         // must match the part: 160x120
  config.fb_count = 1;
  esp_camera_init(&config);
}

void loop() {
  camera_fb_t* fb = esp_camera_fb_get();       // waits for the next frame
  if (!fb) return;                             // no frame within 2 s
  // fb->buf: fb->width x fb->height bytes (grayscale), or 2 bytes per pixel (RGB565, high byte first)
  esp_camera_fb_return(fb);
}
```

- `esp_camera_init()` fails like the real driver when the frame size doesn't match the part (0x20002), the format isn't grayscale or RGB565 (0x20004), or no camera has that SCCB pin (0x20001).
- `esp_camera_fb_get()` waits in simulated time for a frame newer than the last one; frames come at `fps`, after `latency_ms`, with this part's noise and drops. With all `fb_count` buffers still out it returns NULL.
- Sensor settings (`esp_camera_sensor_get()->set_vflip(...)` …) are accepted and have no effect.
- **Depth cameras:** the same API with `PIXFORMAT_DEPTH16` (millimetres, 0 = no return) and, for the 80×60 part, `FRAMESIZE_RS_80X60`. Both are RoboticsStudio-only: a depth sketch doesn't compile for a real ESP32.

To see what the camera sees, add an **Image** canvas in the Plot panel with the camera's signal.

## Workarounds

- **Black or empty images:** the site points into the part or the mount. Check the preview's view cone, or rotate the snap angle when attaching.
- **The simulation runs slower than real time with cameras:** the simulation waits for every frame, so the firmware always gets it exactly `latency_ms` after capture. If the GPU can't render `fps` frames per second at the chosen speed, the real-time factor drops instead. Lower `fps` or the resolution if that bothers you.
- **Motion blur and rolling shutter** aren't modelled. For blur, average consecutive frames in the firmware.
