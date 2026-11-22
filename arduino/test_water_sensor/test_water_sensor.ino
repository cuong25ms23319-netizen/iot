/*
 * TEST WATER SENSOR - chạy độc lập, không cần LCD / Bluetooth / Pi
 *
 * Đấu nối (giống mạch Smart Greenhouse):
 *   Water sensor : S -> A1, + -> 5V, - -> GND
 *
 * Cách dùng: nạp code, mở Serial Monitor 9600 baud. Mỗi 0.5 giây in một dòng.
 *   1. Lau khô cảm biến, chờ số ổn định, gõ  e  rồi Enter  -> ghi nhận mức KHÔ.
 *   2. Nhúng cảm biến tới vạch bạn coi là ĐẦY, gõ  f  rồi Enter -> ghi nhận mức ĐẦY.
 *   3. Sketch in ra hai dòng hằng số để chép vào smart_greenhouse.ino.
 *   Gõ  r  để xoá min/max và làm lại.
 * Chỉ nhúng phần có các vạch kim loại, không để nước chạm phần mạch phía trên.
 */

const uint8_t PIN_WATER = A1;

int rawEmpty = 20;       // giá trị mặc định đang dùng trong smart_greenhouse.ino
int rawFull  = 520;
int rawMin = 1023, rawMax = 0;

int readWater() {                       // trung bình 16 mẫu để giảm nhiễu
  long sum = 0;
  for (uint8_t i = 0; i < 16; i++) { sum += analogRead(PIN_WATER); delay(2); }
  return (int)(sum / 16);
}

void printConstants() {
  Serial.println();
  Serial.println(F("--- Chep 2 dong nay vao smart_greenhouse.ino ---"));
  Serial.print(F("const uint16_t WATER_RAW_EMPTY = ")); Serial.print(rawEmpty); Serial.println(F(";"));
  Serial.print(F("const uint16_t WATER_RAW_FULL  = ")); Serial.print(rawFull);  Serial.println(F(";"));
  if (rawFull - rawEmpty < 100)
    Serial.println(F("CANH BAO: hai muc qua gan nhau (< 100), do se khong chinh xac. Kiem tra day S -> A1 va nguon."));
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== TEST WATER SENSOR (S -> A1) ==="));
  Serial.println(F("Go: e = ghi muc KHO, f = ghi muc DAY, r = xoa min/max"));
}

void loop() {
  int raw = readWater();
  if (raw < rawMin) rawMin = raw;
  if (raw > rawMax) rawMax = raw;

  // Lệnh từ Serial Monitor
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'e' || c == 'E') { rawEmpty = raw; Serial.print(F(">> Muc KHO = ")); Serial.println(raw); printConstants(); }
    else if (c == 'f' || c == 'F') { rawFull = raw; Serial.print(F(">> Muc DAY = ")); Serial.println(raw); printConstants(); }
    else if (c == 'r' || c == 'R') { rawMin = 1023; rawMax = 0; Serial.println(F(">> Da xoa min/max")); }
  }

  // Phần trăm theo hai mức đã ghi (giống công thức trong smart_greenhouse.ino)
  int pct = 0;
  if (rawFull > rawEmpty) pct = map(constrain(raw, rawEmpty, rawFull), rawEmpty, rawFull, 0, 100);

  Serial.print(F("raw=")); Serial.print(raw);
  Serial.print(F("  min=")); Serial.print(rawMin);
  Serial.print(F(" max=")); Serial.print(rawMax);
  Serial.print(F("  ->  ")); Serial.print(pct); Serial.print(F(" %  |"));
  for (uint8_t i = 0; i < 20; i++) Serial.print(i < pct / 5 ? '#' : '.');     // thanh mức 20 ô
  Serial.print('|');
  if (pct < 15) Serial.print(F("  BON CAN (bom bi khoa)"));
  Serial.println();

  delay(450);
}
