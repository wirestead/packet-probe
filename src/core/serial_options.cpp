#include "capture/serial_options.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace packet_probe {

namespace {

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

unsigned long parse_unsigned(std::string const& value, char const* name) {
  std::size_t parsed = 0;
  unsigned long number = 0;
  try {
    number = std::stoul(value, &parsed, 10);
  } catch (std::logic_error const&) {
    parsed = 0;
  }
  if (value.empty() || parsed != value.size() || value[0] == '-' || value[0] == '+') {
    throw std::invalid_argument(std::string("invalid ") + name + " value: '" + value + "' (expected a number)");
  }
  return number;
}

}  // namespace

SerialParity parse_serial_parity(std::string const& value) {
  auto normalized = lower(value);
  if (normalized == "none") {
    return SerialParity::None;
  }
  if (normalized == "odd") {
    return SerialParity::Odd;
  }
  if (normalized == "even") {
    return SerialParity::Even;
  }
  throw std::invalid_argument("invalid --parity value: " + value);
}

SerialFlowControl parse_serial_flow_control(std::string const& value) {
  auto normalized = lower(value);
  if (normalized == "none") {
    return SerialFlowControl::None;
  }
  if (normalized == "software") {
    return SerialFlowControl::Software;
  }
  if (normalized == "hardware") {
    return SerialFlowControl::Hardware;
  }
  throw std::invalid_argument("invalid --flow-control value: " + value);
}

std::uint8_t parse_serial_data_bits(std::string const& value) {
  auto bits = parse_unsigned(value, "--data-bits");
  if (bits < 5 || bits > 8) {
    throw std::invalid_argument("invalid --data-bits value: " + value);
  }
  return static_cast<std::uint8_t>(bits);
}

std::uint8_t parse_serial_stop_bits(std::string const& value) {
  auto bits = parse_unsigned(value, "--stop-bits");
  if (bits != 1 && bits != 2) {
    throw std::invalid_argument("invalid --stop-bits value: " + value);
  }
  return static_cast<std::uint8_t>(bits);
}

std::uint32_t parse_serial_baudrate(std::string const& value) {
  auto baudrate = parse_unsigned(value, "--baudrate");
  if (baudrate == 0 || baudrate > 4000000UL) {
    throw std::invalid_argument("invalid --baudrate value: " + value);
  }
  return static_cast<std::uint32_t>(baudrate);
}

const char* to_string(SerialParity parity) {
  switch (parity) {
    case SerialParity::None:
      return "none";
    case SerialParity::Odd:
      return "odd";
    case SerialParity::Even:
      return "even";
  }
  return "none";
}

const char* to_string(SerialFlowControl flow_control) {
  switch (flow_control) {
    case SerialFlowControl::None:
      return "none";
    case SerialFlowControl::Software:
      return "software";
    case SerialFlowControl::Hardware:
      return "hardware";
  }
  return "none";
}

}  // namespace packet_probe
