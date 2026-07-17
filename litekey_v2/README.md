# LiteKey V2 — Multi-Slot Password Keyboard with LED

Up to 10 password slots, each with a distinct RGB LED color. Short press types the current slot's password. Long press (>1s) cycles to the next slot (wraps around at the configured slot count).

## Extra Dependency

Requires the **Adafruit NeoPixel** library for the RGB LED (GPIO16). Install via **Tools > Manage Libraries** → search for and install **"Adafruit NeoPixel"** by Adafruit.

## Serial Commands (baud: 115200)

| Command | Description | Example |
|---|---|---|
| `SETPASS:n <password>` | Set slot n password (omit password to clear) | `SETPASS:3 MyPass` |
| `SETSLOTS:n` | Set number of active slots (1..10) | `SETSLOTS:5` |
| `SETENTER:1` or `SETENTER:0` | Enable/disable auto-append Enter | `SETENTER:1` |

- If the current slot exceeds the new slot count after `SETSLOTS`, it resets to slot 1.
- All settings persist across power cycles.

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

## Slot Validation

Every loop, the firmware checks that the current slot is within the active slot count. If not (e.g. after reducing slot count or EEPROM corruption), it resets to slot 1 in both RAM and EEPROM.

## Factory Defaults

| Setting | Default |
|---|---|
| Active slots | 1 |
| Slot 1 password | `DefaultPass` |
| Slots 2–10 | *(empty — sends Enter only)* |
| Auto-append Enter | ON |
