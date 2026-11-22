#!/usr/bin/env python3
"""TEST LED 4 số TM1637 trên Raspberry Pi: hiện "HELo" rồi đếm 0..20.

Đấu nối: CLK -> GPIO23 (chân 16), DIO -> GPIO24 (chân 18), VCC -> 3.3V (chân 1), GND -> chân 6
Chạy:    python3 test_tm1637.py
"""
import time

import config
from tm1637 import TM1637

d = TM1637(config.TM1637_CLK, config.TM1637_DIO)
if not d.enabled:
    raise SystemExit("Không dùng được GPIO: hãy chạy trên Raspberry Pi và cài python3-rpi.gpio")

print("Đang hiện HELo ...")
d.show("HELo")
time.sleep(3)
for n in range(21):
    print("Đang hiện", n)
    d.show("%4d" % n)
    time.sleep(0.5)
d.show("----")
print("Xong. Nếu LED không sáng: kiểm tra dây, thử đổi chỗ CLK và DIO.")
