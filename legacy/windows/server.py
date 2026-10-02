"""Local read-only dashboard + SQLite collector using installed Mosquitto CLI."""
import json
import sqlite3
import subprocess
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from common import ROOT, RUNTIME, CREATE_FLAGS, load_config, mqtt_args

STATIC_ROUTES = {
    "/": ("dashboard.html", "text/html; charset=utf-8"),
    "/index.html": ("dashboard.html", "text/html; charset=utf-8"),
    "/serial": ("serial.html", "text/html; charset=utf-8"),
    "/serial.html": ("serial.html", "text/html; charset=utf-8"),
    "/serial_compare.js": ("serial_compare.js", "text/javascript; charset=utf-8"),
    "/serial_monitor.js": ("serial_monitor.js", "text/javascript; charset=utf-8"),
}


def decode_payload(payload):
    try:
        parsed = json.loads(payload)
        return parsed if isinstance(parsed, dict) else {"value": parsed}
    except (ValueError, TypeError):
        values = {}
        for part in payload.split(";"):
            key, sep, value = part.partition("=")
            if sep:
                try:
                    value = json.loads(value)
                except ValueError:
                    pass
                values[key] = value
        return values or {"raw": payload}


class Collector:
    def __init__(self, config):
        self.config = config
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.process = None
        self.error = ""
        self.last_received = None
        self.db_path = RUNTIME / "telemetry.sqlite3"
        with sqlite3.connect(self.db_path) as db:
            db.execute("CREATE TABLE IF NOT EXISTS telemetry (id INTEGER PRIMARY KEY, received TEXT, topic TEXT, payload TEXT)")

    def record(self, topic, payload):
        received = datetime.now(timezone.utc).isoformat()
        with sqlite3.connect(self.db_path) as db:
            db.execute("INSERT INTO telemetry(received,topic,payload) VALUES (?,?,?)", (received, topic, payload))
        with self.lock:
            self.last_received = received
            self.error = ""

    def run(self):
        while not self.stop.is_set():
            proc = None
            try:
                args = mqtt_args(self.config, "mosquitto_sub") + ["-t", self.config["topic"], "-q", "1", "-F", "%j"]
                with (RUNTIME / "subscriber.log").open("a", encoding="utf-8") as log:
                    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=log,
                                            text=True, encoding="utf-8", errors="replace",
                                            creationflags=CREATE_FLAGS)
                    with self.lock:
                        self.process = proc
                    for line in proc.stdout:
                        if self.stop.is_set():
                            break
                        try:
                            msg = json.loads(line)
                            if isinstance(msg.get("payload"), str):
                                self.record(msg["topic"], msg["payload"])
                        except (ValueError, KeyError):
                            continue
                    proc.stdout.close()
                    proc.wait()
                    with self.lock:
                        self.error = "Subscriber stopped; see runtime/subscriber.log"
            except (OSError, sqlite3.Error) as exc:
                with self.lock:
                    self.error = str(exc)
            finally:
                if proc is not None:
                    if proc.poll() is None:
                        proc.terminate()
                    proc.wait(timeout=5)
                    if proc.stdout is not None:
                        proc.stdout.close()
            self.stop.wait(3)

    def snapshot(self):
        with sqlite3.connect(self.db_path) as db:
            rows = db.execute("SELECT id,received,topic,payload FROM telemetry ORDER BY id DESC LIMIT 100").fetchall()
            count = db.execute("SELECT COUNT(*) FROM telemetry").fetchone()[0]
        with self.lock:
            status = {"subscriber_running": self.process is not None and self.process.poll() is None,
                      "error": self.error, "last_received": self.last_received}
        return {**status, "count": count, "topic": self.config["topic"], "messages": [
            {"id": row[0], "received": row[1], "topic": row[2], "payload": row[3], "data": decode_payload(row[3])}
            for row in rows]}

    def close(self):
        self.stop.set()
        with self.lock:
            if self.process and self.process.poll() is None:
                self.process.terminate()


def make_handler(collector):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/api/telemetry":
                content = json.dumps(collector.snapshot(), ensure_ascii=False).encode("utf-8")
                content_type = "application/json; charset=utf-8"
            elif self.path in STATIC_ROUTES:
                filename, content_type = STATIC_ROUTES[self.path]
                content = (ROOT / "windows" / filename).read_bytes()
            else:
                self.send_error(404); return
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(content)

        def log_message(self, *args):
            pass

    return Handler


def main():
    config = load_config()
    RUNTIME.mkdir(exist_ok=True)
    collector = Collector(config)

    httpd = ThreadingHTTPServer(("127.0.0.1", config["http_port"]), make_handler(collector))
    thread = threading.Thread(target=collector.run, daemon=True)
    thread.start()
    print(f'Dashboard: http://127.0.0.1:{config["http_port"]}', flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        collector.close(); thread.join(timeout=5); httpd.server_close()


if __name__ == "__main__":
    main()
