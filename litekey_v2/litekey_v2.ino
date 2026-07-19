 
/*
 * LiteKey Upgraded — Multi-Slot Password Keyboard for RP2040 Zero
 * - Short press BOOT: types password for the current slot
 * - Long press BOOT (>1s): cycles to the next slot (wraps around)
 * - RGB LED (NeoPixel) indicates the active slot with distinct colors
 * - Up to 10 configurable slots, each with its own password
 * - Serial commands:
 *     SETPASS:n <password>  -> set password for slot n
 *     SETSLOTS:n            -> set number of active slots (1..10)
 *     SETENTER:1 or 0       -> enable/disable auto-append Enter
 *     SETSWAP:1 or 0        -> swap long/short press roles (default 0)
 * - All settings persisted to Flash (EEPROM)
 */

#include <Keyboard.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

// ==================== Constants ====================
#define EEPROM_SIZE       512
#define MAX_SLOTS         10
#define SLOT_PASS_SIZE    38     // 37 chars + null terminator per slot
#define SERIAL_TIMEOUT    1500   // ms to wait for serial before proceeding standalone

#define SHORT_PRESS_MS    50
#define LONG_PRESS_MS     1000

// LED pin (from variant: PIN_NEOPIXEL = 16)
#define LED_PIN           16
#define NUM_LEDS          1

// Firmware version for EEPROM migration
#define FW_VER            0x02

// ==================== EEPROM Address Map ====================
#define ADDR_FW_VER       0      // 1 byte: firmware version (0x02)
#define ADDR_SLOT_COUNT   1      // 1 byte: number of active slots (1..10)
#define ADDR_CUR_SLOT     2      // 1 byte: current active slot (1..n)
#define ADDR_ENTER_FLAG   3      // 1 byte: auto-append-enter (1=on, 0=off)
#define ADDR_SWAP_FLAG    4      // 1 byte: swap long/short press roles (1=swapped, 0=normal)
                                 // 5..127 reserved
#define ADDR_SLOT_PASS_BASE  128 // 10 * 38 = 380 bytes, covers 128..507
                                 // 508..511 reserved

// ==================== Defaults ====================
const int     DEFAULT_SLOT_COUNT = 1;
const int     DEFAULT_CUR_SLOT   = 1;
const bool    DEFAULT_APPEND_ENTER = true;
const bool    DEFAULT_SWAP_PRESS   = false;
const String  DEFAULT_PASSWORDS[MAX_SLOTS] = {
  "DefaultPass", "", "", "", "", "", "", "", "", ""
};

// ==================== LED Colors (R, G, B) ====================
// 10 visually distinct colors for slots 1..10
const uint32_t SLOT_COLORS[MAX_SLOTS] = {
  Adafruit_NeoPixel::Color(255, 0, 0),     // 1  Red
  Adafruit_NeoPixel::Color(0, 255, 0),     // 2  Green
  Adafruit_NeoPixel::Color(0, 0, 255),     // 3  Blue
  Adafruit_NeoPixel::Color(255, 255, 0),   // 4  Yellow
  Adafruit_NeoPixel::Color(0, 255, 255),   // 5  Cyan
  Adafruit_NeoPixel::Color(255, 0, 255),   // 6  Magenta
  Adafruit_NeoPixel::Color(255, 255, 255), // 7  White
  Adafruit_NeoPixel::Color(255, 128, 0),   // 8  Orange
  Adafruit_NeoPixel::Color(128, 0, 255),   // 9  Purple
  Adafruit_NeoPixel::Color(128, 255, 0)    // 10 Lime
};

// ==================== Globals ====================
int    slotCount;                          // number of active slots (1..10)
int    currentSlot;                        // active slot index (1-based)
String slotPasswords[MAX_SLOTS];           // passwords for each slot
bool   appendEnter;                        // auto-append Enter flag
bool   swapPress;                          // swap long/short press roles flag
bool   useSerial;                          // true if serial is available
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ==================== EEPROM Helpers ====================

// Read 1 byte from EEPROM with a default if value is 0xFF (uninitialized)
uint8_t readByteWithDefault(int addr, uint8_t defaultVal) {
  uint8_t val = EEPROM.read(addr);
  if (val == 0xFF) return defaultVal;
  return val;
}

// Read a fixed-size password string from EEPROM (max SLOT_PASS_SIZE-1 chars)
String readPasswordFromEEPROM(int addr) {
  String str = "";
  for (int i = 0; i < SLOT_PASS_SIZE - 1; i++) {
    char c = EEPROM.read(addr + i);
    if (c == '\0') break;
    str += c;
  }
  return str;
}

// Write a password string to EEPROM (null-padded to SLOT_PASS_SIZE bytes)
void writePasswordToEEPROM(int addr, const String &str) {
  int len = str.length();
  if (len > SLOT_PASS_SIZE - 1) len = SLOT_PASS_SIZE - 1;
  // Write string content
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + i, str[i]);
  }
  // Null-pad the rest
  for (int i = len; i < SLOT_PASS_SIZE; i++) {
    EEPROM.write(addr + i, 0);
  }
  EEPROM.commit();
}

// Write a single byte to EEPROM and commit
void writeByteToEEPROM(int addr, uint8_t val) {
  EEPROM.write(addr, val);
  EEPROM.commit();
}

// ==================== LED Control ====================
void setSlotLED(int slotIndex) {
  // slotIndex is 1-based, convert to 0-based for array
  int idx = constrain(slotIndex - 1, 0, MAX_SLOTS - 1);
  strip.setPixelColor(0, SLOT_COLORS[idx]);
  strip.show();
}

void clearLED() {
  strip.setPixelColor(0, 0, 0, 0);
  strip.show();
}

// Brief double LED flash to acknowledge a type-password event
void flashSlotLED() {
  for (int i = 0; i < 2; i++) {
    strip.setPixelColor(0, 0, 0, 0);
    strip.show();
    delay(100);
    setSlotLED(currentSlot);
    if (i < 1) delay(50);
  }
}

// Flash red -> none thrice to signal only one slot is available, then restore current slot color
void flashNoMoreSlots() {
  for (int i = 0; i < 3; i++) {
    strip.setPixelColor(0, Adafruit_NeoPixel::Color(255, 0, 0));
    strip.show();
    delay(150);
    strip.setPixelColor(0, 0, 0, 0);
    strip.show();
    delay(150);
  }
  setSlotLED(currentSlot);
}

// ==================== EEPROM Initialisation / Version Check ====================
// If the firmware version marker at address 0 does not match FW_VER,
// wipe all slots to empty, reset flags to defaults, and write FW_VER.
void initEEPROM() {
  uint8_t ver = EEPROM.read(ADDR_FW_VER);
  if (ver == FW_VER) return;

  // Version mismatch or fresh flash -- reset everything
  // Clear all slot passwords
  for (int i = 0; i < MAX_SLOTS; i++) {
    int addr = ADDR_SLOT_PASS_BASE + i * SLOT_PASS_SIZE;
    for (int j = 0; j < SLOT_PASS_SIZE; j++) {
      EEPROM.write(addr + j, 0);
    }
  }

  // Reset slot count and current slot to defaults
  EEPROM.write(ADDR_SLOT_COUNT, DEFAULT_SLOT_COUNT);
  EEPROM.write(ADDR_CUR_SLOT, DEFAULT_CUR_SLOT);

  // Reset append-enter to default
  EEPROM.write(ADDR_ENTER_FLAG, DEFAULT_APPEND_ENTER ? 1 : 0);

  // Reset swap press to default
  EEPROM.write(ADDR_SWAP_FLAG, DEFAULT_SWAP_PRESS ? 1 : 0);

  // Write firmware version marker
  EEPROM.write(ADDR_FW_VER, FW_VER);
  EEPROM.commit();
}

// ==================== EEPROM Load/Save ====================
void loadSettings() {
  // Slot count
  slotCount = readByteWithDefault(ADDR_SLOT_COUNT, DEFAULT_SLOT_COUNT);
  if (slotCount < 1 || slotCount > MAX_SLOTS) slotCount = DEFAULT_SLOT_COUNT;

  // Current slot
  currentSlot = readByteWithDefault(ADDR_CUR_SLOT, DEFAULT_CUR_SLOT);
  if (currentSlot < 1 || currentSlot > slotCount) currentSlot = DEFAULT_CUR_SLOT;

  // Auto-enter flag
  uint8_t flag = readByteWithDefault(ADDR_ENTER_FLAG, DEFAULT_APPEND_ENTER ? 1 : 0);
  appendEnter = (flag == 1);

  // Swap long/short press flag -- enforce 0/1, default to 0
  uint8_t swapFlag = readByteWithDefault(ADDR_SWAP_FLAG, DEFAULT_SWAP_PRESS ? 1 : 0);
  if (swapFlag != 0 && swapFlag != 1) swapFlag = DEFAULT_SWAP_PRESS ? 1 : 0;
  swapPress = (swapFlag == 1);

  // Slot passwords
  for (int i = 0; i < MAX_SLOTS; i++) {
    int addr = ADDR_SLOT_PASS_BASE + i * SLOT_PASS_SIZE;
    slotPasswords[i] = readPasswordFromEEPROM(addr);
    if (slotPasswords[i].length() == 0) {
      slotPasswords[i] = DEFAULT_PASSWORDS[i];
    }
  }
}

// ==================== Slot Validation ====================
// Ensure currentSlot is within 1..slotCount. If not, reset to 1.
void validateCurrentSlot() {
  if (currentSlot < 1 || currentSlot > slotCount) {
    currentSlot = 1;
    writeByteToEEPROM(ADDR_CUR_SLOT, currentSlot);
  }
}

// ==================== Serial Command Handlers ====================
void handleSerial() {
  if (!useSerial || !Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd.startsWith("SETPASS:")) {
    // Format: SETPASS:n <password> or SETPASS:n (to clear)
    String rest = cmd.substring(8);
    rest.trim();
    int spaceIdx = rest.indexOf(' ');
    int slotNum;
    String pass;
    if (spaceIdx < 1) {
      // SETPASS:n with no password → clear the slot
      slotNum = rest.toInt();
      pass = "";
    } else {
      slotNum = rest.substring(0, spaceIdx).toInt();
      pass = rest.substring(spaceIdx + 1);
      pass.trim();
    }

    if (slotNum < 1 || slotNum > MAX_SLOTS) {
      Serial.print("Invalid slot. Use 1..");
      Serial.println(MAX_SLOTS);
      return;
    }
    if (slotNum > slotCount) {
      Serial.println("Slot exceeds active slot count. Use SETSLOTS first.");
      return;
    }

    slotPasswords[slotNum - 1] = pass;
    int addr = ADDR_SLOT_PASS_BASE + (slotNum - 1) * SLOT_PASS_SIZE;
    writePasswordToEEPROM(addr, pass);

    Serial.print("Slot ");
    Serial.print(slotNum);
    Serial.print(" password set to: ");
    Serial.println(pass.length() > 0 ? pass : "(empty)");
  }
  else if (cmd.startsWith("SETSLOTS:")) {
    int n = cmd.substring(9).toInt();
    if (n < 1 || n > MAX_SLOTS) {
      Serial.print("Invalid count. Use 1..");
      Serial.println(MAX_SLOTS);
      return;
    }
    slotCount = n;
    writeByteToEEPROM(ADDR_SLOT_COUNT, slotCount);

    // If current slot is now out of range, reset to 1
    if (currentSlot > slotCount) {
      currentSlot = 1;
      writeByteToEEPROM(ADDR_CUR_SLOT, currentSlot);
    }
    setSlotLED(currentSlot);
    Serial.print("Active slots set to: ");
    Serial.println(slotCount);
    Serial.print("Current slot: ");
    Serial.println(currentSlot);
  }
  else if (cmd.startsWith("SETENTER:")) {
    String newVal = cmd.substring(9);
    newVal.trim();
    appendEnter = (newVal == "1");
    writeByteToEEPROM(ADDR_ENTER_FLAG, appendEnter ? 1 : 0);
    Serial.print("Auto append Enter: ");
    Serial.println(appendEnter ? "ON (1)" : "OFF (0)");
  }
  else if (cmd.startsWith("SETSWAP:")) {
    String newVal = cmd.substring(8);
    newVal.trim();
    swapPress = (newVal == "1");
    writeByteToEEPROM(ADDR_SWAP_FLAG, swapPress ? 1 : 0);
    Serial.print("Swap long/short press: ");
    Serial.println(swapPress ? "ON (1)" : "OFF (0)");
  }
  else {
    Serial.println("Unknown. Commands: SETPASS:n [password], SETSLOTS:n, SETENTER:1|0, SETSWAP:1|0");
  }
}

// ==================== Slot Cycling ====================
void cycleSlot() {
  // With only one active slot there is nothing to cycle to; warn visually
  if (slotCount <= 1) {
    flashNoMoreSlots();
    return;
  }

  currentSlot++;
  if (currentSlot > slotCount) currentSlot = 1;
  writeByteToEEPROM(ADDR_CUR_SLOT, currentSlot);
  setSlotLED(currentSlot);

  if (useSerial) {
    Serial.print("Slot ");
    Serial.print(currentSlot);
    Serial.print(": ");
    String pass = slotPasswords[currentSlot - 1];
    Serial.println(pass.length() > 0 ? pass : "(empty)");
  }
}

// ==================== Password Typing ====================
void typePassword() {
  // Visual feedback for the input event, even if the slot is empty
  flashSlotLED();

  String pass = slotPasswords[currentSlot - 1];
  if (pass.length() == 0) {
    // Empty slot: just send Enter if auto-append is on
    if (appendEnter) Keyboard.write(KEY_RETURN);
  } else {
    Keyboard.print(pass);
    if (appendEnter) Keyboard.write(KEY_RETURN);
  }
}

// ==================== setup ====================
void setup() {
  // Initialize NeoPixel
  strip.begin();
  strip.setBrightness(50);
  clearLED();

  Keyboard.begin();
  EEPROM.begin(EEPROM_SIZE);

  // Check firmware version; re-initialise EEPROM on mismatch
  initEEPROM();

  // Load all settings from EEPROM
  loadSettings();
  setSlotLED(currentSlot);

  // Serial init with timeout
  Serial.begin(115200);
  useSerial = true;
  unsigned long serialTimeout = millis() + SERIAL_TIMEOUT;
  while (!Serial && millis() < serialTimeout) { delay(10); }

  if (Serial) {
    Serial.println("=== LiteKey V2 ===");
    Serial.print("Slots: ");
    Serial.println(slotCount);
    for (int i = 0; i < slotCount; i++) {
      Serial.print("Slot ");
      Serial.print(i + 1);
      Serial.print(": ");
      String pass = slotPasswords[i];
      Serial.println(pass.length() > 0 ? pass : "(empty)");
    }
    Serial.print("Current: Slot ");
    Serial.println(currentSlot);
    Serial.print("Auto Enter: ");
    Serial.println(appendEnter ? "ON" : "OFF");
    Serial.print("Swap press: ");
    Serial.println(swapPress ? "ON" : "OFF");
    Serial.println("Commands:");
    Serial.println("  SETPASS:n <password>  -> set slot n password (omit <password> to clear)");
    Serial.println("  SETSLOTS:n            -> set active slots (1..10)");
    Serial.println("  SETENTER:1 or 0       -> auto-append Enter");
    Serial.println("  SETSWAP:1 or 0        -> swap long/short press roles");
  } else {
    Serial.end();
    useSerial = false;
  }
}

// ==================== loop ====================
void loop() {
  // ---------- Slot validation (unconditional every loop) ----------
  validateCurrentSlot();

  // ---------- Serial command handling ----------
  handleSerial();

  // ---------- Button detection ----------
  // BOOTSEL returns true when held
  // Default (swapPress=false): short press types password, long press cycles slot.
  // Swapped (swapPress=true):  short press cycles slot, long press types password.
  if (BOOTSEL) {
    unsigned long pressStart = millis();

    while (BOOTSEL) {
      if (millis() - pressStart > LONG_PRESS_MS) {
        // ---------- Long press ----------
        if (swapPress) {
          typePassword();
        } else {
          cycleSlot();
        }
        // Wait for release
        while (BOOTSEL) delay(10);
        return;
      }
      delay(10);
    }

    // Button released before long-press threshold → short press
    unsigned long pressDuration = millis() - pressStart;
    if (pressDuration >= SHORT_PRESS_MS) {
      // ---------- Short press ----------
      if (swapPress) {
        cycleSlot();
      } else {
        typePassword();
      }
    }
  }

  delay(10);
}
