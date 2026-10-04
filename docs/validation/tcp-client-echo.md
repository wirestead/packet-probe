# TCP Client Echo Validation

## Purpose

Validate TCP Direct Mode send/receive recording with text and hex input.

## Requirements

- Python 3
- Built `packet-probe`

## Setup

Start a local TCP echo server:

```sh
python3 -u -c 'import socket
s=socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 19000))
s.listen(1)
c,a=s.accept()
while True:
    d=c.recv(4096)
    if not d: break
    c.sendall(d)'
```

## Packet Probe Command

```sh
echo "hello" | packet-probe tcp-client --host 127.0.0.1 --port 19000 --send-text --log tcp-client.jsonl
```

Hex input:

```sh
echo "02 10 01 00 03 A7" | packet-probe tcp-client --host 127.0.0.1 --port 19000 --send-hex --log tcp-client.jsonl
```

## Expected Stdout

```text
APP -> DEVICE ...
DEVICE -> APP ...
```

## Expected JSONL Snippets

```json
{"type":"metadata","schema":"packet-probe.log.v1"
```

```json
"direction":"app_to_device"
"direction":"device_to_app"
```

## Troubleshooting

- If connection fails, confirm the echo server is listening on `127.0.0.1:19000`.
- If the process waits, close stdin or interrupt with Ctrl-C after the echo is captured.
