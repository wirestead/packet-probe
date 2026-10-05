#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture/tcp_direct_capture_session.hpp"
#include "capture/tcp_server_capture_session.hpp"
#include "packet_probe/core/packet_event.hpp"
#include "packet_probe/core/sequence_allocator.hpp"

#define TEST_ASSERT(cond, msg) \
  if (!(cond)) { \
    std::cerr << "Assertion failed: " << #cond << " - " << msg << std::endl; \
    std::exit(1); \
  }

namespace {

void wait_until(std::function<bool()> const& predicate, std::string const& description,
                std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  TEST_ASSERT(predicate(), "Timeout waiting for: " + description);
}

}  // namespace

int main() {
  std::cout << "Starting TCP Capture Session integration test..." << std::endl;

  std::mutex mutex;
  std::vector<packet_probe::PacketEvent> server_events;
  std::vector<packet_probe::PacketEvent> client_events;

  // 1. Create and start TCP Server session
  packet_probe::TcpServerCaptureOptions server_options;
  server_options.listen_host = "127.0.0.1";
  server_options.listen_port = 19085;
  server_options.session_id = "test-tcp-server";

  packet_probe::TcpServerCaptureSession server_session(
      server_options, [&](packet_probe::PacketEvent const& event) {
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << "[Server Event] type=" << static_cast<int>(event.type)
                  << " dir=" << (event.direction == packet_probe::Direction::AppToDevice ? "TX" : "RX")
                  << " src=" << event.source_endpoint
                  << " dst=" << event.destination_endpoint
                  << " payload_size=" << event.payload.size()
                  << " summary=\"" << event.summary << "\"" << std::endl;
        server_events.push_back(event);
      },
      packet_probe::make_sequence_allocator());

  server_session.start();
  TEST_ASSERT(!server_session.stopped(), "Server session should be running after start()");

  // 2. Create and start TCP Client session
  packet_probe::TcpDirectCaptureOptions client_options;
  client_options.host = "127.0.0.1";
  client_options.port = 19085;
  client_options.session_id = "test-tcp-client";

  packet_probe::TcpDirectCaptureSession client_session(
      client_options, [&](packet_probe::PacketEvent const& event) {
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << "[Client Event] type=" << static_cast<int>(event.type)
                  << " dir=" << (event.direction == packet_probe::Direction::AppToDevice ? "TX" : "RX")
                  << " src=" << event.source_endpoint
                  << " dst=" << event.destination_endpoint
                  << " payload_size=" << event.payload.size()
                  << " summary=\"" << event.summary << "\"" << std::endl;
        client_events.push_back(event);
      },
      packet_probe::make_sequence_allocator());

  client_session.start();
  TEST_ASSERT(!client_session.stopped(), "Client session should be running after start()");

  // Wait for connection to establish
  wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        bool client_connected = false;
        for (auto const& ev : client_events) {
          if (ev.type == packet_probe::EventType::StateChange && ev.summary.find("connected") != std::string::npos) {
            client_connected = true;
          }
        }
        bool server_connected = false;
        for (auto const& ev : server_events) {
          if (ev.type == packet_probe::EventType::StateChange && ev.summary.find("connected") != std::string::npos) {
            server_connected = true;
          }
        }
        return client_connected && server_connected;
      },
      "TCP connection establishment");

  // 3. Send message from Client (App) to Server (Device)
  std::cout << "Sending data from Client to Server..." << std::endl;
  std::vector<std::uint8_t> client_payload = {'H', 'e', 'l', 'l', 'o'};
  bool client_sent = client_session.send(client_payload);
  TEST_ASSERT(client_sent, "Client should successfully send payload");

  // Wait for client to emit TX and server to emit RX
  wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        bool has_client_tx = false;
        for (auto const& ev : client_events) {
          if (ev.type == packet_probe::EventType::RawBytes && ev.direction == packet_probe::Direction::AppToDevice) {
            has_client_tx = true;
          }
        }
        bool has_server_rx = false;
        for (auto const& ev : server_events) {
          if (ev.type == packet_probe::EventType::RawBytes && ev.direction == packet_probe::Direction::DeviceToApp) {
            has_server_rx = true;
          }
        }
        return has_client_tx && has_server_rx;
      },
      "Client TX and Server RX events");

  // Verify client TX and server RX event details (including source/destination endpoints)
  {
    std::lock_guard<std::mutex> lock(mutex);
    packet_probe::PacketEvent const* client_tx = nullptr;
    for (auto const& ev : client_events) {
      if (ev.type == packet_probe::EventType::RawBytes && ev.direction == packet_probe::Direction::AppToDevice) {
        client_tx = &ev;
      }
    }
    TEST_ASSERT(client_tx != nullptr, "Client TX event exists");
    TEST_ASSERT(client_tx->source_endpoint == "packet-probe", "Client TX source endpoint should be 'packet-probe'");
    TEST_ASSERT(client_tx->destination_endpoint == "127.0.0.1:19085", "Client TX destination endpoint should match target host/port");

    packet_probe::PacketEvent const* server_rx = nullptr;
    for (auto const& ev : server_events) {
      if (ev.type == packet_probe::EventType::RawBytes && ev.direction == packet_probe::Direction::DeviceToApp) {
        server_rx = &ev;
      }
    }
    TEST_ASSERT(server_rx != nullptr, "Server RX event exists");
    TEST_ASSERT(server_rx->source_endpoint.rfind("127.0.0.1:", 0) == 0, "Server RX source endpoint should be a 127.0.0.1 socket");
    TEST_ASSERT(server_rx->destination_endpoint == "127.0.0.1:19085", "Server RX destination endpoint should be the server port");
  }

  // 4. Send message from Server (Device) to Client (App)
  std::cout << "Sending data from Server to Client..." << std::endl;
  std::vector<std::uint8_t> server_payload = {'W', 'o', 'r', 'l', 'd'};
  bool server_sent = server_session.send(server_payload);
  TEST_ASSERT(server_sent, "Server should successfully send payload");

  // Wait for server to emit TX and client to emit RX
  wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        bool has_server_tx = false;
        for (auto const& ev : server_events) {
          if (ev.type == packet_probe::EventType::RawBytes && ev.direction == packet_probe::Direction::AppToDevice) {
            has_server_tx = true;
          }
        }
        bool has_client_rx = false;
        for (auto const& ev : client_events) {
          if (ev.type == packet_probe::EventType::RawBytes && ev.direction == packet_probe::Direction::DeviceToApp) {
            has_client_rx = true;
          }
        }
        return has_server_tx && has_client_rx;
      },
      "Server TX and Client RX events");

  // Verify server TX and client RX event details (including source/destination endpoints)
  {
    std::lock_guard<std::mutex> lock(mutex);
    packet_probe::PacketEvent const* server_tx = nullptr;
    for (auto const& ev : server_events) {
      if (ev.type == packet_probe::EventType::RawBytes && ev.direction == packet_probe::Direction::AppToDevice) {
        server_tx = &ev;
      }
    }
    TEST_ASSERT(server_tx != nullptr, "Server TX event exists");
    TEST_ASSERT(server_tx->source_endpoint == "127.0.0.1:19085", "Server TX source endpoint should be server's local endpoint");
    TEST_ASSERT(server_tx->destination_endpoint.rfind("127.0.0.1:", 0) == 0, "Server TX destination endpoint should be the connected client's address");

    packet_probe::PacketEvent const* client_rx = nullptr;
    for (auto const& ev : client_events) {
      if (ev.type == packet_probe::EventType::RawBytes && ev.direction == packet_probe::Direction::DeviceToApp) {
        client_rx = &ev;
      }
    }
    TEST_ASSERT(client_rx != nullptr, "Client RX event exists");
    TEST_ASSERT(client_rx->source_endpoint == "127.0.0.1:19085", "Client RX source endpoint should be server's address");
    TEST_ASSERT(client_rx->destination_endpoint == "packet-probe", "Client RX destination endpoint should be 'packet-probe'");
  }

  // 5. Clean stop
  std::cout << "Stopping sessions..." << std::endl;
  client_session.stop();
  server_session.stop();

  TEST_ASSERT(client_session.stopped(), "Client should be stopped");
  TEST_ASSERT(server_session.stopped(), "Server should be stopped");

  std::cout << "All checks passed successfully!" << std::endl;
  return 0;
}
