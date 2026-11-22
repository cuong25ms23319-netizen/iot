/*
 * TEST SERVO - chỉ để xem servo có quay không
 *
 * Đấu nối: Signal (cam) -> A0, đỏ -> 5V, nâu -> GND
 *
 * Nạp xong servo tự chạy, không cần gõ lệnh:
 *   0 độ -> 90 độ -> 180 độ -> 90 độ -> lặp lại, mỗi vị trí giữ 1 giây.
 * LED trên board (D13) nháy mỗi lần đổi góc, Serial Monitor (9600 baud) in góc đang đặt.
 *
 * Nếu dòng "=== TEST SERVO ===" in lại liên tục: Arduino bị reset do servo làm sụt nguồn
 * -> cấp nguồn 5V riêng cho servo, nối chung GND với Arduino.
 */
#include <Servo.h>

const uint8_t PIN_SERVO = A0;
const uint8_t ANGLES[] = { 0, 90, 180, 90 };

Servo servo;
uint8_t idx = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
  Serial.println(F("=== TEST SERVO (A0) ==="));
  servo.attach(PIN_SERVO);
}

void loop() {
  uint8_t a = ANGLES[idx];
  servo.write(a);
  Serial.print(F("Goc: ")); Serial.println(a);
  digitalWrite(LED_BUILTIN, HIGH); delay(150); digitalWrite(LED_BUILTIN, LOW);
  idx = (idx + 1) % 4;
  delay(850);
}
