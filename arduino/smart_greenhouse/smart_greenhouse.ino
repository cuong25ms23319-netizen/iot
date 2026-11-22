/*
 * SMART GREENHOUSE - Tầng thiết bị (Arduino Uno)
 * Đồ án cuối kỳ IoT Programming - IAD591
 *
 * Cảm biến : DHT11 (nhiệt độ, độ ẩm không khí), LDR (ánh sáng),
 *            biến trở (giả lập độ ẩm đất), water sensor (mực nước bồn tưới)
 * Đầu ra   : LCD1602 I2C, Servo (cửa thông gió), LED vàng (đèn trồng cây),
 *            LED xanh cảnh báo (sáng cùng lúc còi kêu), Buzzer, LED bơm (tuỳ chọn)
 * Giao tiếp: Bluetooth HC-05/HC-06 (SoftwareSerial) + Serial USB (dự phòng).
 *            Mọi bản tin được gửi đồng thời trên CẢ HAI kênh,
 *            lệnh điều khiển được nhận từ CẢ HAI kênh.
 *
 * Thư viện cần cài (Library Manager):
 *   - "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"
 *   - "LiquidCrystal I2C" (Frank de Brabander)
 *   Servo, SoftwareSerial, EEPROM, Wire có sẵn trong Arduino IDE.
 *
 * GIAO THỨC (mỗi bản tin 1 dòng, kết thúc bằng '\n', 9600 baud)
 *   Arduino -> Pi
 *     D,temp,hum,light,soil,water,pump,vent,lamp,mode,alarm,tmax,soilMin,lightMin   (1 giây/lần)
 *        alarm: bit0 = bồn nước cạn, bit1 = quá nhiệt
 *     E,<PUMP_ON|PUMP_OFF|VENT_OPEN|VENT_CLOSE|LAMP_ON|LAMP_OFF|TANK_LOW|TANK_OK|BOOT>,<giá trị>
 *     E,LCD,<địa chỉ I2C dạng thập phân, 0 = không tìm thấy>     (1 lần khi khởi động)
 *     A,<lệnh>,<OK|ERR>
 *   Pi -> Arduino
 *     MODE=AUTO|MANUAL
 *     PUMP=ON|OFF   VENT=OPEN|CLOSE   LAMP=ON|OFF      (đang AUTO thì tự chuyển sang MANUAL)
 *     TMAX=n (20..50 °C)   SOIL=n (5..90 %)   LIGHT=n (5..90 %)
 *     WATER=n (3..60)  tưới ngay n giây rồi tự tắt, dùng được ở mọi chế độ, không đổi chế độ
 *     BEEP   PING
 *     RAW  -> trả về "R,<water>,<soil>,<ldr>" (giá trị analog thô, dùng để hiệu chỉnh)
 */

#include <SoftwareSerial.h>
#include <Servo.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ----------------------------- CẤU HÌNH -----------------------------------
#define DHT_TYPE DHT11          // đổi thành DHT22 nếu dùng DHT22

// Bật/tắt từng phần để lắp và kiểm tra mạch theo từng bước
#define USE_BLUETOOTH 1         // 0: chưa gắn HC-05 -> chỉ dùng Serial USB (không dùng D2, D3)
#define PUMP_ACTIVE_LOW 0       // 1: D4 nối module relay kích mức THẤP (đa số relay 5V) ; 0: LED hoặc relay kích mức cao
#define USE_SOIL_POT  1         // 0: chưa gắn biến trở độ ẩm đất ở A0 -> coi đất luôn 50 %, bơm chỉ bật tay

const uint8_t PIN_BT_RX = 2;    // <- TXD của HC-05
const uint8_t PIN_BT_TX = 3;    // -> RXD của HC-05 (QUA MẠCH HẠ ÁP 1k + 2k)
const uint8_t PIN_PUMP  = 4;    // LED giả lập bơm, HOẶC chân IN của relay điều khiển bơm thật (không gắn gì cũng được)
const uint8_t PIN_ALARM = 6;    // LED xanh dương: đèn cảnh báo, sáng cùng lúc với còi
const uint8_t PIN_LAMP  = 7;    // LED vàng: đèn trồng cây
const uint8_t PIN_BUZZER = 8;
const uint8_t PIN_SERVO = 9;    // cửa thông gió
const uint8_t PIN_SOIL  = A0;   // biến trở: giả lập cảm biến độ ẩm đất
const uint8_t PIN_WATER = A1;   // water sensor: chân S (VCC -> 5V, GND -> GND)
const uint8_t PIN_LDR   = A2;   // 5V -> LDR -> A2 -> 10k -> GND
const uint8_t PIN_DHT   = A3;
// LCD I2C: SDA = A4, SCL = A5

// Bồn nước: water sensor cắm thẳng đứng trong bồn, nước ngập càng cao giá trị analog càng lớn.
// HIỆU CHỈNH: mở Serial Monitor, gõ RAW khi cảm biến khô và khi ngập tới vạch đầy,
// rồi ghi hai giá trị "water" đọc được vào hai hằng số dưới đây.
const uint16_t WATER_RAW_EMPTY = 20;    // giá trị analog khi cảm biến khô  (0 %)
const uint16_t WATER_RAW_FULL  = 520;   // giá trị analog khi ngập tới vạch đầy (100 %)
const uint8_t TANK_LOW_PCT  = 15;   // dưới mức này: khoá bơm + cảnh báo
const uint8_t HUM_MAX       = 85;   // độ ẩm không khí quá cao -> mở cửa gió
const uint8_t VENT_OPEN_DEG = 90;
const uint8_t VENT_CLOSE_DEG = 0;
const uint16_t TELEMETRY_MS = 1000;

// ----------------------------- BIẾN TOÀN CỤC ------------------------------
#if USE_BLUETOOTH
SoftwareSerial bt(PIN_BT_RX, PIN_BT_TX);
#endif
Servo ventServo;
DHT dht(PIN_DHT, DHT_TYPE);
LiquidCrystal_I2C *lcd = nullptr;   // tạo trong setup() sau khi dò được địa chỉ I2C
uint8_t lcdAddr = 0;                // 0 = không tìm thấy LCD

// Ngưỡng điều khiển (chỉnh được từ dashboard, lưu EEPROM)
uint8_t tMax     = 32;   // °C : nóng hơn -> mở cửa thông gió
uint8_t soilMin  = 40;   // %  : đất khô hơn -> bật bơm
uint8_t lightMin = 35;   // %  : tối hơn -> bật đèn

bool autoMode = true;
bool pumpOn = false, ventOpen = false, lampOn = false;
bool tankLow = false, tooHot = false;
unsigned long waterUntil = 0;      // khác 0: đang tưới hẹn giờ (lệnh WATER=n), giá trị là thời điểm dừng

int16_t temperature = -1, humidity = -1;
uint8_t lightPct = 0, soilPct = 0, waterPct = 100;

unsigned long tFast = 0, tDht = 0, tLcd = 0, tTele = 0, tBeep = 0;
unsigned long servoDetachAt = 0, msgUntil = 0;
char lcdMsg[17] = "";
char lcdShown[2][17] = { "", "" };   // nội dung đang hiện trên từng dòng (để chỉ ghi khi thay đổi)

const uint8_t CMD_MAX = 24;
char bufUsb[CMD_MAX], bufBt[CMD_MAX];
uint8_t lenUsb = 0, lenBt = 0;

// ----------------------------- EEPROM -------------------------------------
void saveConfig() {
  EEPROM.update(0, 0x5A);
  EEPROM.update(1, tMax);
  EEPROM.update(2, soilMin);
  EEPROM.update(3, lightMin);
}

void loadConfig() {
  if (EEPROM.read(0) != 0x5A) { saveConfig(); return; }
  tMax = EEPROM.read(1);
  soilMin = EEPROM.read(2);
  lightMin = EEPROM.read(3);
  if (tMax < 20 || tMax > 50) tMax = 32;
  if (soilMin < 5 || soilMin > 90) soilMin = 40;
  if (lightMin < 5 || lightMin > 90) lightMin = 35;
}

// ----------------------------- TIỆN ÍCH -----------------------------------
void sendLine(const char *s) {        // gửi trên cả USB Serial và Bluetooth
  Serial.println(s);
#if USE_BLUETOOTH
  bt.println(s);
#endif
}

void sendEvent(const char *name, int value) {
  char b[32];
  snprintf(b, sizeof(b), "E,%s,%d", name, value);
  sendLine(b);
}

void showMsg(const char *progmemText) {   // hiện thông báo 2.5 s ở dòng 2 LCD
  strncpy_P(lcdMsg, progmemText, 16);
  lcdMsg[16] = 0;
  msgUntil = millis() + 2500;
}

void beep(uint16_t freq, uint16_t ms) { tone(PIN_BUZZER, freq, ms); }

// ----------------------------- ĐIỀU KHIỂN ĐẦU RA --------------------------
void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  digitalWrite(PIN_PUMP, PUMP_ACTIVE_LOW ? !on : on);
  sendEvent(on ? "PUMP_ON" : "PUMP_OFF", soilPct);
  showMsg(on ? PSTR("BOM: BAT") : PSTR("BOM: TAT"));
}

void setVent(bool open) {
  if (open == ventOpen) return;
  ventOpen = open;
  ventServo.attach(PIN_SERVO);
  ventServo.write(open ? VENT_OPEN_DEG : VENT_CLOSE_DEG);
  servoDetachAt = millis() + 700;     // nhả servo sau khi quay xong (tránh rung)
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

// ----------------------------- ĐỌC CẢM BIẾN -------------------------------
uint8_t readSoilPct() {
#if USE_SOIL_POT
  return (uint8_t)((uint32_t)analogRead(PIN_SOIL) * 100 / 1023);
#else
  return 50;
#endif
}

void readSensors() {
  if (servoDetachAt) return;          // servo đang quay: tạm không đo để tránh nhiễu nguồn
  unsigned long now = millis();

  if (now - tFast >= 500) {
    tFast = now;
    // Lọc nhiễu bằng trung bình trượt đơn giản: mới = (cũ*3 + đo)/4
    uint8_t l = (uint8_t)((uint32_t)analogRead(PIN_LDR) * 100 / 1023);
    uint8_t s = readSoilPct();
    lightPct = (lightPct * 3 + l + 2) / 4;
    soilPct  = (soilPct * 3 + s + 2) / 4;

    int w = constrain(analogRead(PIN_WATER), (int)WATER_RAW_EMPTY, (int)WATER_RAW_FULL);
    uint8_t pct = (uint8_t)map(w, WATER_RAW_EMPTY, WATER_RAW_FULL, 0, 100);
    waterPct = (waterPct * 3 + pct + 2) / 4;
  }

  if (now - tDht >= 2500) {
    tDht = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) { temperature = (int16_t)(t + 0.5); humidity = (int16_t)(h + 0.5); }
  }
}

// ----------------------------- LOGIC ĐIỀU KHIỂN ---------------------------
void controlLogic() {
  unsigned long now = millis();

  // Cảnh báo bồn cạn (có trễ 15 % -> 20 % để không nhấp nháy)
  if (!tankLow && waterPct < TANK_LOW_PCT) {
    tankLow = true;
    sendEvent("TANK_LOW", waterPct);
    showMsg(PSTR("!! BON NUOC CAN"));
  } else if (tankLow && waterPct > TANK_LOW_PCT + 5) {
    tankLow = false;
    sendEvent("TANK_OK", waterPct);
  }
  tooHot = temperature >= 0 && temperature >= tMax + 5;

  // An toàn: bồn cạn thì luôn tắt bơm, kể cả ở chế độ MANUAL
  if (tankLow && pumpOn) setPump(false);

  // Tưới hẹn giờ (lệnh WATER=n): bơm chạy đủ thời gian rồi tự tắt; dừng sớm nếu bồn cạn
  if (waterUntil) {
    if (tankLow || (long)(now - waterUntil) >= 0) {
      waterUntil = 0;
      setPump(false);
      sendEvent("WATER_DONE", soilPct);
    } else {
      setPump(true);
    }
  }

  if (autoMode && temperature >= 0) {
    // Cửa thông gió: mở khi nóng hoặc quá ẩm, đóng khi đã mát (trễ 2 °C / 5 %)
    if (!ventOpen && (temperature >= tMax || humidity >= HUM_MAX)) setVent(true);
    else if (ventOpen && temperature <= tMax - 2 && humidity <= HUM_MAX - 5) setVent(false);
  }
  if (autoMode) {
    // Đèn trồng cây: bật khi tối, tắt khi đủ sáng (trễ 10 %)
    if (!lampOn && lightPct < lightMin) setLamp(true);
    else if (lampOn && lightPct > lightMin + 10) setLamp(false);

    // Bơm: bật khi đất khô và bồn còn nước, tắt khi đất đủ ẩm (trễ 10 %).
    // Đang tưới hẹn giờ thì để phần hẹn giờ quyết định.
    if (!waterUntil) {
      if (!pumpOn && soilPct < soilMin && !tankLow) setPump(true);
      else if (pumpOn && soilPct >= soilMin + 10) setPump(false);
    }
  }

  // LED xanh cảnh báo: sáng liên tục suốt thời gian có cảnh báo (bồn cạn hoặc quá nhiệt)
  digitalWrite(PIN_ALARM, tankLow || tooHot);

  // Còi: bồn cạn -> bíp 1 lần mỗi 4 s ; quá nhiệt -> bíp gấp
  uint16_t period = tooHot ? 800 : 4000;
  if ((tankLow || tooHot) && now - tBeep >= period) {
    tBeep = now;
    beep(tooHot ? 2600 : 1200, 200);
  }

  if (servoDetachAt && now >= servoDetachAt) { ventServo.detach(); servoDetachAt = 0; }
}

// ----------------------------- LCD ----------------------------------------
/*
 * Bố cục (16 cột x 2 dòng, không dấu):
 *   Dòng 1 (cố định) : "T25*C H60% AUTO"            (* = ký hiệu độ của LCD)
 *   Dòng 2 (đổi 3 giây/lần):
 *     trang 0        : "Dat: 40% Nc: 80%"            độ ẩm đất + mực nước
 *     trang 1        : "Sang: 70% D:BAT"             ánh sáng + đèn
 *     trang 2        : "Bom:TAT Gio:DONG"            bơm + cửa gió
 *   Thông báo sự kiện (showMsg) chiếm dòng 2 trong 2.5 giây.
 *   Khi bồn cạn, dòng 2 nháy "!! BON NUOC CAN" xen kẽ mỗi giây.
 */

// Dò địa chỉ module I2C của LCD: PCF8574 (0x20-0x27) hoặc PCF8574A (0x38-0x3F)
uint8_t findLcd() {
  const uint8_t first[] = { 0x27, 0x3F };          // hai địa chỉ phổ biến nhất, thử trước
  for (uint8_t i = 0; i < 2; i++) {
    Wire.beginTransmission(first[i]);
    if (Wire.endTransmission() == 0) return first[i];
  }
  for (uint8_t a = 0x20; a <= 0x3F; a++) {
    if (a == 0x28) a = 0x38;                       // bỏ qua khoảng không dùng
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0;
}

// Chỉ ghi ra LCD khi nội dung dòng thay đổi -> không nhấp nháy, ít chiếm bus I2C
void lcdLine(uint8_t row, const char *text) {
  char line[17];
  snprintf_P(line, sizeof(line), PSTR("%-16s"), text);   // đệm khoảng trắng cho đủ 16 cột
  if (!strcmp(line, lcdShown[row])) return;
  strcpy(lcdShown[row], line);
  lcd->setCursor(0, row);
  lcd->print(line);
}

void updateLcd() {
  if (!lcd) return;                    // không tìm thấy LCD: hệ thống vẫn chạy bình thường
  unsigned long now = millis();
  if (now - tLcd < 250) return;
  tLcd = now;

  char buf[20];

  // Dòng 1: nhiệt độ, độ ẩm không khí, chế độ.  \xDF = ký hiệu độ trong bảng mã HD44780
  if (temperature < 0)
    snprintf_P(buf, sizeof(buf), PSTR("T--\xDF" "C H--%% %s"), autoMode ? "AUTO" : "MANU");
  else
    snprintf_P(buf, sizeof(buf), PSTR("T%2d\xDF" "C H%2d%% %s"), temperature, humidity, autoMode ? "AUTO" : "MANU");
  lcdLine(0, buf);

  // Dòng 2
  if (now < msgUntil) {
    lcdLine(1, lcdMsg);                                    // thông báo sự kiện
  } else if (tankLow && (now / 1000) % 2 == 0) {
    strcpy_P(buf, PSTR("!! BON NUOC CAN"));                // cảnh báo nháy mỗi giây
    lcdLine(1, buf);
  } else {
    switch ((now / 3000) % 3) {
      case 0:
        snprintf_P(buf, sizeof(buf), PSTR("Dat:%3d%% Nc:%3d%%"), soilPct, waterPct);
        break;
      case 1:
        snprintf_P(buf, sizeof(buf), PSTR("Sang:%3d%% D:%s"), lightPct, lampOn ? "BAT" : "TAT");
        break;
      default:
        snprintf_P(buf, sizeof(buf), PSTR("Bom:%s Gio:%s"), pumpOn ? "BAT" : "TAT", ventOpen ? "MO" : "DONG");
        break;
    }
    lcdLine(1, buf);
  }
}

// ----------------------------- TRUYỀN DỮ LIỆU -----------------------------
void sendTelemetry() {
  unsigned long now = millis();
  if (now - tTele < TELEMETRY_MS) return;
  if (servoDetachAt) return;          // không gửi khi servo đang quay
  tTele = now;

  char b[72];
  uint8_t alarm = (tankLow ? 1 : 0) | (tooHot ? 2 : 0);
  snprintf(b, sizeof(b), "D,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
           temperature, humidity, lightPct, soilPct, waterPct,
           pumpOn, ventOpen, lampOn, autoMode, alarm, tMax, soilMin, lightMin);
  sendLine(b);
}

// ----------------------------- NHẬN LỆNH ----------------------------------
void ack(const char *cmd, bool ok) {
  char b[40];
  snprintf(b, sizeof(b), "A,%s,%s", cmd, ok ? "OK" : "ERR");
  sendLine(b);
}

void handleCommand(char *cmd) {
  bool ok = true;
  char *eq = strchr(cmd, '=');
  const char *val = eq ? eq + 1 : "";
  if (eq) *eq = 0;                       // tách KEY / VALUE
  int n = atoi(val);

  if (!strcmp(cmd, "PING")) {
    // chỉ cần phản hồi OK
  } else if (!strcmp(cmd, "RAW")) {
    char b[32];                          // giá trị thô để hiệu chỉnh cảm biến
    snprintf(b, sizeof(b), "R,%d,%d,%d", analogRead(PIN_WATER), analogRead(PIN_SOIL), analogRead(PIN_LDR));
    sendLine(b);
  } else if (!strcmp(cmd, "BEEP")) {
    beep(2000, 300);
  } else if (!strcmp(cmd, "MODE")) {
    if (!strcmp(val, "AUTO")) autoMode = true;
    else if (!strcmp(val, "MANUAL")) autoMode = false;
    else ok = false;
  } else if (!strcmp(cmd, "PUMP")) {
    // Lệnh bật/tắt tay được nhận ở mọi chế độ: nếu đang AUTO thì tự chuyển sang MANUAL
    if (!strcmp(val, "ON")) { if (tankLow) ok = false; else { autoMode = false; setPump(true); } }
    else if (!strcmp(val, "OFF")) { autoMode = false; waterUntil = 0; setPump(false); }
    else ok = false;
  } else if (!strcmp(cmd, "WATER")) {
    if (n >= 3 && n <= 60 && !tankLow) {
      waterUntil = millis() + (unsigned long)n * 1000UL;
      if (!waterUntil) waterUntil = 1;
      setPump(true);
      sendEvent("WATER_START", n);
      showMsg(PSTR("TUOI CAY..."));
    } else ok = false;
  } else if (!strcmp(cmd, "VENT")) {
    if (!strcmp(val, "OPEN")) { autoMode = false; setVent(true); }
    else if (!strcmp(val, "CLOSE")) { autoMode = false; setVent(false); }
    else ok = false;
  } else if (!strcmp(cmd, "LAMP")) {
    if (!strcmp(val, "ON")) { autoMode = false; setLamp(true); }
    else if (!strcmp(val, "OFF")) { autoMode = false; setLamp(false); }
    else ok = false;
  } else if (!strcmp(cmd, "TMAX")) {
    if (n >= 20 && n <= 50) { tMax = n; saveConfig(); } else ok = false;
  } else if (!strcmp(cmd, "SOIL")) {
    if (n >= 5 && n <= 90) { soilMin = n; saveConfig(); } else ok = false;
  } else if (!strcmp(cmd, "LIGHT")) {
    if (n >= 5 && n <= 90) { lightMin = n; saveConfig(); } else ok = false;
  } else {
    ok = false;
  }

  if (eq) *eq = '=';                     // ghép lại để phản hồi nguyên lệnh
  ack(cmd, ok);
  if (ok) tTele = 0;                     // gửi trạng thái mới ngay
}

void pollStream(Stream &s, char *buf, uint8_t &len) {
  while (s.available()) {
    char c = s.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = 0;
      if (len) handleCommand(buf);
      len = 0;
    } else if (len < CMD_MAX - 1) {
      buf[len++] = toupper(c);
    }
  }
}

// ----------------------------- SETUP / LOOP -------------------------------
void setup() {
  digitalWrite(PIN_PUMP, PUMP_ACTIVE_LOW ? HIGH : LOW);   // đảm bảo bơm TẮT ngay khi khởi động
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, PUMP_ACTIVE_LOW ? HIGH : LOW);
  pinMode(PIN_ALARM, OUTPUT);
  pinMode(PIN_LAMP, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  Serial.begin(9600);
#if USE_BLUETOOTH
  bt.begin(9600);                // HC-05/HC-06 mặc định 9600 baud
#endif
  dht.begin();
  loadConfig();

  // LCD: tự dò địa chỉ I2C, không cần sửa code khi đổi module
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000, true);   // dây I2C lỏng/chập không làm treo cả hệ thống
#endif
  lcdAddr = findLcd();
  if (lcdAddr) {
    lcd = new LiquidCrystal_I2C(lcdAddr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd->print(F("SMART GREENHOUSE"));
    lcd->setCursor(0, 1);
    lcd->print(F("  IAD591 - IoT  "));
  }

  // Đưa cửa gió về vị trí đóng
  ventServo.attach(PIN_SERVO);
  ventServo.write(VENT_CLOSE_DEG);
  servoDetachAt = millis() + 700;

  beep(1500, 150);
  delay(1500);                   // chờ DHT11 ổn định

  // Giá trị khởi đầu cho bộ lọc
  lightPct = (uint8_t)((uint32_t)analogRead(PIN_LDR) * 100 / 1023);
  soilPct  = readSoilPct();
  waterPct = (uint8_t)map(constrain(analogRead(PIN_WATER), (int)WATER_RAW_EMPTY, (int)WATER_RAW_FULL),
                          WATER_RAW_EMPTY, WATER_RAW_FULL, 0, 100);
  sendEvent("BOOT", 0);
  sendEvent("LCD", lcdAddr);     // vd "E,LCD,39" = tìm thấy ở 0x27 ; "E,LCD,0" = không thấy LCD
  if (!lcdAddr) { beep(400, 600); }
}

void loop() {
  pollStream(Serial, bufUsb, lenUsb);
#if USE_BLUETOOTH
  pollStream(bt, bufBt, lenBt);
#endif
  readSensors();
  controlLogic();
  updateLcd();
  sendTelemetry();
}
