import json
import socket
import threading
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

import pytest

pytest.importorskip("wirestead")

from packet_probe_viewer.gateway import EngineLink, main, make_handler, resolve_cli  # noqa: E402


def _fake_engine(listener: socket.socket) -> None:
    """Answers each command with a result and emits one event, like the real engine."""
    conn, _ = listener.accept()
    f = conn.makefile("rw", encoding="utf-8")
    f.write('{"type":"metadata","schema":"packet-probe.log.v1"}\n')
    f.flush()
    for line in f:
        cmd = json.loads(line)
        f.write(json.dumps({"type": "result", "id": cmd["id"], "ok": True, "echo": cmd["command"]}) + "\n")
        f.write('{"seq":1,"type":"raw_bytes","summary":"hello"}\n')
        f.flush()


def test_gateway_relays_commands_and_events():
    listener = socket.create_server(("127.0.0.1", 0))
    threading.Thread(target=_fake_engine, args=(listener,), daemon=True).start()
    link = EngineLink(f"tcp:127.0.0.1:{listener.getsockname()[1]}")
    link.start()

    server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(link, "secret"))
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_port}"

    def post(body: dict, token: str) -> dict:
        req = urllib.request.Request(base + "/command", data=json.dumps(body).encode(),
                                     headers={"X-Token": token}, method="POST")
        with urllib.request.urlopen(req, timeout=5) as res:
            return json.loads(res.read())

    with pytest.raises(urllib.error.HTTPError) as denied:
        post({"command": "get_status"}, "wrong")
    assert denied.value.code == 403

    events = urllib.request.urlopen(base + "/events?token=secret", timeout=5)
    assert json.loads(events.readline().decode()[len("data: "):])["type"] == "gateway"

    for _ in range(50):
        if link.connected:
            break
        threading.Event().wait(0.1)
    assert link.connected

    result = post({"command": "get_status"}, "secret")
    assert result["ok"] is True and result["echo"] == "get_status"

    while '"summary":"hello"' not in events.readline().decode():
        pass  # readline times out (fails the test) if the event never arrives

    server.shutdown()


def test_resolve_cli_rejects_missing_executables(tmp_path):
    assert resolve_cli(str(tmp_path / "no-such-packet-probe")) is None
    assert resolve_cli("no-such-packet-probe-on-path") is None


def test_main_reports_missing_cli_instead_of_crashing(tmp_path, capsys):
    missing = str(tmp_path / "packet-probe")
    assert main(["--port", "0", "--cli", missing]) == 1
    err = capsys.readouterr().err
    assert "cannot find the packet-probe executable" in err and "--cli" in err


def test_engine_exit_is_published_to_browsers():
    link = EngineLink("tcp:127.0.0.1:1")
    q = link.subscribe()
    q.get_nowait()  # initial connection state
    link.engine_exited(3)
    assert json.loads(q.get_nowait()) == {"type": "gateway", "engine_connected": False, "engine_exit_code": 3}
    # A browser that opens the page later still learns why the engine is gone.
    assert json.loads(link.subscribe().get_nowait())["engine_exit_code"] == 3
