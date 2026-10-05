#pragma once

#include <cstdint>
#include <string>

#include "packet_probe/decoder/decoder_config.hpp"
#include "core/send_input.hpp"
#include "capture/serial_options.hpp"

namespace packet_probe::cli {

struct CliOptions {
  std::string mode;
  std::string host;
  std::uint16_t port = 0;
  std::string serial_port;
  std::uint32_t baudrate = 115200;
  std::uint8_t data_bits = 8;
  std::uint8_t stop_bits = 1;
  SerialParity parity = SerialParity::None;
  SerialFlowControl flow_control = SerialFlowControl::None;
  std::string listen_host;
  std::uint16_t listen_port = 0;
  std::string bind_host = "0.0.0.0";
  std::uint16_t bind_port = 0;
  std::string target_host;
  std::uint16_t target_port = 0;
  std::string log_path;
  std::string ipc_path;
  DecoderConfig decoder_config;
  SendInputOptions send_options;
  int send_option_count = 0;
  bool hex_raw = false;  // parse_args() turns this on for capture modes unless --quiet
  bool quiet = false;
  bool hex_frame = false;
  bool latency = true;
  bool help = false;
  bool version = false;
};

// Parses a base-10 unsigned number for `option`, reporting the option name instead of
// the raw std::stoul error on bad input.
unsigned long parse_number(std::string const& option, std::string const& value);
std::uint16_t parse_port(std::string const& value, std::string const& option = "--port");
CliOptions parse_args(int argc, char** argv);
void validate_options(CliOptions const& options);

}  // namespace packet_probe::cli
