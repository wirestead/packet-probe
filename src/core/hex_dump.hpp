#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace packet_probe {

std::string to_hex(std::vector<std::uint8_t> const& payload, bool spaced = true);
std::string format_event_line(std::int64_t timestamp_ns, std::string const& direction, std::size_t size,
                              std::vector<std::uint8_t> const& payload);

}  // namespace packet_probe
