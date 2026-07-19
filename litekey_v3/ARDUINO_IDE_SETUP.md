# LiteKey v3 — Arduino IDE Setup Guide

This guide covers how to build and upload `litekey_v3.ino` to an RP2040 Zero using the Arduino IDE.

## Hardware assumption

- **RP2040 Zero** (Waveshare or compatible clone)
- Built-in `BOOTSEL` button used as the user button
- Built-in NeoPixel RGB LED on `PIN_NEOPIXEL` (GPIO 16)
- USB connected directly to the RP2040 USB pins

## 1. Install the board support package

LiteKey v3 uses the **Philhower Arduino-Pico** core, which bundles `Keyboard`, `EEPROM`, and `LittleFS`.

1. Open **Arduino IDE** (1.8.x or 2.x).
2. Go to **File → Preferences**.
3. In **Additional Boards Manager URLs**, add:
   ```text
   https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
   ```
4. Click **OK**.
5. Go to **Tools → Board → Boards Manager...**.
6. Search for `pico` and install **Raspberry Pi Pico/RP2040** by Earle F. Philhower, III.
7. Wait for the installation to finish.

## 2. Select the board

Go to **Tools → Board** and choose a board that matches your hardware:

- **Waveshare RP2040-Zero** — best match if available in your core version.
- **Raspberry Pi Pico** — works for many RP2040 Zero clones if the pin mapping is compatible.

If your exact board is not listed, try **Raspberry Pi Pico** first.

## 3. Flash Size / Filesystem setting (critical)

This is the most common reason for `ERR write failed` or `LittleFS.begin(): FAIL`.

Go to **Tools → Flash Size** and select an option that reserves space for a filesystem, for example:

| Flash Size option | LittleFS available? |
|---|---|
| `2MB (no FS)` | **No** — do not use |
| `2MB (FS: 1MB)` | **Yes** — recommended |
| `2MB (FS: 1.5MB)` | **Yes** — also fine |

RP2040 Zero boards usually have **2 MB flash**. Always pick a line that includes `(FS: …)`; otherwise LittleFS has zero bytes allocated and all slot writes will fail.

After changing this setting, re-upload the sketch.

## 4. Library dependencies

Only one external library is required:

- **Adafruit NeoPixel** by Adafruit
  - Install via **Sketch → Include Library → Manage Libraries…**.

The following libraries are already provided by the Philhower core and do **not** need to be installed separately:

- `Keyboard.h`
- `EEPROM.h`
- `LittleFS.h`

## 5. Upload the sketch

1. Open `litekey_v3/litekey_v3.ino` in the Arduino IDE.
2. Connect the RP2040 Zero to USB while holding **BOOTSEL**, or press BOOTSEL once while the IDE starts the upload.
3. Click **Upload**.
4. Open **Tools → Serial Monitor** at **115200 baud**.
5. You should see the LiteKey v3 boot banner.

## 6. First boot after upload

On first boot (or after a full flash erase), the EEPROM header is invalid, so the firmware:

- Forces a `LittleFS.format()`.
- Resets all slots to empty password slots.
- Sets defaults: 1 slot, auto-enter ON, swap OFF, current slot 1.

Existing v2 data in EEPROM is **not** migrated; this is expected because v3 stores slot content in LittleFS instead of EEPROM.

If you change the per-slot capacity firmware setting, the next boot detects the mismatch and resets slots once.

## 7. Per-slot capacity

Each slot has a **fixed capacity of 96 KB**, regardless of how many slots are active. With 10 slots that is 960 KB of LittleFS space, leaving room for filesystem metadata in a 1 MB partition. Changing `SETSLOTS:n` only changes how many slots are active; it no longer wipes slot data.

## 8. Quick sanity check

In the Serial Monitor, run:

```text
DIAGNOSE
```

Expected healthy output starts with:

```text
LittleFS.begin(): OK
```

If you see:

```text
LittleFS.begin(): FAIL
```

go back to **Tools → Flash Size** and select an option with a filesystem partition.

Then try:

```text
SETPASS:1 mysecret
VIEW:1
```

You should see the password stored and read back.

## 9. Serial command summary

```text
SETPASS:n <password>   set password for slot n
SETSLOTS:n             set active slots (1..10)
SETENTER:1|0           auto-append Enter for password slots
SETSWAP:1|0            swap long/short press roles
SETDEBUG:1|0           echo each DuckyScript opcode during execution
SETSCRIPT:n [<<MARKER]  upload DuckyScript (default marker EOF)
SETTEXT:n <<MARKER      upload multiline plain text (types each '\n' as Enter)
VIEW:n                 show slot type and content
LIST                   list active slots
DEL:n                  clear slot n
RESET                  reset all settings and slots
DIAGNOSE               format LittleFS, test read/write, dump state
INFO                   show version/capacity/current slot
HELP                   command help
```

## 10. Common caveats

### Filesystem partition is required
LittleFS needs dedicated flash space. If **Flash Size** is set to `(no FS)`, writes will fail with `ERR write failed` even though the sketch compiles cleanly.

### EEPROM vs LittleFS
- Small settings (magic, version, slot count, flags) live in **EEPROM** addresses 0..15.
- Slot content (passwords and scripts) lives in **LittleFS** files `/slot1.bin` … `/slot10.bin`.

### Uploading does not always erase EEPROM
A normal Arduino upload flashes the program but usually leaves EEPROM alone. If you want a completely clean state, run `RESET` or `DIAGNOSE` after upload.

### Downgrading to v2
If you flash v2 again, the v2 firmware will see an unknown EEPROM version and reset its own settings. Data stored in LittleFS by v3 will not be used by v2.

### Board selection
If you select **Raspberry Pi Pico** for an RP2040 Zero, verify that `PIN_NEOPIXEL` (GPIO 16) is wired the same way. On most RP2040 Zero clones it is.

### USB enumeration
After upload, the RP2040 may take a few seconds to re-enumerate as a USB CDC serial device. The firmware waits up to 1.5 seconds for Serial; if not present, it continues in standalone mode.

### US keyboard layout
The DuckyScript interpreter and `Keyboard.print()` use the US keyboard layout. Non-ASCII characters may not type correctly on other layouts.

### Slot capacity is fixed
Each slot is limited to **96 KB**. Scripts or passwords longer than that will be rejected.

### DuckyScript is compiled to binary opcodes
`SETSCRIPT` compiles each line to a compact binary opcode representation and stores `/slotN.bin` as opcodes, not plain text. `VIEW` decompiles it back to text for display. If an upload error occurs, the entire slot is cleared and reset to an empty password slot.

### SETDEBUG
`SETDEBUG:1` makes the firmware print every DuckyScript opcode as it executes, which is useful for troubleshooting payloads. `SETDEBUG:0` disables this output.

## 11. Platform notes

- **Windows / macOS / Linux**: Arduino IDE setup is the same; the only difference is the serial port name (COMx, /dev/ttyACM0, /dev/cu.usbmodem*, etc.).
- **Arduino IDE 2.x**: the menus and Boards Manager work the same as 1.8.x.
- **arduino-cli**: supported if you install the `rp2040:rp2040` core and use the correct FQBN, e.g. `rp2040:rp2040:waveshare_rp2040_zero` or `rp2040:rp2040:rpipico`. Add `--flash-size` / `--fs-size` options if your FQBN supports them.
