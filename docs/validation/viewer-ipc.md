# Viewer IPC Validation

## Purpose

Validate that the browser viewer (`packet-probe-web`) can start a capture through
the engine, show live events, and send data.

## Requirements

- Built `packet-probe`
- Python 3.10+ with `wirestead-python` and the viewer installed (see
  [viewer/README.md](../../viewer/README.md))
- A browser

## Launch and capture (UDP)

1. Start the gateway:

   ```sh
   packet-probe-web
   ```

   Open the printed `http://127.0.0.1:8080/?token=...` URL.

2. In the left panel choose **UDP**, bind `127.0.0.1` port `19000`, leave the
   send-to target empty, and click **Start capture**.

3. Send a test datagram:

   ```sh
   python3 - <<'PY'
   import socket
   s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   s.sendto(bytes.fromhex("02 10 01 00 03 A7"), ("127.0.0.1", 19000))
   PY
   ```

### Expected results

* The app bar shows `engine connected` and `Capturing · udp`.
* The table shows an RX `raw_bytes` event of 6 bytes (and its `frame` event).
* Selecting the row shows the payload in the Hex, Text, and JSON tabs.
* **Stop** returns the state to `Idle`; **Start capture** works again without
  restarting the gateway.

### Additional checks

- Pause, send datagrams, then Resume: the held events appear.
- Clear empties the table and the detail panel.
- Open the same URL in a second browser tab: both show the same events and state.
- Open the URL without `?token=`: the page reports the missing token and receives
  no events.
- Stop the gateway with Ctrl+C: the spawned `packet-probe engine` exits too.
- Start a second gateway on a port already in use: it exits with
  `cannot listen on ...` and does not leave an engine running.

## Send validation (TCP client)

1. Start a TCP echo server on port 19100 (e.g. `python3 mock_device.py --mode tcp --port 19100`
   or `ncat -l 19100 -k --exec /bin/cat`).
2. In the viewer choose **TCP Client**, host `127.0.0.1` port `19100`, and click
   **Start capture**.
3. In **Send**, choose **Hex**, enter `AA BB CC`, and click **Send**.

### Expected results

* A TX `raw_bytes` event of 3 bytes appears, followed by the echoed RX event.
* The message area reports `Sent 3 bytes`.

### Additional checks

- In **TCP Proxy** mode the Send button is disabled.
- In **UDP** mode without a send-to target the Send button is disabled; with a
  target set, Send works.

## Attach to a running engine

```sh
packet-probe engine --ipc tcp:127.0.0.1:19500
packet-probe-web --ipc tcp:127.0.0.1:19500
```

### Expected results

* The viewer reflects the engine's current state (idle or capturing) on load.
* Stopping the gateway does not stop the engine it attached to.
