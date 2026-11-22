"""Kết nối Arduino <-> Raspberry Pi.

Hai kênh chạy song song, mỗi kênh một luồng tự kết nối lại:
  - bluetooth (ưu tiên) : RFCOMM socket tới HC-05/HC-06
  - serial    (dự phòng): cổng USB / UART

Arduino phát cùng một bản tin trên cả hai kênh. LinkManager chỉ xử lý bản tin
của kênh "đang hoạt động" (kênh ưu tiên cao nhất còn nhận được dữ liệu trong
vài giây gần đây), nên khi Bluetooth mất, hệ thống chuyển sang Serial ngay
mà không cần mở lại cổng (không làm reset Arduino).
"""
import math
import random
import socket
import threading
import time

ALIVE_SECONDS = 3.5     # kênh được coi là "sống" nếu có dữ liệu trong khoảng này
STALE_SECONDS = 10      # không có dữ liệu quá lâu -> đóng và kết nối lại
RETRY_SECONDS = 4


class BluetoothTransport:
    def __init__(self, mac, channel=1):
        self.mac, self.channel = mac, channel
        self.sock = None
        self.buf = b""

    def open(self):
        if not hasattr(socket, "AF_BLUETOOTH"):
            raise OSError("Python không hỗ trợ AF_BLUETOOTH trên máy này")
        s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
        s.settimeout(8)
        s.connect((self.mac, self.channel))
        s.settimeout(1)
        self.sock = s

    def read_line(self):
        if b"\n" not in self.buf:
            try:
                chunk = self.sock.recv(256)
            except socket.timeout:
                return None
            if not chunk:
                raise ConnectionError("Bluetooth đã ngắt")
            self.buf += chunk
        if b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            return line.decode("ascii", "ignore").strip()
        return None

    def write(self, text):
        self.sock.sendall(text.encode("ascii"))

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass


class SerialTransport:
    def __init__(self, port, baud):
        self.port, self.baud = port, baud
        self.ser = None

    def open(self):
        import serial
        self.ser = serial.Serial(self.port, self.baud, timeout=1)

    def read_line(self):
        raw = self.ser.readline()
        if not raw or not raw.endswith(b"\n"):
            return None
        return raw.decode("ascii", "ignore").strip()

    def write(self, text):
        self.ser.write(text.encode("ascii"))

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass


class SimTransport:
    """Arduino giả lập để chạy thử dashboard khi chưa có phần cứng (--simulate)."""

    def __init__(self):
        self.s = dict(temp=29.0, hum=68.0, light=60.0, soil=55.0, water=90.0, pump=0, vent=0,
                      lamp=0, mode=1, tmax=32, soil_min=40, light_min=35, low=0, water_until=0)
        self.out = []
        self.t0 = time.time()

    def open(self):
        self.out.append("E,BOOT,0")

    def _set(self, key, val, on_name, off_name, ref):
        if self.s[key] != val:
            self.s[key] = val
            self.out.append("E,%s,%d" % (on_name if val else off_name, ref))

    def read_line(self):
        if self.out:
            return self.out.pop(0)
        time.sleep(1)
        s = self.s
        t = time.time() - self.t0
        # Môi trường giả lập: nhiệt độ + ánh sáng dao động chậm, đất khô dần, bơm làm ẩm đất và hao nước
        s["temp"] = 30 + 3.5 * math.sin(t / 60) + random.uniform(-0.3, 0.3) - (1.0 if s["vent"] else 0)
        s["hum"] = max(40, min(95, s["hum"] + random.uniform(-0.6, 0.6)))
        s["light"] = max(0, min(100, 50 + 40 * math.sin(t / 45) + random.uniform(-2, 2)))
        s["soil"] = max(0, min(100, s["soil"] + (2.5 if s["pump"] else -0.6)))
        if s["pump"]:
            s["water"] = max(0, s["water"] - 1.2)
        if s["water"] < 5 and random.random() < 0.05:
            s["water"] = 95                       # "châm nước" lại
        temp, soil, light, water = round(s["temp"]), round(s["soil"]), round(s["light"]), round(s["water"])
        if not s["low"] and water < 15:
            s["low"] = 1
            self.out.append("E,TANK_LOW,%d" % water)
        elif s["low"] and water > 20:
            s["low"] = 0
            self.out.append("E,TANK_OK,%d" % water)
        if s["low"]:
            self._set("pump", 0, "PUMP_ON", "PUMP_OFF", soil)
        if s["water_until"]:                          # tưới hẹn giờ (WATER=n)
            if s["low"] or time.time() >= s["water_until"]:
                s["water_until"] = 0
                self._set("pump", 0, "PUMP_ON", "PUMP_OFF", soil)
                self.out.append("E,WATER_DONE,%d" % soil)
            else:
                self._set("pump", 1, "PUMP_ON", "PUMP_OFF", soil)
        if s["mode"]:
            if temp >= s["tmax"]:
                self._set("vent", 1, "VENT_OPEN", "VENT_CLOSE", temp)
            elif temp <= s["tmax"] - 2:
                self._set("vent", 0, "VENT_OPEN", "VENT_CLOSE", temp)
            if light < s["light_min"]:
                self._set("lamp", 1, "LAMP_ON", "LAMP_OFF", light)
            elif light > s["light_min"] + 10:
                self._set("lamp", 0, "LAMP_ON", "LAMP_OFF", light)
            if s["water_until"]:
                pass
            elif soil < s["soil_min"] and not s["low"]:
                self._set("pump", 1, "PUMP_ON", "PUMP_OFF", soil)
            elif soil >= s["soil_min"] + 10:
                self._set("pump", 0, "PUMP_ON", "PUMP_OFF", soil)
        alarm = (1 if s["low"] else 0) | (2 if temp >= s["tmax"] + 5 else 0)
        return "D,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d" % (
            temp, round(s["hum"]), light, soil, water, s["pump"], s["vent"], s["lamp"],
            s["mode"], alarm, s["tmax"], s["soil_min"], s["light_min"])

    def write(self, text):
        cmd = text.strip().upper()
        key, _, val = cmd.partition("=")
        s, ok = self.s, True
        num = int(val) if val.isdigit() else -1
        if key == "MODE" and val in ("AUTO", "MANUAL"):
            s["mode"] = 1 if val == "AUTO" else 0
        # Lệnh bật/tắt tay: nếu đang AUTO thì tự chuyển sang MANUAL (giống Arduino)
        elif key == "PUMP" and val in ("ON", "OFF") and not (val == "ON" and s["low"]):
            s["mode"] = 0
            self._set("pump", int(val == "ON"), "PUMP_ON", "PUMP_OFF", round(s["soil"]))
        elif key == "VENT" and val in ("OPEN", "CLOSE"):
            s["mode"] = 0
            self._set("vent", int(val == "OPEN"), "VENT_OPEN", "VENT_CLOSE", round(s["temp"]))
        elif key == "LAMP" and val in ("ON", "OFF"):
            s["mode"] = 0
            self._set("lamp", int(val == "ON"), "LAMP_ON", "LAMP_OFF", round(s["light"]))
        elif key == "WATER" and 3 <= num <= 60 and not s["low"]:
            s["water_until"] = time.time() + num
            self._set("pump", 1, "PUMP_ON", "PUMP_OFF", round(s["soil"]))
            self.out.append("E,WATER_START,%d" % num)
        elif key == "TMAX" and 20 <= num <= 50:
            s["tmax"] = num
        elif key == "SOIL" and 5 <= num <= 90:
            s["soil_min"] = num
        elif key == "LIGHT" and 5 <= num <= 90:
            s["light_min"] = num
        elif key in ("BEEP", "PING"):
            pass
        else:
            ok = False
        self.out.append("A,%s,%s" % (cmd, "OK" if ok else "ERR"))

    def close(self):
        pass


class LinkWorker(threading.Thread):
    def __init__(self, name, factory, on_line):
        super().__init__(daemon=True, name="link-" + name)
        self.link_name = name
        self.factory = factory
        self.on_line = on_line
        self.transport = None
        self.connected = False
        self.last_data = 0.0
        self.error = "chưa kết nối"
        self.wlock = threading.Lock()

    @property
    def alive(self):
        return self.connected and time.time() - self.last_data < ALIVE_SECONDS

    def send(self, text):
        with self.wlock:
            if not self.connected:
                raise ConnectionError("kênh %s chưa kết nối" % self.link_name)
            self.transport.write(text)

    def run(self):
        while True:
            t = self.factory()
            try:
                t.open()
                self.transport, self.connected, self.error = t, True, ""
                opened = time.time()
                while True:
                    line = t.read_line()
                    now = time.time()
                    if line:
                        self.last_data = now
                        self.on_line(self.link_name, line)
                    elif now - max(self.last_data, opened) > STALE_SECONDS:
                        raise TimeoutError("không nhận được dữ liệu")
            except Exception as exc:          # mất kết nối -> thử lại
                self.error = str(exc) or exc.__class__.__name__
            finally:
                self.connected = False
                t.close()
            time.sleep(RETRY_SECONDS)


class LinkManager:
    """Quản lý các kênh theo thứ tự ưu tiên và chuyển kênh tự động."""

    def __init__(self, on_line, on_switch=None):
        self.workers = []
        self._on_line = on_line
        self._on_switch = on_switch
        self._active = None
        self._lock = threading.Lock()

    def add(self, name, factory):
        self.workers.append(LinkWorker(name, factory, self._dispatch))

    def start(self):
        for w in self.workers:
            w.start()
        threading.Thread(target=self._watch, daemon=True, name="link-watch").start()

    def active(self):
        for w in self.workers:
            if w.alive:
                return w
        return None

    def active_name(self):
        w = self.active()
        return w.link_name if w else "offline"

    def _watch(self):                  # ghi nhận sự kiện chuyển kênh
        while True:
            name = self.active_name()
            if name != self._active:
                old, self._active = self._active, name
                if self._on_switch and old is not None:
                    self._on_switch(old, name)
            time.sleep(0.5)

    def _dispatch(self, name, line):
        with self._lock:
            w = self.active()
            if w is None or w.link_name == name:
                self._on_line(name, line)

    def send(self, cmd):
        w = self.active()
        if w is None:
            raise ConnectionError("Không có kết nối tới Arduino")
        w.send(cmd.strip() + "\n")
        return w.link_name

    def status(self):
        return [{"name": w.link_name, "connected": w.connected, "alive": w.alive,
                 "error": w.error} for w in self.workers]


class SimWaterTransport:
    """Arduino tưới (servo) giả lập cho chế độ --simulate."""

    def __init__(self):
        self.on, self.angle, self.dir, self.out = 0, 0, 1, []

    def open(self):
        self.out.append("E,WBOOT,0")

    def read_line(self):
        if self.out:
            return self.out.pop(0)
        time.sleep(0.5)
        if self.on:
            self.angle += self.dir * 32
            if self.angle >= 140:
                self.angle, self.dir = 140, -1
            if self.angle <= 40:
                self.angle, self.dir = 40, 1
        return "W,%d,%d" % (self.on, self.angle)

    def write(self, text):
        cmd = text.strip().upper()
        if cmd in ("WATER=ON", "WATER=OFF"):
            self.on = int(cmd.endswith("ON"))
            self.angle = 40 if self.on else 0
            self.out.append("A,%s,OK" % cmd)

    def close(self):
        pass


class WaterNode:
    """Arduino thứ hai (servo tưới cây), nối qua một cổng Serial riêng.

    Gateway là bên điều phối: `desired` được đặt theo trạng thái bơm của Arduino chính,
    và được gửi lặp lại mỗi 2 giây (Arduino tưới tự dừng nếu 15 giây không nhận lại lệnh).
    """

    def __init__(self, factory, main_online, on_event=None):
        self.worker = LinkWorker("water", factory, self._on_line)
        self.main_online = main_online      # hàm trả về True khi Arduino chính còn kết nối
        self.on_event = on_event
        self.desired = False
        self.on = 0
        self.angle = 0
        self.wrong_port = False

    def start(self):
        self.worker.start()
        threading.Thread(target=self._sender, daemon=True, name="water-sender").start()

    def _on_line(self, _name, line):
        p = line.split(",")
        try:
            if p[0] == "W" and len(p) >= 3:
                on, angle = int(p[1]), int(p[2])
                if on != self.on and self.on_event:
                    self.on_event("WSERVO_ON" if on else "WSERVO_OFF", "góc=%d" % angle)
                self.on, self.angle, self.wrong_port = on, angle, False
            elif p[0] == "E" and len(p) >= 2 and p[1] == "WFAILSAFE" and self.on_event:
                self.on_event("WFAILSAFE", "servo tưới tự dừng vì mất lệnh")
            elif p[0] == "D":
                self.wrong_port = True       # đây là cổng của Arduino chính: hai cổng đang bị đặt ngược
        except ValueError:
            pass

    def _sender(self):
        last, sent_at = None, 0.0
        while True:
            want = bool(self.desired and self.main_online())
            if not self.worker.connected:
                last = None
            elif want != last or time.time() - sent_at >= 2:
                try:
                    self.worker.send("WATER=ON\n" if want else "WATER=OFF\n")
                    last, sent_at = want, time.time()
                except Exception:
                    last = None
            time.sleep(0.2)

    def status(self):
        w = self.worker
        return {"connected": w.connected, "alive": w.alive, "on": self.on, "angle": self.angle,
                "wrong_port": self.wrong_port, "error": w.error}
