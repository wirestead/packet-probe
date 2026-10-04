#include "cli_decoder_options.hpp"

#include <stdexcept>

#include "decoder/frame_decoder_factory.hpp"

namespace packet_probe::cli {

bool parse_decoder_option(CliOptions& options, std::string const& arg, int& index, int argc, char** argv) {
  if (arg == "--decoder") {
    if (++index >= argc) {
      throw std::invalid_argument("--decoder requires a value");
    }
    options.decoder_config.decoder = argv[index];
    return true;
  }
  if (arg == "--frame-size") {
    if (++index >= argc) {
      throw std::invalid_argument("--frame-size requires a value");
    }
    options.decoder_config.frame_size = parse_number("--frame-size", argv[index]);
    return true;
  }
  if (arg == "--delimiter") {
    if (++index >= argc) {
      throw std::invalid_argument("--delimiter requires a value");
    }
    options.decoder_config.delimiter = parse_delimiter_bytes(argv[index]);
    return true;
  }
  if (arg == "--include-delimiter") {
    options.decoder_config.include_delimiter = true;
    return true;
  }
  if (arg == "--no-include-delimiter") {
    options.decoder_config.include_delimiter = false;
    return true;
  }
  if (arg == "--length-size") {
    if (++index >= argc) {
      throw std::invalid_argument("--length-size requires a value");
    }
    options.decoder_config.length_size = parse_number("--length-size", argv[index]);
    return true;
  }
  if (arg == "--length-endian") {
    if (++index >= argc) {
      throw std::invalid_argument("--length-endian requires a value");
    }
    options.decoder_config.length_endian = argv[index];
    return true;
  }
  if (arg == "--length-includes-header") {
    options.decoder_config.length_includes_header = true;
    return true;
  }
  return false;
}

}  // namespace packet_probe::cli
