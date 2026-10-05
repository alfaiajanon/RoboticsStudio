# Creating a camera (RGB or depth)

A camera module like the OV7670 or ESP32-CAM, or a cheap depth sensor: it renders the scene from the part's lens and hands frames to the script, with latency, noise and dropped frames. Read [component_basics.md](component_basics.md) first. References: `models/camera/cam_ov7670`, `models/camera/depth_cam_80x60`.

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
| `resolution` | pixels. Keep it small: 160×120 (QQVGA) is realistic for cheap modules and fast to process in scripts. Large sizes cost GPU time and script time |
| `fovy` | **vertical** field of view in degrees. OV7670 modules ≈ 25–50 depending on the lens; wide-angle ≈ 90 |

## Interface

One output per camera, target kind `camera`, any name (for example `image` or `depth`). Several cameras in one part (RGB-D) means several outputs; the script picks one by name.

## Emulator

`"type": "camera_emulator"`

| Parameter | Default | Effect |
|---|---|---|
| `fps` | 15 | frames per **simulated** second |
| `latency_ms` | 50 | delay from capture until the script can see the frame (sim time). Real modules: 30–150 ms |
| `drop_probability` | 0 | chance each frame is lost (0..1). 0.01–0.05 feels like a flaky cable |
| `noise_stddev` | 0 | rgb: per-pixel noise on a 0–255 scale. 3–8 for cheap sensors |
| `color_gain` | `[1,1,1]` | rgb: per-channel gain. For example `[1.05, 1, 0.9]` gives the warm cast of an OV7670 |
| `bits_per_channel` | 8 | rgb: quantisation. 5–6 gives the banding of RGB565 modules |
| `depth_noise_stddev` | 0 | depth: noise in meters (≈ 1 % of range for cheap ToF) |
| `depth_min` / `depth_max` | 0 / 0 | depth: outside this range reads 0, like a real sensor. 0 = no limit |

## Script API

Every call takes an optional signal name for multi-camera parts, for example `cam.capture("depth")`.

```javascript
let cam = comp_2;
function loop() {
    if (cam.available()) {                     // a frame newer than the last capture()
        let f = cam.capture();                 // {width, height, time, seq, format, data}
        let px = new Uint8Array(f.data);       // rgb: 3 bytes per pixel, rows top to bottom
        // depth: new Float32Array(f.data), meters
        let c = cam.regionMean(70, 50, 20, 20);   // {r, g, b, gray} or {depth}
        console.log("center gray " + c.gray + "  dropped " + cam.droppedFrames());
    }
    delay(20);
}
```

| Call | Returns |
|---|---|
| `capture()` | the newest frame, `undefined` before the first one |
| `available()` / `seq()` | is there a new frame / frame counter |
| `width()` / `height()` | resolution |
| `getPixel(x, y)` | `{r, g, b}` (rgb) or `{depth}` |
| `getDepth(x, y)` | meters; 0 = no return; −1 = not a depth camera or out of range |
| `toGray()` | `ArrayBuffer`, 1 byte per pixel (depth: near = bright) |
| `regionMean(x, y, w, h)` | mean over a rectangle |
| `droppedFrames()` / `lateFrames()` | frames lost / frames the renderer delivered after their latency had passed |

Per-pixel loops in the script engine are slow. Prefer `regionMean`, `toGray` and small resolutions.

To see what the camera sees, add an **Image** canvas in the Plot panel with the camera's signal.

## Workarounds

- **Black or empty images:** the site points into the part or the mount. Check the preview's view cone, or rotate the snap angle when attaching.
- **`lateFrames()` keeps rising:** the GPU can't keep up with `fps` at the current time scale. Lower `fps` or the resolution. Physics is never slowed down; frames just arrive late or are dropped.
- **Motion blur and rolling shutter** aren't modelled. For blur, average consecutive frames in the script.
