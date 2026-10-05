#include "capture/tcp_server_capture_session.hpp"

#include <cassert>
#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#include "wirestead/wirestead.hpp"

namespace packet_probe {

struct TcpServerCaptureSession::Impl {
  std::unique_ptr<wirestead::TcpServer> server;
  std::mutex state_mutex;
  std::optional<wirestead::ClientId> active_client;
  std::string client_info;
};

TcpServerCaptureSession::TcpServerCaptureSession(TcpServerCaptureOptions options, EventCallback on_event, SharedSequenceAllocator seq_alloc)
    : options_(std::move(options)), on_event_(std::move(on_event)), seq_alloc_(std::move(seq_alloc)), impl_(std::make_unique<Impl>()) {
  assert(seq_alloc_ && "TcpServerCaptureSession requires a non-null SharedSequenceAllocator");
}

TcpServerCaptureSession::~TcpServerCaptureSession() { stop(); }

void TcpServerCaptureSession::start() {
  if (options_.listen_host.empty()) {
    throw std::invalid_argument("tcp-server requires --listen-host");
  }
  if (options_.listen_port == 0) {
    throw std::invalid_argument("tcp-server requires --listen-port");
  }

  impl_->server = std::make_unique<wirestead::TcpServer>(options_.listen_port);
  impl_->server->bind_address(options_.listen_host)
      .max_clients(1)
      // Tuning wirestead to minimize latency & bufferbloat per global instructions
      .backpressure_threshold(512 * 1024)
      .auto_start(false);

  stopped_.store(false);

  impl_->server->on_connect([this](wirestead::ConnectionContext const& ctx) {
    bool has_active = false;
    {
      std::lock_guard<std::mutex> lock(impl_->state_mutex);
      if (impl_->active_client.has_value()) {
        has_active = true;
      } else {
        impl_->active_client = ctx.client_id();
        impl_->client_info = ctx.client_info();
      }
    }

    auto local_ep = options_.listen_host + ":" + std::to_string(options_.listen_port);
    if (has_active) {
      auto summary = "ignored extra TCP client " + ctx.client_info();
      emit(make_event(Direction::DeviceToApp, EventType::StateChange, {},
                      ctx.client_info(), local_ep, std::move(summary)));
      return;
    }

    auto summary = "connected " + ctx.client_info();
    emit(make_event(Direction::DeviceToApp, EventType::StateChange, {},
                    ctx.client_info(), local_ep, std::move(summary)));
  });

  impl_->server->on_disconnect([this](wirestead::ConnectionContext const& ctx) {
    std::string old_client_info;
    bool is_active = false;
    {
      std::lock_guard<std::mutex> lock(impl_->state_mutex);
      if (impl_->active_client.has_value() && impl_->active_client.value() == ctx.client_id()) {
        old_client_info = impl_->client_info;
        impl_->active_client.reset();
        impl_->client_info.clear();
        is_active = true;
      }
    }

    if (is_active) {
      auto local_ep = options_.listen_host + ":" + std::to_string(options_.listen_port);
      auto summary = "disconnected " + old_client_info;
      emit(make_event(Direction::DeviceToApp, EventType::StateChange, {},
                      old_client_info, local_ep, std::move(summary)));
      stopped_.store(true);
    }
  });

  impl_->server->on_data([this](wirestead::MessageContext const& ctx) {
    bool is_active = false;
    std::string client_info;
    {
      std::lock_guard<std::mutex> lock(impl_->state_mutex);
      if (impl_->active_client.has_value() && impl_->active_client.value() == ctx.client_id()) {
        is_active = true;
        client_info = impl_->client_info;
      }
    }

    if (is_active) {
      auto payload = ctx.data_as_vector();
      auto const size = payload.size();
      auto local_ep = options_.listen_host + ":" + std::to_string(options_.listen_port);
      emit(make_event(Direction::DeviceToApp, EventType::RawBytes, std::move(payload),
                      client_info, local_ep,
                      "DEVICE -> APP " + std::to_string(size) + " bytes"));
    }
  });

  impl_->server->on_error([this](wirestead::ErrorContext const& ctx) {
    std::string target_client_info;
    bool is_active = false;
    {
      std::lock_guard<std::mutex> lock(impl_->state_mutex);
      if (ctx.client_id().has_value() && impl_->active_client.has_value() &&
          impl_->active_client.value() == ctx.client_id().value()) {
        target_client_info = impl_->client_info;
        impl_->active_client.reset();
        impl_->client_info.clear();
        is_active = true;
      }
    }

    if (is_active) {
      auto local_ep = options_.listen_host + ":" + std::to_string(options_.listen_port);
      emit(make_event(Direction::DeviceToApp, EventType::Error, {},
                      target_client_info, local_ep, std::string(ctx.message())));
      stopped_.store(true);
    }
  });

  auto started = impl_->server->start_sync();
  if (!started) {
    stopped_.store(true);
    throw std::runtime_error("failed to start TCP server");
  }
}

void TcpServerCaptureSession::stop() {
  if (stopped_.exchange(true)) {
    return;
  }
  if (impl_ && impl_->server) {
    impl_->server->stop();
    impl_->server.reset();
  }
}

bool TcpServerCaptureSession::stopped() const { return stopped_.load(); }

bool TcpServerCaptureSession::client_connected() const {
  std::lock_guard<std::mutex> lock(impl_->state_mutex);
  return impl_->active_client.has_value();
}

bool TcpServerCaptureSession::send(std::vector<std::uint8_t> payload) {
  wirestead::ClientId client_id;
  std::string client_info;

  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    if (stopped_.load() || !impl_->server || !impl_->active_client.has_value()) {
      return false;
    }

    client_id = impl_->active_client.value();
    client_info = impl_->client_info;
  }

  auto const size = payload.size();
  std::string_view data(reinterpret_cast<const char*>(payload.data()), size);
  bool const accepted = static_cast<bool>(impl_->server->send_to(client_id, data));

  if (accepted) {
    auto local_ep = options_.listen_host + ":" + std::to_string(options_.listen_port);
    emit(make_event(Direction::AppToDevice, EventType::RawBytes, std::move(payload),
                    local_ep, client_info,
                    "APP -> DEVICE " + std::to_string(size) + " bytes"));
  }
  return accepted;
}

PacketEvent TcpServerCaptureSession::make_event(
    Direction direction,
    EventType type,
    std::vector<std::uint8_t> payload,
    std::string source_endpoint,
    std::string destination_endpoint,
    std::string summary) {
  PacketEvent event;
  event.sequence = seq_alloc_->next();
  event.timestamp_ns = now_ns();
  event.session_id = options_.session_id;
  event.transport = "tcp";
  event.direction = direction;
  event.type = type;
  event.source_endpoint = std::move(source_endpoint);
  event.destination_endpoint = std::move(destination_endpoint);
  event.payload = std::move(payload);
  event.summary = std::move(summary);
  return event;
}

void TcpServerCaptureSession::emit(PacketEvent const& event) {
  if (on_event_) {
    on_event_(event);
  }
}

}  // namespace packet_probe
