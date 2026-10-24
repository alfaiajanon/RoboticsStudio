// RoboticsStudio Robot arm firmware -- the Uno's half of the two-board arm.
// The ESP32-CAM (arm_vision.ino) sees the ball and works out the arm's
// motion; this Uno drives the three MG996R servos (D9, D10, D11) from the
// pulse widths it sends over a serial link:
//     P <yaw us> <shoulder us> <elbow us>\n
// The link is a SoftwareSerial on D2 (RX) and D3 (TX) at 57600 baud, the
// fastest SoftwareSerial receives reliably on a 16 MHz Uno, through a BSS138
// level shifter (the ESP32-CAM is a 3.3 V board). Serial (USB) stays free for
// the Serial Monitor.
//
// The reader never waits: it takes what has arrived, one character at a
// time, and acts on a complete line. If no command comes for half a second
// the servos hold their last position and the Serial Monitor says so.
//
// The servos are attached on the first command, not in setup(): attach()
// starts pulsing at 1500 us at once, which would swing the arm to its centre
// pose until the ESP32-CAM is up (it boots, starts its camera, and its first
// line can go out before this board listens). Unattached, they stay limp.

#include <Servo.h>
#include <SoftwareSerial.h>

const int YAW_PIN      = 9;
const int SHOULDER_PIN = 10;
const int ELBOW_PIN    = 11;
const int LINK_RX      = 2;
const int LINK_TX      = 3;
const long LINK_BAUD   = 57600;
const unsigned long LINK_TIMEOUT_MS = 500;

SoftwareSerial link(LINK_RX, LINK_TX);
Servo servos[3];

char line[32];
int lineLength = 0;
unsigned long lastCommandMs = 0;
bool linkUp = false;
unsigned long commands = 0, badLines = 0;

// "P 1500 1500 1500" -> the three pulse widths; false if the line is garbled.
bool parse(const char* text, int us[3]) {
  if (text[0] != 'P') return false;
  const char* p = text + 1;
  for (int i = 0; i < 3; i++) {
    char* end;
    const long v = strtol(p, &end, 10);
    if (end == p || v < 1000 || v > 2000) return false;
    us[i] = (int)v;
    p = end;
  }
  return true;
}

void onLine() {
  int us[3];
  if (!parse(line, us)) {
    badLines++;
    return;
  }
  const int pins[3] = {YAW_PIN, SHOULDER_PIN, ELBOW_PIN};
  for (int i = 0; i < 3; i++) {
    if (!servos[i].attached()) servos[i].attach(pins[i], 1000, 2000);
    servos[i].writeMicroseconds(us[i]);
  }
  commands++;
  lastCommandMs = millis();
  if (!linkUp) {
    linkUp = true;
    Serial.println("servos: link up, following the ESP32-CAM");
  }
}

void setup() {
  Serial.begin(115200);
  link.begin(LINK_BAUD);
  Serial.println("servos: waiting for the ESP32-CAM");
}

void loop() {
  while (link.available()) {
    const char c = link.read();
    if (c == '\n') {
      line[lineLength] = '\0';
      onLine();
      lineLength = 0;
    } else if (c != '\r' && lineLength < (int)sizeof(line) - 1) {
      line[lineLength++] = c;
    }
  }
  if (link.overflow()) Serial.println("servos: link buffer overflowed, a command was lost");

  if (linkUp && millis() - lastCommandMs > LINK_TIMEOUT_MS) {
    linkUp = false;
    Serial.println("servos: link lost, holding position");
  }

  // Once a second: how many commands came in, and how many were garbled.
  static unsigned long nextReport = 1000;
  if (millis() >= nextReport) {
    nextReport += 1000;
    Serial.print("servos: ");
    Serial.print(commands);
    Serial.print(" commands, ");
    Serial.print(badLines);
    Serial.println(" garbled");
  }
}
