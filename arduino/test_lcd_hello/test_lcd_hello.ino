/*
 * TEST LCD1602 I2C - hiện "Hello World!" và bộ đếm giây
 *
 * Đấu nối: SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
 * Thư viện: "LiquidCrystal I2C" (Frank de Brabander)
 *
 * Mở Serial Monitor 9600 baud để xem LCD được tìm thấy ở địa chỉ nào.
 * Nếu Serial báo tìm thấy mà màn hình vẫn trống: vặn biến trở xanh sau module I2C.
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C *lcd = nullptr;

uint8_t findLcd() {                       // quét toàn bộ bus I2C, trả về địa chỉ đầu tiên có thiết bị
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0;
}

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== TEST LCD I2C ==="));
  Wire.begin();

  uint8_t addr = findLcd();
  if (!addr) {
    Serial.println(F("KHONG tim thay thiet bi I2C nao."));
    Serial.println(F("Kiem tra: SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND."));
    return;
  }
  Serial.print(F("Tim thay LCD o dia chi 0x"));
  Serial.println(addr, HEX);
  Serial.println(F("Neu man hinh van trong: van bien tro xanh sau module I2C."));

  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->setCursor(0, 0);
  lcd->print(F("Hello World!"));
}

void loop() {
  if (!lcd) return;
  lcd->setCursor(0, 1);
  lcd->print(F("Giay: "));
  lcd->print(millis() / 1000);
  delay(500);
}
