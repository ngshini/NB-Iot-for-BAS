import argparse
import json
import os
import socket
import subprocess
import sys
import time
import webbrowser
import urllib.request
from pathlib import Path
from common import ROOT, RUNTIME, CREATE_FLAGS, load_config, mqtt_args


def port_busy(port):
    with socket.socket() as check:
        try:
            check.bind(('127.0.0.1', port))
            return False
        except OSError as exc:
            if exc.errno in (98, 10048) or getattr(exc, 'winerror', None) == 10048:
                return True
            raise


def compatible_dashboard(config):
    try:
        url = f'http://127.0.0.1:{config["http_port"]}'
        with urllib.request.urlopen(url + '/api/telemetry', timeout=3) as response:
            data = json.load(response)
        with urllib.request.urlopen(url + '/', timeout=3) as response:
            page = response.read(100000).decode('utf-8')
        return (data.get('topic') == config['topic'] and
                isinstance(data.get('messages'), list) and
                'subscriber_running' in data and 'SIM7022' in page and '/api/telemetry' in page)
    except (OSError, ValueError, AttributeError):
        return False


def broker_available(config):
    # Verify subscription/authentication without publishing any data.
    try:
        result = subprocess.run(mqtt_args(config, 'mosquitto_sub') + ['-t', config['topic'], '-E'],
                                timeout=5, capture_output=True, creationflags=CREATE_FLAGS)
        return result.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def startup_plan(config):
    if config['mqtt_port'] == config['http_port']:
        raise RuntimeError('mqtt_port and http_port must be different in config.local.json.')
    http_busy = port_busy(config['http_port'])
    mqtt_busy = port_busy(config['mqtt_port'])
    if http_busy:
        raise RuntimeError(f'HTTP port {config["http_port"]} is used by another application. '
                           'Choose another http_port in config.local.json and run setup.ps1 again.')
    if mqtt_busy:
        raise RuntimeError(f'MQTT port {config["mqtt_port"]} is still occupied after stopping project processes. '
                           'Check the other application or choose another mqtt_port and run setup.ps1 again.')
    return True, True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    config = load_config()
    processes = []
    logs = []
    try:
        print('Stopping previous project processes...', flush=True)
        stopped = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                  '-File', str(ROOT / 'windows/stop.ps1'), '-KeepPid', str(os.getpid())],
                                 creationflags=CREATE_FLAGS)
        if stopped.returncode:
            raise RuntimeError('Could not stop old project processes. See the stop error above.')
        start_broker, start_dashboard = startup_plan(config)
        url = f'http://127.0.0.1:{config["http_port"]}'
        if not start_broker and not start_dashboard:
            print(f'Project is already running: {url}', flush=True)
            print('Reusing the existing server. No extra broker/dashboard was started.')
            if not args.no_browser:
                webbrowser.open(url)
            return 0
        RUNTIME.mkdir(exist_ok=True)
        for needed, name, command in [
            (start_broker, "broker", [str(Path(config["mosquitto_dir"]) / "mosquitto.exe"), "-c", str(RUNTIME / "mosquitto.conf")]),
            (start_dashboard, "dashboard", [sys.executable, str(ROOT / "windows/server.py")]),
        ]:
            if not needed:
                print(f'Reusing existing {name}.', flush=True)
                continue
            log = (RUNTIME / f"{name}.log").open("a", encoding="utf-8")
            logs.append(log)
            proc = subprocess.Popen(command, stdout=log, stderr=log, creationflags=CREATE_FLAGS)
            processes.append(proc)
            time.sleep(1)
            if proc.poll() is not None:
                raise RuntimeError(f"{name} failed; see runtime/{name}.log")
        url = f'http://127.0.0.1:{config["http_port"]}'
        print(f"Running: {url}\nCtrl+C to stop this project's broker and dashboard.", flush=True)
        if not args.no_browser:
            webbrowser.open(url)
        while all(proc.poll() is None for proc in processes):
            time.sleep(1)
        raise RuntimeError("A project process stopped. Check runtime/*.log")
    except KeyboardInterrupt:
        print("Stopping project...")
    except (OSError, RuntimeError) as exc:
        print(f'Cannot start project: {exc}', file=sys.stderr)
        return 1
    finally:
        # Kill only child processes started by this invocation, never an existing broker.
        for proc in reversed(processes):
            if proc.poll() is None:
                # server.py owns a subscriber child; taskkill /T also closes that child.
                if sys.platform == "win32":
                    subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                                   capture_output=True, creationflags=CREATE_FLAGS)
                else:
                    proc.terminate()
                proc.wait(timeout=10)
        for log in logs:
            log.close()


if __name__ == "__main__":
    raise SystemExit(main())
