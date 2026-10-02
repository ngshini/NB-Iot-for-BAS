import argparse
import json
import re
import secrets
import subprocess
from pathlib import Path
from common import ROOT, RUNTIME, CREATE_FLAGS


def configure():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", help="Public TCP tunnel hostname, WITHOUT tcp://")
    parser.add_argument("--port", type=int)
    parser.add_argument("--apn")
    args = parser.parse_args()
    local = ROOT / "config.local.json"
    config = json.loads((local if local.exists() else ROOT / "config.example.json").read_text(encoding="utf-8-sig"))
    if args.host is not None:
        config["public_host"] = args.host
    if args.port is not None:
        config["public_port"] = args.port
    if args.apn is not None:
        config["apn"] = args.apn
    if not config["password"]:
        config["password"] = secrets.token_hex(20)
    # AT string fields must not inject quotes, line breaks or command separators.
    for key in ("public_host", "apn", "username", "password", "topic"):
        if not re.fullmatch(r"[A-Za-z0-9_./:@+-]*", config[key]):
            raise SystemExit(f"Invalid characters in {key}; use ASCII letters/digits and _./:@+-")
    if config["public_host"] and not re.fullmatch(r"[A-Za-z0-9.-]+", config["public_host"]):
        raise SystemExit("public_host must be a hostname or IPv4 address without tcp://, path or port")
    for key in ("mqtt_port", "http_port", "public_port"):
        if not 1 <= config[key] <= 65535:
            raise SystemExit(f"Invalid {key}")
    if config["mqtt_host"] != "127.0.0.1":
        raise SystemExit("This local broker configuration requires mqtt_host=127.0.0.1")
    RUNTIME.mkdir(exist_ok=True)
    passwd = RUNTIME / "mqtt-passwords"
    # mosquitto_passwd -U hashes a temporary plaintext file; no password in process args.
    passwd.write_text(f'{config["username"]}:{config["password"]}\n', encoding="utf-8")
    exe = Path(config["mosquitto_dir"]) / "mosquitto_passwd.exe"
    try:
        subprocess.run([str(exe), "-U", str(passwd)], check=True, capture_output=True,
                       creationflags=CREATE_FLAGS)
    except Exception:
        passwd.unlink(missing_ok=True)
        raise
    conf = RUNTIME / "mosquitto.conf"
    conf.write_text(
        f'listener {config["mqtt_port"]} 127.0.0.1\n'
        'allow_anonymous false\n'
        f'password_file {passwd.as_posix()}\n'
        'persistence false\nlog_dest stdout\nlog_type error\nlog_type warning\nlog_type notice\n'
        'message_size_limit 4096\nmax_connections 20\n', encoding="utf-8")
    local.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    header = (ROOT / "firmware/sim7022_mqtt/config.example.h").read_text(encoding="utf-8")
    replacements = {
        "MODEM_RX": str(config["modem_rx"]), "MODEM_TX": str(config["modem_tx"]),
        "MODEM_BAUD": str(config["modem_baud"]), "VIETTEL_APN": json.dumps(config["apn"]),
        "MQTT_HOST": json.dumps(config["public_host"] or "CHANGE_ME_PUBLIC_HOST"),
        "MQTT_PORT": str(config["public_port"]), "MQTT_USER": json.dumps(config["username"]),
        "MQTT_PASSWORD": json.dumps(config["password"]), "MQTT_TOPIC": json.dumps(config["topic"]),
    }
    for name, value in replacements.items():
        header = re.sub(rf"^#define {name} .+$", lambda m: f"#define {name} {value}", header, flags=re.M)
    (ROOT / "firmware/sim7022_mqtt/config.h").write_text(header, encoding="utf-8")
    print("Generated local config, broker password file and firmware config.h.")
    print(f'Broker: 127.0.0.1:{config["mqtt_port"]}; dashboard: http://127.0.0.1:{config["http_port"]}')
    if not config["public_host"]:
        print("Public host is not configured yet; local simulation is ready.")
    if not config["apn"]:
        print("APN empty: firmware preserves the SIM7022 existing PDP profile.")


if __name__ == "__main__":
    configure()
