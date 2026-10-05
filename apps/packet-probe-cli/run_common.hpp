#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "cli_options.hpp"
#include "core/event_pipeline.hpp"
#include "decoder/frame_decoder_factory.hpp"
#include "core/hex_dump.hpp"
#include "ipc/ipc_event_server.hpp"
#include "recorder/jsonl_recorder.hpp"
#include "core/send_input_parser.hpp"
#include "packet_probe/core/sequence_allocator.hpp"

namespace packet_probe::cli {

using StopRequested = std::function<bool()>;

bool stdin_is_terminal();
std::vector<std::uint8_t> parse_send_line(std::string const& line, SendInputOptions const& send_options);
void print_event(PacketEvent const& event, bool hex_raw_enabled, bool hex_frame_enabled);
std::unique_ptr<JsonlRecorder> make_recorder(CliOptions const& options);
std::unique_ptr<IpcEventServer> make_ipc_server(CliOptions const& options);
EventPipeline make_pipeline(CliOptions const& options, JsonlRecorder& recorder, IpcEventServer* ipc_server,
                            SharedSequenceAllocator seq_alloc);

// Deregisters ipc_server's command handler on scope exit, so a handler that
// captures a session by reference can never fire after that session is gone.
class IpcHandlerGuard {
 public:
  explicit IpcHandlerGuard(IpcEventServer* ipc_server) : ipc_server_(ipc_server) {}
  ~IpcHandlerGuard() {
    if (ipc_server_) {
      ipc_server_->set_command_handler(nullptr);
    }
  }
  IpcHandlerGuard(IpcHandlerGuard const&) = delete;
  IpcHandlerGuard& operator=(IpcHandlerGuard const&) = delete;

 private:
  IpcEventServer* ipc_server_;
};

template <typename Session>
int run_sender_session(Session& session, SendInputOptions const& send_options, StopRequested const& stop_requested,
                        IpcEventServer* ipc_server = nullptr) {
  IpcHandlerGuard const ipc_handler_guard(ipc_server);

  if (send_options.format == SendInputFormat::File) {
    auto payload = read_binary_file(send_options.file_path);
    if (!session.send(std::move(payload))) {
      throw std::runtime_error("failed to send file payload");
    }
    session.stop();
    return 0;
  }

  auto const interactive_stdin = stdin_is_terminal();
  std::string line;
  while (!stop_requested() && !session.stopped() && std::getline(std::cin, line)) {
    auto payload = parse_send_line(line, send_options);
    if (!session.send(std::move(payload))) {
      std::cerr << "failed to send payload\n";
    }
  }

  while (!interactive_stdin && !stop_requested() && !session.stopped()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (!session.stopped()) {
    session.stop();
  }
  return 0;
}

// Registers an IPC command handler that routes {"command":"send","payload_hex":"..."} to session.send().
// The handler must be deregistered (see IpcHandlerGuard) before `session` is destroyed, since it
// captures `session` by reference.
template <typename Session>
void install_ipc_send_handler(IpcEventServer* ipc_server, Session& session) {
  if (!ipc_server) return;
  ipc_server->set_command_handler([&session](IpcClientId /*client_id*/, std::string_view line) {
    nlohmann::json msg;
    try {
      msg = nlohmann::json::parse(std::string(line));
    } catch (nlohmann::json::parse_error const&) {
      return;
    }
    if (!msg.is_object()) return;

    auto const command_it = msg.find("command");
    if (command_it == msg.end() || !command_it->is_string() || *command_it != "send") return;

    auto const payload_it = msg.find("payload_hex");
    if (payload_it == msg.end() || !payload_it->is_string()) return;
    auto const hex = payload_it->get<std::string>();
    if (hex.empty()) return;

    try {
      session.send(parse_hex_payload(hex));
    } catch (std::exception const& ex) {
      std::cerr << "IPC send command failed: " << ex.what() << '\n';
    } catch (...) {
      std::cerr << "IPC send command failed: unknown error\n";
    }
  });
}

}  // namespace packet_probe::cli
