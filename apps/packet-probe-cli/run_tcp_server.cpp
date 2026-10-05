#include "run_modes.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include "capture/tcp_server_capture_session.hpp"

namespace packet_probe::cli {

int run_tcp_server(CliOptions const& options, StopRequested const& stop_requested) {
  auto seq_alloc = make_sequence_allocator();
  auto recorder = make_recorder(options);
  auto ipc_server = make_ipc_server(options);
  auto pipeline = make_pipeline(options, *recorder, ipc_server.get(), seq_alloc);

  TcpServerCaptureOptions capture_options;
  capture_options.listen_host = options.listen_host;
  capture_options.listen_port = options.listen_port;
  capture_options.session_id = "tcp-server-1";

  TcpServerCaptureSession session(
      capture_options,
      [&](PacketEvent const& event) { pipeline.consume(event); },
      seq_alloc);

  install_ipc_send_handler(ipc_server.get(), session);
  session.start();

  // Nothing can be sent before a client connects, so hold stdin/--send-file until then
  // instead of failing the first payload.
  if (options.send_option_count > 0) {
    std::cerr << "waiting for a client to connect before sending...\n";
  }
  while (!stop_requested() && !session.stopped() && !session.client_connected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (stop_requested() || session.stopped()) {
    session.stop();
    return 0;
  }
  return run_sender_session(session, options.send_options, stop_requested, ipc_server.get());
}

}  // namespace packet_probe::cli
