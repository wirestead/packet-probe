#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture/udp_direct_capture_session.hpp"
#include "packet_probe/core/packet_event.hpp"
#include "packet_probe/core/sequence_allocator.hpp"

#define TEST_ASSERT(cond, msg) \
  if (!(cond)) { \
    std::cerr << "Assertion failed: " << #cond << " - " << msg << std::endl; \
    std::exit(1); \
  }

namespace {

using boost::asio::ip::udp;

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

std::vector<std::uint8_t> bytes(std::string const& text) { return {text.begin(), text.end()}; }

}  // namespace

// A capture records datagrams from every sender, not just the first one or the send
// target (wirestead's UdpClient filters to one peer since wirestead #435), and still
// sends to a target that has not sent anything yet.
int main() {
  constexpr std::uint16_t kCapturePort = 19731;

  boost::asio::io_context io;
  udp::socket target(io, udp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  udp::socket other(io, udp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  auto const target_port = target.local_endpoint().port();
  auto const other_port = other.local_endpoint().port();

  std::mutex mutex;
  std::vector<packet_probe::PacketEvent> events;
  packet_probe::UdpDirectCaptureOptions options;
  options.bind_host = "127.0.0.1";
  options.bind_port = kCapturePort;
  options.target_host = "127.0.0.1";
  options.target_port = target_port;
  packet_probe::UdpDirectCaptureSession session(
      options,
      [&](packet_probe::PacketEvent const& event) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(event);
      },
      packet_probe::make_sequence_allocator());
  session.start();

  auto rx_from = [&](std::uint16_t port, std::string const& text) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto const& e : events) {
      if (e.type == packet_probe::EventType::RawBytes && e.direction == packet_probe::Direction::DeviceToApp &&
          e.source_endpoint == "127.0.0.1:" + std::to_string(port) && e.payload == bytes(text)) {
        return true;
      }
    }
    return false;
  };

  udp::endpoint const capture(boost::asio::ip::make_address("127.0.0.1"), kCapturePort);
  other.send_to(boost::asio::buffer(std::string("first")), capture);
  wait_until([&] { return rx_from(other_port, "first"); }, "datagram from the first sender");
  target.send_to(boost::asio::buffer(std::string("second")), capture);
  wait_until([&] { return rx_from(target_port, "second"); }, "datagram from a second sender");
  other.send_to(boost::asio::buffer(std::string("third")), capture);
  wait_until([&] { return rx_from(other_port, "third"); }, "non-target datagram while a target is set");

  // Send reaches the target even though captures from other peers came first.
  TEST_ASSERT(session.send(bytes("ping")), "send to target should be accepted");
  char buffer[64];
  udp::endpoint from;
  auto const n = target.receive_from(boost::asio::buffer(buffer), from);
  TEST_ASSERT(std::string(buffer, n) == "ping", "target should receive the sent payload");
  TEST_ASSERT(from.port() == kCapturePort, "send should come from the capture's bind port");

  session.stop();
  TEST_ASSERT(session.stopped(), "session should be stopped");
  std::cout << "UDP capture session test passed" << std::endl;
  return 0;
}
