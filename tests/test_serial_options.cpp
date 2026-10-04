#include <cassert>
#include <stdexcept>
#include <string>

#include "capture/serial_options.hpp"

namespace {

template <typename Fn>
bool throws_invalid_argument(Fn fn) {
  try {
    fn();
  } catch (std::invalid_argument const&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  packet_probe::SerialCaptureOptions defaults;
  assert(defaults.baudrate == 115200);
  assert(defaults.data_bits == 8);
  assert(defaults.stop_bits == 1);
  assert(defaults.parity == packet_probe::SerialParity::None);
  assert(defaults.flow_control == packet_probe::SerialFlowControl::None);
  assert(defaults.session_id == "serial-1");

  assert(packet_probe::parse_serial_parity("none") == packet_probe::SerialParity::None);
  assert(packet_probe::parse_serial_parity("odd") == packet_probe::SerialParity::Odd);
  assert(packet_probe::parse_serial_parity("even") == packet_probe::SerialParity::Even);
  assert(packet_probe::parse_serial_parity("EVEN") == packet_probe::SerialParity::Even);
  assert(std::string(packet_probe::to_string(packet_probe::SerialParity::Odd)) == "odd");
  assert(throws_invalid_argument([] { packet_probe::parse_serial_parity("mark"); }));

  assert(packet_probe::parse_serial_flow_control("none") == packet_probe::SerialFlowControl::None);
  assert(packet_probe::parse_serial_flow_control("software") == packet_probe::SerialFlowControl::Software);
  assert(packet_probe::parse_serial_flow_control("hardware") == packet_probe::SerialFlowControl::Hardware);
  assert(packet_probe::parse_serial_flow_control("HARDWARE") == packet_probe::SerialFlowControl::Hardware);
  assert(std::string(packet_probe::to_string(packet_probe::SerialFlowControl::Software)) == "software");
  assert(throws_invalid_argument([] { packet_probe::parse_serial_flow_control("xonxoff"); }));

  assert(packet_probe::parse_serial_data_bits("5") == 5);
  assert(packet_probe::parse_serial_data_bits("8") == 8);
  assert(throws_invalid_argument([] { packet_probe::parse_serial_data_bits("4"); }));
  assert(throws_invalid_argument([] { packet_probe::parse_serial_data_bits("9"); }));
  assert(throws_invalid_argument([] { packet_probe::parse_serial_data_bits("8x"); }));

  assert(packet_probe::parse_serial_stop_bits("1") == 1);
  assert(packet_probe::parse_serial_stop_bits("2") == 2);
  assert(throws_invalid_argument([] { packet_probe::parse_serial_stop_bits("0"); }));
  assert(throws_invalid_argument([] { packet_probe::parse_serial_stop_bits("3"); }));

  assert(packet_probe::parse_serial_baudrate("9600") == 9600);
  assert(packet_probe::parse_serial_baudrate("115200") == 115200);
  assert(throws_invalid_argument([] { packet_probe::parse_serial_baudrate("0"); }));
  assert(throws_invalid_argument([] { packet_probe::parse_serial_baudrate("fast"); }));
  try {
    (void)packet_probe::parse_serial_baudrate("fast");
  } catch (std::invalid_argument const& ex) {
    assert(std::string(ex.what()).find("--baudrate") != std::string::npos);
  }

  return 0;
}
