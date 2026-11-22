/*
 * SMART GREENHOUSE - ARDUINO THỨ HAI: SERVO TƯỚI CÂY
 * Đồ án cuối kỳ IoT Programming - IAD591
 *
 * Vai trò: một nút chấp hành riêng cho việc tưới. Nhận lệnh qua Serial (cáp USB nối Raspberry Pi),
 *          điều khiển servo xoay vòi tưới quét qua lại trên luống cây, và báo trạng thái về qua Serial.
 *          Raspberry Pi là bên điều phối: khi Arduino chính báo "bơm đang chạy", Pi gửi WATER=ON cho nút này.
 *
 * Đấu nối:
 *   Servo : Signal (cam) -> A0, đỏ -> 5V, nâu -> GND
 *   USB   : nối vào Raspberry Pi (hoặc máy tính để thử bằng Serial Monitor)
 *   LED có sẵn trên board (D13) sáng trong lúc đang tưới.
 *
 * GIAO THỨC Serial (9600 baud, mỗi bản tin 1 dòng):
 *   Pi -> Arduino 2
 *     WATER=ON | WATER=OFF     bật / tắt tưới
 *     ANGLE=n  (0..180)        đưa servo tới một góc cố định (để cân chỉnh), chỉ khi đang không tưới
 *     PING
 *   Arduino 2 -> Pi
 *     W,<on>,<góc>             mỗi giây: on = 1 đang tưới, 0 nghỉ ; góc hiện tại của servo
 *     E,WBOOT,0                khi khởi động
 *     A,<lệnh>,<OK|ERR>
 *
 * AN TOÀN: nếu đang tưới mà quá 15 giây không nhận lại WATER=ON (Pi treo, rút cáp...) thì tự dừng.
 *          Pi gửi lặp lại trạng thái mong muốn mỗi 2 giây nên bình thường không bao giờ chạm mốc này.
 */

#include <Servo.h>

// ----------------------------- CẤU HÌNH -----------------------------------
const uint8_t PIN_SERVO = A0;
const uint8_t PIN_LED   = LED_BUILTIN;

const uint8_t REST_ANGLE = 0;       // vị trí nghỉ (vòi quay ra ngoài)
const uint8_t SWEEP_MIN  = 40;      // khi tưới: quét qua lại giữa hai góc này
const uint8_t SWEEP_MAX  = 140;
const uint8_t STEP_DEG   = 2;       // mỗi bước 2 độ ...
const uint8_t STEP_MS    = 30;      // ... mỗi 30 ms  (~65 độ/giây)
const unsigned long FAILSAFE_MS = 15000;

// ----------------------------- BIẾN TOÀN CỤC ------------------------------
Servo servo;
bool watering = false;
int angle = REST_ANGLE;
int8_t dir = 1;
unsigned long tStep = 0, tTele = 0, lastOn = 0, lastRx = 0;

const uint8_t CMD_MAX = 20;
char cmdBuf[CMD_MAX];
uint8_t cmdLen = 0;

// ----------------------------- LỆNH ---------------------------------------
void setWatering(bool on) {
  if (on) lastOn = millis();
  if (on == watering) return;
  watering = on;
  digitalWrite(PIN_LED, on);
  if (on) { if (angle < SWEEP_MIN) angle = SWEEP_MIN; dir = 1; }
  else    { angle = REST_ANGLE; }
  servo.write(angle);
  tTele = 0;                        // báo trạng thái mới ngay
}

void handleCommand(char *cmd) {
  bool ok = true;
  char *eq = strchr(cmd, '=');
  const char *val = eq ? eq + 1 : "";
  if (eq) *eq = 0;

  if (!strcmp(cmd, "PING")) {
  } else if (!strcmp(cmd, "WATER")) {
    if (!strcmp(val, "ON")) setWatering(true);
    else if (!strcmp(val, "OFF")) setWatering(false);
    else ok = false;
  } else if (!strcmp(cmd, "ANGLE")) {
    int n = atoi(val);
    if (!watering && n >= 0 && n <= 180) { angle = n; servo.write(angle); } else ok = false;
  } else {
    ok = false;
  }

  if (eq) *eq = '=';
  Serial.print(F("A,")); Serial.print(cmd); Serial.println(ok ? F(",OK") : F(",ERR"));
}

// Lệnh kết thúc khi gặp xuống dòng, hoặc khi không còn ký tự mới trong 100 ms
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
  if (cmdLen && millis() - lastRx > 100) { cmdBuf[cmdLen] = 0; handleCommand(cmdBuf); cmdLen = 0; }
}

// ----------------------------- SETUP / LOOP -------------------------------
void setup() {
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(9600);
  servo.attach(PIN_SERVO);
  servo.write(REST_ANGLE);
  Serial.println(F("E,WBOOT,0"));
}

void loop() {
  unsigned long now = millis();
  pollSerial();

  if (watering) {
    if (now - lastOn > FAILSAFE_MS) {            // mất liên lạc với Pi -> tự dừng tưới
      setWatering(false);
      Serial.println(F("E,WFAILSAFE,0"));
    } else if (now - tStep >= STEP_MS) {         // quét vòi tưới qua lại
      tStep = now;
      angle += dir * STEP_DEG;
      if (angle >= SWEEP_MAX) { angle = SWEEP_MAX; dir = -1; }
      if (angle <= SWEEP_MIN) { angle = SWEEP_MIN; dir = 1; }
      servo.write(angle);
    }
  }

  if (now - tTele >= 1000 || tTele == 0) {
    tTele = now ? now : 1;
    Serial.print(F("W,")); Serial.print(watering ? 1 : 0); Serial.print(','); Serial.println(angle);
  }
}
