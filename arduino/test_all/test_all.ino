/*
 * TEST TỔNG HỢP: LCD1602 I2C + CÒI + SERVO + 2 LED  +  DHT11 + WATER SENSOR + LDR
 * Phát triển từ test_outputs.ino (đã chạy tốt), thêm 3 cảm biến.
 * Chạy độc lập, không cần Bluetooth / Pi.
 *
 * Đấu nối (giống mạch Smart Greenhouse):
 *   LCD I2C      : SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
 *   Còi          : (+) -> D8, (-) -> GND
 *   Servo        : Signal (cam) -> D9, đỏ -> 5V, nâu -> GND
 *   LED          : D6 (bơm), D7 (đèn), mỗi LED qua 220 ohm
 *   DHT11        : DATA -> A3, VCC -> 5V, GND -> GND
 *   Water sensor : S -> A1, + -> 5V, - -> GND
 *   LDR          : 5V -> LDR -> A2 -> điện trở 10k -> GND
 *
 * Mỗi 2 giây chạy một bước (còi / servo như test_outputs) và đọc lại cả 3 cảm biến.
 *   LCD dòng 1: "T29 H68 L59 W58"   T = nhiệt độ °C, H = độ ẩm %, L = ánh sáng %, W = mực nước %
 *   LCD dòng 2: bước đang chạy
 *   Serial Monitor (9600 baud): in đầy đủ giá trị, kể cả giá trị analog thô.
 *
 * Thư viện: "LiquidCrystal I2C" (Frank de Brabander), "DHT sensor library" (Adafruit)
 *           + "Adafruit Unified Sensor". Servo, Wire có sẵn.
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <DHT.h>

#define DHT_TYPE DHT11
const uint8_t PIN_LED_PUMP = 6;
const uint8_t PIN_LED_LAMP = 7;
const uint8_t PIN_BUZZER   = 8;
const uint8_t PIN_SERVO    = 9;
const uint8_t PIN_WATER    = A1;
const uint8_t PIN_LDR      = A2;
const uint8_t PIN_DHT      = A3;

// Chép hai số đã hiệu chỉnh bằng test_water_sensor.ino vào đây
const int WATER_RAW_EMPTY = 20;
const int WATER_RAW_FULL  = 520;

LiquidCrystal_I2C *lcd = nullptr;
Servo servo;
DHT dht(PIN_DHT, DHT_TYPE);
uint8_t step = 0;
unsigned int cycle = 0, dhtFail = 0;

uint8_t findLcd() {
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0;
}

void show(const __FlashStringHelper *text) {      // in ra cả Serial và dòng 2 của LCD
  Serial.println(text);
  if (!lcd) return;
  lcd->setCursor(0, 1);
  lcd->print(F("                "));
  lcd->setCursor(0, 1);
  lcd->print(text);
}

int readAvg(uint8_t pin) {                        // trung bình 8 mẫu, bỏ mẫu đầu sau khi đổi kênh ADC
  analogRead(pin);
  long sum = 0;
  for (uint8_t i = 0; i < 8; i++) { sum += analogRead(pin); delay(2); }
  return (int)(sum / 8);
}

void readAndShowSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool dhtOk = !isnan(t) && !isnan(h);
  if (!dhtOk) dhtFail++;

  int ldrRaw   = readAvg(PIN_LDR);
  int waterRaw = readAvg(PIN_WATER);
  int lightPct = (int)((long)ldrRaw * 100 / 1023);
  int waterPct = map(constrain(waterRaw, WATER_RAW_EMPTY, WATER_RAW_FULL), WATER_RAW_EMPTY, WATER_RAW_FULL, 0, 100);

  // ----- Serial -----
  Serial.print(F("   DHT11: "));
  if (dhtOk) { Serial.print(t, 1); Serial.print(F("C ")); Serial.print(h, 0); Serial.print(F("%")); }
  else       { Serial.print(F("LOI DOC")); }
  Serial.print(F(" (loi=")); Serial.print(dhtFail); Serial.print(F(")"));
  Serial.print(F(" | LDR: ")); Serial.print(ldrRaw); Serial.print(F(" = ")); Serial.print(lightPct); Serial.print(F("%"));
  Serial.print(F(" | WATER: ")); Serial.print(waterRaw); Serial.print(F(" = ")); Serial.print(waterPct); Serial.println(F("%"));

  // ----- LCD dòng 1: "T29 H68 L59 W58" -----
  if (!lcd) return;
  char line[17];
  if (dhtOk) snprintf_P(line, sizeof(line), PSTR("T%d H%d L%d W%d"), (int)(t + 0.5), (int)(h + 0.5), lightPct, waterPct);
  else       snprintf_P(line, sizeof(line), PSTR("T-- H-- L%d W%d"), lightPct, waterPct);
  lcd->setCursor(0, 0);
  lcd->print(F("                "));
  lcd->setCursor(0, 0);
  lcd->print(line);
}

void setup() {
  pinMode(PIN_LED_PUMP, OUTPUT);
  pinMode(PIN_LED_LAMP, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  Serial.begin(9600);
  Serial.println(F("=== TEST TONG HOP: LCD + COI + SERVO + DHT11 + WATER + LDR ==="));

  Wire.begin();
  uint8_t addr = findLcd();
  if (addr) {
    Serial.print(F("LCD: tim thay o dia chi 0x")); Serial.println(addr, HEX);
    lcd = new LiquidCrystal_I2C(addr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd->print(F("TEST TONG HOP"));
  } else {
    Serial.println(F("LCD: KHONG tim thay (kiem tra SDA->A4, SCL->A5, nguon)"));
  }

  dht.begin();
  servo.attach(PIN_SERVO);
  servo.write(0);
  delay(2000);                                    // chờ servo về 0 và DHT11 ổn định
}

void loop() {
  switch (step) {
    case 0:
      cycle++;
      Serial.print(F("--- Vong ")); Serial.print(cycle); Serial.println(F(" ---"));
      show(F("1 Coi: bip cao"));
      tone(PIN_BUZZER, 2000, 300);
      digitalWrite(PIN_LED_PUMP, HIGH);
      break;
    case 1:
      show(F("2 Servo: 90 (MO)"));
      servo.write(90);
      digitalWrite(PIN_LED_LAMP, HIGH);
      break;
    case 2:
      show(F("3 Coi: 2 bip tram"));
      tone(PIN_BUZZER, 600, 200);
      delay(350);
      tone(PIN_BUZZER, 600, 200);
      digitalWrite(PIN_LED_PUMP, LOW);
      break;
    case 3:
      show(F("4 Servo: 0 (DONG)"));
      servo.write(0);
      digitalWrite(PIN_LED_LAMP, LOW);
      break;
  }
  step = (step + 1) % 4;

  delay(700);                 // chờ servo quay xong rồi mới đọc cảm biến (tránh nhiễu nguồn)
  readAndShowSensors();
  delay(1300);
}
