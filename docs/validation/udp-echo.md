# UDP Echo Validation

## Purpose

Validate UDP Direct Mode datagram recording and send input parsing.

## Requirements

- Python 3
- Built `packet-probe`

## Setup

Start a UDP echo server:

```sh
python3 -u -c 'import socket
s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 19106))
while True:
    d,a=s.recvfrom(65535)
    s.sendto(d,a)'
```

## Packet Probe Command

```sh
echo "02 10 01 00 03 A7" | packet-probe udp \
  --bind-host 127.0.0.1 \
  --bind-port 19107 \
  --target-host 127.0.0.1 \
  --target-port 19106 \
  --send-hex \
  --log udp.jsonl
```

## Expected Stdout

```text
APP -> DEVICE 6 bytes  02 10 01 00 03 A7
DEVICE -> APP 6 bytes  02 10 01 00 03 A7
```

## Expected JSONL Snippets

```json
"transport":"udp"
"direction":"app_to_device"
"direction":"device_to_app"
"payload_hex":"0210010003A7"
```

## Troubleshooting

- If UDP bind fails, check whether the port is already in use.
- Large `--send-file` inputs may fail because UDP sends one datagram per file.
