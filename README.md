<p align="center">
    <img src="https://raw.githubusercontent.com/James4Ever0/litekey/main/litekey-logo.png" alt="LiteKey Logo" width="400"/>
</p>

<h2 align="center">Configurable Password Keyboard for RP2040 / RP2350</h2>

https://github.com/user-attachments/assets/5857cb4c-461d-425d-8436-c0ecb6eec525

A firmware family for **RP2040 / RP2350** boards (tested on Waveshare RP2040 Zero) that turns the BOOT button into a password keyboard. All settings are persisted to Flash (EEPROM) and configured via the serial console.

Two versions are available — see each subfolder for details:

### Versions

| Version | Max passwords | Link |
|---|---|---|
| **V1** | 2 | [litekey_v1/](litekey_v1/) |
| **V2** | 10 | [litekey_v2/](litekey_v2/) |

### Compatibility

| | V1 | V2 |
|---|---|---|
| **RGB LED needed** | No | Yes (GPIO16, configurable) |
| **External button wiring** | Supported (modify GPIO pin) | Supported (modify GPIO pin) |
| **Extras** | — | Adafruit NeoPixel library required |

## Important: EEPROM Version Check

Each firmware writes a version marker (V1 = `0x01`, V2 = `0x02`) to the first byte of EEPROM on first boot. On subsequent boots, if the marker does not match the firmware's expected version, **all stored passwords are cleared and settings reset to defaults**.

This means **flashing across different firmware versions** (e.g. V1 → V2, or any future update) will wipe your saved data. Back up your passwords before upgrading.

## Serial Security

Both versions wait **1.5 seconds** for a serial connection at boot. If no serial monitor is opened within that window, the serial port is closed and the device operates solely as a keyboard. Once the serial timeout expires, the device cannot be reconfigured until the next power-on.

## Version-Specific Dependencies

- **V1** — no extra libraries.
- **V2** — requires the **Adafruit NeoPixel** library for the RGB LED. Install via **Tools > Manage Libraries** → search for and install **"Adafruit NeoPixel"** by Adafruit.

## Hardware

- **Board:** Waveshare RP2040 Zero (or any RP2040 / RP2350 board with a BOOTSEL button)
- **Connection:** USB-C to USB-A cable to your PC

## Entering Bootloader (UF2) Mode

Before uploading firmware, put the board into bootloader mode:

1. **Hold** the BOOT button (on the RP2040 Zero, this is the button near the USB-C port)
2. **Press and release** the RESET / RUN button
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

## 3D Printable Shell

A custom shell for the RP2040 Zero is available here:

- [RP2040 Zero Shell on MakerWorld](https://makerworld.com.cn/zh/models/1843350-rp2040-zerowai-ke)

### Assembly Tips

- **Securing the shell halves:** Use thin black **acetate tape** (also called **cloth electrical tape** or **fabric tape**) to fasten the two halves together. It is thin, conforms well, and holds securely without adding bulk.
- **Cable:** Always use a **data transfer cable** (USB-C to USB-A), not a charging-only cable. Charging-only cables lack the D+/D- data lines and will not power the board or allow serial communication.

## Technical Notes

- The BOOT button (BOOTSEL) is connected to the QSPI Flash chip-select line, not a regular GPIO. The Philhower core exposes it as a global `BOOTSEL` object — no `pinMode()` required.
- The firmware uses the `Keyboard.h` HID library to act as a USB keyboard.

## References

- [Waveshare RP2040 Zero Wiki](https://www.waveshare.com/wiki/RP2040-Zero#Arduino_IDE_Series)
- [arduino-pico (Earle Philhower)](https://github.com/earlephilhower/arduino-pico)
