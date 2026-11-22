"""Cấu hình gateway. Có thể ghi đè bằng biến môi trường, ví dụ:
    BT_MAC=98:D3:31:F5:12:AB python3 app.py
"""
import os

# --- Bluetooth (kênh chính) ---
# MAC của HC-05/HC-06: xem bằng `bluetoothctl scan on`. Để rỗng = không dùng Bluetooth.
BT_MAC = os.environ.get("BT_MAC", "")
BT_CHANNEL = int(os.environ.get("BT_CHANNEL", "1"))

# --- Serial (kênh dự phòng) ---
# /dev/ttyACM0 : cáp USB của Arduino Uno (không cần hạ áp)
# /dev/serial0 : UART trên GPIO14/15 (BẮT BUỘC có mạch hạ áp trên đường TX Arduino -> RX Pi)
SERIAL_PORT = os.environ.get("SERIAL_PORT", "/dev/ttyACM0")
BAUDRATE = 9600

# --- Arduino thứ hai: servo tưới cây (Serial qua cáp USB thứ hai) ---
# Để rỗng (WATER_PORT=) nếu chưa gắn. Hai Arduino có thể đổi chỗ ttyACM0/ttyACM1 sau khi cắm lại;
# muốn cố định thì dùng đường dẫn trong /dev/serial/by-id/.
WATER_PORT = os.environ.get("WATER_PORT", "/dev/ttyACM1")

# --- Lưu trữ ---
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.environ.get("DB_PATH", os.path.join(BASE_DIR, "greenhouse.db"))
LOG_INTERVAL = 5          # giây: chu kỳ ghi dữ liệu (ghi ngay khi trạng thái thay đổi)

# --- LED 7 đoạn 4 digit TM1637 (đánh số BCM) ---
USE_TM1637 = os.environ.get("USE_TM1637", "1") == "1"   # đặt USE_TM1637=0 nếu chưa gắn LED 4 số
TM1637_CLK = 23
TM1637_DIO = 24

# --- Web ---
HOST = "0.0.0.0"
PORT = 5000
