#include "run_common.hpp"

#include <utility>

namespace packet_probe::cli {

bool stdin_is_terminal() {
#if defined(_WIN32)
  return _isatty(_fileno(stdin)) != 0;
#else
  return isatty(fileno(stdin)) != 0;
#endif
}

std::vector<std::uint8_t> parse_send_line(std::string const& line, SendInputOptions const& send_options) {
  switch (send_options.format) {
    case SendInputFormat::Text:
      return parse_text_payload(line);
    case SendInputFormat::Hex:
      return parse_hex_payload(line);
    case SendInputFormat::File:
      break;
  }
  throw std::logic_error("send-file does not parse stdin lines");
}

void print_event(PacketEvent const& event, bool hex_raw_enabled, bool hex_frame_enabled) {
  if (event.type == EventType::RawBytes && hex_raw_enabled) {
    auto direction = event.direction == Direction::AppToDevice ? "APP -> DEVICE" : "DEVICE -> APP";
    std::cout << format_event_line(event.timestamp_ns, direction, event.payload.size(), event.payload) << '\n';
    return;
  }

  if (event.type == EventType::Frame && hex_frame_enabled) {
    auto direction = event.direction == Direction::AppToDevice ? "APP -> DEVICE" : "DEVICE -> APP";
    std::cout << "[frame] parent=" << event.parent_sequence << ' ' << direction << ' ' << event.payload.size()
              << " bytes";
    auto const hex = to_hex(event.payload, true);
    if (!hex.empty()) {
      std::cout << "  " << hex;
    }
    std::cout << '\n';
    return;
  }

  if (event.type == EventType::Latency) {
    std::cout << "[latency] request=" << event.request_sequence << " response=" << event.response_sequence << ' '
              << event.latency_ns / 1000 << " us\n";
    return;
  }

  if (event.type == EventType::Error || event.type == EventType::StateChange) {
    std::cout << event.summary << '\n';
  }
}

std::unique_ptr<JsonlRecorder> make_recorder(CliOptions const& options) {
  auto recorder = std::make_unique<JsonlRecorder>();
  if (!options.log_path.empty()) {
    recorder->open(options.log_path);
  }
  return recorder;
}

std::unique_ptr<IpcEventServer> make_ipc_server(CliOptions const& options) {
  if (options.ipc_path.empty()) {
    return nullptr;
  }

  IpcServerOptions ipc_options;
  ipc_options.socket_path = options.ipc_path;
  auto server = std::make_unique<IpcEventServer>(std::move(ipc_options));
  server->start();
  return server;
}

EventPipeline make_pipeline(CliOptions const& options, JsonlRecorder& recorder, IpcEventServer* ipc_server,
                            SharedSequenceAllocator seq_alloc) {
  (void)create_frame_decoder(options.decoder_config);
  auto const hex_raw = options.hex_raw;
  auto const hex_frame = options.hex_frame;
  return EventPipeline(make_frame_decoder_factory(options.decoder_config),
                       [&recorder, ipc_server, hex_raw, hex_frame](PacketEvent const& event) {
    recorder.record(event);
    if (ipc_server != nullptr) {
      ipc_server->broadcast(event);
    }
    print_event(event, hex_raw, hex_frame);
  }, std::move(seq_alloc));
}

}  // namespace packet_probe::cli
