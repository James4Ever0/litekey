/*
 * RP2040 Zero Dual-Mode Password Keyboard
 * - Short press BOOT: outputs shortOutput (configurable, defaults to Enter key)
 * - Long press BOOT: outputs longOutput (configurable, defaults to "DefaultPass")
 * - Optionally appends Enter after output (controlled by EEPROM flag, default on)
 * - Serial commands:
 *     SETLONG:content   -> set long-press output (persistent)
 *     SETSHORT:content  -> set short-press output (persistent)
 *     SETENTER:1 or 0   -> set auto-append-enter (persistent, 1=on 0=off)
 *     Send empty (e.g. SETSHORT:) to clear; short press reverts to Enter
 * - All settings saved to Flash (EEPROM)
 */

#include <Keyboard.h>
#include <EEPROM.h>

// ==================== Constants ====================
// Note: The BOOT button (BOOTSEL) is connected to the QSPI Flash chip-select
// line, not a regular GPIO. It cannot be read with digitalRead(). The
// Philhower core provides a global BOOTSEL object (not a function, no
// parentheses). It reads true when held; use if (BOOTSEL). No pinMode() needed.
#define EEPROM_SIZE      512
#define LONG_ADDR        0     // EEPROM address for long-press string
#define SHORT_ADDR       128   // EEPROM address for short-press string
#define ENTER_FLAG_ADDR  256   // EEPROM address for auto-enter flag (1 byte: 1=on, 0=off)
#define MAX_STR_LEN      128   // Max string length in bytes

#define SHORT_PRESS_MS 50
#define LONG_PRESS_MS  1000
#define SERIAL_TIMEOUT 1500 // Continue even if serial is not ready.

// ==================== Defaults ====================
const String DEFAULT_LONG  = "DefaultPass";  // Default long-press password
const String DEFAULT_SHORT = "";             // Default short-press (empty = Enter key)
const bool   DEFAULT_APPEND_ENTER = true;    // Default: append Enter after output

// ==================== Globals ====================
String longOutput;   // String output on long press
String shortOutput;  // String output on short press
bool   appendEnter;  // Whether to append Enter after output (persisted to EEPROM)

bool useSerial;

// ==================== EEPROM Read/Write Helpers ====================

// Read a string from EEPROM at given address (max maxLen bytes)
String readStringFromEEPROM(int addr, int maxLen) {
  String str = "";
  for (int i = 0; i < maxLen; i++) {
    char c = EEPROM.read(addr + i);
    if (c == '\0') break;
    str += c;
  }
  return str;
}

// Write a string to EEPROM at given address (clears area first)
void writeStringToEEPROM(int addr, int maxLen, String str) {
  // Clear the area first
  for (int i = 0; i < maxLen; i++) {
    EEPROM.write(addr + i, 0);
  }
  // Write new string (truncate if too long)
  int len = str.length();
  if (len > maxLen - 1) len = maxLen - 1;
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + i, str[i]);
  }
  EEPROM.commit();
}

// Write the auto-enter flag (1 byte) and persist
void writeEnterFlagToEEPROM(bool val) {
  EEPROM.write(ENTER_FLAG_ADDR, val ? 1 : 0);
  EEPROM.commit();
}

// ==================== setup ====================
void setup() {
  // BOOTSEL is read via the global object, no pinMode() needed
  Keyboard.begin();
  EEPROM.begin(EEPROM_SIZE);

  // Read long-press setting; fall back to default if empty
  longOutput = readStringFromEEPROM(LONG_ADDR, MAX_STR_LEN);
  if (longOutput.length() == 0) {
    longOutput = DEFAULT_LONG;
  }

  // Read short-press setting; fall back to default (empty string)
  shortOutput = readStringFromEEPROM(SHORT_ADDR, MAX_STR_LEN);
  // Empty shortOutput means send Enter key

  // Read auto-enter flag; invalid values (e.g. 0xFF on fresh Flash) fall back
  uint8_t enterFlag = EEPROM.read(ENTER_FLAG_ADDR);
  if (enterFlag == 0)      appendEnter = false;
  else if (enterFlag == 1) appendEnter = true;
  else                     appendEnter = DEFAULT_APPEND_ENTER;

  Serial.begin(115200);
  useSerial=true;
  unsigned long serialTimeout = millis() + SERIAL_TIMEOUT;
  while (!Serial && millis() < serialTimeout) { delay(10); }

  if (Serial){
    Serial.println("=== RP2040 Dual-Mode Keyboard ===");
    Serial.print("Long press output: ");
    Serial.println(longOutput);
    Serial.print("Short press output: ");
    if (shortOutput.length() == 0) {
      Serial.println("<ENTER> (default)");
    } else {
      Serial.println(shortOutput);
    }
    Serial.print("Auto append Enter: ");
    Serial.println(appendEnter ? "ON (1)" : "OFF (0)");
    Serial.println("Commands:");
    Serial.println("  SETLONG:content  -> set long-press output");
    Serial.println("  SETSHORT:content -> set short-press output");
    Serial.println("  SETENTER:1 or 0  -> set auto-append-enter (1=on, 0=off)");
    Serial.println("  (send empty to clear, e.g. SETSHORT:)");
  } else{
    Serial.end();
    useSerial=false;
  }
}

// ==================== loop ====================
void loop() {
  // ---------- Serial command handling ----------
  if (useSerial && Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("SETLONG:")) {
      String newVal = cmd.substring(8);
      newVal.trim();
      writeStringToEEPROM(LONG_ADDR, MAX_STR_LEN, newVal);
      longOutput = (newVal.length() == 0) ? DEFAULT_LONG : newVal;
      Serial.print("Long output updated to: ");
      Serial.println(longOutput);
    }
    else if (cmd.startsWith("SETSHORT:")) {
      String newVal = cmd.substring(9);
      newVal.trim();
      writeStringToEEPROM(SHORT_ADDR, MAX_STR_LEN, newVal);
      shortOutput = newVal; // empty is valid (sends Enter key only)
      Serial.print("Short output updated to: ");
      if (shortOutput.length() == 0) {
        Serial.println("<ENTER>");
      } else {
        Serial.println(shortOutput);
      }
    }
    else if (cmd.startsWith("SETENTER:")) {
      String newVal = cmd.substring(9);
      newVal.trim();
      appendEnter = (newVal == "1"); // only "1" enables, anything else disables
      writeEnterFlagToEEPROM(appendEnter);
      Serial.print("Auto append Enter updated to: ");
      Serial.println(appendEnter ? "ON (1)" : "OFF (0)");
    }
    else {
      Serial.println("Unknown command. Use SETLONG:, SETSHORT: or SETENTER:");
    }
  }

  // ---------- Button detection ----------
  // BOOTSEL returns true when held (inverted logic vs. normal pull-up button)
  if (BOOTSEL) {
    unsigned long pressStart = millis();

    while (BOOTSEL) {
      if (millis() - pressStart > LONG_PRESS_MS) {
        // ---------- Long press ----------
        Keyboard.print(longOutput);
        if (appendEnter) Keyboard.write(KEY_RETURN);
        // Wait for release
        while (BOOTSEL) delay(10);
        return;
      }
      delay(10);
    }

    // If button was released before the long-press threshold, it's a short press
    unsigned long pressDuration = millis() - pressStart;
    if (pressDuration >= SHORT_PRESS_MS) {
      // ---------- Short press ----------
      if (shortOutput.length() == 0) {
        // Empty string -> send Enter key only (no need to append another)
        Keyboard.write(KEY_RETURN);
      } else {
        Keyboard.print(shortOutput);
        if (appendEnter) Keyboard.write(KEY_RETURN);
      }
    }
  }

  delay(10);
}
