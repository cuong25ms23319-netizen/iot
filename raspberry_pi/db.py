"""Lớp truy cập SQLite cho Smart Greenhouse."""
import os
import sqlite3
import threading
import time

_lock = threading.Lock()
_conn = None


def init(db_path):
    global _conn
    _conn = sqlite3.connect(db_path, check_same_thread=False)
    _conn.row_factory = sqlite3.Row
    _conn.execute("PRAGMA journal_mode=WAL")
    schema = os.path.join(os.path.dirname(os.path.abspath(__file__)), "schema.sql")
    with open(schema, encoding="utf-8") as f:
        _conn.executescript(f.read())
    _conn.commit()


def _now():
    t = time.time()
    return int(t), time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t))


def _exec(sql, args=()):
    with _lock:
        cur = _conn.execute(sql, args)
        _conn.commit()
        return cur


def _query(sql, args=()):
    with _lock:
        return [dict(r) for r in _conn.execute(sql, args).fetchall()]


def insert_reading(r, link):
    epoch, ts = _now()
    _exec(
        "INSERT INTO readings(epoch,ts,temp,hum,light,soil,water,pump,vent,lamp,mode,alarm,link) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (epoch, ts, r["temp"], r["hum"], r["light"], r["soil"], r["water"], r["pump"],
         r["vent"], r["lamp"], r["mode"], r["alarm"], link))


def insert_event(type_, detail=""):
    epoch, ts = _now()
    _exec("INSERT INTO events(epoch,ts,type,detail) VALUES(?,?,?,?)", (epoch, ts, type_, detail))


def insert_command(cmd, link):
    epoch, ts = _now()
    _exec("INSERT INTO commands(epoch,ts,cmd,link) VALUES(?,?,?,?)", (epoch, ts, cmd, link))


def ack_command(cmd, status):
    _exec("UPDATE commands SET status=? WHERE id=(SELECT id FROM commands "
          "WHERE cmd=? AND status='SENT' ORDER BY id ASC LIMIT 1)", (status, cmd))


def history(limit=50):
    return _query("SELECT * FROM readings ORDER BY id DESC LIMIT ?", (limit,))


def events(limit=50):
    return _query("SELECT ts,type,detail FROM events ORDER BY id DESC LIMIT ?", (limit,))


def commands(limit=50):
    return _query("SELECT ts,cmd,link,status FROM commands ORDER BY id DESC LIMIT ?", (limit,))


def chart(minutes, max_points=240):
    """Dữ liệu biểu đồ, gộp theo khoảng thời gian để không quá max_points điểm."""
    since = int(time.time()) - minutes * 60
    bucket = max(1, minutes * 60 // max_points)
    return _query(
        "SELECT (epoch/?)*? AS t, ROUND(AVG(temp),1) AS temp, ROUND(AVG(hum),1) AS hum, "
        "ROUND(AVG(light)) AS light, ROUND(AVG(soil)) AS soil, ROUND(AVG(water)) AS water "
        "FROM readings WHERE epoch>=? GROUP BY t ORDER BY t", (bucket, bucket, since))


def stats_today():
    day = time.strftime("%Y-%m-%d")
    rows = _query("SELECT type, COUNT(*) AS n FROM events WHERE ts LIKE ? GROUP BY type", (day + "%",))
    return {r["type"]: r["n"] for r in rows}


def all_readings():
    return _query("SELECT * FROM readings ORDER BY id")


def count_readings():
    return _query("SELECT COUNT(*) AS n FROM readings")[0]["n"]
