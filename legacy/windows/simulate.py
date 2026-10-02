import argparse
import json
import math
import time
from common import load_config, publish

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Publish simulated data; does NOT use NB-IoT")
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--interval", type=float, default=2)
    args = parser.parse_args()
    config = load_config()
    for n in range(args.count):
        data = {"device": "windows-simulator", "seq": n + 1, "simulated": True,
                "temperature": round(27 + math.sin(n / 3) * 2, 1), "humidity": 65}
        publish(config, json.dumps(data))
        print(f"Sent {n + 1}/{args.count}", flush=True)
        if n + 1 < args.count:
            time.sleep(max(0, args.interval))
