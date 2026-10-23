// Serial Monitor example: type into the Serial Monitor tab (Newline line
// ending) and the board answers. Works on any board and at any level: no
// part, no pin.
//
//   hello          -> echo: hello
//   add 2 40       -> 2 + 40 = 42
//   blink 3        -> counts to 3, one line every 200 ms
//
// What it shows:
//   - Serial.available() / readStringUntil('\n'): a line typed in the monitor
//     arrives byte by byte, at the monitor's baud rate (Real level).
//   - parseInt() on a String's numbers, done by hand with toInt().
//   - At the Real level the monitor's baud rate must match Serial.begin():
//     set the monitor to 115200, or both directions read garbage.

void setup() {
  Serial.begin(115200);
  Serial.println("serial_echo: type a line (hello, add 2 40, blink 3)");
}

void loop() {
  if (!Serial.available()) {
    delay(5);
    return;
  }
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("add ")) {
    const int space = line.indexOf(' ', 4);
    const long a = line.substring(4, space).toInt();
    const long b = space > 0 ? line.substring(space + 1).toInt() : 0;
    Serial.print(a); Serial.print(" + "); Serial.print(b); Serial.print(" = "); Serial.println(a + b);
  } else if (line.startsWith("blink ")) {
    const long n = line.substring(6).toInt();
    for (long i = 1; i <= n; i++) {
      Serial.print("blink "); Serial.println(i);
      delay(200);
    }
  } else {
    Serial.print("echo: ");
    Serial.println(line);
  }
}
