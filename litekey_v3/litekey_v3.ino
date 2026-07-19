/*
 * LiteKey v3 — Multi-Slot Password / DuckyScript Keyboard for RP2040 Zero
 *
 * Hardware:
 *   - RP2040 Zero (PIN_NEOPIXEL = GPIO 16)
 *   - Single BOOTSEL button, read via global BOOTSEL object
 *   - RGB NeoPixel LED for slot indication
 *
 * Storage:
 *   - Settings: EEPROM (addresses 0..15)
 *   - Slot content (passwords / DuckyScripts): LittleFS files /slotN.bin
 *   - Fixed per-slot capacity: 96 KB per slot (independent of active slot count)
 *
 * Button UX (same as LiteKey v2):
 *   - Short press (>=50 ms, <1000 ms): execute current slot
 *   - Long press  (>=1000 ms):          cycle to next slot
 *
 * Serial commands:
 *   SETPASS:n <password>   set password for slot n
 *   SETSLOTS:n             set number of active slots (1..10)
 *   SETENTER:1|0           global append-enter for password slots
 *   SETSWAP:1|0            swap long/short press roles
 *   SETDEBUG:1|0           echo runtime debug info during DuckyScript execution
 *   SETSCRIPT:n            begin multiline DuckyScript upload (end with EOF or custom marker)
 *   SETSCRIPT:n <<MARKER  begin DuckyScript upload ending with MARKER
 *   SETTEXT:n <<MARKER   upload multiline plain text ending with MARKER
 *   VIEW:n                 show slot type and content
 *   LIST                   list all active slots
 *   DEL:n                  clear slot n to empty password
 *   RESET                  force reset all slots and config
 *   INFO                   show version, slot count, capacity
 *   HELP                   command summary
 */

#include <Keyboard.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>

// ==================== Constants ====================
#define EEPROM_SIZE         512
#define FW_VERSION          0x03
#define MAGIC               "LKEY"

#define MAX_SLOTS           10
#define SLOT_CAPACITY_KB    96      // fixed per-slot capacity in KB
#define SLOT_CAPACITY_BYTES ((uint32_t)SLOT_CAPACITY_KB * 1024)
#define SLOT_DIR            ""      // store slot files in LittleFS root to avoid mkdir issues

#define SHORT_PRESS_MS      50
#define LONG_PRESS_MS       1000
#define SERIAL_TIMEOUT      1500

#define LED_PIN             PIN_NEOPIXEL
#define NUM_LEDS            1
#define LED_BRIGHTNESS      50

// Slot type bytes
#define SLOT_TYPE_PASSWORD  0x00
#define SLOT_TYPE_DUCKY     0x01
#define SLOT_TYPE_TEXT      0x02

// Multiline upload modes
#define UPLOAD_NONE         0
#define UPLOAD_SCRIPT       1
#define UPLOAD_TEXT         2

// ==================== DuckyScript Binary Opcodes ====================
// Each script is compiled from text to a compact opcode stream.
// Variable-length opcodes carry their own length prefix, so no delimiter
// byte is needed. The stream ends with OP_END.
#define OP_END                  0x00
#define OP_STRING_SHORT         0x01  // len(1) text[len]    max 255 bytes
#define OP_STRING_LONG          0x02  // len_lo len_hi text[len]  max 65535 bytes
#define OP_STRINGLN_SHORT       0x03  // len(1) text[len]
#define OP_STRINGLN_LONG        0x04  // len_lo len_hi text[len]
#define OP_DELAY_1B             0x05  // ms(1)               0..255 ms
#define OP_DELAY_2B             0x06  // ms_lo ms_hi         0..65535 ms
#define OP_DEFAULT_DELAY_1B     0x07  // ms(1)
#define OP_DEFAULT_DELAY_2B     0x08  // ms_lo ms_hi
#define OP_REPEAT_1B            0x09  // count(1)            0..255
#define OP_REPEAT_2B            0x0A  // count_lo count_hi   0..65535
#define OP_KEY                  0x0B  // key_id(1)
#define OP_CHORD                0x0C  // mod_mask(1) key_id(1)
#define OP_MOD_ALONE            0x0D  // mod_mask(1)
#define OP_REM_SHORT            0x0E  // len(1) text[len]
#define OP_REM_LONG             0x0F  // len_lo len_hi text[len]

#define MOD_CTRL    0x01
#define MOD_SHIFT   0x02
#define MOD_ALT     0x04
#define MOD_GUI     0x08

// Internal key IDs.  ASCII printable chars use their own byte value (0x20..0x7E).
// Special keys start at 0x80.
#define IK_F1       0x80
#define IK_F2       0x81
#define IK_F3       0x82
#define IK_F4       0x83
#define IK_F5       0x84
#define IK_F6       0x85
#define IK_F7       0x86
#define IK_F8       0x87
#define IK_F9       0x88
#define IK_F10      0x89
#define IK_F11      0x8A
#define IK_F12      0x8B
#define IK_ENTER    0x8C
#define IK_TAB      0x8D
#define IK_SPACE    0x8E
#define IK_BACKSPACE 0x8F
#define IK_DELETE   0x90
#define IK_DEL      IK_DELETE
#define IK_ESC      0x91
#define IK_ESCAPE   IK_ESC
#define IK_INSERT   0x92
#define IK_HOME     0x93
#define IK_END      0x94
#define IK_PAGEUP   0x95
#define IK_PGUP     IK_PAGEUP
#define IK_PAGEDOWN 0x96
#define IK_PGDN     IK_PAGEDOWN
#define IK_UP       0x97
#define IK_UPARROW  IK_UP
#define IK_DOWN     0x98
#define IK_DOWNARROW IK_DOWN
#define IK_LEFT     0x99
#define IK_LEFTARROW IK_LEFT
#define IK_RIGHT    0x9A
#define IK_RIGHTARROW IK_RIGHT
#define IK_CAPSLOCK 0x9B
#define IK_CAPS_LOCK IK_CAPSLOCK
#define IK_NUMLOCK  0x9C
#define IK_NUM_LOCK IK_NUMLOCK
#define IK_SCROLLLOCK 0x9D
#define IK_SCROLLOCK  IK_SCROLLLOCK
#define IK_SCROLL_LOCK IK_SCROLLLOCK
#define IK_PRINTSCREEN 0x9E
#define IK_PRINT_SCREEN IK_PRINTSCREEN
#define IK_PAUSE    0x9F
#define IK_BREAK    IK_PAUSE
#define IK_APP      0xA0
#define IK_MENU     IK_APP

struct KeyMapEntry {
  const char *name;
  uint8_t internalId;
  uint8_t hidCode;
};

const KeyMapEntry KEY_MAP[] = {
  {"ENTER",        IK_ENTER,        KEY_RETURN},
  {"RETURN",       IK_ENTER,        KEY_RETURN},
  {"TAB",          IK_TAB,          KEY_TAB},
  {"SPACE",        IK_SPACE,        (uint8_t)' '},
  {"BACKSPACE",    IK_BACKSPACE,    KEY_BACKSPACE},
  {"DELETE",       IK_DELETE,       KEY_DELETE},
  {"DEL",          IK_DELETE,       KEY_DELETE},
  {"ESC",          IK_ESC,          KEY_ESC},
  {"ESCAPE",       IK_ESC,          KEY_ESC},
  {"INSERT",       IK_INSERT,       KEY_INSERT},
  {"HOME",         IK_HOME,         KEY_HOME},
  {"END",          IK_END,          KEY_END},
  {"PAGEUP",       IK_PAGEUP,       KEY_PAGE_UP},
  {"PGUP",         IK_PAGEUP,       KEY_PAGE_UP},
  {"PAGEDOWN",     IK_PAGEDOWN,     KEY_PAGE_DOWN},
  {"PGDN",         IK_PAGEDOWN,     KEY_PAGE_DOWN},
  {"UP",           IK_UP,           KEY_UP_ARROW},
  {"UPARROW",      IK_UP,           KEY_UP_ARROW},
  {"DOWN",         IK_DOWN,         KEY_DOWN_ARROW},
  {"DOWNARROW",    IK_DOWN,         KEY_DOWN_ARROW},
  {"LEFT",         IK_LEFT,         KEY_LEFT_ARROW},
  {"LEFTARROW",    IK_LEFT,         KEY_LEFT_ARROW},
  {"RIGHT",        IK_RIGHT,        KEY_RIGHT_ARROW},
  {"RIGHTARROW",   IK_RIGHT,        KEY_RIGHT_ARROW},
  {"CAPSLOCK",     IK_CAPSLOCK,     KEY_CAPS_LOCK},
  {"CAPS_LOCK",    IK_CAPSLOCK,     KEY_CAPS_LOCK},
  {"NUMLOCK",      IK_NUMLOCK,      KEY_NUM_LOCK},
  {"NUM_LOCK",     IK_NUMLOCK,      KEY_NUM_LOCK},
  {"SCROLLLOCK",   IK_SCROLLLOCK,   KEY_SCROLL_LOCK},
  {"SCROLLOCK",    IK_SCROLLLOCK,   KEY_SCROLL_LOCK},
  {"SCROLL_LOCK",  IK_SCROLLLOCK,   KEY_SCROLL_LOCK},
  {"PRINTSCREEN",  IK_PRINTSCREEN,  KEY_PRINT_SCREEN},
  {"PRINT_SCREEN", IK_PRINTSCREEN,  KEY_PRINT_SCREEN},
  {"PAUSE",        IK_PAUSE,        KEY_PAUSE},
  {"BREAK",        IK_BREAK,        KEY_PAUSE},
  {"APP",          IK_APP,          KEY_RIGHT_GUI},
  {"MENU",         IK_APP,          KEY_RIGHT_GUI},
  {"F1",           IK_F1,           KEY_F1},
  {"F2",           IK_F2,           KEY_F2},
  {"F3",           IK_F3,           KEY_F3},
  {"F4",           IK_F4,           KEY_F4},
  {"F5",           IK_F5,           KEY_F5},
  {"F6",           IK_F6,           KEY_F6},
  {"F7",           IK_F7,           KEY_F7},
  {"F8",           IK_F8,           KEY_F8},
  {"F9",           IK_F9,           KEY_F9},
  {"F10",          IK_F10,          KEY_F10},
  {"F11",          IK_F11,          KEY_F11},
  {"F12",          IK_F12,          KEY_F12},
};

// ==================== LED Colors (R, G, B) ====================
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
uint8_t  slotCount = 1;
uint32_t slotCapacity = SLOT_CAPACITY_BYTES; // computed at boot
uint8_t  currentSlot = 1;             // 1-based
bool     appendEnter = true;
bool     swapPress = false;           // swap long/short press roles
bool     debugMode = false;           // SETDEBUG:1|0 echoes runtime info
bool     useSerial = true;
bool     fsAvailable = false;

uint8_t  uploadMode = UPLOAD_NONE;
uint8_t  uploadSlot = 0;
String   uploadBinary;        // compiled binary or text accumulated during upload
String   uploadMarker;        // expected EOF marker (space-free)
uint16_t uploadLineNo = 0;    // current upload line number
bool     uploadHadError = false;
uint8_t  uploadLastOp = 0;    // last executable opcode, for DuckyScript REPEAT validation

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// DuckyScript execution state
String   lastDuckyLine;
uint16_t defaultDelay = 0;

// ==================== Forward Declarations ====================
void validateAndInitConfig();
void readConfig();
void writeConfig();
void resetAllSlots();
uint32_t computeCapacity(uint8_t count);
String slotPath(uint8_t n);

uint8_t getSlotType(uint8_t n);
void setSlotType(uint8_t n, uint8_t type);
void clearSlot(uint8_t n);
bool writePassword(uint8_t n, const String &pw);
bool writeDuckyScript(uint8_t n, const String &binary);
String readSlotContent(uint8_t n);
String readSlotBinary(uint8_t n);

void setSlotLED(uint8_t slotIndex);
void clearLED();
void flashSlotLED();
void flashNoMoreSlots();

void handleSerial();
void processCommand(const String &cmd);
void beginUpload(uint8_t mode, uint8_t n, const String &marker);
void handleUploadLine(const String &line);
const char* slotTypeName(uint8_t t);
void typeTextSlot(uint8_t n);
bool writeTextSlot(uint8_t n, const String &text);
bool isValidMarker(const String &marker);

void executeSlot(uint8_t n);
void doDiagnose();
void typePasswordSlot(uint8_t n);
void runDuckyBinary(const String &binary);
String compileDuckyScript(const String &script, uint16_t &badLine, String &reason);
String decompileDuckyScript(const String &binary);
bool validateDuckyScript(const String &script, uint16_t &badLine, String &reason);
bool dispatchDuckyLine(const String &line, String &reason, bool actuallyRun);
uint8_t keyCodeForName(const String &name, bool &found);
void pressChord(uint8_t modifiers[], uint8_t modCount, uint8_t key);

bool isModifierToken(const String &tok);
bool parseModifier(const String &tok, uint8_t &key);

uint8_t internalIdForName(const String &name, bool &found);
const char* nameForInternalId(uint8_t id);
uint8_t hidCodeForInternalId(uint8_t id);
bool executeOpcode(uint8_t op, const String &text, uint16_t arg1, uint16_t arg2, String &reason, bool actuallyRun);

// ==================== Setup ====================
void setup() {
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  clearLED();

  Keyboard.begin();
  EEPROM.begin(EEPROM_SIZE);

  // Mount LittleFS. If it fails (unformatted/corrupt), format and retry.
  fsAvailable = LittleFS.begin();
  if (!fsAvailable) {
    LittleFS.format();
    fsAvailable = LittleFS.begin();
  }

  // On first v3 boot (EEPROM magic/version mismatch), force a clean LittleFS.
  char magic[4];
  for (int i = 0; i < 4; i++) magic[i] = EEPROM.read(i);
  uint8_t ver = EEPROM.read(4);
  if (fsAvailable && (strncmp(magic, MAGIC, 4) != 0 || ver != FW_VERSION)) {
    LittleFS.format();
    fsAvailable = LittleFS.begin();
  }

  // Only try to make a sub-directory if we are using one.
  if (fsAvailable && strlen(SLOT_DIR) > 0 && !LittleFS.exists(SLOT_DIR)) {
    LittleFS.mkdir(SLOT_DIR);
  }

  validateAndInitConfig();
  setSlotLED(currentSlot);

  Serial.begin(115200);
  useSerial = true;
  unsigned long serialTimeout = millis() + SERIAL_TIMEOUT;
  while (!Serial && millis() < serialTimeout) { delay(10); }

  if (Serial) {
    Serial.println("=== LiteKey v3 ===");
    Serial.print("Version: 0x");
    Serial.println(FW_VERSION, HEX);
    Serial.print("Slots: ");
    Serial.println(slotCount);
    Serial.print("Capacity per slot: ");
    Serial.println(slotCapacity);
    Serial.print("LittleFS: ");
    Serial.println(fsAvailable ? "available" : "UNAVAILABLE");
    Serial.print("Current slot: ");
    Serial.println(currentSlot);
    Serial.print("Slot files: ");
    for (uint8_t i = 1; i <= slotCount; i++) {
      if (i > 1) Serial.print(", ");
      Serial.print("slot");
      Serial.print(i);
      Serial.print("=");
      String p = slotPath(i);
      if (LittleFS.exists(p)) {
        File tf = LittleFS.open(p, "r");
        if (tf) {
          Serial.print(tf.size());
          tf.close();
        } else {
          Serial.print("?");
        }
      } else {
        Serial.print("-");
      }
    }
    Serial.println();
    Serial.print("Auto Enter: ");
    Serial.println(appendEnter ? "ON" : "OFF");
    Serial.print("Swap Press: ");
    Serial.println(swapPress ? "ON" : "OFF");
    Serial.print("Debug Mode: ");
    Serial.println(debugMode ? "ON" : "OFF");
    Serial.println("Commands: SETPASS:n <password>; SETSLOTS:n; SETENTER:1|0; SETSWAP:1|0; SETDEBUG:1|0; SETSCRIPT:n [<<MARKER]; SETTEXT:n <<MARKER; VIEW:n; LIST; DEL:n; RESET; DIAGNOSE; INFO; HELP");
  } else {
    Serial.end();
    useSerial = false;
  }
}

// ==================== Loop ====================
void loop() {
  if (useSerial) handleSerial();
  handleButton();
  delay(10);
}

// ==================== Config Management ====================
// Small settings are stored in EEPROM (addresses 0..15).
// Layout:
//   bytes 0-3: magic "LKEY"
//   byte  4:   firmware version
//   byte  5:   slotCount
//   byte  6:   perSlotCapacity in KB (e.g. 96 = 96 KB)
//   byte  7:   reserved
//   byte  8:   appendEnter flag
//   bytes 9-10: type bitmap (10 bits, bit i = slot i+1)
//   byte  11:  currentSlot
//   byte  12:  swapPress flag
//   byte  13:  debugMode flag
//   bytes 14-15: reserved
// Slot content (passwords and DuckyScripts) is stored in LittleFS files.
void validateAndInitConfig() {
  readConfig();

  bool configInvalid = false;

  // Validate magic/version
  char magic[4];
  for (int i = 0; i < 4; i++) magic[i] = EEPROM.read(i);
  uint8_t ver = EEPROM.read(4);
  if (strncmp(magic, MAGIC, 4) != 0 || ver != FW_VERSION) {
    configInvalid = true;
  }

  // Validate slot count
  if (slotCount < 1 || slotCount > MAX_SLOTS) {
    slotCount = 1;
    configInvalid = true;
  }

  // Validate capacity (stored in byte 6 as KB)
  uint32_t expectedCap = computeCapacity(slotCount);
  if (slotCapacity != expectedCap) {
    slotCapacity = expectedCap;
    configInvalid = true;
  }

  // Validate current slot
  if (currentSlot < 1 || currentSlot > slotCount) {
    currentSlot = 1;
    configInvalid = true;
  }

  // Validate flags
  appendEnter = (appendEnter != false);
  swapPress = (swapPress == true);
  debugMode = (debugMode == true);

  writeConfig();

  if (configInvalid) {
    resetAllSlots();
  }
}

void readConfig() {
  char magic[4];
  for (int i = 0; i < 4; i++) magic[i] = EEPROM.read(i);
  uint8_t ver = EEPROM.read(4);

  if (strncmp(magic, MAGIC, 4) != 0 || ver != FW_VERSION) {
    slotCount = 1;
    slotCapacity = computeCapacity(1);
    appendEnter = true;
    swapPress = false;
    debugMode = false;
    currentSlot = 1;
    return;
  }

  slotCount = EEPROM.read(5);
  uint8_t capKB = EEPROM.read(6);
  slotCapacity = (uint32_t)capKB * 1024;
  // byte 7 is now reserved (was the high byte of the old uint16 capacity)
  appendEnter = (EEPROM.read(8) != 0);
  // bytes 9-10: type bitmap (not needed as a variable; kept in EEPROM)
  currentSlot = EEPROM.read(11);
  if (currentSlot < 1 || currentSlot > MAX_SLOTS) currentSlot = 1;
  swapPress = (EEPROM.read(12) == 1);
  debugMode = (EEPROM.read(13) == 1);
}

void writeConfig() {
  for (int i = 0; i < 4; i++) EEPROM.write(i, MAGIC[i]);
  EEPROM.write(4, FW_VERSION);
  EEPROM.write(5, slotCount);
  EEPROM.write(6, (uint8_t)(slotCapacity / 1024)); // capacity in KB
  EEPROM.write(7, 0); // reserved
  EEPROM.write(8, appendEnter ? 1 : 0);

  uint16_t typeBits = 0;
  for (uint8_t i = 1; i <= MAX_SLOTS; i++) {
    if (getSlotType(i) == SLOT_TYPE_DUCKY) {
      typeBits |= (1 << (i - 1));
    }
  }
  EEPROM.write(9, typeBits & 0xFF);
  EEPROM.write(10, (typeBits >> 8) & 0xFF);

  EEPROM.write(11, currentSlot);
  EEPROM.write(12, swapPress ? 1 : 0);
  EEPROM.write(13, debugMode ? 1 : 0);

  // reserved bytes
  for (int i = 14; i < 16; i++) EEPROM.write(i, 0);

  EEPROM.commit();
}

uint32_t computeCapacity(uint8_t count) {
  (void)count;
  return SLOT_CAPACITY_BYTES;
}

void resetAllSlots() {
  for (uint8_t i = 1; i <= MAX_SLOTS; i++) {
    clearSlot(i);
  }
}

// ==================== Diagnostics ====================
void doDiagnose() {
  Serial.println("== DIAGNOSE ==");

  // 1. LittleFS mount test
  Serial.print("LittleFS.begin(): ");
  bool mounted = LittleFS.begin();
  Serial.println(mounted ? "OK" : "FAIL");

  // 2. If mount failed, try to format and remount
  if (!mounted) {
    Serial.println("Mount failed; attempting format...");
    Serial.print("LittleFS.format(): ");
    bool fmt = LittleFS.format();
    Serial.println(fmt ? "OK" : "FAIL");
    Serial.print("LittleFS.begin() after format: ");
    mounted = LittleFS.begin();
    Serial.println(mounted ? "OK" : "FAIL");
  }

  if (mounted) {
    // 3. List files before format
    Serial.println("Files before format:");
    Dir dir = LittleFS.openDir("/");
    int fileCount = 0;
    while (dir.next()) {
      fileCount++;
      Serial.print("  ");
      Serial.print(dir.fileName());
      Serial.print("  size=");
      Serial.println(dir.fileSize());
    }
    if (fileCount == 0) Serial.println("  (none)");

    // 4. Format
    Serial.print("LittleFS.format(): ");
    bool fmt = LittleFS.format();
    Serial.println(fmt ? "OK" : "FAIL");

    // 5. Remount
    Serial.print("Remount after format: ");
    mounted = LittleFS.begin();
    Serial.println(mounted ? "OK" : "FAIL");

    if (mounted) {
      // 6. Test write/read
      const char* testPath = "/lk_test.bin";
      File wf = LittleFS.open(testPath, "w");
      if (wf) {
        size_t w = 0;
        w += wf.write((uint8_t)'H');
        w += wf.write((uint8_t)'i');
        w += wf.write((uint8_t)'!');
        w += wf.write((uint8_t)0);
        wf.flush();
        wf.close();
        Serial.print("Test write (4 bytes): ");
        Serial.println(w == 4 ? "OK" : "FAIL");

        File rf = LittleFS.open(testPath, "r");
        if (rf) {
          String content = "";
          while (rf.available()) {
            char c = rf.read();
            if (c == '\0') break;
            content += c;
          }
          rf.close();
          Serial.print("Test read back: '");
          Serial.print(content);
          Serial.println("'");
        } else {
          Serial.println("Test read back: FAIL (cannot open)");
        }
        LittleFS.remove(testPath);
      } else {
        Serial.println("Test write: FAIL (cannot open)");
      }

      // 7. List files after format/cleanup
      Serial.println("Files after format:");
      Dir dir = LittleFS.openDir("/");
      fileCount = 0;
      while (dir.next()) {
        fileCount++;
        Serial.print("  ");
        Serial.print(dir.fileName());
        Serial.print("  size=");
        Serial.println(dir.fileSize());
      }
      if (fileCount == 0) Serial.println("  (none)");
    }
  } else {
    Serial.println("LittleFS is unavailable. Check Arduino IDE board settings.");
  }

  // 8. EEPROM header dump
  Serial.print("EEPROM header (hex): ");
  for (int i = 0; i < 16; i++) {
    if (i > 0) Serial.print(" ");
    uint8_t b = EEPROM.read(i);
    if (b < 0x10) Serial.print("0");
    Serial.print(b, HEX);
  }
  Serial.println();

  // 8. Reset everything to clean state
  Serial.println("Resetting config and slots...");
  resetAllSlots();
  slotCount = 1;
  slotCapacity = computeCapacity(1);
  appendEnter = true;
  swapPress = false;
  debugMode = false;
  currentSlot = 1;
  writeConfig();
  setSlotLED(currentSlot);

  Serial.println("DIAGNOSE complete");
}

// ==================== Slot Management ====================
String slotPath(uint8_t n) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s/slot%d.bin", SLOT_DIR, n);
  return String(buf);
}

uint8_t getSlotType(uint8_t n) {
  if (n < 1 || n > MAX_SLOTS) return SLOT_TYPE_PASSWORD;
  String path = slotPath(n);
  if (!LittleFS.exists(path)) return SLOT_TYPE_PASSWORD;
  File f = LittleFS.open(path, "r");
  if (!f || f.size() < 1) return SLOT_TYPE_PASSWORD;
  uint8_t t = f.read();
  f.close();
  if (t == SLOT_TYPE_DUCKY) return SLOT_TYPE_DUCKY;
  if (t == SLOT_TYPE_TEXT) return SLOT_TYPE_TEXT;
  return SLOT_TYPE_PASSWORD;
}

void setSlotType(uint8_t n, uint8_t type) {
  if (n < 1 || n > MAX_SLOTS) return;
  String path = slotPath(n);
  String content = "";
  if (LittleFS.exists(path)) {
    File rf = LittleFS.open(path, "r");
    if (rf) {
      if (rf.available()) rf.read(); // skip old type byte
      while (rf.available()) {
        char c = rf.read();
        if (c == '\0') break;
        content += c;
      }
      rf.close();
    }
  }
  File f = LittleFS.open(path, "w");
  if (f) {
    f.write((uint8_t)type);
    for (unsigned i = 0; i < content.length(); i++) f.write((uint8_t)content[i]);
    f.write((uint8_t)0);
    f.flush();
    f.close();
  }
}

void clearSlot(uint8_t n) {
  if (n < 1 || n > MAX_SLOTS) return;
  File f = LittleFS.open(slotPath(n), "w");
  if (f) {
    f.write((uint8_t)SLOT_TYPE_PASSWORD);
    f.write((uint8_t)0);
    f.flush();
    f.close();
  }
}

bool writePassword(uint8_t n, const String &pw) {
  if (n < 1 || n > MAX_SLOTS) return false;
  uint32_t maxLen = slotCapacity - 2;
  File f = LittleFS.open(slotPath(n), "w");
  if (!f) return false;
  size_t written = 0;
  written += f.write((uint8_t)SLOT_TYPE_PASSWORD);
  int len = pw.length();
  if ((uint32_t)len > maxLen) len = (int)maxLen;
  for (int i = 0; i < len; i++) written += f.write((uint8_t)pw[i]);
  written += f.write((uint8_t)0);
  f.flush();
  f.close();
  return (written == (size_t)(len + 2));
}

bool writeDuckyScript(uint8_t n, const String &binary) {
  if (n < 1 || n > MAX_SLOTS) return false;
  uint32_t maxBytes = slotCapacity - 2; // type byte + file null terminator
  File f = LittleFS.open(slotPath(n), "w");
  if (!f) return false;
  size_t written = 0;
  written += f.write((uint8_t)SLOT_TYPE_DUCKY);
  int len = binary.length();
  if ((uint32_t)len > maxBytes) len = (int)maxBytes;
  for (int i = 0; i < len; i++) written += f.write((uint8_t)binary[i]);
  // Optional extra null after OP_END if there is room; the file null terminator follows.
  if ((uint32_t)(len + 1) <= maxBytes) {
    written += f.write((uint8_t)0);
  }
  written += f.write((uint8_t)0);
  f.flush();
  f.close();
  return (written >= (size_t)(len + 2)); // type byte + at least one null
}

String readSlotBinary(uint8_t n) {
  String out = "";
  if (n < 1 || n > MAX_SLOTS) return out;
  File f = LittleFS.open(slotPath(n), "r");
  if (!f) return out;
  // skip type byte
  if (f.available()) f.read();
  while (f.available()) {
    char c = f.read();
    if (c == '\0') break;
    out += c;
  }
  f.close();
  return out;
}

String readSlotContent(uint8_t n) {
  if (n < 1 || n > MAX_SLOTS) return "";
  uint8_t t = getSlotType(n);
  if (t == SLOT_TYPE_DUCKY) {
    String binary = readSlotBinary(n);
    return decompileDuckyScript(binary);
  }
  // Password slot: return raw text after type byte
  String out = "";
  File f = LittleFS.open(slotPath(n), "r");
  if (!f) return out;
  if (f.available()) f.read();
  while (f.available()) {
    char c = f.read();
    if (c == '\0') break;
    out += c;
  }
  f.close();
  return out;
}

// ==================== LED Control ====================
void setSlotLED(uint8_t slotIndex) {
  uint8_t idx = constrain(slotIndex - 1, 0, MAX_SLOTS - 1);
  strip.setPixelColor(0, SLOT_COLORS[idx]);
  strip.show();
}

void clearLED() {
  strip.setPixelColor(0, 0, 0, 0);
  strip.show();
}

// Brief double LED flash to acknowledge a slot-execute event
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

// ==================== Serial Command Handling ====================
void handleSerial() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();

  if (uploadMode != UPLOAD_NONE) {
    handleUploadLine(line);
    return;
  }

  processCommand(line);
}

void processCommand(const String &cmd) {
  if (cmd.length() == 0) return;

  if (cmd.startsWith("SETPASS:")) {
    String rest = cmd.substring(8);
    rest.trim();
    int spaceIdx = rest.indexOf(' ');
    int slotNum;
    String pass;
    if (spaceIdx < 1) {
      slotNum = rest.toInt();
      pass = "";
    } else {
      slotNum = rest.substring(0, spaceIdx).toInt();
      pass = rest.substring(spaceIdx + 1);
      pass.trim();
    }

    if (slotNum < 1 || slotNum > MAX_SLOTS) {
      Serial.println("ERR invalid slot");
      return;
    }
    if (slotNum > slotCount) {
      Serial.println("ERR slot exceeds active count");
      return;
    }
    if (pass.length() > slotCapacity - 2) {
      Serial.println("ERR password too long");
      return;
    }

    if (!writePassword(slotNum, pass)) {
      Serial.println("ERR write failed");
      return;
    }
    Serial.print("OK slot ");
    Serial.print(slotNum);
    Serial.print(" password set (");
    Serial.print(pass.length());
    Serial.println(" bytes)");
  }
  else if (cmd.startsWith("SETSLOTS:")) {
    int n = cmd.substring(9).toInt();
    if (n < 1 || n > MAX_SLOTS) {
      Serial.println("ERR invalid count");
      return;
    }
    slotCount = n;
    slotCapacity = SLOT_CAPACITY_BYTES; // fixed per slot
    writeConfig();
    if (currentSlot > slotCount) currentSlot = 1;
    setSlotLED(currentSlot);
    Serial.print("OK slots=");
    Serial.print(slotCount);
    Serial.print(" cap=");
    Serial.println(slotCapacity);
  }
  else if (cmd.startsWith("SETENTER:")) {
    String v = cmd.substring(9);
    v.trim();
    appendEnter = (v == "1");
    writeConfig();
    Serial.print("OK auto enter ");
    Serial.println(appendEnter ? "ON" : "OFF");
  }
  else if (cmd.startsWith("SETSWAP:")) {
    String v = cmd.substring(8);
    v.trim();
    swapPress = (v == "1");
    writeConfig();
    Serial.print("OK swap press ");
    Serial.println(swapPress ? "ON" : "OFF");
  }
  else if (cmd.startsWith("SETDEBUG:")) {
    String v = cmd.substring(9);
    v.trim();
    debugMode = (v == "1");
    writeConfig();
    Serial.print("OK debug mode ");
    Serial.println(debugMode ? "ON" : "OFF");
  }
  else if (cmd.startsWith("SETSCRIPT:")) {
    String rest = cmd.substring(10);
    rest.trim();
    int markerIdx = rest.indexOf(" <<");
    int slotNum;
    String marker;
    if (markerIdx < 0) {
      // Default EOF marker
      for (unsigned i = 0; i < rest.length(); i++) {
        if (!isDigit(rest[i])) {
          Serial.println("ERR use SETSCRIPT:n or SETSCRIPT:n <<MARKER");
          return;
        }
      }
      slotNum = rest.toInt();
      marker = "EOF";
    } else {
      String slotPart = rest.substring(0, markerIdx);
      slotPart.trim();
      for (unsigned i = 0; i < slotPart.length(); i++) {
        if (!isDigit(slotPart[i])) {
          Serial.println("ERR use SETSCRIPT:n or SETSCRIPT:n <<MARKER");
          return;
        }
      }
      slotNum = slotPart.toInt();
      marker = rest.substring(markerIdx + 3);
      marker.trim();
    }
    if (slotNum < 1 || slotNum > MAX_SLOTS) {
      Serial.println("ERR invalid slot");
      return;
    }
    if (slotNum > slotCount) {
      Serial.println("ERR slot exceeds active count");
      return;
    }
    if (!isValidMarker(marker)) {
      Serial.println("ERR marker must be non-empty and space-free");
      return;
    }
    beginUpload(UPLOAD_SCRIPT, slotNum, marker);
  }
  else if (cmd.startsWith("SETTEXT:")) {
    String rest = cmd.substring(8);
    rest.trim();
    int markerIdx = rest.indexOf(" <<");
    if (markerIdx < 0) {
      Serial.println("ERR use SETTEXT:n <<MARKER");
      return;
    }
    String slotPart = rest.substring(0, markerIdx);
    slotPart.trim();
    for (unsigned i = 0; i < slotPart.length(); i++) {
      if (!isDigit(slotPart[i])) {
        Serial.println("ERR use SETTEXT:n <<MARKER");
        return;
      }
    }
    int slotNum = slotPart.toInt();
    String marker = rest.substring(markerIdx + 3);
    marker.trim();
    if (slotNum < 1 || slotNum > MAX_SLOTS) {
      Serial.println("ERR invalid slot");
      return;
    }
    if (slotNum > slotCount) {
      Serial.println("ERR slot exceeds active count");
      return;
    }
    if (!isValidMarker(marker)) {
      Serial.println("ERR marker must be non-empty and space-free");
      return;
    }
    beginUpload(UPLOAD_TEXT, slotNum, marker);
  }
  else if (cmd.startsWith("VIEW:")) {
    int slotNum = cmd.substring(5).toInt();
    if (slotNum < 1 || slotNum > slotCount) {
      Serial.println("ERR invalid slot");
      return;
    }
    uint8_t t = getSlotType(slotNum);
    String content = readSlotContent(slotNum);
    Serial.print("Slot ");
    Serial.print(slotNum);
    Serial.print(" type=");
    Serial.print(slotTypeName(t));
    Serial.print(" size=");
    Serial.println(content.length());
    Serial.println("---");
    Serial.println(content);
    Serial.println("---");
  }
  else if (cmd == "LIST") {
    Serial.println("Slot Type      Size");
    for (uint8_t i = 1; i <= slotCount; i++) {
      uint8_t t = getSlotType(i);
      String content = readSlotContent(i);
      Serial.print(i);
      Serial.print("    ");
      Serial.print(slotTypeName(t));
      // Pad type column to 10 chars for alignment
      int pad = 10 - strlen(slotTypeName(t));
      for (int p = 0; p < pad; p++) Serial.print(" ");
      Serial.println(content.length());
    }
  }
  else if (cmd.startsWith("DEL:")) {
    int slotNum = cmd.substring(4).toInt();
    if (slotNum < 1 || slotNum > slotCount) {
      Serial.println("ERR invalid slot");
      return;
    }
    clearSlot(slotNum);
    setSlotType(slotNum, SLOT_TYPE_PASSWORD);
    writeConfig();
    Serial.print("OK slot ");
    Serial.print(slotNum);
    Serial.println(" cleared");
  }
  else if (cmd == "RESET") {
    resetAllSlots();
    slotCount = 1;
    slotCapacity = computeCapacity(1);
    appendEnter = true;
    swapPress = false;
    debugMode = false;
    currentSlot = 1;
    writeConfig();
    setSlotLED(currentSlot);
    Serial.println("OK reset");
  }
  else if (cmd == "DIAGNOSE") {
    doDiagnose();
  }
  else if (cmd == "INFO") {
    Serial.print("LiteKey v3 version=0x");
    Serial.println(FW_VERSION, HEX);
    Serial.print("slots=");
    Serial.println(slotCount);
    Serial.print("capacity=");
    Serial.println(slotCapacity);
    Serial.print("current=");
    Serial.println(currentSlot);
    Serial.print("autoEnter=");
    Serial.println(appendEnter ? "1" : "0");
    Serial.print("swapPress=");
    Serial.println(swapPress ? "1" : "0");
    Serial.print("debugMode=");
    Serial.println(debugMode ? "1" : "0");
  }
  else if (cmd == "HELP") {
    Serial.println("Commands:");
    Serial.println("  SETPASS:n <password>  -> set password for slot n (omit password to clear)");
    Serial.println("  SETSLOTS:n            -> set number of active slots (1..10)");
    Serial.println("  SETENTER:1|0          -> auto-append Enter after password slots");
    Serial.println("  SETSWAP:1|0           -> swap long/short press roles");
    Serial.println("  SETDEBUG:1|0          -> echo runtime debug info during DuckyScript execution");
    Serial.println("  SETSCRIPT:n           -> begin DuckyScript upload (send 'EOF' alone to finish)");
    Serial.println("  SETSCRIPT:n <<MARKER -> begin DuckyScript upload ending with MARKER");
    Serial.println("  SETTEXT:n <<MARKER  -> upload multiline plain text ending with MARKER");
    Serial.println("  VIEW:n                -> show slot n type and content");
    Serial.println("  LIST                  -> list all active slots with type and size");
    Serial.println("  DEL:n                 -> clear slot n to empty password");
    Serial.println("  RESET                 -> reset all slots and settings to defaults");
    Serial.println("  DIAGNOSE              -> format LittleFS, test read/write, dump state, reset");
    Serial.println("  INFO                  -> show version, slots, capacity, current slot");
    Serial.println("  HELP                  -> show this command summary");
  }
  else {
    Serial.println("ERR unknown command");
  }
}

// ==================== Multiline Upload (DuckyScript + Text) ====================

bool isValidMarker(const String &marker) {
  if (marker.length() == 0) return false;
  for (unsigned i = 0; i < marker.length(); i++) {
    if (marker[i] == ' ') return false;
  }
  return true;
}

const char* slotTypeName(uint8_t t) {
  if (t == SLOT_TYPE_DUCKY) return "ducky";
  if (t == SLOT_TYPE_TEXT) return "text";
  return "password";
}

void beginUpload(uint8_t mode, uint8_t n, const String &marker) {
  uploadMode = mode;
  uploadSlot = n;
  uploadBinary = "";
  uploadBinary.reserve(slotCapacity > 4096 ? 4096 : slotCapacity);
  uploadMarker = marker;
  uploadLineNo = 0;
  uploadHadError = false;
  uploadLastOp = 0;
  Serial.println("OK send lines, end with " + marker);
  Serial.println(">>");
}

void handleUploadLine(const String &line) {
  if (line == uploadMarker) {
    uint8_t mode = uploadMode;
    uploadMode = UPLOAD_NONE;

    if (mode == UPLOAD_SCRIPT) {
      if (uploadHadError) {
        clearSlot(uploadSlot);
        setSlotType(uploadSlot, SLOT_TYPE_PASSWORD);
        writeConfig();
        Serial.println("ERR upload aborted; slot cleared");
        uploadBinary = "";
        return;
      }

      // Append final OP_END
      uint32_t finalSize = uploadBinary.length() + 1;
      if (finalSize > slotCapacity - 2) {
        Serial.println("ERR script exceeds slot capacity");
        uploadBinary = "";
        return;
      }
      uploadBinary += (char)OP_END;

      if (!writeDuckyScript(uploadSlot, uploadBinary)) {
        Serial.println("ERR script write failed");
        uploadBinary = "";
        return;
      }
      setSlotType(uploadSlot, SLOT_TYPE_DUCKY);
      writeConfig();
      Serial.print("OK script stored in slot ");
      Serial.print(uploadSlot);
      Serial.print(" (");
      Serial.print(uploadBinary.length());
      Serial.println(" bytes)");
      uploadBinary = "";
      return;
    }

    if (mode == UPLOAD_TEXT) {
      if (uploadHadError) {
        // Error was already reported and slot was already reset
        uploadBinary = "";
        return;
      }

      // Trim trailing newline if present so an empty final line does not add a blank line
      while (uploadBinary.length() > 0 && uploadBinary[uploadBinary.length() - 1] == '\n') {
        uploadBinary.remove(uploadBinary.length() - 1);
      }

      if (!writeTextSlot(uploadSlot, uploadBinary)) {
        clearSlot(uploadSlot);
        setSlotType(uploadSlot, SLOT_TYPE_PASSWORD);
        writeConfig();
        Serial.println("ERR text write failed; slot cleared");
        uploadBinary = "";
        return;
      }
      setSlotType(uploadSlot, SLOT_TYPE_TEXT);
      writeConfig();
      Serial.print("OK text stored in slot ");
      Serial.print(uploadSlot);
      Serial.print(" (");
      Serial.print(uploadBinary.length());
      Serial.println(" bytes)");
      uploadBinary = "";
      return;
    }

    uploadBinary = "";
    return;
  }

  if (uploadHadError) {
    // Drain lines until marker. Do not echo prompts; upload already failed.
    return;
  }

  uploadLineNo++;

  if (uploadMode == UPLOAD_SCRIPT) {
    // Compile this line on the go.
    String lineBinary = "";
    String reason;
    if (!compileDuckyLine(line, lineBinary, reason, uploadLastOp)) {
      uploadHadError = true;
      Serial.print("ERR line ");
      Serial.print(uploadLineNo);
      Serial.print(": ");
      Serial.println(reason);
      return;
    }

    // Check capacity: current binary + this line + OP_END must fit.
    uint32_t before = uploadBinary.length();
    uint32_t after = before + lineBinary.length();
    uint32_t afterWithEnd = after + 1;
    if (afterWithEnd > slotCapacity - 2) {
      uploadHadError = true;
      Serial.print("ERR line ");
      Serial.print(uploadLineNo);
      Serial.print(": compiled script would exceed slot capacity (before: ");
      Serial.print(before);
      Serial.print(", after: ");
      Serial.print(after);
      Serial.print(", content: \"");
      String shown = line;
      if (shown.length() > 40) {
        shown = shown.substring(0, 37) + "...";
      }
      Serial.print(shown);
      Serial.println("\")");
      return;
    }

    uploadBinary += lineBinary;
    Serial.println(">>");
  }
  else if (uploadMode == UPLOAD_TEXT) {
    // Append line + newline; newlines are typed as Enter when the slot is executed.
    uint32_t before = uploadBinary.length();
    uint32_t addLen = (uint32_t)line.length() + 1; // +1 for '\n'
    uint32_t after = before + addLen;
    uint32_t afterWithNull = after + 1; // +1 for file null terminator
    if (afterWithNull > slotCapacity - 2) {
      uploadHadError = true;
      clearSlot(uploadSlot);
      setSlotType(uploadSlot, SLOT_TYPE_PASSWORD);
      writeConfig();
      Serial.print("ERR line ");
      Serial.print(uploadLineNo);
      Serial.print(": text would exceed slot capacity (before: ");
      Serial.print(before);
      Serial.print(", after: ");
      Serial.print(after);
      Serial.print(", content: \"");
      String shown = line;
      if (shown.length() > 40) {
        shown = shown.substring(0, 37) + "...";
      }
      Serial.print(shown);
      Serial.println("\"); slot cleared");
      return;
    }

    uploadBinary += line;
    uploadBinary += '\n';
    Serial.println(">>");
  }
}

// ==================== Slot Execution ====================
void executeSlot(uint8_t n) {
  if (n < 1 || n > slotCount) return;
  flashSlotLED();
  uint8_t t = getSlotType(n);
  if (t == SLOT_TYPE_DUCKY) {
    String binary = readSlotBinary(n);
    runDuckyBinary(binary);
  } else if (t == SLOT_TYPE_TEXT) {
    typeTextSlot(n);
  } else {
    typePasswordSlot(n);
  }
}

void typePasswordSlot(uint8_t n) {
  String pw = readSlotContent(n);
  if (pw.length() == 0) {
    if (appendEnter) Keyboard.write(KEY_RETURN);
  } else {
    Keyboard.print(pw);
    if (appendEnter) Keyboard.write(KEY_RETURN);
  }
}

void typeTextSlot(uint8_t n) {
  String text = readSlotContent(n);
  for (unsigned i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == '\n') {
      Keyboard.write(KEY_RETURN);
    } else if (c == '\r') {
      // Skip carriage returns
    } else {
      Keyboard.print(c);
    }
  }
}

bool writeTextSlot(uint8_t n, const String &text) {
  if (n < 1 || n > MAX_SLOTS) return false;
  uint32_t maxLen = slotCapacity - 2;
  File f = LittleFS.open(slotPath(n), "w");
  if (!f) return false;
  size_t written = 0;
  written += f.write((uint8_t)SLOT_TYPE_TEXT);
  int len = text.length();
  if ((uint32_t)len > maxLen) len = (int)maxLen;
  for (int i = 0; i < len; i++) written += f.write((uint8_t)text[i]);
  written += f.write((uint8_t)0);
  f.flush();
  f.close();
  return (written == (size_t)(len + 2));
}
void runDuckyBinary(const String &binary) {
  lastDuckyLine = "";
  defaultDelay = 0;

  uint8_t lastOp = 0;
  String lastText = "";
  uint16_t lastArg1 = 0, lastArg2 = 0;

  size_t idx = 0;
  while (idx < binary.length()) {
    uint8_t op = binary[idx++];
    if (op == OP_END) break;

    uint16_t len = 0;
    String text = "";
    uint16_t arg1 = 0, arg2 = 0;

    switch (op) {
      case OP_STRING_SHORT:
      case OP_STRINGLN_SHORT:
      case OP_REM_SHORT:
        if (idx >= binary.length()) { op = OP_END; break; }
        len = binary[idx++];
        break;
      case OP_STRING_LONG:
      case OP_STRINGLN_LONG:
      case OP_REM_LONG:
        if (idx + 1 >= binary.length()) { op = OP_END; break; }
        len = binary[idx] | ((uint16_t)binary[idx + 1] << 8);
        idx += 2;
        break;
      case OP_DELAY_1B:
      case OP_DEFAULT_DELAY_1B:
      case OP_REPEAT_1B:
      case OP_KEY:
      case OP_MOD_ALONE:
        if (idx >= binary.length()) { op = OP_END; break; }
        arg1 = binary[idx++];
        break;
      case OP_DELAY_2B:
      case OP_DEFAULT_DELAY_2B:
      case OP_REPEAT_2B:
        if (idx + 1 >= binary.length()) { op = OP_END; break; }
        arg1 = binary[idx] | ((uint16_t)binary[idx + 1] << 8);
        idx += 2;
        break;
      case OP_CHORD:
        if (idx + 1 >= binary.length()) { op = OP_END; break; }
        arg1 = binary[idx++];
        arg2 = binary[idx++];
        break;
      default:
        if (useSerial) {
          Serial.print("RUNTIME: unknown opcode 0x");
          Serial.println(op, HEX);
        }
        Keyboard.releaseAll();
        return;
    }

    if (op == OP_END) break;

    if (len > 0) {
      if (idx + len > binary.length()) { op = OP_END; break; }
      text = binary.substring(idx, idx + len);
      idx += len;
    }

    if (debugMode && useSerial) {
      Serial.print("DBG EXEC: op=0x");
      Serial.print(op, HEX);
      if (text.length() > 0) {
        Serial.print(" text=\"");
        Serial.print(text);
        Serial.print("\"");
      }
      if (arg1 != 0 || op == OP_DELAY_1B || op == OP_DELAY_2B || op == OP_DEFAULT_DELAY_1B || op == OP_DEFAULT_DELAY_2B || op == OP_REPEAT_1B || op == OP_REPEAT_2B || op == OP_KEY || op == OP_CHORD || op == OP_MOD_ALONE) {
        Serial.print(" arg1=");
        Serial.print(arg1);
      }
      if (op == OP_CHORD) {
        Serial.print(" arg2=");
        Serial.print(arg2);
      }
      Serial.println();
    }

    String reason;
    if (op == OP_REPEAT_1B || op == OP_REPEAT_2B) {
      if (lastOp == 0) {
        if (useSerial) {
          Serial.println("RUNTIME: REPEAT with no previous command");
        }
        break;
      }
      for (uint16_t i = 0; i < arg1; i++) {
        if (!executeOpcode(lastOp, lastText, lastArg1, lastArg2, reason, true)) {
          if (useSerial) {
            Serial.print("RUNTIME: ");
            Serial.println(reason);
          }
          break;
        }
      }
    } else {
      if (!executeOpcode(op, text, arg1, arg2, reason, true)) {
        if (useSerial) {
          Serial.print("RUNTIME: ");
          Serial.println(reason);
        }
        break;
      }

      // Remember last executable opcode for REPEAT
      if (op != OP_REM_SHORT && op != OP_REM_LONG) {
        lastOp = op;
        lastText = text;
        lastArg1 = arg1;
        lastArg2 = arg2;
      }
    }
  }

  // Release any held keys
  Keyboard.releaseAll();
}

bool executeOpcode(uint8_t op, const String &text, uint16_t arg1, uint16_t arg2, String &reason, bool actuallyRun) {
  reason = "";

  switch (op) {
    case OP_STRING_SHORT:
    case OP_STRING_LONG:
      if (actuallyRun) {
        Keyboard.print(text);
        if (defaultDelay > 0) delay(defaultDelay);
      }
      return true;

    case OP_STRINGLN_SHORT:
    case OP_STRINGLN_LONG:
      if (actuallyRun) {
        Keyboard.print(text);
        Keyboard.write(KEY_RETURN);
        if (defaultDelay > 0) delay(defaultDelay);
      }
      return true;

    case OP_DELAY_1B:
    case OP_DELAY_2B:
      if (actuallyRun) delay((unsigned long)arg1);
      return true;

    case OP_DEFAULT_DELAY_1B:
    case OP_DEFAULT_DELAY_2B:
      if (actuallyRun) defaultDelay = arg1;
      return true;

    case OP_REPEAT_1B:
    case OP_REPEAT_2B: {
      // REPEAT re-executes the last executable opcode.
      // We do not have the previous opcode stored here; runDuckyBinary handles it
      // by keeping lastOp/lastText/lastArg1/lastArg2 and calling executeOpcode in a loop.
      // This case should not be reached directly from runDuckyBinary.
      reason = "REPEAT without previous command";
      return false;
    }

    case OP_KEY: {
      uint8_t hid = hidCodeForInternalId((uint8_t)arg1);
      if (actuallyRun) {
        Keyboard.press(hid);
        Keyboard.releaseAll();
        if (defaultDelay > 0) delay(defaultDelay);
      }
      return true;
    }

    case OP_CHORD: {
      uint8_t modMask = (uint8_t)arg1;
      uint8_t keyId = (uint8_t)arg2;
      uint8_t keyHid = hidCodeForInternalId(keyId);
      if (actuallyRun) {
        if (modMask & MOD_CTRL) Keyboard.press(KEY_LEFT_CTRL);
        if (modMask & MOD_SHIFT) Keyboard.press(KEY_LEFT_SHIFT);
        if (modMask & MOD_ALT) Keyboard.press(KEY_LEFT_ALT);
        if (modMask & MOD_GUI) Keyboard.press(KEY_LEFT_GUI);
        Keyboard.press(keyHid);
        Keyboard.releaseAll();
        if (defaultDelay > 0) delay(defaultDelay);
      }
      return true;
    }

    case OP_MOD_ALONE: {
      uint8_t modMask = (uint8_t)arg1;
      if (actuallyRun) {
        if (modMask & MOD_CTRL) Keyboard.press(KEY_LEFT_CTRL);
        if (modMask & MOD_SHIFT) Keyboard.press(KEY_LEFT_SHIFT);
        if (modMask & MOD_ALT) Keyboard.press(KEY_LEFT_ALT);
        if (modMask & MOD_GUI) Keyboard.press(KEY_LEFT_GUI);
        Keyboard.releaseAll();
        if (defaultDelay > 0) delay(defaultDelay);
      }
      return true;
    }

    case OP_REM_SHORT:
    case OP_REM_LONG:
      if (debugMode && useSerial) {
        Serial.print("DBG REM: ");
        Serial.println(text);
      }
      return true;

    default:
      reason = "unknown opcode";
      return false;
  }
}

// ==================== DuckyScript Text Validator (used before compile) ====================
void runDuckyScript(const String &script) {
  // Kept for compatibility; new code uses runDuckyBinary.
  // If called with raw text, compile and run.
  uint16_t badLine = 0;
  String reason;
  String binary = compileDuckyScript(script, badLine, reason);
  if (binary.length() > 0) {
    runDuckyBinary(binary);
  } else if (useSerial) {
    Serial.print("RUNTIME: compile failed: ");
    Serial.println(reason);
  }
}

bool validateDuckyScript(const String &script, uint16_t &badLine, String &reason) {
  String prev = lastDuckyLine;
  uint16_t prevDelay = defaultDelay;
  lastDuckyLine = "";
  defaultDelay = 0;

  int lineNo = 1;
  int start = 0;
  while (start <= script.length()) {
    int end = script.indexOf('\n', start);
    if (end < 0) end = script.length();
    String line = script.substring(start, end);
    line.trim();

    if (line.length() > 0 && !line.startsWith("REM")) {
      if (!dispatchDuckyLine(line, reason, false)) {
        badLine = lineNo;
        lastDuckyLine = prev;
        defaultDelay = prevDelay;
        return false;
      }
    }

    if (end >= script.length()) break;
    start = end + 1;
    lineNo++;
  }

  lastDuckyLine = prev;
  defaultDelay = prevDelay;
  reason = "";
  return true;
}

bool dispatchDuckyLine(const String &line, String &reason, bool actuallyRun) {
  if (line.length() == 0) return true;
  if (line.startsWith("REM")) return true;

  String upper = line;
  upper.toUpperCase();

  // STRING / STRINGLN
  if (upper.startsWith("STRING ") || upper == "STRING" ||
      upper.startsWith("STRINGLN ") || upper == "STRINGLN") {
    bool ln = upper.startsWith("STRINGLN");
    int idx = ln ? 8 : 6;
    String text = "";
    if (line.length() > idx && line[idx] == ' ') {
      text = line.substring(idx + 1);
    }
    if (actuallyRun) {
      Keyboard.print(text);
      if (ln) Keyboard.write(KEY_RETURN);
      if (defaultDelay > 0) delay(defaultDelay);
    }
    lastDuckyLine = line;
    return true;
  }

  // DELAY / SLEEP
  if (upper.startsWith("DELAY ") || upper.startsWith("SLEEP ")) {
    int sp = line.indexOf(' ');
    String num = line.substring(sp + 1);
    num.trim();
    long ms = num.toInt();
    if (ms < 0 || ms > 60000) {
      reason = "invalid delay value";
      return false;
    }
    if (actuallyRun) delay((unsigned long)ms);
    lastDuckyLine = line;
    return true;
  }

  // DEFAULT_DELAY / DEFAULTDELAY
  if (upper.startsWith("DEFAULT_DELAY ") || upper.startsWith("DEFAULTDELAY ")) {
    int sp = line.indexOf(' ');
    String num = line.substring(sp + 1);
    num.trim();
    long ms = num.toInt();
    if (ms < 0 || ms > 60000) {
      reason = "invalid default delay";
      return false;
    }
    if (actuallyRun) defaultDelay = (uint16_t)ms;
    lastDuckyLine = line;
    return true;
  }

  // REPEAT
  if (upper.startsWith("REPEAT ")) {
    int sp = line.indexOf(' ');
    String num = line.substring(sp + 1);
    num.trim();
    long n = num.toInt();
    if (n < 0 || n > 999) {
      reason = "invalid repeat count";
      return false;
    }
    if (lastDuckyLine.length() == 0) {
      reason = "REPEAT with no previous command";
      return false;
    }
    if (actuallyRun) {
      String saved = lastDuckyLine;
      for (long i = 0; i < n; i++) {
        String r;
        if (!dispatchDuckyLine(saved, r, true)) {
          reason = "repeat failed: " + r;
          return false;
        }
      }
      lastDuckyLine = saved;
    }
    return true;
  }

  // Tokenize for chords / single keys
  int tokCount = 0;
  String tokens[8];
  String tmp = line;
  while (tmp.length() > 0 && tokCount < 8) {
    int sp = tmp.indexOf(' ');
    if (sp < 0) {
      tokens[tokCount++] = tmp;
      break;
    }
    tokens[tokCount++] = tmp.substring(0, sp);
    tmp = tmp.substring(sp + 1);
    tmp.trim();
  }

  if (tokCount == 0) return true;

  // First token may be a hyphenated modifier list (CTRL-ALT, etc.)
  uint8_t modifiers[4];
  uint8_t modCount = 0;
  String keyTok = "";

  String first = tokens[0];
  first.toUpperCase();
  if (first.indexOf('-') >= 0) {
    // Hyphenated modifier(s) + key
    int lastDash = first.lastIndexOf('-');
    String modPart = first.substring(0, lastDash);
    keyTok = first.substring(lastDash + 1);
    keyTok.toUpperCase();
    // modPart may contain multiple modifiers separated by '-'
    int pos = 0;
    while (pos <= modPart.length()) {
      int dash = modPart.indexOf('-', pos);
      String m = (dash < 0) ? modPart.substring(pos) : modPart.substring(pos, dash);
      m.toUpperCase();
      uint8_t mk;
      if (parseModifier(m, mk)) {
        if (modCount < 4) modifiers[modCount++] = mk;
      } else {
        reason = "unknown modifier " + m;
        return false;
      }
      if (dash < 0) break;
      pos = dash + 1;
    }
    // If the part after the last dash is itself a modifier (e.g., "CTRL-ALT DEL"),
    // treat it as a modifier and consume the next token as the key.
    if (isModifierToken(keyTok) && tokCount > 1) {
      uint8_t mk;
      if (parseModifier(keyTok, mk) && modCount < 4) modifiers[modCount++] = mk;
      keyTok = tokens[1];
      keyTok.toUpperCase();
    }
  } else if (isModifierToken(first)) {
    // Space-separated modifier(s) + key
    for (int i = 0; i < tokCount; i++) {
      String t = tokens[i];
      t.toUpperCase();
      uint8_t mk;
      if (parseModifier(t, mk)) {
        if (modCount < 4) modifiers[modCount++] = mk;
      } else {
        keyTok = t;
        break;
      }
    }
    // Single modifier alone (e.g., "GUI") presses that modifier key.
    if (keyTok.length() == 0 && modCount == 1) {
      uint8_t singleKey = modifiers[0];
      if (actuallyRun) {
        Keyboard.press(singleKey);
        Keyboard.releaseAll();
        if (defaultDelay > 0) delay(defaultDelay);
      }
      lastDuckyLine = line;
      return true;
    }
  } else {
    keyTok = first;
  }

  if (keyTok.length() == 0) {
    reason = "missing key";
    return false;
  }

  bool found;
  uint8_t keyCode = keyCodeForName(keyTok, found);
  if (!found) {
    reason = "unknown key " + keyTok;
    return false;
  }

  if (actuallyRun) {
    pressChord(modifiers, modCount, keyCode);
    if (defaultDelay > 0) delay(defaultDelay);
  }

  lastDuckyLine = line;
  return true;
}

// ==================== Binary Compiler / Decompiler ====================

uint8_t internalIdForName(const String &name, bool &found) {
  String n = name;
  n.toUpperCase();
  found = false;

  // Single ASCII printable character
  if (n.length() == 1) {
    char c = n[0];
    if (c >= 32 && c <= 126) {
      found = true;
      return (uint8_t)c;
    }
  }

  size_t count = sizeof(KEY_MAP) / sizeof(KEY_MAP[0]);
  for (size_t i = 0; i < count; i++) {
    if (n == KEY_MAP[i].name) {
      found = true;
      return KEY_MAP[i].internalId;
    }
  }
  return 0;
}

const char* nameForInternalId(uint8_t id) {
  if (id >= 32 && id <= 126) {
    // Return a static buffer for single ASCII char
    static char buf[2];
    buf[0] = (char)id;
    buf[1] = '\0';
    return buf;
  }
  size_t count = sizeof(KEY_MAP) / sizeof(KEY_MAP[0]);
  for (size_t i = 0; i < count; i++) {
    if (KEY_MAP[i].internalId == id) {
      return KEY_MAP[i].name;
    }
  }
  return "?";
}

uint8_t hidCodeForInternalId(uint8_t id) {
  if (id >= 32 && id <= 126) {
    return id; // ASCII char passed directly to Keyboard.press
  }
  size_t count = sizeof(KEY_MAP) / sizeof(KEY_MAP[0]);
  for (size_t i = 0; i < count; i++) {
    if (KEY_MAP[i].internalId == id) {
      return KEY_MAP[i].hidCode;
    }
  }
  return 0;
}

static void appendByte(String &out, uint8_t b) {
  out += (char)b;
}

static void appendTextOpcode(String &out, uint8_t shortOp, uint8_t longOp, const String &text) {
  uint16_t len = text.length();
  if (len <= 255) {
    appendByte(out, shortOp);
    appendByte(out, (uint8_t)len);
  } else {
    appendByte(out, longOp);
    appendByte(out, (uint8_t)(len & 0xFF));
    appendByte(out, (uint8_t)((len >> 8) & 0xFF));
  }
  for (uint16_t i = 0; i < len; i++) {
    appendByte(out, (uint8_t)text[i]);
  }
}

static bool tokenizeLine(const String &line, String tokens[], int &tokCount, int maxTokens) {
  tokCount = 0;
  String tmp = line;
  while (tmp.length() > 0 && tokCount < maxTokens) {
    int sp = tmp.indexOf(' ');
    if (sp < 0) {
      tokens[tokCount++] = tmp;
      break;
    }
    tokens[tokCount++] = tmp.substring(0, sp);
    tmp = tmp.substring(sp + 1);
    tmp.trim();
  }
  return true;
}

static bool parseChord(const String &line, String &keyTok, uint8_t &modMask, String &reason) {
  String tokens[8];
  int tokCount = 0;
  tokenizeLine(line, tokens, tokCount, 8);
  if (tokCount == 0) {
    reason = "missing key";
    return false;
  }

  modMask = 0;
  keyTok = "";

  String first = tokens[0];
  first.toUpperCase();
  if (first.indexOf('-') >= 0) {
    int lastDash = first.lastIndexOf('-');
    String modPart = first.substring(0, lastDash);
    keyTok = first.substring(lastDash + 1);
    keyTok.toUpperCase();
    int pos = 0;
    while (pos <= modPart.length()) {
      int dash = modPart.indexOf('-', pos);
      String m = (dash < 0) ? modPart.substring(pos) : modPart.substring(pos, dash);
      m.toUpperCase();
      if (m == "CTRL" || m == "CONTROL") modMask |= MOD_CTRL;
      else if (m == "SHIFT") modMask |= MOD_SHIFT;
      else if (m == "ALT") modMask |= MOD_ALT;
      else if (m == "GUI" || m == "WINDOWS" || m == "COMMAND") modMask |= MOD_GUI;
      else {
        reason = "unknown modifier " + m;
        return false;
      }
      if (dash < 0) break;
      pos = dash + 1;
    }
    if (isModifierToken(keyTok) && tokCount > 1) {
      String mt = keyTok;
      mt.toUpperCase();
      if (mt == "CTRL" || mt == "CONTROL") modMask |= MOD_CTRL;
      else if (mt == "SHIFT") modMask |= MOD_SHIFT;
      else if (mt == "ALT") modMask |= MOD_ALT;
      else if (mt == "GUI" || mt == "WINDOWS" || mt == "COMMAND") modMask |= MOD_GUI;
      keyTok = tokens[1];
      keyTok.toUpperCase();
    }
  } else if (isModifierToken(first)) {
    for (int i = 0; i < tokCount; i++) {
      String t = tokens[i];
      t.toUpperCase();
      if (t == "CTRL" || t == "CONTROL") modMask |= MOD_CTRL;
      else if (t == "SHIFT") modMask |= MOD_SHIFT;
      else if (t == "ALT") modMask |= MOD_ALT;
      else if (t == "GUI" || t == "WINDOWS" || t == "COMMAND") modMask |= MOD_GUI;
      else {
        keyTok = t;
        break;
      }
    }
    if (keyTok.length() == 0 && modMask != 0) {
      // modifier alone - caller decides
      return true;
    }
  } else {
    keyTok = first;
  }

  if (keyTok.length() == 0) {
    reason = "missing key";
    return false;
  }
  return true;
}

bool compileDuckyLine(const String &line, String &out, String &reason, uint8_t &lastOp) {
  if (line.length() == 0) return true;

  String upper = line;
  upper.toUpperCase();

  if (upper.startsWith("REM")) {
    String text = "";
    if (line.length() > 3 && line[3] == ' ') text = line.substring(4);
    appendTextOpcode(out, OP_REM_SHORT, OP_REM_LONG, text);
    return true;
  }

  // STRING / STRINGLN
  if (upper.startsWith("STRING ") || upper == "STRING" ||
      upper.startsWith("STRINGLN ") || upper == "STRINGLN") {
    bool ln = upper.startsWith("STRINGLN");
    int idx = ln ? 8 : 6;
    String text = "";
    if (line.length() > idx && line[idx] == ' ') {
      text = line.substring(idx + 1);
    }
    appendTextOpcode(out, ln ? OP_STRINGLN_SHORT : OP_STRING_SHORT,
                     ln ? OP_STRINGLN_LONG : OP_STRING_LONG, text);
    lastOp = ln ? OP_STRINGLN_SHORT : OP_STRING_SHORT;
    return true;
  }

  // DELAY / SLEEP
  if (upper.startsWith("DELAY ") || upper.startsWith("SLEEP ")) {
    int sp = line.indexOf(' ');
    String num = line.substring(sp + 1);
    num.trim();
    long ms = num.toInt();
    if (ms < 0 || ms > 60000) {
      reason = "invalid delay value";
      return false;
    }
    if (ms <= 255) {
      appendByte(out, OP_DELAY_1B);
      appendByte(out, (uint8_t)ms);
    } else {
      appendByte(out, OP_DELAY_2B);
      appendByte(out, (uint8_t)(ms & 0xFF));
      appendByte(out, (uint8_t)((ms >> 8) & 0xFF));
    }
    lastOp = OP_DELAY_1B;
    return true;
  }

  // DEFAULT_DELAY / DEFAULTDELAY
  if (upper.startsWith("DEFAULT_DELAY ") || upper.startsWith("DEFAULTDELAY ")) {
    int sp = line.indexOf(' ');
    String num = line.substring(sp + 1);
    num.trim();
    long ms = num.toInt();
    if (ms < 0 || ms > 60000) {
      reason = "invalid default delay";
      return false;
    }
    if (ms <= 255) {
      appendByte(out, OP_DEFAULT_DELAY_1B);
      appendByte(out, (uint8_t)ms);
    } else {
      appendByte(out, OP_DEFAULT_DELAY_2B);
      appendByte(out, (uint8_t)(ms & 0xFF));
      appendByte(out, (uint8_t)((ms >> 8) & 0xFF));
    }
    lastOp = OP_DEFAULT_DELAY_1B;
    return true;
  }

  // REPEAT
  if (upper.startsWith("REPEAT ")) {
    int sp = line.indexOf(' ');
    String num = line.substring(sp + 1);
    num.trim();
    long n = num.toInt();
    if (n < 0 || n > 65535) {
      reason = "invalid repeat count";
      return false;
    }
    if (lastOp == 0) {
      reason = "REPEAT with no previous command";
      return false;
    }
    if (n <= 255) {
      appendByte(out, OP_REPEAT_1B);
      appendByte(out, (uint8_t)n);
    } else {
      appendByte(out, OP_REPEAT_2B);
      appendByte(out, (uint8_t)(n & 0xFF));
      appendByte(out, (uint8_t)((n >> 8) & 0xFF));
    }
    return true;
  }

  // Chord or single key
  String keyTok;
  uint8_t modMask = 0;
  if (!parseChord(line, keyTok, modMask, reason)) {
    return false;
  }

  // Modifier alone
  if (keyTok.length() == 0) {
    appendByte(out, OP_MOD_ALONE);
    appendByte(out, modMask);
    lastOp = OP_MOD_ALONE;
    return true;
  }

  bool found;
  uint8_t keyId = internalIdForName(keyTok, found);
  if (!found) {
    reason = "unknown key " + keyTok;
    return false;
  }

  if (modMask == 0) {
    appendByte(out, OP_KEY);
    appendByte(out, keyId);
    lastOp = OP_KEY;
  } else {
    appendByte(out, OP_CHORD);
    appendByte(out, modMask);
    appendByte(out, keyId);
    lastOp = OP_CHORD;
  }
  return true;
}

String compileDuckyScript(const String &script, uint16_t &badLine, String &reason) {
  String binary = "";
  binary.reserve(1024);
  uint8_t lastOp = 0;

  int lineNo = 1;
  int start = 0;
  while (start <= script.length()) {
    int end = script.indexOf('\n', start);
    if (end < 0) end = script.length();
    String line = script.substring(start, end);
    line.trim();

    if (line.length() > 0) {
      if (!compileDuckyLine(line, binary, reason, lastOp)) {
        badLine = lineNo;
        return "";
      }
    }

    if (end >= script.length()) break;
    start = end + 1;
    lineNo++;
  }

  appendByte(binary, OP_END);
  return binary;
}

String decompileDuckyScript(const String &binary) {
  String out = "";
  size_t idx = 0;

  while (idx < binary.length()) {
    uint8_t op = binary[idx++];
    if (op == OP_END) break;

    uint16_t len = 0;
    String text = "";
    uint16_t arg1 = 0, arg2 = 0;

    switch (op) {
      case OP_STRING_SHORT:
      case OP_STRINGLN_SHORT:
      case OP_REM_SHORT:
        if (idx >= binary.length()) { op = OP_END; break; }
        len = binary[idx++];
        break;
      case OP_STRING_LONG:
      case OP_STRINGLN_LONG:
      case OP_REM_LONG:
        if (idx + 1 >= binary.length()) { op = OP_END; break; }
        len = binary[idx] | ((uint16_t)binary[idx + 1] << 8);
        idx += 2;
        break;
      case OP_DELAY_1B:
      case OP_DEFAULT_DELAY_1B:
      case OP_REPEAT_1B:
      case OP_KEY:
      case OP_MOD_ALONE:
        if (idx >= binary.length()) { op = OP_END; break; }
        arg1 = binary[idx++];
        break;
      case OP_DELAY_2B:
      case OP_DEFAULT_DELAY_2B:
      case OP_REPEAT_2B:
        if (idx + 1 >= binary.length()) { op = OP_END; break; }
        arg1 = binary[idx] | ((uint16_t)binary[idx + 1] << 8);
        idx += 2;
        break;
      case OP_CHORD:
        if (idx + 1 >= binary.length()) { op = OP_END; break; }
        arg1 = binary[idx++];
        arg2 = binary[idx++];
        break;
      default:
        out += "REM unknown opcode 0x";
        out += String(op, HEX);
        out += "\n";
        idx = binary.length();
        break;
    }

    if (op == OP_END) break;

    if (len > 0) {
      if (idx + len > binary.length()) { break; }
      text = binary.substring(idx, idx + len);
      idx += len;
    }

    switch (op) {
      case OP_STRING_SHORT:
      case OP_STRING_LONG:
        out += "STRING ";
        out += text;
        out += "\n";
        break;
      case OP_STRINGLN_SHORT:
      case OP_STRINGLN_LONG:
        out += "STRINGLN ";
        out += text;
        out += "\n";
        break;
      case OP_DELAY_1B:
      case OP_DELAY_2B:
        out += "DELAY ";
        out += String(arg1);
        out += "\n";
        break;
      case OP_DEFAULT_DELAY_1B:
      case OP_DEFAULT_DELAY_2B:
        out += "DEFAULT_DELAY ";
        out += String(arg1);
        out += "\n";
        break;
      case OP_REPEAT_1B:
      case OP_REPEAT_2B:
        out += "REPEAT ";
        out += String(arg1);
        out += "\n";
        break;
      case OP_KEY:
        out += nameForInternalId((uint8_t)arg1);
        out += "\n";
        break;
      case OP_CHORD: {
        uint8_t modMask = (uint8_t)arg1;
        if (modMask & MOD_CTRL) { out += "CTRL"; out += " "; }
        if (modMask & MOD_SHIFT) { out += "SHIFT"; out += " "; }
        if (modMask & MOD_ALT) { out += "ALT"; out += " "; }
        if (modMask & MOD_GUI) { out += "GUI"; out += " "; }
        out += nameForInternalId((uint8_t)arg2);
        out += "\n";
        break;
      }
      case OP_MOD_ALONE: {
        uint8_t modMask = (uint8_t)arg1;
        if (modMask & MOD_CTRL) out += "CTRL\n";
        if (modMask & MOD_SHIFT) out += "SHIFT\n";
        if (modMask & MOD_ALT) out += "ALT\n";
        if (modMask & MOD_GUI) out += "GUI\n";
        break;
      }
      case OP_REM_SHORT:
      case OP_REM_LONG:
        out += "REM ";
        out += text;
        out += "\n";
        break;
      default:
        break;
    }
  }

  return out;
}

// ==================== Key Name Resolution ====================
bool isModifierToken(const String &tok) {
  String t = tok;
  t.toUpperCase();
  return (t == "GUI" || t == "WINDOWS" || t == "CTRL" || t == "CONTROL" ||
          t == "ALT" || t == "SHIFT" || t == "COMMAND");
}

bool parseModifier(const String &tok, uint8_t &key) {
  String t = tok;
  t.toUpperCase();
  if (t == "GUI" || t == "WINDOWS" || t == "COMMAND") { key = KEY_LEFT_GUI; return true; }
  if (t == "CTRL" || t == "CONTROL") { key = KEY_LEFT_CTRL; return true; }
  if (t == "ALT") { key = KEY_LEFT_ALT; return true; }
  if (t == "SHIFT") { key = KEY_LEFT_SHIFT; return true; }
  return false;
}

uint8_t keyCodeForName(const String &name, bool &found) {
  String n = name;
  n.toUpperCase();
  found = true;

  if (n == "ENTER" || n == "RETURN") return KEY_RETURN;
  if (n == "TAB") return KEY_TAB;
  if (n == "SPACE") return (uint8_t)' ';
  if (n == "BACKSPACE") return KEY_BACKSPACE;
  if (n == "DELETE" || n == "DEL") return KEY_DELETE;
  if (n == "ESC" || n == "ESCAPE") return KEY_ESC;
  if (n == "INSERT") return KEY_INSERT;
  if (n == "HOME") return KEY_HOME;
  if (n == "END") return KEY_END;
  if (n == "PAGEUP" || n == "PGUP") return KEY_PAGE_UP;
  if (n == "PAGEDOWN" || n == "PGDN") return KEY_PAGE_DOWN;
  if (n == "UP" || n == "UPARROW") return KEY_UP_ARROW;
  if (n == "DOWN" || n == "DOWNARROW") return KEY_DOWN_ARROW;
  if (n == "LEFT" || n == "LEFTARROW") return KEY_LEFT_ARROW;
  if (n == "RIGHT" || n == "RIGHTARROW") return KEY_RIGHT_ARROW;
  if (n == "CAPSLOCK" || n == "CAPS_LOCK") return KEY_CAPS_LOCK;
  if (n == "NUMLOCK" || n == "NUM_LOCK") return KEY_NUM_LOCK;
  if (n == "SCROLLLOCK" || n == "SCROLLOCK" || n == "SCROLL_LOCK") return KEY_SCROLL_LOCK;
  if (n == "PRINTSCREEN" || n == "PRINT_SCREEN") return KEY_PRINT_SCREEN;
  if (n == "PAUSE") return KEY_PAUSE;
  if (n == "BREAK") return KEY_PAUSE;
  if (n == "APP" || n == "MENU") return KEY_RIGHT_GUI; // closest generic mapping

  if (n == "F1") return KEY_F1;
  if (n == "F2") return KEY_F2;
  if (n == "F3") return KEY_F3;
  if (n == "F4") return KEY_F4;
  if (n == "F5") return KEY_F5;
  if (n == "F6") return KEY_F6;
  if (n == "F7") return KEY_F7;
  if (n == "F8") return KEY_F8;
  if (n == "F9") return KEY_F9;
  if (n == "F10") return KEY_F10;
  if (n == "F11") return KEY_F11;
  if (n == "F12") return KEY_F12;

  // Single ASCII character
  if (n.length() == 1) {
    char c = n[0];
    if (c >= 32 && c <= 126) return (uint8_t)c;
  }

  found = false;
  return 0;
}

void pressChord(uint8_t modifiers[], uint8_t modCount, uint8_t key) {
  for (uint8_t i = 0; i < modCount; i++) {
    Keyboard.press(modifiers[i]);
  }
  Keyboard.press(key);
  Keyboard.releaseAll();
}

// ==================== Button Handling ====================
void cycleSlot() {
  // With only one active slot there is nothing to cycle to; warn visually
  if (slotCount <= 1) {
    flashNoMoreSlots();
    return;
  }

  currentSlot++;
  if (currentSlot > slotCount) currentSlot = 1;
  setSlotLED(currentSlot);
  writeConfig();

  if (useSerial) {
    Serial.print("Slot ");
    Serial.print(currentSlot);
    Serial.print(": ");
    uint8_t t = getSlotType(currentSlot);
    String content = readSlotContent(currentSlot);
    if (t == SLOT_TYPE_PASSWORD) {
      Serial.println(content.length() > 0 ? content : "(empty)");
    } else {
      Serial.print("[");
      Serial.print(slotTypeName(t));
      Serial.print("] ");
      Serial.println(content.length());
    }
  }
}

void handleButton() {
  if (BOOTSEL) {
    unsigned long pressStart = millis();

    while (BOOTSEL) {
      if (millis() - pressStart > LONG_PRESS_MS) {
        // Long press
        if (swapPress) {
          executeSlot(currentSlot);
        } else {
          cycleSlot();
        }
        while (BOOTSEL) delay(10);
        return;
      }
      delay(10);
    }

    unsigned long pressDuration = millis() - pressStart;
    if (pressDuration >= SHORT_PRESS_MS) {
      // Short press
      if (swapPress) {
        cycleSlot();
      } else {
        executeSlot(currentSlot);
      }
    }
  }
}
