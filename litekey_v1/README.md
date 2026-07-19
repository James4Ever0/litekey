# LiteKey V1 — Two Configurable Outputs

Short press types its stored string (default: Enter key). Long press (>1s) types its stored string (default: `DefaultPass`). Both are independently configurable.

## Extra Dependency (optional)

Requires the **Adafruit NeoPixel** library only if the LED is enabled (`USE_LED 1`, the default). Install via **Tools > Manage Libraries** → search for and install **"Adafruit NeoPixel"** by Adafruit. To skip the LED entirely, set `USE_LED 0` in `litekey_v1.ino` — no library needed.

## Serial Commands (baud: 115200)

| Command | Description | Example |
|---|---|---|
| `SETLONG:content` | Set long-press output | `SETLONG:MyPass123` |
| `SETSHORT:content` | Set short-press output | `SETSHORT:hello` |
| `SETENTER:1` or `SETENTER:0` | Enable/disable auto-append Enter | `SETENTER:1` |

- Send an empty value (e.g. `SETSHORT:`) to clear — short press reverts to sending just the Enter key.
- All settings persist across power cycles.

## LED (optional)

The built-in NeoPixel on GPIO 16 flashes on press:
- **Short press** — red flash
- **Long press** — green flash

The LED is enabled by default (`#define USE_LED 1`). To disable it, change `#define USE_LED 1` to `#define USE_LED 0` in `litekey_v1.ino`. When disabled, the NeoPixel library is not compiled in and no LED initialization or flashing occurs — useful for boards without an RGB LED or to save resources.

## Factory Defaults

| Setting | Default |
|---|---|
| Long press output | `DefaultPass` |
| Short press output | *(Enter key only)* |
| Auto-append Enter | ON |

## Board Compatibility

V1 is written for the **Waveshare RP2040 Zero** and reads the BOOT button via the Philhower core's `BOOTSEL` object. It may work on other RP2040 / RP2350 boards if the boot button press state can be detected — check your board's documentation and the board library code. If your board does not support `BOOTSEL`, you can wire a free GPIO pin to GND with an external button and modify the firmware to read that pin instead.

Your board **must support USB HID keyboard output** (via `Keyboard.h`). If it does not, this firmware is not compatible with your hardware.
