# Send Input

Packet Probe direct modes can send command payloads while they capture traffic.
Packet Probe provides a common input parser for TCP, Serial, and UDP Direct Mode.

## Formats

### Text

```sh
--send-text
```

This is the default. Each stdin line is sent as text bytes.

```sh
echo "hello" | packet-probe tcp-client --host 127.0.0.1 --port 9000 --send-text
```

### Hex

```sh
--send-hex
```

Each stdin line is parsed as hexadecimal bytes before sending.

Accepted examples:

```text
02 10 01 00 03 A7
0210010003A7
0x02 0x10 0x01 0x00 0x03 0xA7
02:10:01:00:03:A7
02-10-01-00-03-A7
```

Invalid examples:

```text
0
GG
02 1
02 0x
02 ZZ
```

### File

```sh
--send-file <path>
```

The file is read as binary data and sent once. The current implementation reads the whole file into
memory and sends it as a single payload.

```sh
packet-probe serial \
  --port /dev/ttyUSB0 \
  --baudrate 115200 \
  --send-file command.bin \
  --log serial.jsonl
```

## Transport Behavior

TCP and Serial are stream-oriented.
UDP is datagram-oriented.

For UDP, each stdin line becomes one datagram.
For TCP and Serial, each stdin line becomes one send operation.

UDP send input requires `--target-host` and `--target-port`.

## Examples

Serial hex command:

```sh
echo "02 10 01 00 03 A7" | packet-probe serial \
  --port /dev/ttyUSB0 \
  --baudrate 115200 \
  --send-hex \
  --log serial.jsonl
```

TCP hex command:

```sh
echo "02 10 01 00 03 A7" | packet-probe tcp-client \
  --host 127.0.0.1 \
  --port 9000 \
  --send-hex \
  --log tcp.jsonl
```

UDP hex datagram:

```sh
echo "02 10 01 00 03 A7" | packet-probe udp \
  --bind-host 0.0.0.0 \
  --bind-port 9000 \
  --target-host 127.0.0.1 \
  --target-port 9100 \
  --send-hex \
  --log udp.jsonl
```

## Limitations

- Replay is not implemented.
- There is no interactive command shell.
- CRC generation and protocol-specific command builders are not implemented.
- File input is not optimized for large files.
- UDP file input sends the whole file as one datagram; oversized datagrams may fail.
