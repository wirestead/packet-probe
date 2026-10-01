---
name: run-packet-probe-viewer
description: Build the packet-probe engine and run/drive the Packet Probe web viewer (packet-probe-web gateway + browser UI) - start a UDP capture, send a test datagram, verify events and send over the gateway's HTTP/SSE API, and optionally take browser screenshots. Use when asked to run, start, or screenshot the viewer, or to confirm a viewer change works in the real app end-to-end.
---

The viewer is `viewer/packet_probe_viewer/`: `gateway.py` (stdlib HTTP server,
`packet-probe-web`) serves `web/index.html` and relays the engine's JSONL IPC over
one wirestead-python connection. Browsers get events over SSE (`GET /events`) and
send commands with `POST /command`; both need the startup token. See
`viewer/README.md` and `docs/ipc-protocol.md`.

All paths are relative to the repo root.

## Prerequisites

- Built engine: `cmake -S . -B build && cmake --build build` (MSVC:
  `cmake --build build --config Debug`; the binary is then `build/Debug/packet-probe.exe`).
- Python with `wirestead` (wirestead-python) importable:
  `python -c "import wirestead; print(wirestead.__version__)"`. Install a release
  wheel (see `viewer/README.md`) or build it from the sibling `wirestead-python` repo.
  On the Linux dev box a built one lives at
  `../wirestead-python/build/compat-venv/bin/python`.

## Run (agent path)

```bash
python .claude/skills/run-packet-probe-viewer/driver.py --python <python-with-wirestead>
```

The driver starts the gateway on a free port, configures and starts a UDP capture
over HTTP, sends `ping` from a peer socket and waits for the RX `raw_bytes` event on
SSE, sends `ABC` back and checks the peer receives it, stops the capture, then stops
the gateway and checks its engine is gone. Prints `PASS` / exits 0, or `FAIL: ...` /
exits 1. `--cli <path>` picks the engine binary (default: `PACKET_PROBE_CLI`, then
`build/`).

## Run (human path)

```bash
cd viewer
PYTHONPATH=. python -m packet_probe_viewer.gateway   # or `packet-probe-web` if installed
```

Open the printed `http://127.0.0.1:8080/?token=...`. Use `--port` if 8080 is taken.
A gateway started with Claude Code's `!` prefix is backgrounded after 2 minutes and
killed after 30; run it from a regular terminal for long sessions.

## Screenshots

There is no browser in the repo toolchain. For screenshots, install Playwright in a
scratch directory (`npm i playwright && npx playwright install chromium-headless-shell`)
and drive the page against a running gateway: wait for `#engine-dot.on`, click
`#start`, wait for `#state-chip.capturing`, send traffic, click a `#rows tr`, then
screenshot. Useful selectors: `#mode` (hidden select behind the mode pills),
`[data-key="bind_port"]` etc. for mode fields, `#send-input`/`#send`, `#rows tr`,
`.tabs button[data-tab=hex|text|json]`. Test both `colorScheme: "dark"` and
`"light"` and a ~390px-wide viewport.

## Test

```bash
cd viewer
PYTHONPATH=. python -m pytest -q
```

`tests/test_gateway.py` runs against a fake engine (no build needed) and is skipped
if `wirestead` is not importable. On machines with ROS installed, pytest's ROS
plugins break collection; use `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`.

## Gotchas

- **UDP send-to target filters reception.** With a target set, wirestead only
  delivers datagrams from that exact peer (wirestead #435); traffic from other ports
  is dropped silently. Leave it empty to accept the first sender, or send from the
  target address (the driver binds its peer socket to the target port).
- **Send needs a UDP target** and is unavailable in tcp-proxy mode; the UI disables
  the button in both cases.
- **Port 8080 already in use**: the gateway exits with `cannot listen on ...` before
  spawning an engine; pass `--port`.
- **Attached engines** (`--ipc ...`) are not stopped when the gateway stops; only an
  engine the gateway spawned is.
