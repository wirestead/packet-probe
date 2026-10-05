#include "core/hex_dump.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace packet_probe {

std::string to_hex(std::vector<std::uint8_t> const& payload, bool spaced) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < payload.size(); ++i) {
    if (spaced && i != 0) {
      out << ' ';
    }
    out << std::setw(2) << static_cast<unsigned>(payload[i]);
  }
  return out.str();
}

std::string format_event_line(std::int64_t timestamp_ns, std::string const& direction, std::size_t size,
                              std::vector<std::uint8_t> const& payload) {
  auto const micros_since_epoch = timestamp_ns / 1000;
  auto const seconds_since_epoch = micros_since_epoch / 1000000;
  auto const micros_part = micros_since_epoch % 1000000;

  std::time_t time_value = static_cast<std::time_t>(seconds_since_epoch);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &time_value);
#else
  localtime_r(&time_value, &local_time);
#endif

  std::ostringstream out;
  out << '[' << std::setfill('0') << std::setw(2) << local_time.tm_hour << ':' << std::setw(2)
      << local_time.tm_min << ':' << std::setw(2) << local_time.tm_sec << '.' << std::setw(6)
      << micros_part << "] " << direction << ' ' << size << " bytes";
  auto const hex = to_hex(payload, true);
  if (!hex.empty()) {
    out << "  " << hex;
  }
  return out.str();
}

}  // namespace packet_probe
