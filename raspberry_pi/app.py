#!/usr/bin/env python3
"""SMART GREENHOUSE - Gateway + Web Dashboard (Raspberry Pi 3B+)

Chạy thật   : BT_MAC=98:D3:xx:xx:xx:xx python3 app.py
Chạy thử PC : python3 app.py --simulate      (không cần Arduino / Bluetooth / GPIO)
Dashboard   : http://<ip-của-pi>:5000
"""
import argparse
import csv
import io
import re
import threading
import time

from flask import Flask, Response, jsonify, render_template, request

import config
import db
from link import (BluetoothTransport, LinkManager, SerialTransport, SimTransport,
                  SimWaterTransport, WaterNode)
from tm1637 import TM1637

app = Flask(__name__)

state = {}                      # bản tin D mới nhất (đã giải mã)
state_lock = threading.Lock()
last_logged = {"t": 0.0, "key": None}
display = None
links = None
water = None                    # Arduino thứ hai (servo tưới), None nếu không dùng

# Thứ tự các trường trong bản tin "D,..." của Arduino
FIELDS = ("temp", "hum", "light", "soil", "water", "pump", "vent", "lamp",
          "mode", "alarm", "tmax", "soil_min", "light_min")

# Chỉ cho phép các lệnh hợp lệ đi xuống Arduino
CMD_RE = re.compile(r"^(MODE=(AUTO|MANUAL)|PUMP=(ON|OFF)|VENT=(OPEN|CLOSE)|LAMP=(ON|OFF)|"
                    r"TMAX=\d{2}|SOIL=\d{1,2}|LIGHT=\d{1,2}|WATER=\d{1,2}|BEEP|PING)$")


# ----------------------------- Xử lý dữ liệu từ Arduino --------------------
def on_line(link_name, line):
    parts = line.split(",")
    kind = parts[0]
    if kind == "D":
        handle_data(link_name, parts[1:])
    elif kind == "E" and len(parts) >= 2:
        db.insert_event(parts[1], "giá trị=%s" % (parts[2] if len(parts) > 2 else "?"))
    elif kind == "A" and len(parts) >= 3:
        db.ack_command(parts[1], parts[2])


def handle_data(link_name, values):
    if len(values) != len(FIELDS):
        return                                   # bản tin lỗi / thiếu trường -> bỏ qua
    try:
        r = dict(zip(FIELDS, (int(v) for v in values)))
    except ValueError:
        return
    # Xử lý tại Edge: loại giá trị không hợp lệ, tách cờ cảnh báo
    if r["temp"] < 0 or r["hum"] < 0:
        r["temp"] = r["hum"] = None              # DHT11 chưa đọc được
    r["tank_low"] = bool(r["alarm"] & 1)
    r["too_hot"] = bool(r["alarm"] & 2)
    r["link"] = link_name
    r["ts"] = time.strftime("%H:%M:%S")
    with state_lock:
        state.clear()
        state.update(r)

    # Ghi SQLite: ngay khi trạng thái thiết bị đổi, hoặc định kỳ LOG_INTERVAL giây
    key = (r["pump"], r["vent"], r["lamp"], r["mode"], r["alarm"])
    now = time.time()
    if key != last_logged["key"] or now - last_logged["t"] >= config.LOG_INTERVAL:
        last_logged.update(t=now, key=key)
        db.insert_reading(r, link_name)

    if water is not None:
        water.desired = bool(r["pump"])         # bơm chạy -> servo tưới quét ; bơm tắt -> nghỉ

    update_display(r)


def update_display(r=None):
    """LED 4 số: nhiệt độ (vd "29*C"), "LO  " khi bồn cạn, "----" khi mất kết nối."""
    if display is None:
        return
    if r is None:
        display.show("----")
    elif r["tank_low"] and int(time.time()) % 4 < 2:
        display.show("LO  ")                     # xen kẽ cảnh báo bồn cạn với nhiệt độ
    elif r["temp"] is None:
        display.show("--*C")
    else:
        display.show("%2d*C" % r["temp"])


def on_switch(old, new):
    db.insert_event("LINK", "%s -> %s" % (old, new))
    if new == "offline":
        update_display(None)


# ----------------------------- Web ----------------------------------------
@app.route("/")
def index():
    return render_template("dashboard.html")


@app.route("/api/status")
def api_status():
    with state_lock:
        s = dict(state)
    active = links.active_name()
    online = active != "offline" and bool(s)
    return jsonify(online=online, link=active, links=links.status(), data=s if online else None,
                   water_node=water.status() if water else None,
                   today=db.stats_today(), rows=db.count_readings())


@app.route("/api/chart")
def api_chart():
    minutes = min(max(request.args.get("minutes", 15, type=int), 1), 1440)
    rows = db.chart(minutes)
    fmt = "%H:%M:%S" if minutes <= 60 else "%H:%M"
    for r in rows:
        r["label"] = time.strftime(fmt, time.localtime(r["t"]))
    return jsonify(rows)


@app.route("/api/history")
def api_history():
    limit = min(request.args.get("limit", 30, type=int), 500)
    return jsonify(readings=db.history(limit), events=db.events(limit), commands=db.commands(limit))


@app.route("/api/command", methods=["POST"])
def api_command():
    cmd = str((request.get_json(silent=True) or {}).get("cmd", "")).strip().upper()
    if not CMD_RE.match(cmd):
        return jsonify(ok=False, error="Lệnh không hợp lệ"), 400
    # Ghi lệnh trước khi gửi để phản hồi (A,...) của Arduino luôn tìm thấy bản ghi
    db.insert_command(cmd, links.active_name())
    try:
        link_name = links.send(cmd)
    except Exception as exc:
        db.ack_command(cmd, "ERR")
        return jsonify(ok=False, error=str(exc)), 503
    return jsonify(ok=True, cmd=cmd, link=link_name)


@app.route("/export.csv")
def export_csv():
    rows = db.all_readings()
    out = io.StringIO()
    if rows:
        w = csv.DictWriter(out, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    return Response(out.getvalue(), mimetype="text/csv",
                    headers={"Content-Disposition": "attachment; filename=greenhouse_readings.csv"})


# ----------------------------- Khởi động ----------------------------------
def main():
    global display, links, water
    ap = argparse.ArgumentParser(description="Smart Greenhouse gateway")
    ap.add_argument("--simulate", action="store_true", help="dùng Arduino giả lập")
    ap.add_argument("--port", type=int, default=config.PORT)
    args = ap.parse_args()

    db.init(config.DB_PATH)
    links = LinkManager(on_line, on_switch)
    main_online = lambda: links.active_name() != "offline"
    if args.simulate:
        links.add("simulator", SimTransport)
        water = WaterNode(SimWaterTransport, main_online, db.insert_event)
    else:
        if config.USE_TM1637:
            display = TM1637(config.TM1637_CLK, config.TM1637_DIO)
            update_display(None)
        if config.BT_MAC:
            links.add("bluetooth", lambda: BluetoothTransport(config.BT_MAC, config.BT_CHANNEL))
        else:
            print("[!] Chưa đặt BT_MAC -> chỉ dùng Serial")
        links.add("serial", lambda: SerialTransport(config.SERIAL_PORT, config.BAUDRATE))
        if config.WATER_PORT:
            water = WaterNode(lambda: SerialTransport(config.WATER_PORT, config.BAUDRATE),
                              main_online, db.insert_event)
    links.start()
    if water is not None:
        water.start()
    db.insert_event("START", "gateway khởi động")
    app.run(host=config.HOST, port=args.port, threaded=True, use_reloader=False)


if __name__ == "__main__":
    main()
