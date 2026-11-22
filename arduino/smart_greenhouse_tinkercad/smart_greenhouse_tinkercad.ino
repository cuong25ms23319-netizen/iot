/*
 * SMART GREENHOUSE - BẢN MÔ PHỎNG TINKERCAD
 * Cùng logic, cùng chân, cùng màn hình LCD và cùng giao thức Serial với bản chạy mạch thật
 * (smart_greenhouse.ino). Khác biệt vì Tinkercad thiếu linh kiện:
 *
 *   Mạch thật                  Tinkercad
 *   -------------------------  ---------------------------------------------
 *   DHT11 (A3)                 TMP36 (A3): chỉ có nhiệt độ, độ ẩm KK cố định 60 %
 *   Water sensor (A1)          Biến trở thứ hai (A1): vặn để đổi mực nước
 *   Bluetooth HC-05 (D2, D3)   Bỏ: chỉ dùng Serial Monitor
 *   EEPROM lưu ngưỡng          Bỏ: ngưỡng về mặc định mỗi lần chạy lại
 *
 * ĐẤU NỐI TRONG TINKERCAD
 *   LCD 16x2 (I2C)  : loại PCF8574, địa chỉ 39 (0x27). SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
 *   TMP36           : mặt phẳng quay về phía bạn: chân trái -> 5V, chân giữa -> A3, chân phải -> GND
 *   Biến trở 1      : chân giữa -> A0 (độ ẩm đất), hai chân ngoài -> 5V và GND
 *   Biến trở 2      : chân giữa -> A1 (mực nước),  hai chân ngoài -> 5V và GND
 *   Quang trở       : 5V -> quang trở -> A2 -> điện trở 10k -> GND
 *   LED xanh (bơm)  : D6 -> 220 ohm -> LED -> GND
 *   LED vàng (đèn)  : D7 -> 220 ohm -> LED -> GND
 *   Piezo           : (+) -> D8, (-) -> GND
 *   Micro servo     : Signal -> D9, Power -> 5V, Ground -> GND
 *
 * LỆNH (gõ trong Serial Monitor, 9600 baud):
 *   MODE=AUTO | MODE=MANUAL
 *   PUMP=ON|OFF   VENT=OPEN|CLOSE   LAMP=ON|OFF        (đang AUTO thì tự chuyển sang MANUAL)
 *   TMAX=20..50   SOIL=5..90   LIGHT=5..90   BEEP   PING
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ----------------------------- CẤU HÌNH -----------------------------------
const uint8_t PIN_PUMP   = 6;
const uint8_t PIN_LAMP   = 7;
const uint8_t PIN_BUZZER = 8;
const uint8_t PIN_SERVO  = 9;
const uint8_t PIN_SOIL   = A0;
const uint8_t PIN_WATER  = A1;
const uint8_t PIN_LDR    = A2;
const uint8_t PIN_TMP36  = A3;

const uint8_t TANK_LOW_PCT   = 15;
const uint8_t HUM_MAX        = 85;
const uint8_t VENT_OPEN_DEG  = 90;
const uint8_t VENT_CLOSE_DEG = 0;
const uint16_t TELEMETRY_MS  = 1000;

// ----------------------------- BIẾN TOÀN CỤC ------------------------------
Servo ventServo;
LiquidCrystal_I2C *lcd = nullptr;
uint8_t lcdAddr = 0;

uint8_t tMax = 32, soilMin = 40, lightMin = 35;
bool autoMode = true;
bool pumpOn = false, ventOpen = false, lampOn = false;
bool tankLow = false, tooHot = false;

int16_t temperature = 25, humidity = 60;      // độ ẩm KK cố định vì Tinkercad không có DHT11
uint8_t lightPct = 0, soilPct = 0, waterPct = 100;

unsigned long tFast = 0, tLcd = 0, tTele = 0, tBeep = 0, msgUntil = 0, lastRx = 0;
char lcdMsg[17] = "";
char lcdShown[2][17] = { "", "" };

const uint8_t CMD_MAX = 24;
char cmdBuf[CMD_MAX];
uint8_t cmdLen = 0;

// ----------------------------- TIỆN ÍCH -----------------------------------
void sendEvent(const char *name, int value) {
  Serial.print(F("E,")); Serial.print(name); Serial.print(','); Serial.println(value);
}

void showMsg(const char *progmemText) {
  strncpy_P(lcdMsg, progmemText, 16);
  lcdMsg[16] = 0;
  msgUntil = millis() + 2500;
}

void beep(uint16_t freq, uint16_t ms) { tone(PIN_BUZZER, freq, ms); }

// ----------------------------- ĐẦU RA -------------------------------------
void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  digitalWrite(PIN_PUMP, on);
  sendEvent(on ? "PUMP_ON" : "PUMP_OFF", soilPct);
  showMsg(on ? PSTR("BOM: BAT") : PSTR("BOM: TAT"));
}

void setVent(bool open) {
  if (open == ventOpen) return;
  ventOpen = open;
  ventServo.write(open ? VENT_OPEN_DEG : VENT_CLOSE_DEG);
  sendEvent(open ? "VENT_OPEN" : "VENT_CLOSE", temperature);
  showMsg(open ? PSTR("CUA GIO: MO") : PSTR("CUA GIO: DONG"));
}

void setLamp(bool on) {
  if (on == lampOn) return;
  lampOn = on;
  digitalWrite(PIN_LAMP, on);
  sendEvent(on ? "LAMP_ON" : "LAMP_OFF", lightPct);
  showMsg(on ? PSTR("DEN: BAT") : PSTR("DEN: TAT"));
}

// ----------------------------- CẢM BIẾN -----------------------------------
uint8_t readPct(uint8_t pin) {
  return (uint8_t)((uint32_t)analogRead(pin) * 100 / 1023);
}

void readSensors() {
  unsigned long now = millis();
  if (now - tFast < 500) return;
  tFast = now;

  lightPct = readPct(PIN_LDR);
  soilPct  = readPct(PIN_SOIL);
  waterPct = readPct(PIN_WATER);

  // TMP36: 10 mV/°C, lệch 500 mV  ->  °C = (mV - 500) / 10
  long mv = (long)analogRead(PIN_TMP36) * 5000 / 1023;
  temperature = (int16_t)((mv - 500 + 5) / 10);
}

// ----------------------------- LOGIC ĐIỀU KHIỂN ---------------------------
void controlLogic() {
  unsigned long now = millis();

  if (!tankLow && waterPct < TANK_LOW_PCT) {
    tankLow = true;
    sendEvent("TANK_LOW", waterPct);
    showMsg(PSTR("!! BON NUOC CAN"));
  } else if (tankLow && waterPct > TANK_LOW_PCT + 5) {
    tankLow = false;
    sendEvent("TANK_OK", waterPct);
  }
  tooHot = temperature >= tMax + 5;

  if (tankLow && pumpOn) setPump(false);          // an toàn: bồn cạn thì luôn tắt bơm

  if (autoMode) {
    if (!ventOpen && (temperature >= tMax || humidity >= HUM_MAX)) setVent(true);
    else if (ventOpen && temperature <= tMax - 2 && humidity <= HUM_MAX - 5) setVent(false);

    if (!lampOn && lightPct < lightMin) setLamp(true);
    else if (lampOn && lightPct > lightMin + 10) setLamp(false);

    if (!pumpOn && soilPct < soilMin && !tankLow) setPump(true);
    else if (pumpOn && soilPct >= soilMin + 10) setPump(false);
  }

  uint16_t period = tooHot ? 800 : 4000;
  if ((tankLow || tooHot) && now - tBeep >= period) {
    tBeep = now;
    beep(tooHot ? 2600 : 1200, 200);
  }
}

// ----------------------------- LCD ----------------------------------------
uint8_t findLcd() {
  for (uint8_t a = 0x20; a <= 0x27; a++) {         // dải địa chỉ PCF8574 trong Tinkercad (32..39)
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0;
}

void lcdLine(uint8_t row, const char *text) {
  char line[17];
  snprintf_P(line, sizeof(line), PSTR("%-16s"), text);
  if (!strcmp(line, lcdShown[row])) return;       // chỉ ghi khi nội dung thay đổi
  strcpy(lcdShown[row], line);
  lcd->setCursor(0, row);
  lcd->print(line);
}

void updateLcd() {
  unsigned long now = millis();
  if (now - tLcd < 250) return;
  tLcd = now;

  char buf[20];
  snprintf_P(buf, sizeof(buf), PSTR("T%2d\xDF" "C H%2d%% %s"), temperature, humidity, autoMode ? "AUTO" : "MANU");
  lcdLine(0, buf);

  if (now < msgUntil) {
    lcdLine(1, lcdMsg);
  } else if (tankLow && (now / 1000) % 2 == 0) {
    strcpy_P(buf, PSTR("!! BON NUOC CAN"));
    lcdLine(1, buf);
  } else {
    switch ((now / 3000) % 3) {
      case 0:  snprintf_P(buf, sizeof(buf), PSTR("Dat:%3d%% Nc:%3d%%"), soilPct, waterPct); break;
      case 1:  snprintf_P(buf, sizeof(buf), PSTR("Sang:%3d%% D:%s"), lightPct, lampOn ? "BAT" : "TAT"); break;
      default: snprintf_P(buf, sizeof(buf), PSTR("Bom:%s Gio:%s"), pumpOn ? "BAT" : "TAT", ventOpen ? "MO" : "DONG"); break;
    }
    lcdLine(1, buf);
  }
}

// ----------------------------- SERIAL -------------------------------------
void sendTelemetry() {
  unsigned long now = millis();
  if (now - tTele < TELEMETRY_MS) return;
  tTele = now;

  char b[72];
  uint8_t alarm = (tankLow ? 1 : 0) | (tooHot ? 2 : 0);
  snprintf_P(b, sizeof(b), PSTR("D,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u"),
             temperature, humidity, lightPct, soilPct, waterPct,
             pumpOn, ventOpen, lampOn, autoMode, alarm, tMax, soilMin, lightMin);
  Serial.println(b);
}

void handleCommand(char *cmd) {
  bool ok = true;
  char *eq = strchr(cmd, '=');
  const char *val = eq ? eq + 1 : "";
  if (eq) *eq = 0;
  int n = atoi(val);

  if (!strcmp(cmd, "PING")) {
  } else if (!strcmp(cmd, "BEEP")) {
    beep(2000, 300);
  } else if (!strcmp(cmd, "MODE")) {
    if (!strcmp(val, "AUTO")) autoMode = true;
    else if (!strcmp(val, "MANUAL")) autoMode = false;
    else ok = false;
  } else if (!strcmp(cmd, "PUMP")) {
    // Lệnh bật/tắt tay được nhận ở mọi chế độ: nếu đang AUTO thì tự chuyển sang MANUAL
    if (!strcmp(val, "ON")) { if (tankLow) ok = false; else { autoMode = false; setPump(true); } }
    else if (!strcmp(val, "OFF")) { autoMode = false; setPump(false); }
    else ok = false;
  } else if (!strcmp(cmd, "VENT")) {
    if (!strcmp(val, "OPEN")) { autoMode = false; setVent(true); }
    else if (!strcmp(val, "CLOSE")) { autoMode = false; setVent(false); }
    else ok = false;
  } else if (!strcmp(cmd, "LAMP")) {
    if (!strcmp(val, "ON")) { autoMode = false; setLamp(true); }
    else if (!strcmp(val, "OFF")) { autoMode = false; setLamp(false); }
    else ok = false;
  } else if (!strcmp(cmd, "TMAX")) {
    if (n >= 20 && n <= 50) tMax = n; else ok = false;
  } else if (!strcmp(cmd, "SOIL")) {
    if (n >= 5 && n <= 90) soilMin = n; else ok = false;
  } else if (!strcmp(cmd, "LIGHT")) {
    if (n >= 5 && n <= 90) lightMin = n; else ok = false;
  } else {
    ok = false;
  }

  if (eq) *eq = '=';
  Serial.print(F("A,")); Serial.print(cmd); Serial.println(ok ? F(",OK") : F(",ERR"));
}

// Serial Monitor của Tinkercad không gửi ký tự xuống dòng, nên lệnh được coi là
// kết thúc khi gặp '\n' HOẶC khi không còn ký tự mới trong 100 ms.
void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    lastRx = millis();
    if (c == '\r' || c == '\n') {
      if (cmdLen) { cmdBuf[cmdLen] = 0; handleCommand(cmdBuf); cmdLen = 0; }
    } else if (c != ' ' && cmdLen < CMD_MAX - 1) {
      cmdBuf[cmdLen++] = toupper(c);
    }
  }
  if (cmdLen && millis() - lastRx > 100) {
    cmdBuf[cmdLen] = 0;
    handleCommand(cmdBuf);
    cmdLen = 0;
  }
}

// ----------------------------- SETUP / LOOP -------------------------------
void setup() {
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_LAMP, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  Serial.begin(9600);

  Wire.begin();
  lcdAddr = findLcd();
  if (!lcdAddr) lcdAddr = 0x27;                 // không dò được thì dùng địa chỉ mặc định 39
  lcd = new LiquidCrystal_I2C(lcdAddr, 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->print(F("SMART GREENHOUSE"));
  lcd->setCursor(0, 1);
  lcd->print(F(" Tinkercad demo "));

  ventServo.attach(PIN_SERVO);
  ventServo.write(VENT_CLOSE_DEG);
  beep(1500, 150);
  delay(1000);

  sendEvent("BOOT", 0);
  sendEvent("LCD", lcdAddr);
}

void loop() {
  pollSerial();
  readSensors();
  controlLogic();
  updateLcd();
  sendTelemetry();
}
