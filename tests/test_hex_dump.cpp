#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "core/hex_dump.hpp"

int main() {
  std::vector<std::uint8_t> payload{0x02, 0x10, 0x01, 0x00, 0x03, 0xA7};
  assert(packet_probe::to_hex(payload, true) == "02 10 01 00 03 A7");
  assert(packet_probe::to_hex(payload, false) == "0210010003A7");

  std::vector<std::uint8_t> empty;
  assert(packet_probe::to_hex(empty, true).empty());
  assert(packet_probe::to_hex(empty, false).empty());

  auto line = packet_probe::format_event_line(1781234567890000000LL, "RX", payload.size(), payload);
  assert(line.find("RX 6 bytes  02 10 01 00 03 A7") != std::string::npos);

  return 0;
}
