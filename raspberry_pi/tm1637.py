"""Driver tối giản cho LED 7 đoạn 4 digit TM1637 (bit-bang bằng RPi.GPIO).

Cấp nguồn module bằng 3.3V của Pi để mức logic DIO/CLK an toàn cho GPIO.
Nếu không có RPi.GPIO (chạy thử trên PC) thì driver tự vô hiệu hoá.
"""
import threading
import time

SEG = {
    "0": 0x3F, "1": 0x06, "2": 0x5B, "3": 0x4F, "4": 0x66, "5": 0x6D, "6": 0x7D,
    "7": 0x07, "8": 0x7F, "9": 0x6F, " ": 0x00, "-": 0x40, "F": 0x71, "U": 0x3E,
    "L": 0x38, "P": 0x73, "E": 0x79, "r": 0x50, "A": 0x77, "H": 0x76,
    "C": 0x39, "*": 0x63, "o": 0x5C,          # "*" = ký hiệu độ (°)
}


class TM1637:
    def __init__(self, clk, dio, brightness=4):
        self.clk, self.dio = clk, dio
        self.brightness = brightness
        self.lock = threading.Lock()
        self.last = None
        try:
            import RPi.GPIO as GPIO
            self.gpio = GPIO
            GPIO.setwarnings(False)
            GPIO.setmode(GPIO.BCM)
            GPIO.setup(clk, GPIO.OUT, initial=GPIO.HIGH)
            GPIO.setup(dio, GPIO.OUT, initial=GPIO.HIGH)
            self.enabled = True
        except Exception as exc:
            self.gpio = None
            self.enabled = False
            print("[TM1637] không dùng được GPIO (%s) -> bỏ qua LED 4 digit" % exc)

    def _w(self, pin, val):
        self.gpio.output(pin, val)
        time.sleep(0.00005)

    def _start(self):
        self._w(self.dio, 1); self._w(self.clk, 1); self._w(self.dio, 0); self._w(self.clk, 0)

    def _stop(self):
        self._w(self.clk, 0); self._w(self.dio, 0); self._w(self.clk, 1); self._w(self.dio, 1)

    def _byte(self, b):
        for i in range(8):
            self._w(self.dio, (b >> i) & 1)
            self._w(self.clk, 1)
            self._w(self.clk, 0)
        # bỏ qua bit ACK
        self._w(self.dio, 1)
        self._w(self.clk, 1)
        self._w(self.clk, 0)

    def show(self, text, colon=False):
        """Hiển thị tối đa 4 ký tự (số, khoảng trắng, '-', F, U, L, P, E, r, A, H, C, o, * = độ)."""
        text = str(text)[:4].rjust(4)
        if not self.enabled or (text, colon) == self.last:
            return
        data = [SEG.get(ch, 0x00) for ch in text]
        if colon:
            data[1] |= 0x80
        with self.lock:
            self._start(); self._byte(0x40); self._stop()            # ghi dữ liệu, tự tăng địa chỉ
            self._start(); self._byte(0xC0)
            for d in data:
                self._byte(d)
            self._stop()
            self._start(); self._byte(0x88 | self.brightness); self._stop()   # bật hiển thị
            self.last = (text, colon)
