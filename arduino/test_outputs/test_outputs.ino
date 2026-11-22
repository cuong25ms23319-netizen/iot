/*
 * TEST ĐẦU RA: LCD1602 I2C + CÒI + SERVO  (kèm 2 LED ở D6, D7 nếu đã nối)
 * Chạy độc lập, không cần cảm biến / Bluetooth / Pi.
 *
 * Đấu nối (giống mạch Smart Greenhouse):
 *   LCD I2C : SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
 *   Còi     : (+) -> D8, (-) -> GND
 *   Servo   : Signal (cam) -> D9, đỏ -> 5V, nâu -> GND
 *   LED     : D6 (bơm), D7 (đèn), mỗi LED qua 220 ohm  (chưa nối thì bỏ qua)
 *
 * Mỗi 2 giây chạy một bước, lặp lại mãi. LCD và Serial Monitor (9600 baud) ghi bước đang chạy:
 *   1. Còi bíp 1 tiếng cao        2. Servo quay tới 90 độ (mở)
 *   3. Còi bíp 2 tiếng trầm       4. Servo quay về 0 độ (đóng)
 *
 * Nếu Serial Monitor in lại dòng "=== TEST DAU RA ===" liên tục: Arduino đang bị reset
 * do servo làm sụt nguồn -> cấp nguồn 5V riêng cho servo, nối chung GND.
 * Thư viện: "LiquidCrystal I2C" (Frank de Brabander). Servo, Wire có sẵn.
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

const uint8_t PIN_LED_PUMP = 6;
const uint8_t PIN_LED_LAMP = 7;
const uint8_t PIN_BUZZER   = 8;
const uint8_t PIN_SERVO    = 9;

LiquidCrystal_I2C *lcd = nullptr;
Servo servo;
uint8_t step = 0;
unsigned int cycle = 0;

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

void setup() {
  pinMode(PIN_LED_PUMP, OUTPUT);
  pinMode(PIN_LED_LAMP, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  Serial.begin(9600);
  Serial.println(F("=== TEST DAU RA: LCD + COI + SERVO ==="));

  Wire.begin();
  uint8_t addr = findLcd();
  if (addr) {
    Serial.print(F("LCD: tim thay o dia chi 0x")); Serial.println(addr, HEX);
    lcd = new LiquidCrystal_I2C(addr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd->print(F("TEST DAU RA"));
  } else {
    Serial.println(F("LCD: KHONG tim thay (kiem tra SDA->A4, SCL->A5, nguon)"));
  }

  servo.attach(PIN_SERVO);
  servo.write(0);
  delay(1000);
}

void loop() {
  switch (step) {
    case 0:
      cycle++;
      if (lcd) { lcd->setCursor(12, 0); lcd->print(F("#")); lcd->print(cycle); }
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
  delay(2000);
}
