#pragma once

#include <string>
#include <vector>

namespace packet_probe::cli {

// Serial ports present on this machine, sorted: /dev/serial/by-id and /dev/ttyUSB*/ttyACM*
// on Linux, /dev/cu.* on macOS, and COM ports on Windows.
std::vector<std::string> list_serial_ports();

}  // namespace packet_probe::cli
