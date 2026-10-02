"""Serve BAS Monitor and bridge MQTT/TLS to a same-origin JSON API.

The browser only talks to 127.0.0.1. This avoids corporate/home routers that
block MQTT-over-WebSocket ports while keeping the broker connection encrypted.
"""
from __future__ import annotations

import argparse
import json
import secrets
import socket
import ssl
import struct
import threading
import time
from collections import deque
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse


def mqtt_string(value: str) -> bytes:
    data = value.encode("utf-8")
    return struct.pack("!H", len(data)) + data


def mqtt_packet(first_byte: int, body: bytes) -> bytes:
    length = len(body)
    encoded = bytearray()
    while True:
        digit, length = length % 128, length // 128
        encoded.append(digit | (0x80 if length else 0))
        if not length:
            return bytes([first_byte]) + bytes(encoded) + body


def read_exact(sock: socket.socket, count: int) -> bytes:
    data = b""
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise ConnectionError("broker closed the connection")
        data += chunk
    return data


def read_packet(sock: socket.socket) -> tuple[int, bytes]:
    first = read_exact(sock, 1)[0]
    length = shift = 0
    while True:
        digit = read_exact(sock, 1)[0]
        length += (digit & 0x7f) << shift
        shift += 7
        if not digit & 0x80:
            break
    return first, read_exact(sock, length) if length else b""


class BrokerBridge:
    def __init__(self, host: str, port: int, topic: str, cafile: Path):
        self.host, self.port, self.topic, self.cafile = host, port, topic, cafile
        self.lock = threading.Lock()
        self.messages: deque[dict] = deque(maxlen=200)
        self.sequence = 0
        self.status = "starting"
        self.error = ""

    def snapshot(self, since: int) -> dict:
        with self.lock:
            return {
                "status": self.status,
                "error": self.error,
                "topic": self.topic,
                "sequence": self.sequence,
                "messages": [item for item in self.messages if item["sequence"] > since],
            }

    def set_status(self, status: str, error: str = "") -> None:
        with self.lock:
            self.status, self.error = status, error

    def add(self, topic: str, payload: str) -> None:
        with self.lock:
            self.sequence += 1
            self.messages.append({
                "sequence": self.sequence,
                "topic": topic,
                "payload": payload,
                "received_ms": int(time.time() * 1000),
            })

    def connect(self) -> ssl.SSLSocket:
        context = ssl.create_default_context(cafile=str(self.cafile))
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
        raw = socket.create_connection((self.host, self.port), timeout=30)
        sock = context.wrap_socket(raw, server_hostname=self.host)
        client_id = "bas-local-" + secrets.token_hex(4)
        body = mqtt_string("MQTT") + bytes([4, 0x02, 0, 60]) + mqtt_string(client_id)
        sock.sendall(mqtt_packet(0x10, body))
        kind, data = read_packet(sock)
        if kind != 0x20 or len(data) != 2 or data[1] != 0:
            raise ConnectionError("MQTT CONNECT rejected")
        sock.sendall(mqtt_packet(0x82, struct.pack("!H", 1) + mqtt_string(self.topic) + b"\x00"))
        kind, data = read_packet(sock)
        if kind != 0x90 or not data or data[-1] == 0x80:
            raise ConnectionError("MQTT SUBSCRIBE rejected")
        sock.settimeout(5)
        return sock

    def run(self) -> None:
        while True:
            sock = None
            try:
                self.set_status("connecting")
                sock = self.connect()
                self.set_status("connected")
                last_ping = time.monotonic()
                while True:
                    if time.monotonic() - last_ping > 25:
                        sock.sendall(b"\xc0\x00")
                        last_ping = time.monotonic()
                    try:
                        kind, data = read_packet(sock)
                    except socket.timeout:
                        continue
                    if kind & 0xf0 != 0x30 or len(data) < 2:
                        continue
                    size = struct.unpack("!H", data[:2])[0]
                    offset = 2 + size + (2 if kind & 0x06 else 0)
                    topic = data[2:2 + size].decode("utf-8", "replace")
                    payload = data[offset:].decode("utf-8", "replace")
                    self.add(topic, payload)
            except Exception as exc:
                self.set_status("error", str(exc))
                time.sleep(5)
            finally:
                if sock:
                    try:
                        sock.close()
                    except OSError:
                        pass


class Handler(SimpleHTTPRequestHandler):
    bridge: BrokerBridge

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/api/broker":
            try:
                since = int(parse_qs(parsed.query).get("since", ["0"])[0])
            except ValueError:
                since = 0
            content = json.dumps(self.bridge.snapshot(since), ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
            return
        super().do_GET()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8090)
    parser.add_argument("--site", type=Path, required=True)
    parser.add_argument("--cafile", type=Path, required=True)
    parser.add_argument("--topic", default="bas/BAS_WIND_001/telemetry")
    args = parser.parse_args()
    bridge = BrokerBridge("test.mosquitto.org", 8883, args.topic, args.cafile)
    Handler.bridge = bridge
    threading.Thread(target=bridge.run, daemon=True, name="mqtt-bridge").start()
    handler = partial(Handler, directory=str(args.site))
    server = ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    print(f"BAS Monitor: http://127.0.0.1:{args.port}/")
    print(f"MQTT TLS bridge: test.mosquitto.org:8883 -> {args.topic}")
    server.serve_forever()


if __name__ == "__main__":
    main()
