# Creating a display

A TFT screen the firmware draws on. The picture shows on the part in the 3D view, in camera images, and in Image plot canvases. Read [component_basics.md](component_basics.md) first. Reference: `models/display/display_tft_160x128`.

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

The surface is required; a display without one fails to load. Until something is drawn, the screen shows a dark "powered-off" panel.

## Interface

One input with target kind `display`, any name (for example `screen`). Only the **first** display signal of a part is driven.

## Pins

For an SPI TFT (ST7735): `sck` (`spi_sck`), `mosi` (`spi_mosi`), `cs`, `dc`, `rst` (`digital`), `led` (`pwm`, the backlight), `vcc`, `gnd`. The part listens on the board's SPI bus while `cs` is LOW. An undriven `led` counts as wired to VCC; LOW turns the backlight off, PWM dims it.

## Emulator

`"type": "display_emulator"`

| Parameter | Default | Effect |
|---|---|---|
| `refresh_hz` | 30 | how often the panel shows a new frame (sim time). Drawing faster than this just skips frames |

## Firmware: an ST7735 on SPI

From firmware the display is a real ST7735 controller: the sketch uses the Adafruit libraries (`Adafruit_ST7735`, `Adafruit_GFX`; RoboticsStudio ships a compatible subset), and the same sketch drives a real 1.8" TFT. See `examples/firmware/tft_display` and [firmware.md](firmware.md).

```cpp
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "robot_pins.h"
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);   // the part named "TFT"

void setup() {
  tft.initR(INITR_BLACKTAB);   // reset + wake-up: about 1 s, as on the real controller
  tft.setRotation(1);          // landscape, upright on this part
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 10); tft.setTextColor(ST77XX_WHITE); tft.print("Hello");
}
```

- The controller powers up asleep and dark: nothing shows before `initR()`.
- Commands handled: reset, sleep in/out, display on/off, colour inversion, the address window (`CASET`/`RASET`), `RAMWR` (RGB565 pixels), `MADCTL` (rotation, mirroring, BGR) and `COLMOD`. Others are accepted and ignored.
- The controller's memory is the native 128×160 portrait frame; this part's landscape screen shows it turned, as a module mounted on its side: `setRotation(1)` draws upright.
- Drawing appears as it happens (the controller has no back buffer); the panel refreshes at `refresh_hz`.
- SPI transfers take no simulated time at the Prototype level; bus timing comes with the Real level.

## Workarounds

- **Picture upside down:** rotate the surface 180° about its Z.
- **Picture mirrored:** the surface faces into the part. Its +Z must point toward the viewer, so rotate it 180° about Y and make sure it sits in front of the face.
- **Monochrome OLED look:** draw only black and one color (white, blue or yellow). There's no 1-bit mode yet.
- **Screen invisible in 3D:** the surface is inside the geom or facing inward. Check the dark quad in the editor preview.
