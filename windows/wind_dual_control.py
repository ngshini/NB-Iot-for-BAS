"""Control ATMC dual wind firmware over USB (requires pyserial).

configure-speed changes ES-WS-02 address 1 to 2: ONLY this sensor may be
connected to RS485 during that operation. Close the web COM connection first.
"""
import argparse
import json
import time
from pathlib import Path
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM11')
    parser.add_argument('action', choices=['configure-speed', 'start', 'stop', 'monitor'])
    parser.add_argument('--seconds', type=float, default=15)
    parser.add_argument('--speed-only-connected', action='store_true')
    args = parser.parse_args()
    if args.action == 'configure-speed' and not args.speed_only_connected:
        parser.error('Disconnect ES-WS-04; confirm with --speed-only-connected')
    log_dir = Path(__file__).resolve().parent.parent / 'runtime'
    log_dir.mkdir(exist_ok=True)
    log_path = log_dir / time.strftime('wind-dual-%Y%m%d-%H%M%S.log')
    good = {'ES-WS-02': [], 'ES-WS-04': []}
    configured = False
    with serial.Serial(port=None, baudrate=115200, timeout=0.4) as port:
        port.dtr = False
        port.rts = False
        port.port = args.port
        port.open()
        time.sleep(1)
        if args.action == 'configure-speed':
            port.write(b'STOP\n'); port.flush(); time.sleep(1)
            port.write(b'SET_SPEED_ADDRESS_2\n')
        elif args.action == 'start':
            port.write(b'START\n')
        elif args.action == 'stop':
            port.write(b'STOP\n')
        end = time.monotonic() + args.seconds
        with log_path.open('w', encoding='utf-8') as log:
            while time.monotonic() < end:
                line = port.readline().decode('utf-8', errors='replace').strip()
                if not line:
                    continue
                print(line, flush=True); log.write(line + '\n')
                configured |= 'SPEED_ADDRESS_2_OK' in line
                if line.startswith(('[ESWS02] ', '[ESWS04] ')):
                    try:
                        data = json.loads(line[9:])
                    except json.JSONDecodeError:
                        continue
                    if data.get('status') == 'ok' and data.get('sensor') in good:
                        good[data['sensor']].append(data)
    print('Log:', log_path)
    print('Valid samples:', {key: len(value) for key, value in good.items()})
    if args.action == 'configure-speed':
        return 0 if configured else 1
    if args.action in ('start', 'monitor'):
        return 0 if all(good.values()) else 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
