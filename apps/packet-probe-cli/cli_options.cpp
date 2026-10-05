#include "cli_options.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <stdexcept>

#include "cli_decoder_options.hpp"
#include "cli_send_options.hpp"

namespace packet_probe::cli {

unsigned long parse_number(std::string const& option, std::string const& value) {
  std::size_t parsed = 0;
  unsigned long number = 0;
  try {
    number = std::stoul(value, &parsed, 10);
  } catch (std::logic_error const&) {
    parsed = 0;
  }
  if (value.empty() || parsed != value.size() || value[0] == '-' || value[0] == '+') {
    throw std::invalid_argument("invalid " + option + " value: '" + value + "' (expected a number)");
  }
  return number;
}

std::uint16_t parse_port(std::string const& value, std::string const& option) {
  auto const port = parse_number(option, value);
  if (port == 0 || port > 65535) {
    throw std::invalid_argument("invalid " + option + " value: " + value + " (expected 1-65535)");
  }
  return static_cast<std::uint16_t>(port);
}

CliOptions parse_args(int argc, char** argv) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.size() > 1 && arg[0] == '-') {
      options.given_options.push_back(arg);
    }
    if (arg == "--help") {
      options.help = true;
    } else if (arg == "--version") {
      options.version = true;
    } else if (arg == "--hex" || arg == "--hex-raw") {
      // Raw byte lines are printed by default now; kept so existing scripts still parse.
    } else if (arg == "--quiet" || arg == "-q") {
      options.quiet = true;
    } else if (arg == "--ascii") {
      options.ascii = true;
    } else if (arg == "--hex-frame") {
      options.hex_frame = true;
    } else if (arg == "--latency") {
      options.latency = true;
    } else if (arg == "--no-latency") {
      options.latency = false;
    } else if (parse_send_option(options, arg, i, argc, argv)) {
    } else if (parse_decoder_option(options, arg, i, argc, argv)) {
    } else if (arg == "--host") {
      if (++i >= argc) {
        throw std::invalid_argument("--host requires a value");
      }
      options.host = argv[i];
    } else if (arg == "--port") {
      if (++i >= argc) {
        throw std::invalid_argument("--port requires a value");
      }
      if (options.mode == "serial") {
        options.serial_port = argv[i];
      } else {
        options.port = parse_port(argv[i]);
      }
    } else if (arg == "--baudrate") {
      if (++i >= argc) {
        throw std::invalid_argument("--baudrate requires a value");
      }
      options.baudrate = parse_serial_baudrate(argv[i]);
    } else if (arg == "--data-bits") {
      if (++i >= argc) {
        throw std::invalid_argument("--data-bits requires a value");
      }
      options.data_bits = parse_serial_data_bits(argv[i]);
    } else if (arg == "--stop-bits") {
      if (++i >= argc) {
        throw std::invalid_argument("--stop-bits requires a value");
      }
      options.stop_bits = parse_serial_stop_bits(argv[i]);
    } else if (arg == "--parity") {
      if (++i >= argc) {
        throw std::invalid_argument("--parity requires a value");
      }
      options.parity = parse_serial_parity(argv[i]);
    } else if (arg == "--flow-control") {
      if (++i >= argc) {
        throw std::invalid_argument("--flow-control requires a value");
      }
      options.flow_control = parse_serial_flow_control(argv[i]);
    } else if (arg == "--listen-host") {
      if (++i >= argc) {
        throw std::invalid_argument("--listen-host requires a value");
      }
      options.listen_host = argv[i];
    } else if (arg == "--listen-port") {
      if (++i >= argc) {
        throw std::invalid_argument("--listen-port requires a value");
      }
      options.listen_port = parse_port(argv[i], "--listen-port");
    } else if (arg == "--bind-host") {
      if (++i >= argc) {
        throw std::invalid_argument("--bind-host requires a value");
      }
      options.bind_host = argv[i];
    } else if (arg == "--bind-port") {
      if (++i >= argc) {
        throw std::invalid_argument("--bind-port requires a value");
      }
      options.bind_port = parse_port(argv[i], "--bind-port");
    } else if (arg == "--target-host") {
      if (++i >= argc) {
        throw std::invalid_argument("--target-host requires a value");
      }
      options.target_host = argv[i];
    } else if (arg == "--target-port") {
      if (++i >= argc) {
        throw std::invalid_argument("--target-port requires a value");
      }
      options.target_port = parse_port(argv[i], "--target-port");
    } else if (arg == "--log") {
      if (++i >= argc) {
        throw std::invalid_argument("--log requires a value");
      }
      options.log_path = argv[i];
    } else if (arg == "--ipc") {
      if (++i >= argc) {
        throw std::invalid_argument("--ipc requires a value");
      }
      options.ipc_path = argv[i];
    } else if (!arg.empty() && arg[0] == '-') {
      throw std::invalid_argument("unknown option: " + arg);
    } else if (options.mode.empty()) {
      options.mode = arg;
    } else {
      throw std::invalid_argument("unexpected argument: " + arg);
    }
  }
  // The engine is driven over IPC and stays silent; capture modes show traffic unless --quiet.
  options.hex_raw = options.mode != "engine" && options.mode != "list-serial-ports" && !options.quiet;
  return options;
}

void validate_options(CliOptions const& options) {
  if (options.mode == "list-serial-ports") {
    return;
  }
  if (options.mode == "engine") {
    if (options.ipc_path.empty()) {
      throw std::invalid_argument("engine requires --ipc");
    }
    return;
  }
  if (options.mode == "tcp-client") {
    if (options.host.empty()) {
      throw std::invalid_argument("tcp-client requires --host");
    }
    if (options.port == 0) {
      throw std::invalid_argument("tcp-client requires --port");
    }
    return;
  }
  if (options.mode == "tcp-server") {
    if (options.listen_host.empty()) {
      throw std::invalid_argument("tcp-server requires --listen-host");
    }
    if (options.listen_port == 0) {
      throw std::invalid_argument("tcp-server requires --listen-port");
    }
    return;
  }
  if (options.mode == "tcp-proxy") {
    if (options.listen_host.empty()) {
      throw std::invalid_argument("tcp-proxy requires --listen-host");
    }
    if (options.listen_port == 0) {
      throw std::invalid_argument("tcp-proxy requires --listen-port");
    }
    if (options.target_host.empty()) {
      throw std::invalid_argument("tcp-proxy requires --target-host");
    }
    if (options.target_port == 0) {
      throw std::invalid_argument("tcp-proxy requires --target-port");
    }
    return;
  }
  if (options.mode == "serial") {
    if (options.serial_port.empty()) {
      throw std::invalid_argument("serial requires --port");
    }
    if (options.baudrate == 0) {
      throw std::invalid_argument("serial requires --baudrate");
    }
    return;
  }
  if (options.mode == "udp") {
    if (options.bind_host.empty()) {
      throw std::invalid_argument("udp requires --bind-host");
    }
    if (options.bind_port == 0) {
      throw std::invalid_argument("udp requires --bind-port");
    }
    auto const has_target_host = !options.target_host.empty();
    auto const has_target_port = options.target_port != 0;
    if (has_target_host != has_target_port) {
      throw std::invalid_argument("udp target requires both --target-host and --target-port");
    }
    if (options.send_option_count > 0 && (!has_target_host || !has_target_port)) {
      throw std::invalid_argument("udp send input requires --target-host and --target-port");
    }
    return;
  }
  auto const modes = std::string(" (expected tcp-client, tcp-server, tcp-proxy, serial, udp, engine, or list-serial-ports)");
  if (options.mode.empty()) {
    throw std::invalid_argument("missing mode" + modes);
  }
  throw std::invalid_argument("unknown mode: " + options.mode + modes);
}

namespace {

bool contains(std::initializer_list<char const*> list, std::string const& value) {
  return std::any_of(list.begin(), list.end(), [&](char const* item) { return value == item; });
}

}  // namespace

std::vector<std::string> ignored_option_warnings(CliOptions const& options) {
  std::initializer_list<char const*> const common = {
      "--help", "--version", "--hex", "--hex-raw", "--quiet", "-q", "--ascii", "--hex-frame", "--log", "--ipc", "--decoder",
      "--frame-size", "--delimiter", "--include-delimiter", "--no-include-delimiter", "--length-size",
      "--length-endian", "--length-includes-header"};
  std::initializer_list<char const*> const send = {"--send-text", "--send-hex", "--send-file"};
  std::initializer_list<char const*> const serial = {"--port", "--baudrate", "--data-bits", "--stop-bits",
                                                     "--parity", "--flow-control"};

  auto const& mode = options.mode;
  auto mode_accepts = [&](std::string const& option) {
    if (mode == "engine") {
      return contains({"--help", "--version", "--ipc"}, option);
    }
    if (mode == "list-serial-ports") {
      return contains({"--help", "--version"}, option);
    }
    if (contains(common, option)) {
      return true;
    }
    if (mode == "tcp-client") {
      return contains({"--host", "--port"}, option) || contains(send, option);
    }
    if (mode == "tcp-server") {
      return contains({"--listen-host", "--listen-port"}, option) || contains(send, option);
    }
    if (mode == "tcp-proxy") {
      return contains({"--listen-host", "--listen-port", "--target-host", "--target-port", "--latency",
                       "--no-latency"},
                      option);
    }
    if (mode == "serial") {
      return contains(serial, option) || contains(send, option);
    }
    if (mode == "udp") {
      return contains({"--bind-host", "--bind-port", "--target-host", "--target-port"}, option) ||
             contains(send, option);
    }
    return true;  // unknown modes are reported by validate_options()
  };

  // Decoder sub-options only apply to their own decoder.
  auto decoder = options.decoder_config.decoder;
  std::transform(decoder.begin(), decoder.end(), decoder.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  auto decoder_ignores = [&](std::string const& option) -> char const* {
    if (option == "--frame-size" && decoder != "fixed") return "fixed";
    if ((option == "--delimiter" || option == "--include-delimiter" || option == "--no-include-delimiter") &&
        decoder != "delimiter") {
      return "delimiter";
    }
    if ((option == "--length-size" || option == "--length-endian" || option == "--length-includes-header") &&
        decoder != "length-prefix") {
      return "length-prefix";
    }
    return nullptr;
  };

  std::vector<std::string> warnings;
  for (auto const& option : options.given_options) {
    std::string message;
    if (!mode_accepts(option)) {
      message = option + " is ignored in " + mode + " mode";
    } else if (decoder_ignores(option) != nullptr) {
      message = option + " is ignored unless --decoder " + decoder_ignores(option);
    }
    if (!message.empty() &&
        std::none_of(warnings.begin(), warnings.end(), [&](std::string const& w) { return w == message; })) {
      warnings.push_back(std::move(message));
    }
  }
  return warnings;
}

}  // namespace packet_probe::cli
