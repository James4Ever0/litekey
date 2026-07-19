# LiteKey V2 — Multi-Slot Password Keyboard with LED

Up to 10 password slots, each with a distinct RGB LED color. Short press types the current slot's password. Long press (>1s) cycles to the next slot (wraps around at the configured slot count).

## Extra Dependency

Requires the **Adafruit NeoPixel** library for the RGB LED. Install via **Tools > Manage Libraries** → search for and install **"Adafruit NeoPixel"** by Adafruit.

## LED Pin Compatibility

The firmware drives the NeoPixel on **GPIO16**, which is the built-in RGB LED pin on the **Waveshare RP2040 Zero**. Other boards may use a different pin or lack a NeoPixel entirely.

- **Waveshare RP2350 Zero / Pico 2** — check your board's pinout (`PIN_NEOPIXEL` in the variant file) and change `#define LED_PIN 16` accordingly.
- **Boards without an RGB LED** — V2 requires a NeoPixel to indicate the active slot. If your board has no usable RGB LED, use **V1** instead (also needs a NeoPixel).

## Serial Commands (baud: 115200)

| Command | Description | Example |
|---|---|---|
| `SETPASS:n <password>` | Set slot n password (omit password to clear) | `SETPASS:3 MyPass` |
| `SETSLOTS:n` | Set number of active slots (1..10) | `SETSLOTS:5` |
| `SETENTER:1` or `SETENTER:0` | Enable/disable auto-append Enter | `SETENTER:1` |
| `SETSWAP:1` or `SETSWAP:0` | Swap long/short press roles | `SETSWAP:1` |

- If the current slot exceeds the new slot count after `SETSLOTS`, it resets to slot 1.
- All settings persist across power cycles.

## Swap Press

By default (`SETSWAP:0`), short press types the password and long press cycles the slot. When swap is enabled (`SETSWAP:1`), the roles are reversed — short press cycles the slot, long press types the password.

## LED Slot Colors

| Slot | Color |
|---|---|
| 1 | Red |
| 2 | Green |
| 3 | Blue |
| 4 | Yellow |
| 5 | Cyan |
| 6 | Magenta |
| 7 | White |
| 8 | Orange |
| 9 | Purple |
| 10 | Lime |

## LED Feedback

- **Steady on** — the active slot's color is always displayed.
- **Flash on type** — the LED briefly turns off then on again when a password is typed.
- **Red flashes** — when cycling is attempted but only one slot is active, the LED flashes red 3 times.

## Slot Validation

Every loop, the firmware checks that the current slot is within the active slot count. If not (e.g. after reducing slot count or EEPROM corruption), it resets to slot 1 in both RAM and EEPROM.

## Factory Defaults

| Setting | Default |
|---|---|
| Active slots | 1 |
| Slot 1 password | `DefaultPass` |
| Slots 2–10 | *(empty — sends Enter only)* |
| Auto-append Enter | ON |
| Swap press | OFF |
