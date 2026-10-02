"""Read real ES-WS-02 USB telemetry; release COM when the check finishes.

Requires pyserial. Example: python windows/check_wind_serial.py --port COM11
Close the browser's COM connection before running this command.
"""
import argparse
import json
import time
from pathlib import Path
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--seconds', type=float, default=20)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    log_dir = root / 'runtime'
    log_dir.mkdir(exist_ok=True)
    log_path = log_dir / time.strftime('esws02-%Y%m%d-%H%M%S.log')
    measurements = []
    with serial.Serial(port=None, baudrate=115200, timeout=0.5) as port:
        port.dtr = False
        port.rts = False
        port.port = args.port
        port.open()
        end = time.monotonic() + args.seconds
        with log_path.open('w', encoding='utf-8') as log:
            while time.monotonic() < end:
                line = port.readline().decode('utf-8', errors='replace').strip()
                if not line:
                    continue
                print(line, flush=True)
                log.write(line + '\n')
                if not line.startswith('[ESWS02] '):
                    continue
                try:
                    item = json.loads(line[9:])
                except json.JSONDecodeError:
                    continue
                if (item.get('sensor') == 'ES-WS-02' and item.get('status') == 'ok'
                        and isinstance(item.get('speed_mps'), (int, float))):
                    measurements.append(item)
    print('Log:', log_path)
    if not measurements:
        print('FAIL: no valid ES-WS-02 measurement received')
        return 1
    speeds = [item['speed_mps'] for item in measurements]
    print(f'PASS: {len(speeds)} measurements; min={min(speeds)} max={max(speeds)} m/s')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
