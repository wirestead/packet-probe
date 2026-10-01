# Packet Probe

Packet Probe is a lightweight device communication analyzer for inspecting,
recording, and decoding packets, frames, and messages over TCP, UDP, Serial, and UDS.

It is not an OS-level packet sniffer. Packet Probe focuses on application/device-level
communication sessions with connected equipment.

## Overview

Packet Probe connects to known device communication sessions and records the raw bytes
exchanged with target equipment. The CLI is named `packet-probe`.

The core model records timestamp, direction, size, payload, transport, and session
metadata as `PacketEvent` values. Events can be printed as one-line hex output and
written as JSONL for later viewer support.

## Features

- TCP Direct Mode
- TCP Server Direct Mode
- TCP Proxy Mode
- Serial Direct Mode
- UDP Direct Mode
- JSONL recording
- UDS IPC event stream
- Raw byte and frame events
- Frame decoders: raw, fixed-size, delimiter, length-prefix
- Common send input: text, hex, file

## Current Limitations

- No UDS capture mode yet
- No protocol-specific message decoder yet
- No external decoder plugin system yet
- No replay support yet
- TCP server mode accepts one client connection per process run
- UDS IPC currently uses synchronous broadcast

## Build

By default, CMake looks for a sibling wirestead source tree at `../wirestead`.
If that path does not exist, it falls back to `find_package(wirestead CONFIG REQUIRED)`.
Packet Probe requires a C++20-capable compiler.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Parallel build:

```sh
cmake --build build -j2
```

## Usage

```sh
packet-probe --help
packet-probe --version
packet-probe tcp-client --host 127.0.0.1 --port 9000
packet-probe tcp-client --host 127.0.0.1 --port 9000 --hex
packet-probe tcp-client --host 127.0.0.1 --port 9000 --log capture.jsonl --hex
packet-probe tcp-server --listen-host 0.0.0.0 --listen-port 9000 --log tcp-server.jsonl --hex
packet-probe serial --port /dev/ttyUSB0 --baudrate 115200 --hex
packet-probe serial --port COM3 --baudrate 115200 --log serial.jsonl --hex
packet-probe udp --bind-host 0.0.0.0 --bind-port 9000 --log udp.jsonl --hex
```

In `tcp-client` mode, lines typed on stdin are sent to the target as raw bytes and
recorded as TX events. Bytes received from the target are recorded as RX events.

Send input examples:

```sh
echo "02 10 01 00 03 A7" | packet-probe serial \
  --port /dev/ttyUSB0 \
  --baudrate 115200 \
  --send-hex \
  --log serial.jsonl \
  --hex

packet-probe serial \
  --port /dev/ttyUSB0 \
  --baudrate 115200 \
  --send-file command.bin \
  --log serial.jsonl \
  --hex
```

IPC event stream example:

```sh
packet-probe serial \
  --port /dev/ttyUSB0 \
  --baudrate 115200 \
  --ipc /tmp/packet-probe.sock \
  --log serial.jsonl
```

JSONL event example:

```json
{"seq":1,"parent_seq":0,"time_ns":1781234567890,"session":"tcp-client-1","transport":"tcp","direction":"device_to_app","type":"raw_bytes","size":6,"payload_hex":"0210010003A7","summary":"RX 6 bytes"}
```

## TCP Server Mode

TCP Server Mode lets Packet Probe listen for a remote TCP client and inspect the
communication session.

```text
[Target Device / App TCP Client] -> [Packet Probe TCP Server]
```

TCP server mode currently accepts one client connection per process run.

Send input is attempted only after the CLI reads stdin or `--send-file`.
For `--send-file`, a remote client must already be connected when the file payload is sent.
If you need to send a command after connection, use stdin-based `--send-text` or `--send-hex`,
or start the client before sending the file.

Example:

```sh
packet-probe tcp-server \
  --listen-host 0.0.0.0 \
  --listen-port 9000 \
  --log tcp-server.jsonl \
  --hex
```

## TCP Proxy Mode

TCP Proxy Mode places Packet Probe between an existing application and a target device.

```text
[Existing App] -> [Packet Probe] -> [Target Device]
```

Example:

```sh
packet-probe tcp-proxy \
  --listen-host 127.0.0.1 \
  --listen-port 9000 \
  --target-host 192.168.0.10 \
  --target-port 9000 \
  --log capture.jsonl \
  --hex \
  --latency
```

This mode is useful when you want to inspect the actual communication flow between
an existing application and connected equipment.

Proxy events use communication-flow directions:

- `app_to_device`: bytes forwarded from the existing app to the target device
- `device_to_app`: bytes forwarded from the target device back to the existing app

Without a protocol decoder, request/response pairing is heuristic-based.
For accurate pairing, protocol-specific decoder support will be added later.

Manual validation:

```sh
packet-probe tcp-proxy \
  --listen-host 127.0.0.1 \
  --listen-port 9000 \
  --target-host 127.0.0.1 \
  --target-port 9100 \
  --log proxy.jsonl \
  --hex
```

Then connect a test client to `127.0.0.1:9000` while a target echo server is
listening on `127.0.0.1:9100`.

## Serial Direct Mode

Serial Direct Mode connects directly to a serial target device.

Linux example:

```sh
packet-probe serial --port /dev/ttyUSB0 --baudrate 115200 --log serial.jsonl --hex
```

Windows example:

```sh
packet-probe serial --port COM3 --baudrate 115200 --log serial.jsonl --hex
```

Supported serial options:

- `--data-bits <5|6|7|8>`, default: `8`
- `--stop-bits <1|2>`, default: `1`
- `--parity <none|odd|even>`, default: `none`
- `--flow-control <none|software|hardware>`, default: `none`

Packet Probe supports text, hex, and binary file input for sending commands.
Use `--send-text`, `--send-hex`, or `--send-file`.

Manual validation options are documented in [docs/serial-validation.md](docs/serial-validation.md).

## UDP Direct Mode

UDP Direct Mode binds a UDP socket and records every received datagram, from any
sender; each event's `source` is the sender's address.

```sh
packet-probe udp --bind-host 0.0.0.0 --bind-port 9000 --log udp.jsonl --hex
```

If `--target-host` and `--target-port` are provided, stdin lines are sent as UDP
datagrams to that target and recorded as `app_to_device` events.

Send input details are documented in [docs/send-input.md](docs/send-input.md).

## Frame Decoders

By default, Packet Probe uses `--decoder raw`, which treats each raw payload as one
frame. TCP and Serial streams can use boundary-oriented decoders:

```sh
packet-probe serial --port /dev/ttyUSB0 --baudrate 115200 \
  --decoder delimiter --delimiter 0A --log serial.jsonl --hex-frame

packet-probe tcp-client --host 127.0.0.1 --port 9000 \
  --decoder length-prefix --length-size 2 --length-endian big
```

`--hex` prints raw byte events. Use `--hex-frame` to also print frame events.
Decoder details are documented in [docs/decoders.md](docs/decoders.md).

MessageDecoder extension interface is available as a future extension point.
Packet Probe does not include protocol-specific message decoders yet.

## Public API

Packet Probe is primarily a CLI tool. Its public C++ API is intentionally small.

Public headers are limited to:

- `packet_probe/packet_probe.hpp`
- `packet_probe/version.hpp`
- `packet_probe/core/packet_event.hpp`
- `packet_probe/core/jsonl_serializer.hpp`
- `packet_probe/decoder/*`
- `packet_probe/ipc/ipc_protocol.hpp`

Capture sessions, recorders, IPC server implementation, send input parsing, and
CLI helpers are internal implementation details.

## Viewer

Packet Probe includes a browser viewer under `viewer/`. `packet-probe-web` starts
`packet-probe engine`, serves the UI, and relays the engine's IPC stream to the
browser, so capture can be configured, started, stopped, sent to, and inspected from
any browser on Linux, macOS, or Windows. It can also attach to an engine that is
already running (`--ipc <path>` or `--ipc tcp:127.0.0.1:<port>`) and open JSONL logs.

```sh
packet-probe-web          # prints http://127.0.0.1:8080/?token=...
```

See [viewer/README.md](viewer/README.md) for installation, security notes, and usage.

## Documentation

- [Capture Modes](docs/capture-modes.md)
- [Send Input](docs/send-input.md)
- [Frame Decoders](docs/decoders.md)
- [Header Structure](docs/header-structure.md)
- [JSONL Format](docs/jsonl-format.md)
- [IPC Protocol](docs/ipc-protocol.md)
- [IPC Event Stream Validation](docs/validation/ipc-event-stream.md)
- [Viewer IPC Validation](docs/validation/viewer-ipc.md)
- [Viewer JSONL Log Validation](docs/validation/viewer-jsonl-log.md)
- [Validation Guides](docs/validation/)
