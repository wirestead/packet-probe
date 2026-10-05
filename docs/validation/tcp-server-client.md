# TCP Server Validation

## Purpose

Validate TCP Server Direct Mode by connecting a test TCP client to Packet Probe.

## Requirements

- Python 3
- Built `packet-probe`

## Packet Probe Command (Interactive text mode)

Start the TCP server and listen on `127.0.0.1:19000`:

```bash
packet-probe tcp-server \
  --listen-host 127.0.0.1 \
  --listen-port 19000 \
  --log tcp-server.jsonl
```

### Send-Text Example

By default, any text typed in stdin of the `packet-probe` server process will be sent to the remote client as text. 

### Send-Hex Example

If you want to send hex bytes on connection, run:

```bash
echo "02 90 00 91" | packet-probe tcp-server \
  --listen-host 127.0.0.1 \
  --listen-port 19000 \
  --send-hex \
  --log tcp-server.jsonl
```

## Test Client Connection

Connect a client and send some hex payload from another terminal:

```bash
python3 -u -c 'import socket
s=socket.create_connection(("127.0.0.1", 19000))
s.sendall(bytes.fromhex("02 10 01 00 03 A7"))
print("Received response:", s.recv(4096).hex())
s.close()
'
```

## Send-File

`--send-file` waits for the first client to connect, sends the file, and exits:

```bash
packet-probe tcp-server \
  --listen-host 127.0.0.1 \
  --listen-port 19000 \
  --send-file command.bin
```

Stdin lines (`--send-text`, `--send-hex`) are likewise held until a client connects.

## Expected Stdout

On server terminal:

```text
connected 127.0.0.1:44650
DEVICE -> APP 6 bytes  02 10 01 00 03 A7
APP -> DEVICE 4 bytes  02 90 00 91
disconnected 127.0.0.1:44650
```

## Expected Directions

- Remote client -> Packet Probe: `device_to_app`
- Packet Probe -> Remote client: `app_to_device`

## Expected JSONL Snippets

```json
{"type":"metadata","schema":"packet-probe.log.v1","event_schema":"packet-probe.event.v1","tool":"packet-probe","version":"0.1.0"}
{"seq":1,"parent_seq":0,"time_ns":1781426521288273031,"session":"tcp-server-1","transport":"tcp","direction":"device_to_app","source":"127.0.0.1:44650","destination":"127.0.0.1:19000","type":"state_change","size":0,"payload_hex":"","summary":"connected 127.0.0.1:44650"}
{"seq":2,"parent_seq":0,"time_ns":1781426521288393851,"session":"tcp-server-1","transport":"tcp","direction":"device_to_app","source":"127.0.0.1:44650","destination":"127.0.0.1:19000","type":"raw_bytes","size":6,"payload_hex":"0210010003A7","summary":"DEVICE -> APP 6 bytes"}
{"seq":3,"parent_seq":0,"time_ns":1781426522072801741,"session":"tcp-server-1","transport":"tcp","direction":"app_to_device","source":"127.0.0.1:19000","destination":"127.0.0.1:44650","type":"raw_bytes","size":4,"payload_hex":"02900091","summary":"APP -> DEVICE 4 bytes"}
{"seq":4,"parent_seq":0,"time_ns":1781426522073873262,"session":"tcp-server-1","transport":"tcp","direction":"device_to_app","source":"127.0.0.1:44650","destination":"127.0.0.1:19000","type":"state_change","size":0,"payload_hex":"","summary":"disconnected 127.0.0.1:44650"}
```
