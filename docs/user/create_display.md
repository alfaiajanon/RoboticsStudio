# Creating a display

A TFT or OLED screen the script draws on. The picture shows on the part in the 3D view, in camera images, and in Image plot canvases. Read [component_basics.md](component_basics.md) first. Reference: `models/display/display_tft_160x128`.

## Structure

| Piece | Setup |
|---|---|
| body `base` | the PCB box, about 10 g |
| geom `screen` | the glass area, for example a thin box on the PCB's front. The display targets this geom |
| connectors | `back` (behind the board, Z pointing back: the screen faces away from what it's mounted on), optionally `bottom_edge` (stands upright) |

## Device

```json
"displays": [{ "id": "panel", "target": {"kind": "geom", "id": "screen"},
               "resolution": [160, 128],
               "surface": { "transform": {"pos": [0, 0, 0.0017], "quat": [1, 0, 0, 0]},
                            "size": [0.017, 0.0136] } }]
```

| Field | Effect |
|---|---|
| `resolution` | framebuffer pixels: 128×64 (SSD1306 OLED), 160×128 (1.8" TFT), 320×240 (2.4" TFT) |
| `surface.transform` | where the picture is drawn, in the screen geom's **body** frame. The picture faces the surface's **+Z**; image right is **+X**, image up is **+Y** |
| `surface.size` | half-width and half-height in meters. Match the aspect of `resolution` or pixels get stretched |

Put the surface about **0.1 mm in front** of the geom's face, or it flickers (z-fighting). In the Component Editor, **Fit Surface to Geom** does this for box geoms: it covers the box's +Z face.

The surface is required; a display without one fails to load. Until the script sends a frame, the screen shows a dark "powered-off" panel.

## Interface

One input with target kind `display`, any name (for example `screen`). Only the **first** display signal of a part is driven.

## Emulator

`"type": "display_emulator"`

| Parameter | Default | Effect |
|---|---|---|
| `refresh_hz` | 30 | how often the panel shows a new frame (sim time). Drawing faster than this just skips frames |
| `transfer_ms` | 0 | how long `display()` blocks the script, like pushing the buffer over SPI/I²C. About 5–20 ms for a 160×128 TFT, 25+ ms for an I²C OLED. Max 200 |

## Script API

Drawing goes to a hidden buffer; **nothing shows until `display()`**, like Adafruit GFX. Colors are numbers `0xRRGGBB` or strings (`"#ff8800"`, `"red"`).

```javascript
let screen = comp_3;
let n = 0;
function loop() {
    screen.clear();
    screen.fillRect(0, 0, screen.width(), 12, 0x003366);
    screen.drawText(2, 1, "count " + n++, 0xffffff, 10);
    screen.drawCircle(80, 70, 20, "yellow");
    screen.display();
    delay(100);
}
```

| Call | |
|---|---|
| `width()`, `height()` | resolution |
| `clear()`, `fill(c)` | whole buffer black / color |
| `drawPixel(x, y, c)` | |
| `drawLine(x0, y0, x1, y1, c)` | |
| `drawRect` / `fillRect(x, y, w, h, c)` | |
| `drawCircle` / `fillCircle(x, y, r, c)` | |
| `drawText(x, y, text, c, size)` | (x, y) = top-left, size in pixels (default 8) |
| `drawGray(data, w, h, x, y)` | blit a grayscale `ArrayBuffer`, for example a camera's `toGray()` |
| `drawRGB(data, w, h, x, y)` | blit an RGB888 `ArrayBuffer`, for example a camera frame's `data` |
| `display()` | send the buffer to the panel (blocks `transfer_ms`) |

## Workarounds

- **Picture upside down:** rotate the surface 180° about its Z.
- **Picture mirrored:** the surface faces into the part. Its +Z must point toward the viewer, so rotate it 180° about Y and make sure it sits in front of the face.
- **Monochrome OLED look:** draw only black and one color (white, blue or yellow). There's no 1-bit mode yet.
- **Screen invisible in 3D:** the surface is inside the geom or facing inward. Check the dark quad in the editor preview.
