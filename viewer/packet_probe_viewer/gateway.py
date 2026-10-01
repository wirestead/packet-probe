"""Web gateway: serves the browser UI and relays the engine's JSONL IPC stream.

    Browser  <-- HTTP: UI, SSE: engine lines, POST: commands -->  gateway
    gateway  <-- wirestead-python TcpClient/UdsClient (JSONL IPC) -->  packet-probe engine

The gateway holds a single upstream IPC connection and fans it out to any number
of browsers through per-browser bounded queues, so a slow browser drops its own
lines instead of stalling the engine's synchronous broadcast. Every /events and
/command request must carry the startup token (see docs/ipc-protocol.md for the
messages relayed).
"""

import argparse
import itertools
import json
import os
import queue
import secrets
import signal
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import wirestead

INDEX_HTML = Path(__file__).parent / "web" / "index.html"
CLIENT_QUEUE_MAX = 10000
COMMAND_TIMEOUT_S = 5.0
MAX_COMMAND_BYTES = 1 << 20
SSE_KEEPALIVE_S = 15.0


class EngineLink:
    """One upstream IPC connection to the engine, fanned out to browser subscribers."""

    def __init__(self, address: str):
        self.address = address
        self.connected = False
        self._lock = threading.Lock()
        self._client = None
        self._subscribers: set[queue.Queue] = set()
        self._pending: dict[str, queue.Queue] = {}
        self._ids = itertools.count(1)

    def start(self) -> None:
        threading.Thread(target=self._run, name="engine-link", daemon=True).start()

    def _make_client(self):
        if self.address.startswith("tcp:"):
            host, _, port = self.address[len("tcp:"):].rpartition(":")
            client = wirestead.TcpClient(host, int(port))
        else:
            client = wirestead.UdsClient(self.address)
        client.auto_start(False)
        # Must hold the engine's longest line: the hex of the largest payload.
        client.use_line_framer("\n", False, 1 << 20)
        client.on_message(self._on_message)
        return client

    def _run(self) -> None:
        while True:
            client = self._make_client()
            try:
                if client.start_sync():
                    with self._lock:
                        self._client = client
                    self._set_connected(True)
                    while client.connected():
                        time.sleep(0.2)
            except Exception as exc:
                print(f"packet-probe-web: engine link error: {exc}", file=sys.stderr)
            finally:
                with self._lock:
                    self._client = None
                client.stop()
                self._set_connected(False)
            time.sleep(1.0)

    def _set_connected(self, value: bool) -> None:
        if self.connected == value:
            return
        self.connected = value
        self._publish(json.dumps({"type": "gateway", "engine_connected": value}))

    def _on_message(self, ctx) -> None:
        line = bytes(ctx.data).decode("utf-8", errors="replace")
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            return
        if isinstance(obj, dict) and obj.get("type") == "result":
            with self._lock:
                waiter = self._pending.pop(obj.get("id"), None)
            if waiter is not None:
                waiter.put(obj)
            return
        self._publish(line)

    def _publish(self, line: str) -> None:
        with self._lock:
            subscribers = list(self._subscribers)
        for q in subscribers:
            try:
                q.put_nowait(line)
            except queue.Full:
                pass  # slow browser: drop its line rather than block the engine

    def subscribe(self) -> queue.Queue:
        q: queue.Queue = queue.Queue(CLIENT_QUEUE_MAX)
        with self._lock:
            self._subscribers.add(q)
        q.put_nowait(json.dumps({"type": "gateway", "engine_connected": self.connected}))
        return q

    def unsubscribe(self, q: queue.Queue) -> None:
        with self._lock:
            self._subscribers.discard(q)

    def command(self, cmd: dict) -> dict:
        """Sends one command and waits for its result (results are only sent to us)."""
        command_id = f"g{next(self._ids)}"
        waiter: queue.Queue = queue.Queue(1)
        with self._lock:
            client = self._client
            if client is None:
                return {"type": "result", "ok": False, "error": "engine not connected"}
            self._pending[command_id] = waiter
        try:
            client.send_line(json.dumps({**cmd, "type": "command", "id": command_id}))
            return waiter.get(timeout=COMMAND_TIMEOUT_S)
        except queue.Empty:
            return {"type": "result", "ok": False, "error": "engine did not answer in time"}
        finally:
            with self._lock:
                self._pending.pop(command_id, None)


def make_handler(link: EngineLink, token: str):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, format, *args):  # keep the console for engine output
            pass

        def _authorized(self, query: dict) -> bool:
            supplied = self.headers.get("X-Token") or query.get("token", [""])[0]
            return secrets.compare_digest(supplied.encode(), token.encode())

        def _send(self, status: int, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            url = urlparse(self.path)
            if url.path == "/":
                self._send(200, INDEX_HTML.read_bytes(), "text/html; charset=utf-8")
            elif url.path == "/events":
                if not self._authorized(parse_qs(url.query)):
                    self._send(403, b"forbidden", "text/plain")
                    return
                self._stream_events()
            else:
                self._send(404, b"not found", "text/plain")

        def do_POST(self):
            url = urlparse(self.path)
            if url.path != "/command":
                self._send(404, b"not found", "text/plain")
                return
            if not self._authorized(parse_qs(url.query)):
                self._send(403, b"forbidden", "text/plain")
                return
            length = int(self.headers.get("Content-Length") or 0)
            if length <= 0 or length > MAX_COMMAND_BYTES:
                self._send(413, b"bad length", "text/plain")
                return
            try:
                cmd = json.loads(self.rfile.read(length))
            except json.JSONDecodeError:
                cmd = None
            if not isinstance(cmd, dict) or not isinstance(cmd.get("command"), str):
                self._send(400, b'{"ok":false,"error":"expected {\\"command\\": ...}"}', "application/json")
                return
            result = link.command(cmd)
            self._send(200, json.dumps(result).encode(), "application/json")

        def _stream_events(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Accel-Buffering", "no")  # don't buffer behind nginx
            self.end_headers()
            self.close_connection = True
            q = link.subscribe()
            try:
                while True:
                    try:
                        chunk = f"data: {q.get(timeout=SSE_KEEPALIVE_S)}\n\n"
                    except queue.Empty:
                        chunk = ": keepalive\n\n"
                    self.wfile.write(chunk.encode())
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                link.unsubscribe(q)

    return Handler


def find_packet_probe_binary() -> str:
    # 1. Check environment variable
    env_path = os.environ.get("PACKET_PROBE_CLI")
    if env_path:
        return env_path

    # 2. Check workspace build directory relative to this file
    try:
        current_dir = Path(__file__).resolve().parent
        workspace_root = current_dir.parents[1]
        build_root = workspace_root / "build"
        exe_name = "packet-probe.exe" if os.name == "nt" else "packet-probe"

        candidates = [
            build_root / "packet-probe",
            build_root / "apps" / "packet-probe-cli" / "packet-probe",
            build_root / exe_name,
            build_root / "apps" / "packet-probe-cli" / exe_name,
        ]
        # Multi-config generators (MSVC/Ninja Multi-Config) place binaries
        # under a per-configuration subdirectory instead of build/ directly.
        for config in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"):
            candidates.append(build_root / config / exe_name)
            candidates.append(build_root / "apps" / "packet-probe-cli" / config / exe_name)

        for candidate in candidates:
            if candidate.exists() and os.access(candidate, os.X_OK):
                return str(candidate)
    except Exception:
        pass

    return "packet-probe"

def _free_loopback_port() -> int:
    # ponytail: the port can be taken between close() and the engine's bind; fine
    # for a local tool, pass --ipc explicitly if it ever collides.
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Packet Probe web gateway")
    parser.add_argument("--host", default="127.0.0.1",
                        help="HTTP bind address (default 127.0.0.1; 0.0.0.0 allows remote browsers)")
    parser.add_argument("--port", type=int, default=8080, help="HTTP port (default 8080)")
    parser.add_argument("--ipc", default="",
                        help="Attach to a running engine: UDS path or tcp:127.0.0.1:<port>. "
                             "Without it, the gateway spawns its own engine.")
    parser.add_argument("--cli", default="", help="packet-probe executable for the spawned engine")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)

    address = args.ipc or f"tcp:127.0.0.1:{_free_loopback_port()}"
    link = EngineLink(address)

    # Bind HTTP before spawning the engine, so a taken port can't orphan it.
    token = secrets.token_urlsafe(16)
    try:
        server = ThreadingHTTPServer((args.host, args.port), make_handler(link, token))
    except OSError as exc:
        print(f"packet-probe-web: cannot listen on {args.host}:{args.port}: {exc} (try --port)", file=sys.stderr)
        return 1
    server.daemon_threads = True

    engine = None
    if not args.ipc:
        cli = args.cli or find_packet_probe_binary()
        engine = subprocess.Popen([cli, "engine", "--ipc", address])
    link.start()

    shown_host = "127.0.0.1" if args.host in ("0.0.0.0", "::") else args.host
    print(f"packet-probe-web: engine {address}")
    print(f"packet-probe-web: open http://{shown_host}:{server.server_port}/?token={token}", flush=True)
    if args.host not in ("127.0.0.1", "localhost", "::1"):
        print("packet-probe-web: WARNING remote access is plain HTTP; put it behind a TLS "
              "reverse proxy or an SSH tunnel on untrusted networks.", file=sys.stderr)

    # Run the cleanup below on SIGTERM too, not just Ctrl+C, so the engine isn't orphaned.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if engine is not None:
            engine.terminate()
            try:
                engine.wait(3)
            except subprocess.TimeoutExpired:
                engine.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
