# TCP Proxy Echo Validation

## Purpose

Validate TCP Proxy Mode forwarding, bidirectional events, and latency events.

## Requirements

- Python 3
- `nc` or another TCP client
- Built `packet-probe`

## Setup

Start a target TCP echo server on `127.0.0.1:19100`.

```sh
python3 -u -c 'import socket
s=socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 19100))
s.listen(1)
c,a=s.accept()
while True:
    d=c.recv(4096)
    if not d: break
    c.sendall(d)'
```

## Packet Probe Command

```sh
packet-probe tcp-proxy \
  --listen-host 127.0.0.1 \
  --listen-port 19099 \
  --target-host 127.0.0.1 \
  --target-port 19100 \
  --log tcp-proxy.jsonl
```

In another terminal:

```sh
printf "hello" | nc 127.0.0.1 19099
```

## Expected Stdout

```text
APP -> DEVICE ...
DEVICE -> APP ...
[latency] request=... response=...
```

## Expected JSONL Snippets

```json
"direction":"app_to_device"
"direction":"device_to_app"
"type":"latency"
```

## Troubleshooting

- Confirm the target echo server is running before starting the proxy client.
- If no latency appears, confirm response bytes are received from the target.
