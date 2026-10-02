"""Integration test against a real, isolated Mosquitto broker on this machine."""
import json
import socket
import sqlite3
import subprocess
import sys
import threading
import time
import unittest
import urllib.request
import urllib.error
from http.server import ThreadingHTTPServer
from pathlib import Path
from common import ROOT, RUNTIME, CREATE_FLAGS, load_config, mqtt_args, publish
from server import Collector, decode_payload, make_handler


class MonitorHTTPTests(unittest.TestCase):
    def test_serial_page_assets_and_private_file_isolation(self):
        class StubCollector:
            def snapshot(self):
                return {"messages": [], "topic": "nbiot/test"}
        httpd = ThreadingHTTPServer(('127.0.0.1', 0), make_handler(StubCollector()))
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        base = f'http://127.0.0.1:{httpd.server_port}'
        try:
            for path, marker, mime in [
                ('/serial', 'serial_monitor.js', 'text/html'),
                ('/serial_compare.js', 'function compare', 'text/javascript'),
                ('/serial_monitor.js', 'navigator.serial.requestPort', 'text/javascript'),
                ('/', 'href="/serial"', 'text/html'),
            ]:
                with urllib.request.urlopen(base + path, timeout=3) as response:
                    self.assertEqual(response.headers.get_content_type(), mime)
                    self.assertIn(marker, response.read().decode('utf-8'))
            for path in ('/config.local.json', '/../config.local.json', '/server.py'):
                with self.assertRaises(urllib.error.HTTPError) as failure:
                    urllib.request.urlopen(base + path, timeout=3)
                self.assertEqual(failure.exception.code, 404)
            with urllib.request.urlopen(base + '/api/telemetry', timeout=3) as response:
                self.assertEqual(json.load(response)['topic'], 'nbiot/test')
        finally:
            httpd.shutdown(); thread.join(timeout=3); httpd.server_close()


class PayloadTests(unittest.TestCase):
    def test_firmware_payload(self):
        data = decode_payload("device=esp32-01;seq=3;temperature=25.3;humidity=65;simulated=1")
        self.assertEqual(data["device"], "esp32-01")
        self.assertEqual(data["temperature"], 25.3)
        self.assertEqual(data["simulated"], 1)

    def test_json_and_unstructured_payloads(self):
        self.assertEqual(decode_payload('{"seq":2}'), {"seq": 2})
        self.assertEqual(decode_payload("hello"), {"raw": "hello"})
        self.assertEqual(decode_payload("[1,2]"), {"value": [1, 2]})


class BrokerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = dict(load_config())
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            cls.config["mqtt_port"] = sock.getsockname()[1]
        cls.config["topic"] = "nbiot/integration-test/telemetry"
        cls.directory = RUNTIME / ("test-" + str(time.time_ns()))
        cls.directory.mkdir(parents=True)
        conf = (RUNTIME / "mosquitto.conf").read_text(encoding="utf-8")
        original_port = load_config()["mqtt_port"]
        conf = conf.replace(f"listener {original_port} ", f'listener {cls.config["mqtt_port"]} ')
        (cls.directory / "mosquitto.conf").write_text(conf, encoding="utf-8")
        cls.log = (cls.directory / "broker.log").open("w", encoding="utf-8")
        cls.broker = subprocess.Popen([str(Path(cls.config["mosquitto_dir"]) / "mosquitto.exe"),
                                      "-c", str(cls.directory / "mosquitto.conf")],
                                     stdout=cls.log, stderr=cls.log, creationflags=CREATE_FLAGS)
        for _ in range(50):
            if cls.broker.poll() is not None:
                cls.log.close()
                raise RuntimeError("Test broker failed: " + (cls.directory / "broker.log").read_text())
            try:
                with socket.create_connection(("127.0.0.1", cls.config["mqtt_port"]), timeout=.2):
                    break
            except OSError:
                time.sleep(.1)
        else:
            cls.broker.terminate(); cls.broker.wait(); cls.log.close()
            raise RuntimeError("Test broker readiness timeout")

    @classmethod
    def tearDownClass(cls):
        cls.broker.terminate(); cls.broker.wait(timeout=10); cls.log.close()

    def test_authenticated_mqtt_to_sqlite(self):
        collector = Collector(self.config)
        collector.db_path = self.directory / "messages.sqlite3"
        with sqlite3.connect(collector.db_path) as db:
            db.execute("CREATE TABLE telemetry (id INTEGER PRIMARY KEY, received TEXT, topic TEXT, payload TEXT)")
        thread = threading.Thread(target=collector.run, daemon=True)
        thread.start()
        payload = 'device=test;seq=42;temperature=28.5;simulated=1'
        try:
            deadline = time.monotonic() + 10
            # Repeated publication tolerates subscriber setup without relying on a fixed sleep.
            while time.monotonic() < deadline:
                publish(self.config, payload)
                if collector.snapshot()["count"]:
                    break
                time.sleep(.2)
            snapshot = collector.snapshot()
            self.assertGreater(snapshot["count"], 0)
            self.assertEqual(snapshot["messages"][0]["data"]["seq"], 42)
            self.assertEqual(snapshot["messages"][0]["payload"], payload)
            publish(self.config, '{"device":"json-test","seq":43,"simulated":true}')
            for _ in range(40):
                if collector.snapshot()["messages"][0]["data"].get("seq") == 43:
                    break
                time.sleep(.1)
            self.assertEqual(collector.snapshot()["messages"][0]["data"]["seq"], 43)
        finally:
            collector.close(); thread.join(timeout=5)

    def test_wrong_password_is_rejected(self):
        wrong = dict(self.config, password="deliberately-wrong-password")
        with self.assertRaises(RuntimeError):
            publish(wrong, "must-not-be-accepted")

    def test_anonymous_connection_is_rejected(self):
        exe = str(Path(self.config["mosquitto_dir"]) / "mosquitto_pub.exe")
        proc = subprocess.run([exe, "-h", "127.0.0.1", "-p", str(self.config["mqtt_port"]),
                               "-t", self.config["topic"], "-m", "anonymous"],
                              capture_output=True, timeout=10, creationflags=CREATE_FLAGS)
        self.assertNotEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
