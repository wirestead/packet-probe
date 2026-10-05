#include "serial_ports.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace packet_probe::cli {

std::vector<std::string> list_serial_ports() {
  std::vector<std::string> ports;

#if defined(_WIN32)
  char target[512];
  for (int n = 1; n <= 256; ++n) {
    auto const name = "COM" + std::to_string(n);
    if (QueryDosDeviceA(name.c_str(), target, sizeof(target)) != 0) {
      ports.push_back(name);
    }
  }
  // COM ports sort by number, not as text (COM10 after COM9).
  return ports;
#else
  auto starts_with = [](std::string const& value, char const* prefix) { return value.rfind(prefix, 0) == 0; };
  std::error_code ec;
  for (char const* dir : {"/dev/serial/by-id", "/dev"}) {
    if (!std::filesystem::exists(dir, ec) || ec) {
      ec.clear();
      continue;
    }
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    bool const in_dev_root = std::string(dir) == "/dev";
    for (auto const& entry : it) {
      auto const name = entry.path().filename().string();
      if (in_dev_root && !starts_with(name, "ttyUSB") && !starts_with(name, "ttyACM") && !starts_with(name, "cu.")) {
        continue;
      }
      ports.push_back(entry.path().string());
    }
  }

  std::sort(ports.begin(), ports.end());
  ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
  return ports;
#endif
}

}  // namespace packet_probe::cli
