# Architecture

Packet Probe is organized around a transport-independent event model.

```text
[Target Device]
      |
 TCP / UDP / Serial / UDS
      |
[Packet Probe Core]
      |
 UDS IPC  (events ↓  commands ↑)
      |
[Viewer]
```

The current implementation is CLI-first. It records communication events from direct TCP,
UDP, and Serial sessions or TCP proxy sessions and writes them to stdout,
optional JSONL logs, and optional UDS IPC event streams. The IPC channel is
bidirectional: the viewer receives events and can send commands (e.g. `send`) back
to the CLI.

Engine mode (`packet-probe engine --ipc <path>`) is a long-lived variant of the CLI
process that starts idle and accepts configure/start_capture/stop_capture commands over
the same IPC channel (see [docs/ipc-protocol.md](ipc-protocol.md)):

```text
[Viewer]
   |  configure / start_capture / stop_capture / send  (commands)
   |  result / status / events                          (replies)
   v
[EngineController] -- owns --> [CaptureSession] (one of the 5 direct/proxy sessions)
   |
   `--> [EventPipeline] --> [JsonlRecorder] + [IpcEventServer] (broadcast)
```

`EngineController` and the single `IpcEventServer`/`SequenceAllocator` it is built on
persist across repeated configure/start/stop cycles, so a viewer can change capture
settings and restart a session without restarting the `packet-probe` process. The five
direct/proxy CLI modes are unaffected: they still parse capture configuration from argv
and run a single session for the process lifetime, unrelated to engine mode.

TCP Proxy Mode:

```text
[Existing App]
      |
      | TCP
      v
[Packet Probe TCP Proxy]
      |
      | TCP
      v
[Target Device]
```

Core responsibilities:

- define stable `PacketEvent` data
- record timestamp, direction, transport, size, payload, and summary
- record source and destination endpoints when proxying
- provide heuristic latency events for request/response-style traffic
- derive frame events from raw payloads through a transport-independent decoder pipeline
- keep capture, recorder, decoder, and IPC layers separable

Capture responsibilities:

- connect to a known device communication session
- convert transport callbacks into `PacketEvent` values
- keep transport-specific code out of the core event model

Recorder responsibilities:

- serialize each event as one JSON object per line
- preserve a stable log format that a future viewer can load

Decoder pipeline:

```text
Raw Bytes
  -> Frame
    -> Future: Decoded Message
```

Raw byte events preserve the original transport payload. Frame events are derived
from raw byte events and use `parent_seq` to refer back to the source event.

Latency tracking:

- app_to_device raw byte events are treated as request candidates
- the next device_to_app raw byte event is paired as the response
- the emitted latency event records request sequence, response sequence, and elapsed time

Without a protocol decoder, request/response pairing is heuristic-based. Protocol-specific
decoders are expected to provide accurate pairing later.

Viewer integration happens through the IPC boundary, not by mixing UI code into the
core library: the browser viewer talks to the engine only through the `packet-probe-web`
gateway, which is an ordinary IPC client.

UDS capture mode and UDS IPC are separate features. UDS capture mode analyzes Unix
Domain Socket communication sessions. UDS IPC is an internal local communication
channel between Packet Probe Core and the viewer.

Sequence allocation:

All capture sessions and the `EventPipeline` share a single `SequenceAllocator`
created at startup. This gives every raw and derived event a globally unique sequence
number without relying on separate counter ranges or magic offsets.
