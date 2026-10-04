# Serial PTY Loopback Validation

## Purpose

Validate Serial Direct Mode without physical serial hardware.

## Requirements

- Linux
- `socat`
- Built `packet-probe`

## Setup

Create a PTY pair:

```sh
socat -d -d pty,raw,echo=0 pty,raw,echo=0
```

Note the two printed PTY paths, for example `/dev/pts/3` and `/dev/pts/4`.

## Packet Probe Command

```sh
packet-probe serial --port /dev/pts/3 --baudrate 115200 --send-hex --log serial.jsonl
```

In another terminal:

```sh
printf "hello" > /dev/pts/4
```

Or type hex bytes into the Packet Probe stdin:

```text
02 10 01 00 03 A7
```

## Expected Stdout

```text
DEVICE -> APP ...
APP -> DEVICE ...
```

## Expected JSONL Snippets

```json
"transport":"serial"
"payload_hex":"0210010003A7"
```

## Troubleshooting

- If the serial port does not exist, check the PTY names printed by `socat`.
- If no bytes are received, ensure the opposite PTY is used for writes.
