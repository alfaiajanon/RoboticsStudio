// 1.8" ST7735 TFT over SPI with the Adafruit libraries. Name the display part
// "TFT". The display powers up asleep and dark until initR() wakes it.
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "robot_pins.h"

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);              // landscape
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Hello!");
  tft.drawRect(0, 0, tft.width(), tft.height(), ST77XX_BLUE);
}

void loop() {
  static int x = 10;
  tft.fillRect(10, 60, 140, 20, ST77XX_BLACK);   // clear the bar
  tft.fillRect(10, 60, x, 20, tft.color565(255, 128, 0));
  tft.setTextSize(1);
  tft.setCursor(10, 100);
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);   // with background: overwrites the old text
  tft.print("t = ");
  tft.print(millis() / 1000.0, 1);
  tft.print(" s ");
  x = x >= 140 ? 10 : x + 10;
  delay(200);
}
