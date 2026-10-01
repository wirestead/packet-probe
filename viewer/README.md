# Packet Probe Viewer

Packet Probe Viewer is an optional viewer for Packet Probe event streams.

The viewer subscribes to Packet Probe's UDS IPC stream and displays
JSONL events in a table, hex view, and detail panel. It drives capture through
`packet-probe`'s long-lived `engine` mode (see
[docs/ipc-protocol.md](../docs/ipc-protocol.md), "Control Protocol v2"): the
viewer sends `configure`/`start_capture`/`stop_capture`/`send` commands over IPC
instead of assembling CLI argv strings, so changing capture settings does not
require restarting the `packet-probe` process.

## Install

The viewer depends on PySide6. Live IPC stream subscription also requires `wirestead-python` to be installed.

For local development with sibling repositories, we recommend a local install of `wirestead-python`:

```sh
python -m pip install -e ../wirestead-python \
  -Ccmake.define.WIRESTEAD_CORE_SOURCE_DIR=../wirestead

cd viewer
python -m pip install -e .
```

*Note: In the final packaged standalone executable distributions, the `wirestead-python` native extension will be bundled automatically.*

## Run

```sh
packet-probe-viewer --socket /tmp/packet-probe.sock
```

## Test

```sh
cd viewer
python -m pip install -e ".[test]"
python -m pytest
```

## Web UI (packet-probe-web)

`packet-probe-web` serves a browser UI and relays the engine's IPC stream, so the
viewer works from any browser and on Windows (it talks to the engine over TCP
loopback IPC). It needs `wirestead-python`, not PySide6.

```sh
packet-probe-web                      # spawns `packet-probe engine --ipc tcp:127.0.0.1:<free port>`
packet-probe-web --ipc tcp:127.0.0.1:19500   # or attach to a running engine (UDS paths work too)
```

Open the printed `http://127.0.0.1:8080/?token=...` URL. Every request needs that
token. `--host 0.0.0.0` lets other machines connect; that is plain HTTP, so put it
behind a TLS reverse proxy or an SSH tunnel on untrusted networks.

## Attach to an already-running engine

Use this when `packet-probe engine --ipc <path>` is already running.

```sh
packet-probe engine --ipc /tmp/packet-probe.sock
```

In the viewer, set `Socket Path` to the same path and click `Attach`. The viewer
syncs with whatever the engine is currently doing (idle or capturing) via
`get_status`, and can then `configure`/`start_capture`/`stop_capture` it like any
other connection.

## Launch the engine from the viewer

Use this when you want the viewer to start `packet-probe engine` for you - the
default flow.

1. Set `CLI Path` to the `packet-probe` executable (auto-detected if it's on `PATH`
   or in a sibling `build/` directory).
2. Choose a mode and fill in its fields (bind/target host and port, serial port and
   baud rate, etc.) and optional frame decoder settings.
3. Click `Start Capture`.

The viewer spawns `packet-probe engine --ipc <generated path>`, connects, sends
`configure` with the fields from step 2, then `start_capture`. Changing fields and
clicking `Start Capture` again after `Stop Capture` reconfigures and restarts the
capture session without spawning a new process.

## Open a JSONL log

The viewer can open Packet Probe JSONL logs recorded with `--log`.

```sh
packet-probe udp \
  --bind-host 127.0.0.1 \
  --bind-port 19000 \
  --log udp.jsonl
```

Then open `udp.jsonl` from the viewer using:

```text
File > Open Log...
```

or the `Open Log` button.

## UI Layout

Packet Probe Viewer follows a compact desktop communication-tool layout.

- Top controls: CLI path, capture mode/fields, decoder settings, IPC socket, config
  preview, and action buttons
- Center view: live or offline event table
- Bottom tabs: Hex, JSON detail, and Process Log

## Send Command

Send is always over IPC, using the same command/result protocol as
configure/start_capture/stop_capture (see docs/ipc-protocol.md). Sending requires an
active connection and a capturing engine; it works in tcp-client, tcp-server, serial,
and udp modes. TCP proxy mode does not support send.

## Limitations

- no replay
- Unix Domain Socket IPC only for now
- viewer currently keeps events in memory
- very large captures should be recorded as JSONL and analyzed separately
- filter subscription and snapshot requests not implemented yet
