# LiteKey V1 — Two-Press Password Keyboard

Short press types a configurable string (default: Enter key). Long press (>1s) types a configurable password (default: `DefaultPass`).

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
