#include "cli_help.hpp"

#include <ostream>

namespace packet_probe::cli {

void print_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe [--help] [--version]\n"
      << "  packet-probe tcp-client --host <host> --port <port> [--log <path>] [--ipc <path>] [--hex]\n"
      << "  packet-probe tcp-server --listen-host <host> --listen-port <port> [--log <path>] [--ipc <path>] [--hex]\n"
      << "  packet-probe tcp-proxy --listen-host <host> --listen-port <port> "
         "--target-host <host> --target-port <port> [--log <path>] [--ipc <path>] [--hex] [--latency]\n"
      << "  packet-probe serial --port <path> --baudrate <rate> [--log <path>] [--ipc <path>] [--hex]\n"
      << "  packet-probe udp --bind-host <host> --bind-port <port> [--target-host <host>] "
         "[--target-port <port>] [--log <path>] [--ipc <path>] [--hex]\n"
      << "  packet-probe engine --ipc <path>\n"
      << "\n"
      << "Modes:\n"
      << "  tcp-client    Connect directly to a TCP target device\n"
      << "  tcp-server    Listen for a remote TCP client connection\n"
      << "  tcp-proxy     Listen locally and proxy one TCP client to a target device\n"
      << "  serial        Connect directly to a serial target device\n"
      << "  udp           Bind a UDP socket and inspect datagrams\n"
      << "  engine        Start idle and accept configure/start_capture/stop_capture\n"
      << "                commands over IPC (see docs/ipc-protocol.md)\n"
      << "\n"
      << "Options:\n"
      << "  --host <host>     Target host for tcp-client mode\n"
      << "  --port <port>     Target TCP port for tcp-client mode\n"
      << "  --listen-host <host>  Local listen host for tcp-proxy mode\n"
      << "  --listen-port <port>  Local listen port for tcp-proxy mode\n"
      << "  --bind-host <host>    UDP bind host, default: 0.0.0.0\n"
      << "  --bind-port <port>    UDP bind port\n"
      << "  --target-host <host>  Target host for tcp-proxy or UDP send mode\n"
      << "  --target-port <port>  Target port for tcp-proxy or UDP send mode\n"
      << "  --baudrate <rate>     Serial baudrate for serial mode\n"
      << "  --data-bits <5|6|7|8> Serial data bits, default: 8\n"
      << "  --stop-bits <1|2>     Serial stop bits, default: 1\n"
      << "  --parity <none|odd|even>  Serial parity, default: none\n"
      << "  --flow-control <none|software|hardware>  Serial flow control, default: none\n"
      << "  --decoder <raw|fixed|delimiter|length-prefix>  Frame decoder, default: raw\n"
      << "  --frame-size <bytes>  Fixed-size frame length\n"
      << "  --delimiter <hex|CRLF|LF>  Delimiter frame boundary\n"
      << "  --include-delimiter   Include delimiter in delimiter frames, default: enabled\n"
      << "  --length-size <1|2|4> Length-prefix field size, default: 2\n"
      << "  --length-endian <little|big>  Length-prefix endian, default: big\n"
      << "  --length-includes-header  Length includes the prefix bytes\n"
      << "  --send-text       Send stdin lines as text bytes, default\n"
      << "  --send-hex        Parse stdin lines as hex bytes before sending\n"
      << "  --send-file <path>  Send one binary file payload and exit\n"
      << "  --log <path>      Write events as JSONL\n"
      << "  --ipc <path>      Broadcast events as JSONL over a Unix Domain Socket\n"
      << "                        (or tcp:127.0.0.1:<port> for TCP loopback)\n"
      << "  --hex             Print one-line hex output for raw byte events\n"
      << "  --hex-raw         Print one-line hex output for raw byte events\n"
      << "  --hex-frame       Print one-line hex output for frame events\n"
      << "  --latency         Enable heuristic request/response latency events\n"
      << "  --help            Show this help\n"
      << "  --version         Show version\n";
}

void print_tcp_server_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe tcp-server --listen-host <host> --listen-port <port> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --listen-host <host>  Local listen host\n"
      << "  --listen-port <port>  Local listen port\n"
      << "  --decoder <raw|fixed|delimiter|length-prefix>\n"
      << "  --send-text           Send stdin lines as text bytes, default\n"
      << "  --send-hex            Parse stdin lines as hex bytes before sending\n"
      << "  --send-file <path>    Send one binary file payload and exit\n"
      << "  --log <path>          Write events as JSONL\n"
      << "  --ipc <path>          Broadcast events as JSONL over a Unix Domain Socket\n"
      << "                        (or tcp:127.0.0.1:<port> for TCP loopback)\n"
      << "  --hex                 Print one-line hex output for raw byte events\n"
      << "  --hex-frame           Print one-line hex output for frame events\n"
      << "  --help                Show this help\n";
}

void print_tcp_proxy_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe tcp-proxy --listen-host <host> --listen-port <port> "
         "--target-host <host> --target-port <port> [--log <path>] [--hex] [--latency]\n"
      << "\n"
      << "Options:\n"
      << "  --listen-host <host>  Local listen host\n"
      << "  --listen-port <port>  Local listen port\n"
      << "  --target-host <host>  Target device host\n"
      << "  --target-port <port>  Target device TCP port\n"
      << "  --decoder <raw|fixed|delimiter|length-prefix>\n"
      << "  --log <path>          Write events as JSONL\n"
      << "  --ipc <path>          Broadcast events as JSONL over a Unix Domain Socket\n"
      << "                        (or tcp:127.0.0.1:<port> for TCP loopback)\n"
      << "  --hex                 Print one-line hex output for raw byte events\n"
      << "  --hex-frame           Print one-line hex output for frame events\n"
      << "  --latency             Enable heuristic request/response latency events\n"
      << "  --help                Show this help\n";
}

void print_serial_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe serial --port <path> --baudrate <rate> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --port <path>              Serial port path, e.g. /dev/ttyUSB0 or COM3\n"
      << "  --baudrate <rate>          Serial baudrate, e.g. 9600, 115200, 921600\n"
      << "  --data-bits <5|6|7|8>      Data bits, default: 8\n"
      << "  --stop-bits <1|2>          Stop bits, default: 1\n"
      << "  --parity <none|odd|even>   Parity, default: none\n"
      << "  --flow-control <none|software|hardware>\n"
      << "  --decoder <raw|fixed|delimiter|length-prefix>\n"
      << "  --delimiter <hex|CRLF|LF>  Delimiter frame boundary\n"
      << "  --send-text                Send stdin lines as text bytes, default\n"
      << "  --send-hex                 Parse stdin lines as hex bytes before sending\n"
      << "  --send-file <path>         Send one binary file payload and exit\n"
      << "  --log <path>               Write events as JSONL\n"
      << "  --ipc <path>               Broadcast events as JSONL over a Unix Domain Socket\n"
      << "                        (or tcp:127.0.0.1:<port> for TCP loopback)\n"
      << "  --hex                      Print one-line hex output for raw byte events\n"
      << "  --hex-frame                Print one-line hex output for frame events\n"
      << "  --help                     Show this help\n";
}

void print_udp_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe udp --bind-host <host> --bind-port <port> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --bind-host <host>         UDP bind host, default: 0.0.0.0\n"
      << "  --bind-port <port>         UDP bind port\n"
      << "  --target-host <host>       Optional UDP target host for stdin sends\n"
      << "  --target-port <port>       Optional UDP target port for stdin sends\n"
      << "  --decoder <raw|fixed|delimiter|length-prefix>\n"
      << "  --send-text                Send stdin lines as text datagrams, default\n"
      << "  --send-hex                 Parse stdin lines as hex datagrams before sending\n"
      << "  --send-file <path>         Send one binary file datagram and exit\n"
      << "  --log <path>               Write events as JSONL\n"
      << "  --ipc <path>               Broadcast events as JSONL over a Unix Domain Socket\n"
      << "                        (or tcp:127.0.0.1:<port> for TCP loopback)\n"
      << "  --hex                      Print one-line hex output for raw byte events\n"
      << "  --hex-frame                Print one-line hex output for frame events\n"
      << "  --help                     Show this help\n";
}

void print_engine_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe engine --ipc <path>\n"
      << "\n"
      << "Starts idle (no capture session) and waits for IPC control commands on\n"
      << "<path>: configure, start_capture, stop_capture, get_status,\n"
      << "list_serial_ports, send. See docs/ipc-protocol.md for the message schema.\n"
      << "\n"
      << "Options:\n"
      << "  --ipc <path>          Unix Domain Socket path for the IPC control channel,\n"
      << "                        or tcp:127.0.0.1:<port> for TCP loopback (Windows)\n"
      << "  --help                Show this help\n";
}

void print_help_for_mode(std::ostream& out, CliOptions const& options) {
  if (options.mode == "tcp-server") {
    print_tcp_server_help(out);
  } else if (options.mode == "tcp-proxy") {
    print_tcp_proxy_help(out);
  } else if (options.mode == "serial") {
    print_serial_help(out);
  } else if (options.mode == "udp") {
    print_udp_help(out);
  } else if (options.mode == "engine") {
    print_engine_help(out);
  } else {
    print_help(out);
  }
}

}  // namespace packet_probe::cli
