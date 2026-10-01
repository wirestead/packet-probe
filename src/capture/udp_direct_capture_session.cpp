#include "capture/udp_direct_capture_session.hpp"

#include <cassert>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>

#include "wirestead/wirestead.hpp"
#include "wirestead/transport/udp/udp.hpp"

namespace packet_probe {

namespace {

std::string endpoint(std::string const& host, std::uint16_t port) {
  return host + ":" + std::to_string(port);
}

std::string summary_for(Direction direction, std::size_t size) {
  auto prefix = direction == Direction::AppToDevice ? "APP -> DEVICE " : "DEVICE -> APP ";
  return std::string(prefix) + std::to_string(size) + " bytes";
}

}  // namespace

// Uses the transport-level UdpChannel rather than wirestead::UdpClient: UdpClient is
// point-to-point and, since wirestead #435, only delivers datagrams from its configured
// or first-seen peer. A capture must record every datagram arriving at the bind
// address, so this receives through on_bytes_from (all senders, with their endpoint)
// and sends to the target with an explicit destination.
struct UdpDirectCaptureSession::Impl {
  std::shared_ptr<wirestead::transport::UdpChannel> channel;
  std::optional<boost::asio::ip::udp::endpoint> target;
  std::mutex start_mutex;
  std::optional<std::promise<bool>> start_result;

  void resolve_start(bool ok) {
    std::lock_guard<std::mutex> lock(start_mutex);
    if (start_result) {
      start_result->set_value(ok);
      start_result.reset();
    }
  }
};

UdpDirectCaptureSession::UdpDirectCaptureSession(UdpDirectCaptureOptions options, EventCallback on_event, SharedSequenceAllocator seq_alloc)
    : options_(std::move(options)), on_event_(std::move(on_event)), seq_alloc_(std::move(seq_alloc)), impl_(std::make_unique<Impl>()) {
  assert(seq_alloc_ && "UdpDirectCaptureSession requires a non-null SharedSequenceAllocator");
}

UdpDirectCaptureSession::~UdpDirectCaptureSession() { stop(); }

void UdpDirectCaptureSession::start() {
  if (options_.bind_host.empty()) {
    throw std::invalid_argument("udp requires --bind-host");
  }
  if (options_.bind_port == 0) {
    throw std::invalid_argument("udp requires --bind-port");
  }
  if (options_.target_host.empty() != (options_.target_port == 0)) {
    throw std::invalid_argument("udp requires both --target-host and --target-port when sending is enabled");
  }

  impl_->target.reset();
  if (!options_.target_host.empty()) {
    boost::system::error_code ec;
    auto const address = boost::asio::ip::make_address(options_.target_host, ec);
    if (ec) {
      throw std::invalid_argument("udp --target-host must be an IP address: " + options_.target_host);
    }
    impl_->target = boost::asio::ip::udp::endpoint(address, options_.target_port);
  }

  wirestead::config::UdpConfig config;
  config.bind_address = options_.bind_host;
  config.local_port = options_.bind_port;

  auto const local = endpoint(options_.bind_host, options_.bind_port);
  impl_->channel = wirestead::transport::UdpChannel::create(config);
  std::future<bool> started;
  {
    std::lock_guard<std::mutex> lock(impl_->start_mutex);
    impl_->start_result.emplace();
    started = impl_->start_result->get_future();
  }
  stopped_.store(false);

  impl_->channel->on_bytes_from([this, local](wirestead::memory::ConstByteSpan data,
                                              boost::asio::ip::udp::endpoint const& from) {
    std::vector<std::uint8_t> payload(data.begin(), data.end());
    auto const size = payload.size();
    emit(make_event(Direction::DeviceToApp, EventType::RawBytes, std::move(payload),
                    endpoint(from.address().to_string(), from.port()), local, summary_for(Direction::DeviceToApp, size)));
  });
  impl_->channel->on_state([this, local](wirestead::base::LinkState state) {
    using wirestead::base::LinkState;
    // Without a configured remote the socket opens as Listening; it later reports
    // Connected when the transport learns a default peer, which a capture ignores.
    if (state == LinkState::Listening) {
      emit(make_event(Direction::DeviceToApp, EventType::StateChange, {}, local, "packet-probe", "udp listening " + local));
      impl_->resolve_start(true);
    } else if (state == LinkState::Error) {
      auto const info = impl_->channel ? impl_->channel->last_error_info() : std::nullopt;
      emit(make_event(Direction::DeviceToApp, EventType::Error, {}, local, "packet-probe",
                      info ? info->message : std::string("udp error")));
      stopped_.store(true);
      impl_->resolve_start(false);
    } else if (state == LinkState::Closed) {
      emit(make_event(Direction::DeviceToApp, EventType::StateChange, {}, local, "packet-probe", "udp stopped " + local));
      stopped_.store(true);
      impl_->resolve_start(false);
    }
  });

  impl_->channel->start();
  if (!started.get()) {
    stopped_.store(true);
    impl_->channel->stop();
    impl_->channel.reset();
    throw std::runtime_error("failed to start UDP capture");
  }
}

void UdpDirectCaptureSession::stop() {
  if (stopped_.exchange(true)) {
    return;
  }
  if (impl_ && impl_->channel) {
    impl_->channel->stop();
    impl_->channel.reset();
  }
}

bool UdpDirectCaptureSession::stopped() const { return stopped_.load(); }

bool UdpDirectCaptureSession::send(std::vector<std::uint8_t> payload) {
  if (!impl_->channel || stopped_.load() || !impl_->target) {
    return false;
  }

  auto const size = payload.size();
  auto const accepted = impl_->channel->async_write_to(wirestead::memory::ConstByteSpan(payload.data(), size), *impl_->target);
  if (accepted) {
    emit(make_event(Direction::AppToDevice, EventType::RawBytes, std::move(payload),
                    endpoint(options_.bind_host, options_.bind_port), endpoint(options_.target_host, options_.target_port),
                    summary_for(Direction::AppToDevice, size)));
  }
  return accepted;
}

PacketEvent UdpDirectCaptureSession::make_event(Direction direction, EventType type, std::vector<std::uint8_t> payload,
                                                std::string source_endpoint, std::string destination_endpoint,
                                                std::string summary) {
  PacketEvent event;
  event.sequence = seq_alloc_->next();
  event.timestamp_ns = now_ns();
  event.session_id = options_.session_id;
  event.transport = "udp";
  event.direction = direction;
  event.type = type;
  event.source_endpoint = std::move(source_endpoint);
  event.destination_endpoint = std::move(destination_endpoint);
  event.payload = std::move(payload);
  event.summary = std::move(summary);
  return event;
}

void UdpDirectCaptureSession::emit(PacketEvent const& event) {
  if (!on_event_) {
    return;
  }
  try {
    on_event_(event);
  } catch (...) {
  }
}

}  // namespace packet_probe
