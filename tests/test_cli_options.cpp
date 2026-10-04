#include <cassert>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "../apps/packet-probe-cli/cli_options.hpp"

namespace {

bool throws_invalid_argument(auto fn) {
  try {
    fn();
  } catch (std::invalid_argument const&) {
    return true;
  }
  return false;
}

packet_probe::cli::CliOptions parse(std::initializer_list<char const*> args) {
  auto storage = std::vector<char const*>(args.begin(), args.end());
  return packet_probe::cli::parse_args(static_cast<int>(storage.size()), const_cast<char**>(storage.data()));
}

}  // namespace

int main() {
  auto defaults = parse({"packet-probe", "tcp-client", "--host", "127.0.0.1", "--port", "9000"});
  assert(defaults.send_options.format == packet_probe::SendInputFormat::Text);
  assert(defaults.send_options.file_path.empty());
  packet_probe::cli::validate_options(defaults);

  auto ipc = parse({"packet-probe", "udp", "--bind-host", "127.0.0.1", "--bind-port", "9000", "--ipc",
                    "/tmp/packet-probe.sock"});
  assert(ipc.ipc_path == "/tmp/packet-probe.sock");
  packet_probe::cli::validate_options(ipc);

  assert(throws_invalid_argument([] {
    (void)parse({"packet-probe", "tcp-client", "--host", "127.0.0.1", "--port", "9000", "--send-text",
                 "--send-hex"});
  }));

  assert(throws_invalid_argument([] {
    (void)parse({"packet-probe", "serial", "--port", "/dev/ttyUSB0", "--baudrate", "115200", "--send-file",
                 "command.bin", "--send-hex"});
  }));

  assert(throws_invalid_argument([] {
    (void)parse({"packet-probe", "serial", "--port", "/dev/ttyUSB0", "--baudrate", "115200", "--send-file"});
  }));

  auto file = parse({"packet-probe", "serial", "--port", "/dev/ttyUSB0", "--baudrate", "115200", "--send-file",
                     "command.bin"});
  assert(file.send_options.format == packet_probe::SendInputFormat::File);
  assert(file.send_options.file_path == "command.bin");
  packet_probe::cli::validate_options(file);

  assert(throws_invalid_argument([] {
    auto options = parse({"packet-probe", "udp", "--bind-host", "127.0.0.1", "--bind-port", "9000", "--send-hex"});
    packet_probe::cli::validate_options(options);
  }));

  assert(throws_invalid_argument([] {
    auto options =
        parse({"packet-probe", "udp", "--bind-host", "127.0.0.1", "--bind-port", "9000", "--target-host", "127.0.0.1"});
    packet_probe::cli::validate_options(options);
  }));

  assert(throws_invalid_argument([] {
    auto options = parse({"packet-probe", "udp", "--bind-host", "127.0.0.1", "--bind-port", "9000",
                          "--target-port", "9100"});
    packet_probe::cli::validate_options(options);
  }));

  assert(throws_invalid_argument([] {
    auto options = parse({"packet-probe", "udp", "--bind-host", "127.0.0.1", "--bind-port", "9000", "--send-file",
                          "command.bin"});
    packet_probe::cli::validate_options(options);
  }));

  auto udp_target = parse({"packet-probe", "udp", "--bind-host", "127.0.0.1", "--bind-port", "9000",
                           "--target-host", "127.0.0.1", "--target-port", "9100"});
  packet_probe::cli::validate_options(udp_target);

  // tcp-server validation tests
  assert(throws_invalid_argument([] {
    auto options = parse({"packet-probe", "tcp-server", "--listen-port", "9000"});
    packet_probe::cli::validate_options(options);
  }));

  assert(throws_invalid_argument([] {
    auto options = parse({"packet-probe", "tcp-server", "--listen-host", "127.0.0.1"});
    packet_probe::cli::validate_options(options);
  }));

  auto tcp_server_valid = parse({"packet-probe", "tcp-server", "--listen-host", "127.0.0.1", "--listen-port", "9000"});
  packet_probe::cli::validate_options(tcp_server_valid);

  auto tcp_server_send_hex = parse({"packet-probe", "tcp-server", "--listen-host", "127.0.0.1", "--listen-port", "9000", "--send-hex"});
  assert(tcp_server_send_hex.send_options.format == packet_probe::SendInputFormat::Hex);
  packet_probe::cli::validate_options(tcp_server_send_hex);

  auto tcp_server_ipc = parse({"packet-probe", "tcp-server", "--listen-host", "127.0.0.1", "--listen-port", "9000", "--ipc", "/tmp/packet-probe.sock"});
  assert(tcp_server_ipc.ipc_path == "/tmp/packet-probe.sock");
  packet_probe::cli::validate_options(tcp_server_ipc);

  // Raw byte lines print by default for capture modes; --quiet turns them off, engine stays silent.
  assert(defaults.hex_raw);
  assert(parse({"packet-probe", "tcp-client", "--host", "h", "--port", "9000", "--hex"}).hex_raw);
  assert(!parse({"packet-probe", "tcp-client", "--host", "h", "--port", "9000", "--quiet"}).hex_raw);
  assert(!parse({"packet-probe", "tcp-client", "-q", "--host", "h", "--port", "9000"}).hex_raw);
  assert(!parse({"packet-probe", "engine", "--ipc", "/tmp/pp.sock"}).hex_raw);
  assert(parse({"packet-probe", "serial", "--ascii"}).ascii);

  assert(!parse({"packet-probe", "tcp-proxy", "--no-latency"}).latency);

  auto delimiter = parse({"packet-probe", "serial", "--decoder", "delimiter", "--delimiter", "LF"});
  assert(delimiter.decoder_config.include_delimiter);
  delimiter = parse({"packet-probe", "serial", "--decoder", "delimiter", "--no-include-delimiter"});
  assert(!delimiter.decoder_config.include_delimiter);

  // Bad numbers name the option instead of surfacing std::stoul's message.
  auto error_message = [](std::initializer_list<char const*> args) -> std::string {
    try {
      auto options = parse(args);
      packet_probe::cli::validate_options(options);
    } catch (std::invalid_argument const& ex) {
      return ex.what();
    }
    return {};
  };
  assert(error_message({"packet-probe", "tcp-client", "--host", "h", "--port", "abc"}).find("--port") !=
         std::string::npos);
  assert(error_message({"packet-probe", "tcp-server", "--listen-port", "99999"}).find("--listen-port") !=
         std::string::npos);
  assert(error_message({"packet-probe", "udp", "--bind-port", "0"}).find("--bind-port") != std::string::npos);
  assert(error_message({"packet-probe", "udp", "--target-port", "-1"}).find("--target-port") != std::string::npos);
  assert(error_message({"packet-probe", "serial", "--frame-size", "x"}).find("--frame-size") != std::string::npos);
  assert(error_message({"packet-probe", "serial", "--length-size", ""}).find("--length-size") != std::string::npos);
  assert(error_message({"packet-probe", "tcp-client", "--port", "99999999999999999999"}).find("--port") !=
         std::string::npos);
  assert(error_message({"packet-probe", "--quiet"}).find("missing mode") != std::string::npos);
  assert(error_message({"packet-probe", "foo"}).find("unknown mode: foo") != std::string::npos);
  assert(error_message({"packet-probe", "list-serial-ports"}).empty());

  // Options the mode or decoder ignores produce warnings, not errors.
  using packet_probe::cli::ignored_option_warnings;
  auto udp_baud = parse({"packet-probe", "udp", "--bind-port", "9000", "--baudrate", "9600"});
  auto warnings = ignored_option_warnings(udp_baud);
  assert(warnings.size() == 1 && warnings[0] == "--baudrate is ignored in udp mode");
  assert(ignored_option_warnings(parse({"packet-probe", "tcp-proxy", "--listen-host", "h", "--listen-port", "1",
                                        "--target-host", "h", "--target-port", "2", "--send-hex"}))
             .size() == 1);
  warnings = ignored_option_warnings(parse({"packet-probe", "serial", "--port", "/dev/x", "--frame-size", "8"}));
  assert(warnings.size() == 1 && warnings[0] == "--frame-size is ignored unless --decoder fixed");
  assert(ignored_option_warnings(parse({"packet-probe", "serial", "--port", "/dev/x", "--decoder", "Fixed",
                                        "--frame-size", "8", "--parity", "odd", "-q"}))
             .empty());
  assert(ignored_option_warnings(parse({"packet-probe", "engine", "--ipc", "x", "--log", "a.jsonl"})).size() == 1);
  assert(ignored_option_warnings(parse({"packet-probe", "tcp-client", "--host", "h", "--port", "1", "--latency",
                                        "--latency"}))
             .size() == 1);

  return 0;
}
