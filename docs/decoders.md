# Decoders

Packet Probe separates byte capture from protocol interpretation.

```text
Raw Bytes
  -> Frame
    -> Decoded Message
```

Packet Probe currently provides frame decoders for raw, fixed-size, delimiter,
and length-prefix framing. A minimal `MessageDecoder` extension interface exists,
but protocol-specific message decoding is intentionally left for later work.

## Event Types

- RawBytes Event: payload observed directly from TCP, UDP, or Serial transport.
- Frame Event: logical frame extracted from a raw stream or datagram by a `FrameDecoder`.
- DecodedMessage Event: protocol-specific `MessageDecoder` output that interprets a frame
  into meaningful fields. Currently only the extension point exists.

Frame events copy direction and endpoint metadata from the raw event that produced
them. Each frame event records `parent_seq`, which points to the source raw event.

## Supported Frame Decoders

### Raw

```sh
--decoder raw
```

Treats each raw payload as one frame. This is the default and is a good fit for UDP
datagrams or transports where frame boundaries are already known.

### Fixed Size

```sh
--decoder fixed --frame-size 16
```

Splits the stream into frames of a fixed byte length. Partial frames are buffered
until enough bytes arrive.

### Delimiter

```sh
--decoder delimiter --delimiter 0A
--decoder delimiter --delimiter CRLF
```

Splits the stream whenever the delimiter appears. Delimiters are included in frame
payloads by default; use `--no-include-delimiter` to strip them. `--include-delimiter`
is accepted for explicitness. The web viewer's "Keep delimiter in frame" box has the
same default.

### Length Prefix

```sh
--decoder length-prefix --length-size 2 --length-endian big
```

Reads the frame length from the beginning of each buffered frame. Supported length
field sizes are `1`, `2`, and `4` bytes. Supported endian values are `big` and
`little`.

By default, the length value describes the payload length after the length field.
Use `--length-includes-header` when the length value includes the length field bytes.

## Examples

```sh
packet-probe serial --port /dev/ttyUSB0 --baudrate 115200 \
  --decoder delimiter --delimiter 0A --log serial.jsonl --hex-frame
```

```sh
packet-probe tcp-client --host 127.0.0.1 --port 9000 \
  --decoder length-prefix --length-size 2 --length-endian big --log capture.jsonl
```

```sh
packet-probe udp --bind-host 0.0.0.0 --bind-port 9000 \
  --decoder raw --log udp.jsonl
```

## Limitations

- Protocol-specific message decoders are not implemented yet.
- Checksum and CRC validation are not implemented.
- External decoder plugins are not implemented.
- Length-prefix decoding assumes the length field starts at byte offset 0.
