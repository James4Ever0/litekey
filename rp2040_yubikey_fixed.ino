/*
 * RP2040 Zero 双模式密码键盘
 * - 短按 BOOT 键：输出 shortOutput (可自定义，默认为回车键)
 * - 长按 BOOT 键：输出 longOutput (可自定义，默认为 "DefaultPass")
 * - 可选：输入字符串后自动追加回车 (由 EEPROM 标志控制，默认开启)
 * - 串口命令：
 *     SETLONG:新内容   -> 设置长按输出 (持久化)
 *     SETSHORT:新内容  -> 设置短按输出 (持久化)
 *     SETENTER:1或0    -> 设置输入后是否自动追加回车 (持久化, 1=追加 0=不追加)
 *     发送空内容 (如 SETSHORT:) 可清空，此时短按恢复为回车
 * - 所有设置保存在 Flash (EEPROM) 中
 */

#include <Keyboard.h>
#include <EEPROM.h>

// ==================== 常量 ====================
// 注意：BOOT 键（BOOTSEL）接在 QSPI Flash 片选线上，不在普通 GPIO 上，
// 不能用 digitalRead() 读取。Philhower 内核提供全局对象 BOOTSEL
// （不是函数，无括号，按住时为 true，直接用 if (BOOTSEL) 判断）。无需 pinMode()。
#define EEPROM_SIZE      512
#define LONG_ADDR        0     // 长按字符串起始地址
#define SHORT_ADDR       128   // 短按字符串起始地址
#define ENTER_FLAG_ADDR  256   // “自动回车”标志地址（1 字节：1=追加, 0=不追加）
#define MAX_STR_LEN      128   // 每个字符串最大长度（字节）

#define SHORT_PRESS_MS 50
#define LONG_PRESS_MS  1000
#define SERIAL_TIMEOUT 1500 // Continue even if serial is not ready.

// ==================== 默认值 ====================
const String DEFAULT_LONG  = "DefaultPass";  // 长按默认密码
const String DEFAULT_SHORT = "";             // 短按默认为空（代表回车）
const bool   DEFAULT_APPEND_ENTER = true;    // 默认：输入后自动追加回车

// ==================== 全局变量 ====================
String longOutput;   // 长按时输出的字符串
String shortOutput;  // 短按时输出的字符串
bool   appendEnter;  // 输入字符串后是否自动追加回车（持久化到 EEPROM）

bool useSerial;

// ==================== EEPROM 读写通用函数 ====================

// 从指定地址读取字符串（最大 maxLen 字节）
String readStringFromEEPROM(int addr, int maxLen) {
  String str = "";
  for (int i = 0; i < maxLen; i++) {
    char c = EEPROM.read(addr + i);
    if (c == '\0') break;
    str += c;
  }
  return str;
}

// 将字符串写入指定地址（自动清空该区域）
void writeStringToEEPROM(int addr, int maxLen, String str) {
  // 先清空该区域
  for (int i = 0; i < maxLen; i++) {
    EEPROM.write(addr + i, 0);
  }
  // 写入新字符串（截断超长部分）
  int len = str.length();
  if (len > maxLen - 1) len = maxLen - 1;
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + i, str[i]);
  }
  EEPROM.commit(); // 持久化
}

// 写入“自动回车”标志（1 字节）并持久化
void writeEnterFlagToEEPROM(bool val) {
  EEPROM.write(ENTER_FLAG_ADDR, val ? 1 : 0);
  EEPROM.commit(); // 持久化
}

// ==================== setup ====================
void setup() {
  // BOOT 键用 BOOTSEL 读取，无需 pinMode()
  Keyboard.begin();
  EEPROM.begin(EEPROM_SIZE);

  // 读取长按设置，若为空则使用默认值
  longOutput = readStringFromEEPROM(LONG_ADDR, MAX_STR_LEN);
  if (longOutput.length() == 0) {
    longOutput = DEFAULT_LONG;
  }

  // 读取短按设置，若为空则使用默认值（空字符串）
  shortOutput = readStringFromEEPROM(SHORT_ADDR, MAX_STR_LEN);
  // 短按默认为空，表示发送回车，不需要额外赋值

  // 读取“自动回车”标志；非法值（如全新 Flash 的 0xFF）回退到默认
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
    Serial.println("  SETLONG:内容   -> set long-press output");
    Serial.println("  SETSHORT:内容  -> set short-press output");
    Serial.println("  SETENTER:1或0  -> set auto-append-enter (1=on, 0=off)");
    Serial.println("  (send empty to clear, e.g. SETSHORT:)");
  } else{
    Serial.end();
    useSerial=false;
  }
}

// ==================== loop ====================
void loop() {
  // ---------- 串口命令处理 ----------
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
      shortOutput = newVal; // 可以为空
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
      appendEnter = (newVal == "1"); // 只有 "1" 开启，其余一律关闭
      writeEnterFlagToEEPROM(appendEnter);
      Serial.print("Auto append Enter updated to: ");
      Serial.println(appendEnter ? "ON (1)" : "OFF (0)");
    }
    else {
      Serial.println("Unknown command. Use SETLONG:, SETSHORT: or SETENTER:");
    }
  }

  // ---------- 按钮检测 ----------
  // BOOTSEL 按住时返回 true（逻辑与普通上拉按键相反，无需再判断 LOW）
  if (BOOTSEL) {
    unsigned long pressStart = millis();

    while (BOOTSEL) {
      if (millis() - pressStart > LONG_PRESS_MS) {
        // ---------- 长按事件 ----------
        Keyboard.print(longOutput);
        if (appendEnter) Keyboard.write(KEY_RETURN); // 自动追加回车
        // 等待释放
        while (BOOTSEL) delay(10);
        return;
      }
      delay(10);
    }

    // 如果按钮在长按阈值前释放，则为短按
    unsigned long pressDuration = millis() - pressStart;
    if (pressDuration >= SHORT_PRESS_MS) {
      // ---------- 短按事件 ----------
      if (shortOutput.length() == 0) {
        // 空字符串 -> 发送回车键（本身就是回车，不再追加）
        Keyboard.write(KEY_RETURN);
      } else {
        Keyboard.print(shortOutput);
        if (appendEnter) Keyboard.write(KEY_RETURN); // 自动追加回车
      }
    }
  }

  delay(10);
}
