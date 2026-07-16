# LiteKey - RP2040 Zero Dual-Mode Password Keyboard

A firmware for the **Waveshare RP2040 Zero** board that turns the BOOT button into a dual-function password keyboard:
- **Short press** — outputs a configurable short string (default: Enter key)
- **Long press** (>1s) — outputs a configurable password (default: `DefaultPass`)

All settings are persisted to Flash (EEPROM) and can be changed via the serial console.

## Hardware

- **Board:** Waveshare RP2040 Zero (or any RP2040 board with a BOOTSEL button)
- **Connection:** USB-C to USB-A cable to your PC

## Entering Bootloader (UF2) Mode

Before uploading firmware for the first time, put the board into bootloader mode:

1. **Hold** the BOOT button (on the RP2040 Zero, this is the button near the USB-C port)
2. **Press and release** the RESET/RUN button
3. **Release** the BOOT button
4. A USB drive named `RPI-RP2` should appear on your PC

If the drive does not appear, try a different cable (some power-only cables lack data lines).

## Arduino IDE Setup

### 1. Add Board Manager URL

Open **File > Preferences** (or **Arduino IDE > Settings** on macOS). In the **"Additional boards manager URLs"** field, add:

```
https://github.com/earlephilhower/arduino-pico/releases/download/4.5.2/package_rp2040_index.json
```

Click **OK**.

### 2. Install Board Package

Open **Tools > Board > Boards Manager**, search for **"RP2040"**, and install **"Raspberry Pi Pico/RP2040" by Earle F. Philhower, III**.

### 3. Select Board

Go to **Tools > Board > Raspberry Pi RP2040 Boards** and select **"Waveshare RP2040 Zero"**.

### 4. USB Stack

Under **Tools**, ensure **"USB Stack"** is set to **"Adafruit TinyUSB"** (required for the Keyboard HID library and serial).

### 5. Upload

Put the board into bootloader mode (see above), then click the **Upload** button in Arduino IDE.

## Usage

Once flashed, the board works as a USB HID keyboard.

### Basic Operation

- **Short press** the BOOT button — outputs the configured short string
- **Long press** (>1s) the BOOT button — outputs the configured long string

### Serial Console Configuration

The board can be configured via the **Arduino IDE Serial Monitor** (Tools > Serial Monitor, baud rate: **115200**).

**Important:** The board only waits **1.5 seconds** for a serial connection at boot. If no serial monitor is opened within that window, the serial port is disabled and the device operates solely as a keyboard. This is a security feature — once the serial timeout expires, the device cannot be reconfigured until next power-on.

If you connect in time, the following commands are available:

| Command | Description | Example |
|---|---|---|
| `SETLONG:内容` | Set long-press output | `SETLONG:MyPass123` |
| `SETSHORT:内容` | Set short-press output | `SETSHORT:hello` |
| `SETENTER:1` or `SETENTER:0` | Enable/disable auto-append Enter | `SETENTER:1` |

- Send an empty value (e.g. `SETSHORT:`) to clear it — short press will revert to sending just the Enter key.
- All settings persist across power cycles.

### Factory Defaults

| Setting | Default |
|---|---|
| Long press output | `DefaultPass` |
| Short press output | *(Enter key only)* |
| Auto-append Enter | ON |

## Technical Notes

- The BOOT button (BOOTSEL) is connected to the QSPI Flash chip-select line, not a regular GPIO. The Philhower core exposes it as a global `BOOTSEL` object — no `pinMode()` required.
- The firmware uses the `Keyboard.h` HID library to act as a USB keyboard.
- Serial is only available during the first 1.5s after boot. This limits the reconfiguration window, making the device more secure in unattended deployments.

## 3D Printable Shell

A custom shell for the RP2040 Zero is available here:

- [RP2040 Zero Shell on MakerWorld](https://makerworld.com.cn/zh/models/1843350-rp2040-zerowai-ke)

### Assembly Tips

- **Securing the shell halves:** Use thin black **acetate tape** (also called **cloth electrical tape** or **fabric tape**) to fasten the two halves together. It is thin, conforms well, and holds securely without adding bulk.
- **Cable:** Always use a **data transfer cable** (USB-C to USB-A), not a charging-only cable. Charging-only cables lack the D+/D- data lines and will not power the board or allow serial communication.

## References

- [Waveshare RP2040 Zero Wiki](https://www.waveshare.com/wiki/RP2040-Zero#Arduino_IDE_Series)
- [arduino-pico (Earle Philhower)](https://github.com/earlephilhower/arduino-pico)
