"""Watch (and optionally self-test) the BAS MQTT topic on broker.emqx.io over TLS.

Standard library only. Uses the same root CA file and TLS 1.2 as the SIM7022 firmware,
so a successful run also proves that CA and TLS settings match the broker.

  py windows\\bas_mqtt_check.py                      # subscribe and print device messages
  py windows\\bas_mqtt_check.py --self-test          # publish+receive on a private test topic

QoS 0 has no PUBACK: a firmware "Publish OK" only means the modem sent the packet.
Seeing the message here is the confirmation that it reached the broker.
"""
import argparse
import json
import secrets
import socket
import ssl
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CA = ROOT / 'firmware' / 'bas_sim7022_mqtts' / 'DigiCertGlobalRootG2.pem'
BAS_FIELDS = {'distance', 'sternDistance', 'bowSpeed', 'sternSpeed', 'angle', 'waterLevel',
              'waterFlow', 'waterDirection', 'windForce', 'windDirection'}
SAMPLE = {'distance': 120.5, 'sternDistance': 118.2, 'bowSpeed': 2.1, 'sternSpeed': 1.8, 'angle': 1.2,
          'waterLevel': 4.2, 'waterFlow': 0.3, 'waterDirection': 'NE', 'windForce': 15,
          'windDirection': 'NE'}
CONNACK = {0: 'accepted', 1: 'unacceptable protocol version', 2: 'identifier rejected',
           3: 'server unavailable', 4: 'bad user name or password', 5: 'not authorized'}


def utf8(text):
    data = text.encode('utf-8')
    return struct.pack('!H', len(data)) + data


def packet(first_byte, body):
    length, encoded = len(body), bytearray()
    while True:
        digit, length = length % 128, length // 128
        encoded.append(digit | (0x80 if length else 0))
        if not length:
            return bytes([first_byte]) + bytes(encoded) + body


def read_exact(sock, count):
    data = b''
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise ConnectionError('broker closed the connection')
        data += chunk
    return data


def read_packet(sock):
    first = read_exact(sock, 1)[0]
    length, shift = 0, 0
    while True:
        digit = read_exact(sock, 1)[0]
        length += (digit & 0x7F) << shift
        shift += 7
        if not digit & 0x80:
            break
    return first, read_exact(sock, length) if length else b''


def connect(args, client_id):
    context = ssl.create_default_context(cafile=str(args.cafile))
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2  # as the modem
    raw = socket.create_connection((args.host, args.port), timeout=30)
    sock = context.wrap_socket(raw, server_hostname=args.host)
    cert = sock.getpeercert()
    subject = dict(item[0] for item in cert['subject'])
    print(f'TLS OK: {sock.version()} {sock.cipher()[0]}, server cert {subject.get("commonName")}, '
          f'expires {cert["notAfter"]}')
    # MQTT 3.1.1, clean session, no credentials, same keep-alive as the firmware.
    body = utf8('MQTT') + bytes([4, 0x02]) + struct.pack('!H', args.keepalive) + utf8(client_id)
    sock.sendall(packet(0x10, body))
    kind, data = read_packet(sock)
    if kind != 0x20 or len(data) != 2 or data[1] != 0:
        code = data[1] if len(data) == 2 else -1
        raise ConnectionError(f'CONNACK refused: {code} {CONNACK.get(code, "")}')
    print(f'MQTT 3.1.1 connected as {client_id}')
    return sock


def subscribe(sock, topic):
    sock.sendall(packet(0x82, struct.pack('!H', 1) + utf8(topic) + b'\x00'))
    kind, data = read_packet(sock)
    if kind != 0x90 or data[-1] == 0x80:
        raise ConnectionError('SUBSCRIBE rejected')
    print(f'Subscribed to {topic} (QoS 0)')


def publish(sock, topic, text):
    sock.sendall(packet(0x30, utf8(topic) + text.encode('utf-8')))  # QoS 0, retain 0


def describe(payload):
    try:
        data = json.loads(payload)
    except ValueError as exc:
        return f'INVALID JSON ({exc})'
    if not isinstance(data, dict):
        return 'JSON is not an object'
    notes = []
    unknown = sorted(set(data) - BAS_FIELDS)
    if unknown:
        notes.append('unexpected fields: ' + ', '.join(unknown))
    missing = sorted(BAS_FIELDS - set(data))
    if missing and data.keys() != {'distance'}:
        notes.append('missing: ' + ', '.join(missing))
    return 'valid JSON' + ('; ' + '; '.join(notes) if notes else '')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--host', default='broker.emqx.io')
    parser.add_argument('--port', type=int, default=8883)
    parser.add_argument('--topic', default='bas/BAS_TEST_001/telemetry')
    parser.add_argument('--cafile', type=Path, default=DEFAULT_CA)
    parser.add_argument('--keepalive', type=int, default=60)
    parser.add_argument('--seconds', type=int, default=0, help='stop after N seconds (0 = until Ctrl+C)')
    parser.add_argument('--self-test', action='store_true',
                        help='publish the sample payload to a random private topic and wait for it')
    args = parser.parse_args()

    # Never reuse the device client id: the broker would disconnect the device.
    client_id = 'bas-monitor-' + secrets.token_hex(4)
    topic = f'bas-selftest/{secrets.token_hex(6)}/telemetry' if args.self_test else args.topic
    sock = connect(args, client_id)
    subscribe(sock, topic)
    if args.self_test:
        text = json.dumps(SAMPLE, separators=(',', ':'))
        publish(sock, topic, text)
        print(f'Published sample to {topic}')
    deadline = time.monotonic() + (args.seconds or (20 if args.self_test else 0))
    sock.settimeout(5)
    last_ping = time.monotonic()
    while not args.seconds and not args.self_test or time.monotonic() < deadline:
        if time.monotonic() - last_ping > args.keepalive / 2:
            sock.sendall(b'\xc0\x00')
            last_ping = time.monotonic()
        try:
            kind, data = read_packet(sock)
        except (socket.timeout, TimeoutError):
            continue
        if kind & 0xF0 != 0x30:
            continue
        size = struct.unpack('!H', data[:2])[0]
        offset = 2 + size + (2 if kind & 0x06 else 0)
        received_topic = data[2:2 + size].decode('utf-8', 'replace')
        text = data[offset:].decode('utf-8', 'replace')
        stamp = time.strftime('%H:%M:%S')
        print(f'{stamp} {received_topic} {text}\n         -> {describe(text)}')
        if args.self_test and received_topic == topic:
            ok = json.loads(text) == SAMPLE
            print('SELF-TEST PASSED' if ok else 'SELF-TEST FAILED: payload changed')
            sock.sendall(b'\xe0\x00')
            return 0 if ok else 1
    sock.sendall(b'\xe0\x00')
    if args.self_test:
        print('SELF-TEST FAILED: message not received')
        return 1
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
    except (OSError, ConnectionError, ssl.SSLError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        sys.exit(1)
