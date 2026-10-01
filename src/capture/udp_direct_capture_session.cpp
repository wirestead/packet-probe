#include "capture/udp_direct_capture_session.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

#include "wirestead/wirestead.hpp"

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

struct UdpDirectCaptureSession::Impl {
  std::unique_ptr<wirestead::UdpClient> client;
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

  wirestead::config::UdpConfig config;
  config.bind_address = options_.bind_host;
  config.local_port = options_.bind_port;
  if (!options_.target_host.empty()) {
    config.remote_address = options_.target_host;
    config.remote_port = options_.target_port;
  }

  impl_->client = std::make_unique<wirestead::UdpClient>(config);
  stopped_.store(false);
  impl_->client->on_data([this](wirestead::MessageContext const& ctx) {
    auto payload = ctx.data_as_vector();
    auto source = ctx.client_info().empty() ? "udp-peer" : ctx.client_info();
    emit(make_event(Direction::DeviceToApp, EventType::RawBytes, std::move(payload), std::move(source),
                    endpoint(options_.bind_host, options_.bind_port), summary_for(Direction::DeviceToApp, ctx.data().size())));
  });
  impl_->client->on_connect([this](wirestead::ConnectionContext const&) {
    emit(make_event(Direction::DeviceToApp, EventType::StateChange, {}, endpoint(options_.bind_host, options_.bind_port),
                    "packet-probe", "udp listening " + endpoint(options_.bind_host, options_.bind_port)));
  });
  impl_->client->on_disconnect([this](wirestead::ConnectionContext const&) {
    emit(make_event(Direction::DeviceToApp, EventType::StateChange, {}, endpoint(options_.bind_host, options_.bind_port),
                    "packet-probe", "udp stopped " + endpoint(options_.bind_host, options_.bind_port)));
    stopped_.store(true);
  });
  impl_->client->on_error([this](wirestead::ErrorContext const& ctx) {
    emit(make_event(Direction::DeviceToApp, EventType::Error, {}, endpoint(options_.bind_host, options_.bind_port),
                    "packet-probe", std::string(ctx.message())));
  });

  auto started = impl_->client->start();
  if (!started.get()) {
    stopped_.store(true);
    throw std::runtime_error("failed to start UDP capture");
  }
}

void UdpDirectCaptureSession::stop() {
  if (stopped_.exchange(true)) {
    return;
  }
  if (impl_ && impl_->client) {
    impl_->client->stop();
    impl_->client.reset();
  }
}

bool UdpDirectCaptureSession::stopped() const { return stopped_.load(); }

bool UdpDirectCaptureSession::send(std::vector<std::uint8_t> payload) {
  if (!impl_->client || !impl_->client->connected() || options_.target_host.empty() || options_.target_port == 0) {
    return false;
  }

  auto const size = payload.size();
  auto sent_payload = payload;
  auto accepted = impl_->client->send_move(std::move(payload));
  if (accepted) {
    emit(make_event(Direction::AppToDevice, EventType::RawBytes, std::move(sent_payload),
                    endpoint(options_.bind_host, options_.bind_port), endpoint(options_.target_host, options_.target_port),
                    summary_for(Direction::AppToDevice, size)));
  }
  return static_cast<bool>(accepted);
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
