#include <csignal>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "cli_help.hpp"
#include "cli_options.hpp"
#include "run_modes.hpp"
#include "serial_ports.hpp"
#include "packet_probe/version.hpp"

namespace {

std::sig_atomic_t g_stop_requested = 0;

void handle_signal(int) { g_stop_requested = 1; }

bool stop_requested() { return g_stop_requested != 0; }

int run_mode(packet_probe::cli::CliOptions const& options) {
  using namespace packet_probe::cli;

  if (options.mode == "tcp-client") {
    return run_tcp_client(options, stop_requested);
  }
  if (options.mode == "tcp-server") {
    return run_tcp_server(options, stop_requested);
  }
  if (options.mode == "tcp-proxy") {
    return run_tcp_proxy(options, stop_requested);
  }
  if (options.mode == "serial") {
    return run_serial(options, stop_requested);
  }
  if (options.mode == "udp") {
    return run_udp(options, stop_requested);
  }
  if (options.mode == "engine") {
    return run_engine(options, stop_requested);
  }
  if (options.mode == "list-serial-ports") {
    auto const ports = list_serial_ports();
    for (auto const& port : ports) {
      std::cout << port << '\n';
    }
    if (ports.empty()) {
      std::cerr << "no serial ports found\n";
    }
    return 0;
  }

  throw std::invalid_argument("unknown or missing mode: " + options.mode);
}

// The first argument naming a known mode, so a usage error can point at that mode's help.
std::string mode_in_args(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string const arg = argv[i];
    if (arg == "tcp-client" || arg == "tcp-server" || arg == "tcp-proxy" || arg == "serial" || arg == "udp" ||
        arg == "engine" || arg == "list-serial-ports") {
      return arg;
    }
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  try {
    auto const options = packet_probe::cli::parse_args(argc, argv);
    if (options.help || argc == 1) {
      packet_probe::cli::print_help_for_mode(std::cout, options);
      return 0;
    }
    if (options.version) {
      std::cout << "packet-probe " << PACKET_PROBE_VERSION << '\n';
      return 0;
    }

    packet_probe::cli::validate_options(options);
    for (auto const& warning : packet_probe::cli::ignored_option_warnings(options)) {
      std::cerr << "packet-probe: warning: " << warning << '\n';
    }
    return run_mode(options);
  } catch (std::exception const& ex) {
    std::cerr << "packet-probe: " << ex.what() << '\n';
    auto const mode = mode_in_args(argc, argv);
    std::cerr << "Run 'packet-probe " << (mode.empty() ? "" : mode + " ") << "--help' for usage.\n";
    return 2;
  }
}
