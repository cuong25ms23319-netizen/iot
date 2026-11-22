/*
 * TEST CHUNG CÁC CẢM BIẾN: DHT11 + QUANG TRỞ + WATER SENSOR + BIẾN TRỞ
 * Chạy độc lập, không cần LCD / Bluetooth / servo / Pi.
 *
 * Đấu nối (giống mạch Smart Greenhouse):
 *   DHT11        : DATA -> A3, VCC -> 5V, GND -> GND
 *   Quang trở    : 5V -> LDR -> A2 -> điện trở 10k -> GND
 *   Water sensor : S -> A1, + -> 5V, - -> GND
 *   Biến trở     : chân giữa -> A0, hai chân ngoài -> 5V và GND   (chưa nối thì bỏ qua cột SOIL)
 *
 * Cách dùng: nạp code, mở Serial Monitor 9600 baud. Mỗi 2 giây in một dòng cho cả 4 cảm biến.
 *   Gõ  e  + Enter khi water sensor KHÔ, gõ  f  + Enter khi ngập tới vạch ĐẦY
 *   -> sketch in hai hằng số WATER_RAW_EMPTY / WATER_RAW_FULL để chép vào smart_greenhouse.ino.
 * Thư viện: "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor".
 */
#include <DHT.h>

#define DHT_TYPE DHT11            // đổi thành DHT22 nếu dùng DHT22
const uint8_t PIN_SOIL  = A0;
const uint8_t PIN_WATER = A1;
const uint8_t PIN_LDR   = A2;
const uint8_t PIN_DHT   = A3;

DHT dht(PIN_DHT, DHT_TYPE);
unsigned int dhtOk = 0, dhtFail = 0;
int waterEmpty = 20, waterFull = 520;      // giá trị mặc định trong smart_greenhouse.ino

int readAvg(uint8_t pin) {                 // trung bình 8 mẫu; đọc bỏ 1 mẫu đầu sau khi đổi kênh ADC
  analogRead(pin);
  long sum = 0;
  for (uint8_t i = 0; i < 8; i++) { sum += analogRead(pin); delay(2); }
  return (int)(sum / 8);
}

void printConstants() {
  Serial.println(F("--- Chep 2 dong nay vao smart_greenhouse.ino ---"));
  Serial.print(F("const uint16_t WATER_RAW_EMPTY = ")); Serial.print(waterEmpty); Serial.println(F(";"));
  Serial.print(F("const uint16_t WATER_RAW_FULL  = ")); Serial.print(waterFull);  Serial.println(F(";"));
  if (waterFull - waterEmpty < 100) Serial.println(F("CANH BAO: hai muc qua gan nhau (< 100)."));
}

void setup() {
  Serial.begin(9600);
  dht.begin();
  Serial.println(F("=== TEST CHUNG: DHT11 (A3) | LDR (A2) | WATER (A1) | SOIL (A0) ==="));
  Serial.println(F("Go: e = ghi muc nuoc KHO, f = ghi muc nuoc DAY"));
  delay(2000);                             // DHT11 cần 1-2 giây sau khi cấp nguồn
}

void loop() {
  int water = readAvg(PIN_WATER);
  int ldr   = readAvg(PIN_LDR);
  int soil  = readAvg(PIN_SOIL);

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'e' || c == 'E') { waterEmpty = water; printConstants(); }
    else if (c == 'f' || c == 'F') { waterFull = water; printConstants(); }
  }

  // ---------- DHT11 ----------
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  Serial.print(F("DHT11: "));
  if (isnan(t) || isnan(h)) {
    dhtFail++;
    Serial.print(F("LOI DOC    "));
  } else {
    dhtOk++;
    Serial.print(t, 1); Serial.print(F("C ")); Serial.print(h, 0); Serial.print(F("%"));
  }
  Serial.print(F(" [ok=")); Serial.print(dhtOk); Serial.print(F(" loi=")); Serial.print(dhtFail); Serial.print(F("]"));

  // ---------- Quang trở ----------
  Serial.print(F(" | LDR: ")); Serial.print(ldr);
  Serial.print(F(" (")); Serial.print((long)ldr * 100 / 1023); Serial.print(F("%)"));
  if (ldr <= 2) Serial.print(F(" HO MACH?"));
  else if (ldr >= 1021) Serial.print(F(" THIEU 10k?"));

  // ---------- Water sensor ----------
  int pct = 0;
  if (waterFull > waterEmpty) pct = map(constrain(water, waterEmpty, waterFull), waterEmpty, waterFull, 0, 100);
  Serial.print(F(" | WATER: ")); Serial.print(water);
  Serial.print(F(" (")); Serial.print(pct); Serial.print(F("%)"));
  if (pct < 15) Serial.print(F(" CAN"));

  // ---------- Biến trở (độ ẩm đất giả lập) ----------
  Serial.print(F(" | SOIL: ")); Serial.print(soil);
  Serial.print(F(" (")); Serial.print((long)soil * 100 / 1023); Serial.println(F("%)"));

  delay(1900);                             // DHT11 đọc tối đa 1 lần mỗi giây
}
