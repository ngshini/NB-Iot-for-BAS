"""Project configuration; uses only Python's standard library."""
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "runtime"
CREATE_FLAGS = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def load_config():
    path = ROOT / "config.local.json"
    if not path.exists():
        raise SystemExit("Run windows/setup.ps1 first.")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def mqtt_args(config, program):
    exe = Path(config["mosquitto_dir"]) / (program + (".exe" if os.name == "nt" else ""))
    if not exe.exists():
        raise FileNotFoundError(exe)
    return [str(exe), "-h", config["mqtt_host"], "-p", str(config["mqtt_port"]),
            "-u", config["username"], "-P", config["password"]]


def publish(config, payload, topic=None):
    args = mqtt_args(config, "mosquitto_pub") + ["-t", topic or config["topic"], "-q", "1", "-s"]
    result = subprocess.run(args, input=payload, text=True, encoding="utf-8",
                            capture_output=True, timeout=20, creationflags=CREATE_FLAGS)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "MQTT publish failed")
