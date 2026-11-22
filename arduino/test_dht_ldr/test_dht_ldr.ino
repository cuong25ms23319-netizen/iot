/*
 * TEST DHT11 + QUANG TRỞ (LDR) - chạy độc lập, không cần LCD / Bluetooth / Pi
 *
 * Đấu nối (giống mạch Smart Greenhouse):
 *   DHT11 : DATA -> A3, VCC -> 5V, GND -> GND
 *   LDR   : 5V -> LDR -> A2 -> điện trở 10k -> GND
 *
 * Cách dùng: nạp code, mở Serial Monitor 9600 baud. Mỗi 2 giây in một dòng.
 *   - Hà hơi vào DHT11      -> độ ẩm phải tăng, nhiệt độ tăng nhẹ.
 *   - Che tay lên LDR       -> giá trị giảm ; rọi đèn điện thoại -> giá trị tăng.
 * Thư viện: "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor".
 */
#include <DHT.h>

#define DHT_TYPE DHT11            // đổi thành DHT22 nếu dùng DHT22
const uint8_t PIN_DHT = A3;
const uint8_t PIN_LDR = A2;

DHT dht(PIN_DHT, DHT_TYPE);
unsigned int okCount = 0, failCount = 0;
int ldrMin = 1023, ldrMax = 0;

void setup() {
  Serial.begin(9600);
  dht.begin();
  Serial.println(F("=== TEST DHT11 (A3) + LDR (A2) ==="));
  delay(2000);                    // DHT11 cần khoảng 1-2 giây sau khi cấp nguồn
}

void loop() {
  // ---------- DHT11 ----------
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  Serial.print(F("DHT11: "));
  if (isnan(t) || isnan(h)) {
    failCount++;
    Serial.print(F("LOI DOC (kiem tra day DATA -> A3, VCC, GND)"));
  } else {
    okCount++;
    Serial.print(t, 1); Serial.print(F(" C, "));
    Serial.print(h, 0); Serial.print(F(" %"));
    if (t < 0 || t > 50 || h < 20 || h > 95) Serial.print(F("  <-- ngoai dai do cua DHT11, nghi ngo sai"));
  }
  Serial.print(F("   [ok=")); Serial.print(okCount);
  Serial.print(F(" loi=")); Serial.print(failCount); Serial.print(F("]"));

  // ---------- LDR ----------
  long sum = 0;
  for (uint8_t i = 0; i < 8; i++) { sum += analogRead(PIN_LDR); delay(2); }   // trung bình 8 mẫu
  int raw = sum / 8;
  if (raw < ldrMin) ldrMin = raw;
  if (raw > ldrMax) ldrMax = raw;

  Serial.print(F("   |   LDR: raw=")); Serial.print(raw);
  Serial.print(F(" (")); Serial.print((long)raw * 100 / 1023); Serial.print(F(" %)"));
  Serial.print(F("  min=")); Serial.print(ldrMin);
  Serial.print(F(" max=")); Serial.print(ldrMax);
  if (raw <= 2)         Serial.print(F("  <-- luon ~0: ho mach phia 5V hoac LDR chua noi"));
  else if (raw >= 1021) Serial.print(F("  <-- luon ~1023: thieu dien tro 10k xuong GND"));
  Serial.println();

  delay(2000);                    // DHT11 chỉ đọc được tối đa 1 lần mỗi giây
}
