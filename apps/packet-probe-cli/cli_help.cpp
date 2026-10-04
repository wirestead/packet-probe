#include "cli_help.hpp"

#include <ostream>

namespace packet_probe::cli {

namespace {

void print_decoder_options(std::ostream& out) {
  out << "\n"
      << "Frame decoder:\n"
      << "  --decoder <raw|fixed|delimiter|length-prefix>  Frame decoder, default: raw\n"
      << "  --frame-size <bytes>       fixed: frame length\n"
      << "  --delimiter <hex|CRLF|LF>  delimiter: frame boundary, e.g. 0A or 0D0A\n"
      << "  --no-include-delimiter     delimiter: strip the delimiter from frames\n"
      << "                             (kept by default; --include-delimiter keeps it)\n"
      << "  --length-size <1|2|4>      length-prefix: length field size, default: 2\n"
      << "  --length-endian <little|big>  length-prefix: byte order, default: big\n"
      << "  --length-includes-header   length-prefix: length counts the prefix bytes\n";
}

void print_send_options(std::ostream& out, char const* unit) {
  out << "\n"
      << "Send (stdin lines are sent to the target):\n"
      << "  --send-text                Send each stdin line as text " << unit << ", default\n"
      << "  --send-hex                 Parse each stdin line as hex, e.g. \"02 10 FF\"\n"
      << "  --send-file <path>         Send one binary file payload and exit\n";
}

void print_output_options(std::ostream& out, bool latency) {
  out << "\n"
      << "Output:\n"
      << "  --log <path>               Write events as JSONL\n"
      << "  --ipc <path>               Broadcast events as JSONL over a Unix Domain Socket\n"
      << "                             (or tcp:127.0.0.1:<port> for TCP loopback)\n"
      << "  -q, --quiet                Do not print raw byte lines (printed by default)\n"
      << "  --hex-frame                Also print decoded frame lines\n";
  if (latency) {
    out << "  --no-latency               Turn off heuristic request/response latency events\n";
  }
  out << "  --help                     Show this help\n";
}

}  // namespace

void print_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe <mode> [options]\n"
      << "  packet-probe <mode> --help     Show the options for one mode\n"
      << "  packet-probe --version\n"
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
      << "Examples:\n"
      << "  packet-probe tcp-client --host 192.168.0.10 --port 9000\n"
      << "  packet-probe tcp-server --listen-host 0.0.0.0 --listen-port 9000 --log capture.jsonl\n"
      << "  packet-probe tcp-proxy --listen-host 127.0.0.1 --listen-port 9000 \\\n"
      << "                         --target-host 192.168.0.10 --target-port 9000\n"
      << "  packet-probe serial --port /dev/ttyUSB0 --baudrate 115200 --decoder delimiter --delimiter LF\n"
      << "  packet-probe udp --bind-port 9000 --target-host 192.168.0.10 --target-port 9000\n"
      << "  echo \"02 10 01 00 03\" | packet-probe serial --port COM3 --baudrate 9600 --send-hex\n"
      << "\n"
      << "Received and sent bytes are printed as one hex line per event; use --quiet to\n"
      << "only log them. Run 'packet-probe <mode> --help' for every option of a mode.\n";
}

void print_tcp_client_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe tcp-client --host <host> --port <port> [options]\n"
      << "\n"
      << "Connect:\n"
      << "  --host <host>              Target device host\n"
      << "  --port <port>              Target device TCP port\n";
  print_send_options(out, "bytes");
  print_decoder_options(out);
  print_output_options(out, false);
}

void print_tcp_server_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe tcp-server --listen-host <host> --listen-port <port> [options]\n"
      << "\n"
      << "Accepts one client connection per run.\n"
      << "\n"
      << "Listen:\n"
      << "  --listen-host <host>       Local listen host, e.g. 0.0.0.0\n"
      << "  --listen-port <port>       Local listen port\n";
  print_send_options(out, "bytes");
  print_decoder_options(out);
  print_output_options(out, false);
}

void print_tcp_proxy_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe tcp-proxy --listen-host <host> --listen-port <port> "
         "--target-host <host> --target-port <port> [options]\n"
      << "\n"
      << "Proxy:\n"
      << "  --listen-host <host>       Local host your app connects to\n"
      << "  --listen-port <port>       Local port your app connects to\n"
      << "  --target-host <host>       Target device host\n"
      << "  --target-port <port>       Target device TCP port\n";
  print_decoder_options(out);
  print_output_options(out, true);
}

void print_serial_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe serial --port <path> --baudrate <rate> [options]\n"
      << "\n"
      << "Serial port:\n"
      << "  --port <path>              Serial port path, e.g. /dev/ttyUSB0 or COM3\n"
      << "  --baudrate <rate>          Serial baudrate, e.g. 9600, 115200, 921600\n"
      << "  --data-bits <5|6|7|8>      Data bits, default: 8\n"
      << "  --stop-bits <1|2>          Stop bits, default: 1\n"
      << "  --parity <none|odd|even>   Parity, default: none\n"
      << "  --flow-control <none|software|hardware>  Flow control, default: none\n";
  print_send_options(out, "bytes");
  print_decoder_options(out);
  print_output_options(out, false);
}

void print_udp_help(std::ostream& out) {
  out << "Usage:\n"
      << "  packet-probe udp --bind-port <port> [options]\n"
      << "\n"
      << "Records every datagram arriving at the bind address, from any sender.\n"
      << "\n"
      << "Socket:\n"
      << "  --bind-host <host>         UDP bind host, default: 0.0.0.0\n"
      << "  --bind-port <port>         UDP bind port\n"
      << "  --target-host <host>       UDP target host for sends (needs --target-port)\n"
      << "  --target-port <port>       UDP target port for sends (needs --target-host)\n";
  print_send_options(out, "datagrams");
  print_decoder_options(out);
  print_output_options(out, false);
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
  if (options.mode == "tcp-client") {
    print_tcp_client_help(out);
  } else if (options.mode == "tcp-server") {
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
