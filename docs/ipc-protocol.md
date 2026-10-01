# IPC Protocol

Packet Probe implements a bidirectional IPC channel between the CLI and the
viewer. The IPC stream lets a viewer subscribe to the same event JSONL used by
file recording and send commands back to the CLI — without mixing UI code into
capture, recorder, or decoder libraries.

## UDS Capture Mode vs UDS IPC

UDS capture mode and UDS IPC are separate features.

- UDS Capture Mode analyzes Unix Domain Socket communication sessions as a
  transport.
- UDS IPC is an internal local communication channel between Packet Probe Core
  and a future viewer.

UDS capture mode is not implemented yet. UDS IPC event streaming is implemented
using the wirestead UDS transport.

## Transport

Packet Probe IPC uses a wirestead stream transport, chosen by the `--ipc` value:

- `<path>`: Unix Domain Socket (wirestead `UdsServer`), socket file mode `0600`.
- `tcp:<host>:<port>`: TCP (wirestead `TcpServer`). The host must be a loopback
  address (`127.x.x.x` or `::1`); anything else is rejected, because the channel
  has no authentication. Use this on Windows, where UDS is not yet validated.

The protocol is newline-delimited JSON over a local byte stream and is identical
on both transports. Each viewer client receives one metadata line on connection,
followed by event JSON lines.

## CLI Usage

Use `--ipc <path>` with any capture mode:

```sh
packet-probe udp \
  --bind-host 127.0.0.1 \
  --bind-port 19000 \
  --ipc /tmp/packet-probe.sock \
  --log udp.jsonl

packet-probe engine --ipc tcp:127.0.0.1:19500
```

`--ipc`, `--log`, `--hex`, and `--hex-frame` are independent. A capture can write
JSONL to disk and broadcast the same events to IPC clients at the same time.

## Protocol

The protocol is JSONL over a stream socket. Both directions use newline-delimited JSON.

- Message boundary: newline-delimited JSON
- Encoding: UTF-8 JSON lines
- Log metadata schema: `packet-probe.log.v1`
- Event schema: `packet-probe.event.v1`

Each client receives a metadata line immediately after connecting:

```json
{"type":"metadata","schema":"packet-probe.log.v1","event_schema":"packet-probe.event.v1","tool":"packet-probe","version":"0.1.0"}
```

After metadata, each captured event is sent as one JSON line using the same event
schema as JSONL log files.

### Core to viewer messages

- metadata
- raw packet event
- frame event
- latency event
- error event
- state event
- result (engine mode only; ack for a command, see "Control Protocol v2" below)
- status (engine mode only; broadcast on engine state changes, see "Control Protocol v2" below)

### Viewer to core messages

#### send

Sends a payload to the connected device. Supported in tcp-client, tcp-server, serial, and udp modes. Not supported in tcp-proxy mode.

```json
{"type":"command","command":"send","payload_hex":"AABBCC"}
```

- `payload_hex`: hex-encoded bytes to send (no spaces, separators, or `0x` prefix)
- The CLI decodes the hex payload and calls the session's `send()` method
- In the five direct/proxy CLI modes (`tcp-client`, `tcp-server`, `tcp-proxy`, `serial`,
  `udp`), if decoding fails or the session has no send method, the command is silently
  ignored — this legacy behavior is unchanged for backward compatibility.
- In `engine` mode (see below), `send` is dispatched through the same command/result
  protocol as every other engine command and always receives an explicit `result` ack.

## Control Protocol v2 (Engine Mode)

`packet-probe engine --ipc <path>` (see [capture-modes.md](capture-modes.md#engine-mode))
adds a small set of additional commands so a viewer can configure and start/stop capture
sessions without restarting the process. These commands are only recognized when
`packet-probe` was started in `engine` mode; the five direct/proxy CLI modes only
recognize the legacy `send` command described above.

Every engine command may include an `id` (any string chosen by the caller). The engine
echoes that `id` back in the corresponding `result` message so a viewer can correlate
requests with responses; multiple viewer clients may be issuing commands concurrently, so
callers should not assume replies arrive in the order requests were sent, and should not
assume no other message (e.g. a captured event, or another client's result) will be
interleaved on the stream — always dispatch on each line's `type` (and `id` for results),
never on position.

### Commands

```json
{"type":"command","id":"c1","command":"configure","config":{"mode":"udp","bind_host":"0.0.0.0","bind_port":19000}}
{"type":"command","id":"c2","command":"start_capture"}
{"type":"command","id":"c3","command":"stop_capture"}
{"type":"command","id":"c4","command":"get_status"}
{"type":"command","id":"c5","command":"list_serial_ports"}
```

- `configure`: replaces the engine's capture configuration. Only accepted while idle
  (fails with an error if a capture session is currently running). `config` mirrors the
  CLI's own options: `mode` (one of `tcp-client`, `tcp-server`, `tcp-proxy`, `serial`,
  `udp`), the mode's host/port fields (`host`/`port`, `listen_host`/`listen_port`,
  `bind_host`/`bind_port`, `target_host`/`target_port`, `serial_port`), serial framing
  (`baudrate`, `data_bits`, `stop_bits`, `parity`, `flow_control`), `log_path`, `hex_raw`,
  `hex_frame`, `latency`, and a nested `decoder` object (`decoder`, `frame_size`,
  `delimiter_hex`, `include_delimiter`, `length_size`, `length_endian`,
  `length_includes_header`). Unknown/omitted fields fall back to the same defaults as
  the CLI. Invalid or mode-inappropriate combinations are rejected with an error, using
  the same validation rules as CLI argv parsing.
- `start_capture`: builds and starts a capture session from the last `configure`d
  config. Fails if not yet configured, or if already capturing.
- `stop_capture`: stops the active capture session and returns to idle. The configured
  options are retained, so a subsequent `start_capture` (optionally preceded by a new
  `configure`) does not require reconnecting or restarting the process.
- `get_status`: returns the current engine state, configuration (if any), and event
  counters without changing anything.
- `list_serial_ports`: best-effort scan for available serial devices. Always returns
  `ok:true` with a (possibly empty) `ports` array; never fails.

### Results

Every command (including `send`, in engine mode) receives exactly one `result` message,
sent only to the client that issued the command:

```json
{"type":"result","id":"c1","ok":true}
{"type":"result","id":"c2","ok":false,"error":"not configured; call configure first"}
{"type":"result","id":"c5","ok":true,"ports":["/dev/ttyUSB0","/dev/ttyACM0"]}
```

`get_status`'s result additionally includes `engine_state`, `config` (if configured),
and `counters`; see the `status` message below for the exact shape.

### Status broadcasts

After every successful `configure`, `start_capture`, and `stop_capture`, the engine
broadcasts a `status` message to **all** connected clients (not just the one that issued
the command), so every viewer stays in sync with engine state changed by another client:

```json
{"type":"status","engine_state":"capturing","config":{"mode":"udp", "...": "..."},"counters":{"events_seen":42}}
```

- `engine_state`: `"idle"` or `"capturing"`
- `config`: the currently configured options (omitted if never configured)
- `counters.events_seen`: total events consumed by the pipeline since the engine started

## Multi-Client Behavior

Multiple viewer clients can connect to the same socket.

- Each client receives metadata on connection.
- Events are broadcast to all currently connected clients.
- If a client disconnects or a write fails, only that client is removed.
- Client failures do not stop capture, recording, or other IPC clients.

## Backpressure and Slow Clients

The current IPC implementation uses synchronous broadcast to connected clients.

This keeps the implementation simple, but a slow or blocked viewer client may delay
event broadcast. Packet Probe removes clients when socket writes fail, but it does
not yet use non-blocking sockets, per-client queues, or writer threads.

Future versions may add:

- non-blocking socket writes
- per-client bounded queues
- drop counters for slow clients
- viewer-side flow control

## Socket Lifecycle

When IPC starts (UDS only; a TCP listener has no file to manage):

- the parent directory must exist
- a stale socket at the same path is removed
- a directory or non-socket file at the path is treated as an error

When Packet Probe stops:

- the server socket is closed
- connected client sockets are closed
- the socket path is unlinked

## Viewer Policy

In the five direct/proxy CLI modes, the CLI itself starts capture from argv and the
viewer only subscribes to events and sends `send` commands back over the same socket;
capture start/stop control, filter subscription, snapshot requests, and other mutation
APIs remain out of scope for those modes.

In `engine` mode, the viewer drives capture start/stop and configuration itself via the
Control Protocol v2 commands described above. Filter subscription, snapshot requests, and
log replay remain out of scope even in engine mode (see "Future Extensions").

## Reconnect Policy

The viewer should tolerate disconnects and reconnect to the IPC socket. On
reconnect, the core sends a fresh metadata line before new event lines. In engine mode,
a reconnecting viewer should call `get_status` to recover the current engine state and
configuration, since it will not have observed any `status` broadcasts made while
disconnected.

## Future Extensions

- filter subscription
- snapshot request
- log replay stream
