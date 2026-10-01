# JSONL Format

Packet Probe writes newline-delimited JSON. A log file starts with one metadata
line, followed by event lines. UDS IPC event streams use the same metadata and
event JSON line format.

## Metadata

```json
{"type":"metadata","schema":"packet-probe.log.v1","event_schema":"packet-probe.event.v1","tool":"packet-probe","version":"0.1.0"}
```

The metadata line is not a `PacketEvent`.

## Event Schema

Event lines use `packet-probe.event.v1`.

Common fields:

- `seq`: event sequence number
- `parent_seq`: source event sequence for derived events, always present
- `parent_seqs`: every raw event sequence that contributed to a derived event, in
  arrival order; always present, `[]` for raw transport events
- `time_ns`: wall-clock timestamp in nanoseconds since Unix epoch
- `session`: capture session id
- `transport`: `tcp`, `udp`, `serial`, or future transport name
- `direction`: communication-flow direction such as `app_to_device` or `device_to_app`
- `type`: event type such as `raw_bytes`, `frame`, `latency`, `error`, or `state_change`
- `source`: optional source endpoint
- `destination`: optional destination endpoint
- `size`: payload size in bytes
- `payload_hex`: uppercase compact hex string, empty when payload is empty
- `summary`: human-readable event summary
- `decoded`: optional decoded JSON object for future message decoders

Latency fields:

- `request_seq`
- `response_seq`
- `latency_ns`
- `latency_us`
- `request_size`
- `response_size`

## Sequence Policy

- `seq` is unique within one engine or CLI run: all capture sessions and the event
  pipeline draw from one shared `SequenceAllocator`, so raw and derived events never
  collide.
- Raw transport events use `parent_seq: 0` and `parent_seqs: []`.
- Derived events (frames, decoder errors) set `parent_seq` to the raw event that
  completed them, and `parent_seqs` to all raw events whose bytes they were assembled
  from. A frame split across two TCP reads, for example, has `parent_seq: 11` and
  `parent_seqs: [10, 11]`; a frame from a single read has `parent_seqs: [parent_seq]`.
