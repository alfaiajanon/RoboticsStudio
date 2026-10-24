/*
 * rs_abi.h: everything the firmware asks of the simulator.
 *
 * The robot's firmware is compiled to WebAssembly; these are its imports
 * from the wasm module "rs", served by the simulator's FirmwareRuntime. The
 * Arduino core (Arduino.h, Servo.h, ...) is written on top of them, so a
 * sketch never calls them directly.
 *
 * All times are simulated time. Calls that take time (delay_us, yield) hand
 * control back to the physics until that time is reached; the firmware's
 * own code takes no simulated time, except a small fixed cost per
 * millis()/micros() call and per loop().
 *
 * Pins are board pin numbers (the ideal board's, named in robot_pins.h, at the
 * Prototype level; the real board's own at the Real level). An unknown pin is ignored (the
 * simulator logs it once). What a pin can do, the PWM frequency, the ADC
 * resolution and the bus speeds are the board's: the ideal board (Prototype)
 * does everything, instantly; a real board (Real level) has its own limits.
 *
 * wasi-libc also imports four WASI calls (fd_write, fd_seek, fd_close,
 * clock_time_get) for printf and time(); the simulator serves those too:
 * stdout/stderr go to the Output panel, the clock is simulated time.
 */
#ifndef RS_ABI_H
#define RS_ABI_H

#include <stdint.h>

#define RS_IMPORT(name) __attribute__((import_module("rs"), import_name(#name)))

#ifdef __cplusplus
extern "C" {
#endif

/* ---- time ---- */
RS_IMPORT(millis)       uint32_t rs_millis(void);
RS_IMPORT(micros)       uint32_t rs_micros(void);
RS_IMPORT(delay_us)     void     rs_delay_us(uint32_t us);
RS_IMPORT(yield)        void     rs_yield(void);          /* let the physics run, at the current time */

/* ---- pins ---- */
RS_IMPORT(pin_mode)     void     rs_pin_mode(int32_t pin, int32_t mode);
RS_IMPORT(digital_write) void    rs_digital_write(int32_t pin, int32_t level);
RS_IMPORT(digital_read) int32_t  rs_digital_read(int32_t pin);
/* 0..255 duty at the board's PWM frequency for that pin (ideal board: 490 Hz).
   On a real board's pin without PWM: digitalWrite(value >= 128), as on an Uno. */
RS_IMPORT(analog_write) void     rs_analog_write(int32_t pin, int32_t value);
/* The board's ADC counts: 0..1023 on the ideal board and an Uno, 0..4095 on an ESP32. */
RS_IMPORT(analog_read)  int32_t  rs_analog_read(int32_t pin);
RS_IMPORT(pwm_write_us) void     rs_pwm_write_us(int32_t pin, int32_t us);      /* servo pulse; us <= 0 stops it */
RS_IMPORT(pwm_write_ns) void     rs_pwm_write_ns(int32_t pin, int32_t ns);      /* the same in ns (timer ticks finer than 1 us) */

/* ---- buses (the board's shared I2C and SPI buses; transaction level) ---- */
/* I2C write transaction; stop = 0: a repeated start follows.
   Returns Wire::endTransmission()'s status: 0 ok, 2 no device at the address. */
RS_IMPORT(i2c_write)    int32_t  rs_i2c_write(int32_t address, const uint8_t* data, uint32_t len, int32_t stop);
/* I2C read transaction: returns the bytes read (0 = no device). */
RS_IMPORT(i2c_read)     int32_t  rs_i2c_read(int32_t address, uint8_t* data, uint32_t len);
/* SPI: clocks the bytes out to every part whose chip-select pin is LOW. */
RS_IMPORT(spi_transfer) void     rs_spi_transfer(const uint8_t* data, uint32_t len);
/* Bus clocks. On a real board a transaction takes its bit time in simulated
   time (I2C: 9 bits per byte incl. the address; SPI: 8 bits per byte, capped
   at the board's maximum). The ideal board's buses take no time. */
RS_IMPORT(i2c_set_clock) void    rs_i2c_set_clock(uint32_t hz);
RS_IMPORT(spi_set_clock) void    rs_spi_set_clock(uint32_t hz);

/* ---- camera (esp32-camera subset), reached through its SCCB data pin ---- */
/* Checks the frame size and pixel format against the part; returns 0 or an
   esp32-camera error code. Formats: RGB565 0, GRAYSCALE 3, DEPTH16 9. */
RS_IMPORT(camera_init)  int32_t  rs_camera_init(int32_t sda_pin, int32_t format, int32_t width, int32_t height);
/* Blocks in simulated time until a frame newer than the last one, writes it
   into buf and 6 x uint32 into meta (width, height, format, seq, timestamp us
   low, high); returns the bytes written, 0 on timeout (2 s) or a short buf. */
RS_IMPORT(camera_frame) int32_t  rs_camera_frame(int32_t sda_pin, uint8_t* buf, uint32_t cap, uint32_t* meta);

/* ---- serial ports ----
   Ports: 0 = Serial, 1 = Serial1, 2 = Serial2 (the board's own), 16 and up =
   SoftwareSerial instances (soft_serial_open). Serial is the board's USB port:
   what it sends appears in the Serial Monitor and the Output panel, what is
   typed in the monitor arrives on it. On a real board a byte takes 10 bits
   at the baud rate on the wire (8N1), queued behind the bytes before it in
   the board's TX buffer; a write waits while that buffer is full. The ideal
   board's Serial is instant and works without begin(). */
RS_IMPORT(serial_write) void     rs_serial_write(const char* data, uint32_t len);   /* = uart_write(0, ...) */
/* rx/tx: pin numbers for a port whose pins are chosen in begin() (ESP32
   Serial1/Serial2); -1 = the core's default. Returns 0, or -1 if the board
   has no such port. */
RS_IMPORT(uart_begin)   int32_t  rs_uart_begin(int32_t port, uint32_t baud, int32_t rx, int32_t tx);
RS_IMPORT(uart_end)     void     rs_uart_end(int32_t port);
RS_IMPORT(uart_write)   int32_t  rs_uart_write(int32_t port, const uint8_t* data, uint32_t len);
RS_IMPORT(uart_available) int32_t rs_uart_available(int32_t port);
RS_IMPORT(uart_read)    int32_t  rs_uart_read(int32_t port);    /* -1 if nothing has arrived */
RS_IMPORT(uart_peek)    int32_t  rs_uart_peek(int32_t port);
RS_IMPORT(uart_available_for_write) int32_t rs_uart_available_for_write(int32_t port);
RS_IMPORT(uart_flush)   void     rs_uart_flush(int32_t port);   /* waits until everything is sent */
/* Waits (simulated time) until a byte has arrived or the timeout passes: 1 or 0.
   What Stream's timed reads (readStringUntil, parseInt, ...) poll with. */
RS_IMPORT(uart_wait)    int32_t  rs_uart_wait(int32_t port, uint32_t timeout_us);
/* SoftwareSerial: a port on any two pins; only the listening one receives. */
RS_IMPORT(soft_serial_open) int32_t rs_soft_serial_open(int32_t rx, int32_t tx);
RS_IMPORT(soft_serial_listen) int32_t rs_soft_serial_listen(int32_t port);   /* -1: stop listening; 1 if it switched */
RS_IMPORT(soft_serial_overflow) int32_t rs_soft_serial_overflow(int32_t port);   /* and clears it */

#ifdef __cplusplus
}
#endif

#endif /* RS_ABI_H */
