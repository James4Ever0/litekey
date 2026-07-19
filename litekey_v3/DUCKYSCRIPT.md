# LiteKey DuckyScript Grammar

LiteKey v3 runs a **self-contained, keyboard-only subset** of DuckyScript directly on the RP2040 Zero. Scripts are compiled into compact binary opcodes and stored in flash slots as `/slotN.bin`; they are decompiled back to text only when you `VIEW` a slot.

## Design goals

- No external references, extensions, variables, or control flow.
- Only keyboard injection commands.
- Line-by-line grammar checking at upload time.
- Sufficient for common automation tasks: opening dialogs, typing strings, pressing hotkeys, adding delays.

## Supported commands

### Comments

```ducky
REM this line is ignored
```

Blank lines are also ignored.

### Text typing

```ducky
STRING hello world
STRINGLN hello world   ; types the text, then presses Enter
```

Everything after the first space is typed literally. `STRINGLN` appends an `ENTER` keystroke.

### Delays

```ducky
DELAY 1000          ; wait 1000 ms
SLEEP 1000          ; alias for DELAY
DEFAULT_DELAY 100   ; wait 100 ms after every subsequent command
```

Delay values must be between 0 and 60000 ms.

### Single keys

A line containing only a key name presses and releases that key.

```ducky
ENTER
TAB
F5
ESC
```

### Modifier chords

Modifiers can be written with spaces or hyphens. The key is the last token.

```ducky
GUI r
WINDOWS r
CTRL c
ALT F4
CTRL-ALT DELETE
CTRL-SHIFT ESC
ALT-SHIFT TAB
CTRL-ALT-SHIFT DELETE
```

Supported modifiers:

| Token | Key |
|---|---|
| `GUI`, `WINDOWS`, `COMMAND` | Left GUI / Windows / Command |
| `CTRL`, `CONTROL` | Left Ctrl |
| `ALT` | Left Alt |
| `SHIFT` | Left Shift |

### REPEAT

```ducky
DOWN
REPEAT 10
```

Repeats the previous command 10 additional times. The repeat count must be 0–999.

## Supported keys

### Letters, digits, and symbols

Any single printable ASCII character can be used as a key or inside `STRING`/`STRINGLN`.

### Special keys

| Token | Token aliases |
|---|---|
| `ENTER` | `RETURN` |
| `TAB` | |
| `SPACE` | |
| `BACKSPACE` | |
| `DELETE` | `DEL` |
| `ESC` | `ESCAPE` |
| `INSERT` | |
| `HOME` | |
| `END` | |
| `PAGEUP` | `PGUP` |
| `PAGEDOWN` | `PGDN` |
| `UP` | `UPARROW` |
| `DOWN` | `DOWNARROW` |
| `LEFT` | `LEFTARROW` |
| `RIGHT` | `RIGHTARROW` |
| `CAPSLOCK` | `CAPS_LOCK` |
| `NUMLOCK` | `NUM_LOCK` |
| `SCROLLLOCK` | `SCROLLOCK`, `SCROLL_LOCK` |
| `PRINTSCREEN` | `PRINT_SCREEN` |
| `PAUSE` | |
| `BREAK` | |
| `APP` | `MENU` (mapped to the right GUI key) |

A modifier name on its own is also accepted as a single keypress, e.g. `GUI` or `SHIFT`.

### Function keys

`F1` through `F12` are supported.

## Unsupported features

The following DuckyScript features are **not** supported by LiteKey v3:

- Mouse commands
- Variables (`VAR`) and constants (`DEFINE`)
- Control flow (`IF`, `WHILE`, `FUNCTION`)
- `REM_BLOCK` / `END_REM`
- `STRING` / `STRINGLN` blocks (`END_STRING`, `END_STRINGLN`)
- `INJECT_MOD`
- `ATTACKMODE` and device-mode commands
- LED/button commands (`LED_R`, `BUTTON_DEF`, etc.)
- Extensions

If a script uses an unsupported command, the upload will fail with a line number.

## Upload protocol

Use the serial command `SETSCRIPT:n` to upload a script to slot `n`. The firmware compiles each line to binary opcodes as it is received and writes it to LittleFS incrementally. Send a line containing exactly `EOF` to finish.

You can also use a custom EOF marker: `SETSCRIPT:n <<MARKER` (the marker must be non-empty and contain no spaces).

```text
Host: SETSCRIPT:2
Device: >>
Host: DELAY 1000
Device: >>
Host: GUI r
Device: >>
Host: EOF
Device: OK script stored in slot 2 (6 bytes)
```

The device echoes `>>` after each line. The upload ends when the host sends a line matching the chosen EOF marker. If the script exceeds the slot capacity or contains a grammar error, the device responds with `ERR line X: reason`, aborts the upload, and clears the slot back to an empty password slot.

### Plain text upload

For non-DuckyScript multiline text, use `SETTEXT:n <<MARKER`. The text is stored verbatim and typed out when the slot is executed; each stored newline is converted to an `ENTER` keystroke. The marker must be non-empty and space-free, and if the text exceeds the slot capacity the upload aborts and the slot is cleared.

## Grammar errors

The grammar checker runs at upload time and reports the first problem it finds:

```text
ERR line 3: unknown key FOOBAR
ERR line 7: invalid delay value
ERR line 12: missing key
ERR line 15: REPEAT with no previous command
```

## Keyboard layout

LiteKey v3 uses the US keyboard layout. Characters outside the basic ASCII printable set may not type correctly.

## Runtime debugging

Use `SETDEBUG:1` to make the firmware echo every opcode as it executes. This is useful for verifying that a compiled payload behaves as expected:

```text
SETDEBUG:1
OK debug mode ON
```

During execution you will see lines such as:

```text
OP_KEY A
OP_CHORD GUI r
OP_DELAY 500
```

Use `SETDEBUG:0` to disable echoing. The setting is saved to EEPROM and persists across reboots.

## Examples

See the `examples/` directory for ready-to-use payloads:

- `taskmanager.txt` — open Windows Task Manager (`CTRL-SHIFT ESC`)
- `opencmd.txt` — open Run dialog, launch `cmd.exe`, type `echo "hello world"`
- `hello.txt` — simple `STRINGLN Hello, World!`
- `lockkeys.txt` — toggle Caps Lock, Num Lock, Scroll Lock
- `refresh.txt` — press F5 to refresh
