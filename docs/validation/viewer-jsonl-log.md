# Viewer JSONL Log Validation

## Purpose

Validate that the browser viewer can open a saved JSONL log and display its events.

## Generate log

```sh
packet-probe udp \
  --bind-host 127.0.0.1 \
  --bind-port 19000 \
  --log udp.jsonl
```

Send a test datagram:

```sh
python3 - <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.sendto(bytes.fromhex("02 10 01 00 03 A7"), ("127.0.0.1", 19000))
PY
```

Stop Packet Probe.

## Open log

Start `packet-probe-web`, open the printed URL, click **Open log…** in the toolbar,
and select `udp.jsonl`.

## Expected result

* The table shows the recorded events; the metadata line is not shown as an event.
* The message area reports the file name and event count, plus the number of
  malformed lines skipped, if any.
* Selecting an event shows its payload in the Hex and Text tabs and the full event
  in the JSON tab.
* The file is read in the browser; nothing is uploaded to the gateway.
