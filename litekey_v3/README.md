# LiteKey V3 — Multi-Slot Password / DuckyScript Keyboard

Up to 10 slots, each storing a **password**, a **DuckyScript payload**, or **multiline text**. Short press executes the current slot. Long press (>1s) cycles to the next slot. Settings live in EEPROM; slot content lives in **LittleFS** (96 KB per slot).

## Setup

[**ARDUINO_IDE_SETUP.md**](ARDUINO_IDE_SETUP.md) — board package, Flash Size / FS partition, library dependencies, upload steps, and common caveats.

## DuckyScript

[**DUCKYSCRIPT.md**](DUCKYSCRIPT.md) — full grammar reference: supported commands, keys, modifiers, upload protocol, error handling, and examples.

## Serial Commands

See the command table at the top of [`litekey_v3.ino`](litekey_v3.ino) or send `HELP` over serial. Key commands:

```
SETPASS:n <password>   set password for slot n
SETSCRIPT:n            begin DuckyScript upload (end with EOF or custom marker)
SETTEXT:n <<MARKER     upload multiline plain text ending with MARKER
SETSLOTS:n             set active slots (1..10)
SETENTER:1|0           auto-append Enter for password slots
SETSWAP:1|0            swap long/short press roles
SETDEBUG:1|0           echo DuckyScript opcodes during execution
VIEW:n                 show slot type and content
LIST                   list active slots
DEL:n                  clear slot n
RESET                  reset all settings and slots
DIAGNOSE               format LittleFS, test read/write, dump state
INFO                   show version/capacity/current slot
HELP                   command help
```

## Slot Types

| Type | Stored as | Execution |
|---|---|---|
| Password | Plain text in `/slotN.bin` | Types text; appends Enter if `SETENTER:1` |
| DuckyScript | Compiled binary opcodes in `/slotN.bin` | Runs keyboard injection payload |
| Text | Plain text in `/slotN.bin` | Types each line; newlines send Enter |

## LED Feedback

- **Steady on** — the active slot's color is always displayed (same 10-color scheme as V2).
- **Flash on execute** — the LED briefly turns off then on when a slot is triggered.
- **Red flashes** — when cycling is attempted but only one slot is active, the LED flashes red 3 times.

## New in V3 vs V2

- DuckyScript interpreter with compile-to-binary pipeline
- Plain text slot type
- LittleFS storage (96 KB per slot, independent of active slot count)
- `VIEW`, `LIST`, `DEL`, `INFO`, `DIAGNOSE` commands
- `SETDEBUG` for runtime opcode tracing
- Multiline upload protocol with custom EOF markers
