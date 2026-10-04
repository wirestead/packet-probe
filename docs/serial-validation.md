# Serial Validation

Serial Direct Mode requires either physical serial hardware or a virtual serial pair.
These checks are intentionally manual and are not part of the default CTest suite.

## Linux

Use a loopback plug on a physical serial adapter, or create a PTY pair with `socat`:

```sh
socat -d -d pty,raw,echo=0 pty,raw,echo=0
```

The command prints two PTY paths, for example `/dev/pts/3` and `/dev/pts/4`.

Run Packet Probe on one side:

```sh
packet-probe serial --port /dev/pts/3 --baudrate 115200 --log serial.jsonl
```

From another terminal, write bytes to the peer:

```sh
printf "hello" > /dev/pts/4
```

Validation criteria:

- serial RX event is printed
- `serial.jsonl` contains `"transport":"serial"`
- `payload_hex` records the received bytes

For TX validation, type a line into the Packet Probe terminal and verify it appears
on the peer PTY.

## Windows

Use a physical COM loopback adapter or a virtual COM pair tool. Run:

```sh
packet-probe serial --port COM3 --baudrate 115200 --log serial.jsonl
```

Validation criteria are the same as Linux:

- serial RX event is printed
- `serial.jsonl` contains `"transport":"serial"`
- `payload_hex` records the received bytes
