# LiteKey V1 — Two Configurable Outputs

Short press types its stored string (default: Enter key). Long press (>1s) types its stored string (default: `DefaultPass`). Both are independently configurable.

## Serial Commands (baud: 115200)

| Command | Description | Example |
|---|---|---|
| `SETLONG:content` | Set long-press output | `SETLONG:MyPass123` |
| `SETSHORT:content` | Set short-press output | `SETSHORT:hello` |
| `SETENTER:1` or `SETENTER:0` | Enable/disable auto-append Enter | `SETENTER:1` |

- Send an empty value (e.g. `SETSHORT:`) to clear — short press reverts to sending just the Enter key.
- All settings persist across power cycles.

## Factory Defaults

| Setting | Default |
|---|---|
| Long press output | `DefaultPass` |
| Short press output | *(Enter key only)* |
| Auto-append Enter | ON |

## LED

V1 does not use the RGB LED.

## Board Compatibility

V1 is written for the **Waveshare RP2040 Zero** and reads the BOOT button via the Philhower core's `BOOTSEL` object. It may work on other RP2040 / RP2350 boards if the boot button press state can be detected — check your board's documentation and the board library code. If your board does not support `BOOTSEL`, you can wire a free GPIO pin to GND with an external button and modify the firmware to read that pin instead.

Your board **must support USB HID keyboard output** (via `Keyboard.h`). If it does not, this firmware is not compatible with your hardware.
