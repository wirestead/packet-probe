"""Drives packet-probe-web end to end over HTTP, the same way the browser does.

Starts the gateway (which spawns `packet-probe engine`), starts a UDP capture,
sends a datagram, waits for its event on the SSE stream, sends one back, stops,
and checks that stopping the gateway also stops its engine. Exit 0 = pass.

    python .claude/skills/run-packet-probe-viewer/driver.py [--python PY] [--cli PATH]
"""

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")
    sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--python", default=sys.executable, help="Python with wirestead-python installed")
    parser.add_argument("--cli", default="", help="packet-probe executable (default: auto-detect)")
    args = parser.parse_args()

    http_port, udp_port, peer_port = free_port(), free_port(), free_port()
    env = {**os.environ, "PYTHONPATH": str(REPO / "viewer")}
    cmd = [args.python, "-m", "packet_probe_viewer.gateway", "--port", str(http_port)]
    if args.cli:
        cmd += ["--cli", args.cli]
    gw = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    lines: list[str] = []
    threading.Thread(target=lambda: lines.extend(iter(gw.stdout.readline, "")), daemon=True).start()
    base = token = engine_addr = None
    for _ in range(100):
        out = "".join(lines)
        m = re.search(r"open (http://\S+)/\?token=(\S+)", out)
        e = re.search(r"engine (tcp:\S+)", out)
        if m and e:
            base, token, engine_addr = m.group(1), m.group(2), e.group(1)
            break
        if gw.poll() is not None:
            fail("gateway exited:\n" + out)
        time.sleep(0.1)
    if not base:
        fail("gateway did not print its URL:\n" + "".join(lines))
    print(f"gateway {base}  engine {engine_addr}")

    def command(body: dict) -> dict:
        req = urllib.request.Request(base + "/command", data=json.dumps(body).encode(),
                                     headers={"X-Token": token}, method="POST")
        with urllib.request.urlopen(req, timeout=10) as res:
            return json.loads(res.read())

    events: list[dict] = []
    def read_sse() -> None:
        with urllib.request.urlopen(f"{base}/events?token={token}", timeout=30) as res:
            for raw in res:
                line = raw.decode().strip()
                if line.startswith("data: "):
                    events.append(json.loads(line[len("data: "):]))
    threading.Thread(target=read_sse, daemon=True).start()

    def wait_for(pred, what: str, timeout: float = 5.0) -> dict:
        deadline = time.time() + timeout
        while time.time() < deadline:
            for ev in list(events):
                if pred(ev):
                    return ev
            time.sleep(0.05)
        fail(f"timed out waiting for {what}; got {[e.get('type') for e in events]}")

    try:
        wait_for(lambda e: e.get("type") == "gateway" and e.get("engine_connected"), "engine link")
        config = {"mode": "udp", "bind_host": "127.0.0.1", "bind_port": udp_port,
                  "target_host": "127.0.0.1", "target_port": peer_port}
        for body in ({"command": "configure", "config": config}, {"command": "start_capture"}):
            r = command(body)
            if not r.get("ok"):
                fail(f"{body['command']}: {r}")
        wait_for(lambda e: e.get("type") == "status" and e.get("engine_state") == "capturing", "capturing status")

        peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        peer.bind(("127.0.0.1", peer_port))
        peer.settimeout(5)
        peer.sendto(b"ping", ("127.0.0.1", udp_port))
        wait_for(lambda e: e.get("type") == "raw_bytes" and e.get("payload_hex") == "70696E67", "RX ping event")
        print("RX ping event: ok")

        r = command({"command": "send", "payload_hex": "414243"})
        if not r.get("ok"):
            fail(f"send: {r}")
        if peer.recvfrom(64)[0] != b"ABC":
            fail("peer did not receive the sent payload")
        wait_for(lambda e: e.get("type") == "raw_bytes" and e.get("direction") == "app_to_device", "TX event")
        print("TX send: ok")

        if not command({"command": "stop_capture"}).get("ok"):
            fail("stop_capture")
        wait_for(lambda e: e.get("type") == "status" and e.get("engine_state") == "idle", "idle status")
    finally:
        gw.terminate()
        gw.wait(10)

    # The gateway must take its engine down with it (SIGTERM path).
    host, port = engine_addr[len("tcp:"):].rsplit(":", 1)
    time.sleep(0.5)
    try:
        socket.create_connection((host, int(port)), timeout=1).close()
        fail("engine still listening after the gateway stopped")
    except OSError:
        pass
    print("PASS")


if __name__ == "__main__":
    main()
