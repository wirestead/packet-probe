# Packet Probe Viewer

Packet Probe Viewer is a browser UI for Packet Probe. `packet-probe-web` serves the
page and relays the engine's IPC stream, so captures can be driven and watched from
any browser, on Linux, macOS, and Windows.

```text
Browser  --HTTP: page, SSE: events, POST: commands-->  packet-probe-web
packet-probe-web  --wirestead-python, JSONL IPC (TCP loopback or UDS)-->  packet-probe engine
```

The gateway drives `packet-probe engine` with the IPC control protocol
(`configure`/`start_capture`/`stop_capture`/`send`, see
[docs/ipc-protocol.md](../docs/ipc-protocol.md)), so changing capture settings does
not restart the engine process.

## Install

The gateway needs `wirestead-python` (import name `wirestead`). It is published as
wheels on the [wirestead-python releases](https://github.com/wirestead/wirestead-python/releases),
not on PyPI:

```sh
gh release download v0.9.6 -R wirestead/wirestead-python -p "wirestead-0.9.6-cp312-cp312-*<platform>*.whl"
python -m pip install wirestead-*.whl

cd viewer
python -m pip install -e .
```

`<platform>` is e.g. `manylinux_2_27_x86_64`, `win_amd64`, or `macosx_15_0_arm64`. For
local development against a sibling `wirestead` checkout you can instead build it:

```sh
python -m pip install -e ../wirestead-python \
  -Ccmake.define.WIRESTEAD_CORE_SOURCE_DIR=../wirestead
```

## Run

```sh
packet-probe-web
```

This spawns `packet-probe engine --ipc tcp:127.0.0.1:<free port>` and prints the URL
to open, e.g. `http://127.0.0.1:8080/?token=...`. Stopping the gateway (Ctrl+C or
SIGTERM) stops that engine too. The `packet-probe` executable is found through
`PACKET_PROBE_CLI`, a sibling `build/` directory, or `PATH`; `--cli` overrides it.

Options:

- `--port <n>`: HTTP port (default 8080).
- `--ipc <address>`: attach to an already-running engine instead of spawning one;
  `<address>` is a UDS path or `tcp:127.0.0.1:<port>`.
- `--host <addr>`: HTTP bind address (default `127.0.0.1`).

## Security

Every `/events` and `/command` request must carry the token printed at startup;
it changes on every run. The gateway only listens on loopback by default. With
`--host 0.0.0.0` other machines can connect, but over plain HTTP: put it behind a TLS
reverse proxy or an SSH tunnel on untrusted networks. The engine's own IPC port is
always loopback-only.

## Using the UI

- **Left panel**: capture mode and its addresses, frame decoder, optional JSONL
  recording (written by the engine, on the engine's machine), Start/Stop, and Send
  (text with an optional line ending, or hex) with saved macros.
- **Event table**: live events with direction/type/text filters and Pause. **Open
  log…** loads a JSONL log recorded with `--log`; it is parsed in the browser and not
  uploaded.
- **Detail panel**: Hex, Text, and JSON views of the selected event.

Form values and macros are remembered per browser.

UDP note: every datagram arriving at the bind address is recorded, from any sender
(each event's `source` is the sender). Send needs a send-to target.

## Test

```sh
cd viewer
python -m pip install -e ".[test]"
python -m pytest
```

## Limitations

- The browser keeps the most recent 5000 events; record large captures with JSONL.
- One engine per gateway; several browsers can watch and control it at once.
- No replay, filter subscription, or snapshot requests yet.
