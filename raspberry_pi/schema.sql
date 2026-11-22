-- SMART GREENHOUSE - Database schema (SQLite)

CREATE TABLE IF NOT EXISTS readings (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    epoch   INTEGER NOT NULL,          -- Unix time (giây) để truy vấn theo khoảng thời gian
    ts      TEXT    NOT NULL,          -- 'YYYY-MM-DD HH:MM:SS' (giờ địa phương)
    temp    INTEGER,                   -- °C  (DHT11)
    hum     INTEGER,                   -- %   độ ẩm không khí (DHT11)
    light   INTEGER NOT NULL,          -- %   ánh sáng (LDR)
    soil    INTEGER NOT NULL,          -- %   độ ẩm đất (biến trở giả lập)
    water   INTEGER NOT NULL,          -- %   mực nước bồn (water sensor)
    pump    INTEGER NOT NULL,          -- 0: tắt, 1: bật
    vent    INTEGER NOT NULL,          -- 0: đóng, 1: mở
    lamp    INTEGER NOT NULL,          -- 0: tắt, 1: bật
    mode    INTEGER NOT NULL,          -- 0: MANUAL, 1: AUTO
    alarm   INTEGER NOT NULL,          -- bit0: bồn cạn, bit1: quá nhiệt
    link    TEXT    NOT NULL           -- kênh nhận: bluetooth | serial | simulator
);
CREATE INDEX IF NOT EXISTS idx_readings_epoch ON readings(epoch);

CREATE TABLE IF NOT EXISTS events (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    epoch   INTEGER NOT NULL,
    ts      TEXT    NOT NULL,
    type    TEXT    NOT NULL,          -- PUMP_ON, PUMP_OFF, VENT_OPEN, VENT_CLOSE, LAMP_ON, LAMP_OFF,
                                       -- TANK_LOW, TANK_OK, BOOT, LINK, START
    detail  TEXT
);

CREATE TABLE IF NOT EXISTS commands (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    epoch   INTEGER NOT NULL,
    ts      TEXT    NOT NULL,
    cmd     TEXT    NOT NULL,          -- ví dụ PUMP=ON
    link    TEXT    NOT NULL,          -- kênh đã gửi
    status  TEXT    NOT NULL DEFAULT 'SENT'   -- SENT -> OK | ERR (theo phản hồi của Arduino)
);
