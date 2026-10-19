// MPU6050 over I2C with plain Wire register reads, as in most tutorials.
// The chip powers up asleep: it must be woken first. Every unit has its own
// offsets, so the gyro is calibrated at rest before use.
#include <Wire.h>
#include <math.h>

const uint8_t MPU = 0x68;          // 0x69 with AD0 high
float gyroOffset[3] = {0, 0, 0};

void writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

// Raw accel XYZ, temperature, gyro XYZ (big-endian int16).
void readRaw(int16_t v[7]) {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, (uint8_t)14);
  for (int i = 0; i < 7; i++) {
    int hi = Wire.read();
    int lo = Wire.read();
    v[i] = (int16_t)((hi << 8) | lo);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  writeReg(0x6B, 0x00);            // wake up
  writeReg(0x1A, 0x03);            // digital low-pass filter: 44 Hz
  writeReg(0x1B, 0x00);            // gyro +-250 deg/s: 131 LSB per deg/s
  writeReg(0x1C, 0x00);            // accel +-2 g: 16384 LSB per g
  delay(100);

  Serial.println("calibrating the gyro, keep still");
  long sum[3] = {0, 0, 0};
  int16_t v[7];
  for (int i = 0; i < 200; i++) {
    readRaw(v);
    for (int a = 0; a < 3; a++) sum[a] += v[4 + a];
    delay(5);
  }
  for (int a = 0; a < 3; a++) gyroOffset[a] = sum[a] / 200.0;
}

void loop() {
  int16_t v[7];
  readRaw(v);
  float ax = v[0] / 16384.0, ay = v[1] / 16384.0, az = v[2] / 16384.0;
  float gz = (v[6] - gyroOffset[2]) / 131.0;
  float pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / M_PI;
  float roll = atan2(ay, az) * 180.0 / M_PI;
  Serial.print("pitch "); Serial.print(pitch, 1);
  Serial.print("  roll "); Serial.print(roll, 1);
  Serial.print("  yaw rate "); Serial.println(gz, 2);
  delay(100);
}
