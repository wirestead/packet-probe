#include "capture/serial_direct_capture_session.hpp"

#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "wirestead/wirestead.hpp"

namespace packet_probe {

namespace {

std::string summary_for(Direction direction, std::size_t size) {
  auto prefix = direction == Direction::AppToDevice ? "APP -> DEVICE " : "DEVICE -> APP ";
  return std::string(prefix) + std::to_string(size) + " bytes";
}

}  // namespace

struct SerialDirectCaptureSession::Impl {
  std::unique_ptr<wirestead::Serial> serial;
};

SerialDirectCaptureSession::SerialDirectCaptureSession(SerialCaptureOptions options, EventCallback on_event, SharedSequenceAllocator seq_alloc)
    : options_(std::move(options)), on_event_(std::move(on_event)), seq_alloc_(std::move(seq_alloc)), impl_(std::make_unique<Impl>()) {
  assert(seq_alloc_ && "SerialDirectCaptureSession requires a non-null SharedSequenceAllocator");
}

SerialDirectCaptureSession::~SerialDirectCaptureSession() { stop(); }

void SerialDirectCaptureSession::start() {
  if (options_.port.empty()) {
    throw std::invalid_argument("serial requires --port");
  }
  if (options_.baudrate == 0) {
    throw std::invalid_argument("serial requires --baudrate");
  }
#if !defined(_WIN32)
  if (!std::filesystem::exists(options_.port)) {
    throw std::runtime_error("serial port does not exist: " + options_.port);
  }
#endif

  impl_->serial = std::make_unique<wirestead::Serial>(options_.port, options_.baudrate);
  impl_->serial->data_bits(options_.data_bits)
      .stop_bits(options_.stop_bits)
      .parity(to_string(options_.parity))
      .flow_control(to_string(options_.flow_control));

  stopped_.store(false);
  impl_->serial->on_data([this](wirestead::MessageContext const& ctx) {
    auto payload = ctx.data_as_vector();
    emit(make_event(Direction::DeviceToApp, EventType::RawBytes, std::move(payload), options_.port, "packet-probe",
                    summary_for(Direction::DeviceToApp, ctx.data().size())));
  });
  impl_->serial->on_connect([this](wirestead::ConnectionContext const&) {
    emit(make_event(Direction::DeviceToApp, EventType::StateChange, {}, options_.port, "packet-probe",
                    "serial connected " + options_.port));
  });
  impl_->serial->on_disconnect([this](wirestead::ConnectionContext const&) {
    emit(make_event(Direction::DeviceToApp, EventType::StateChange, {}, options_.port, "packet-probe",
                    "serial disconnected " + options_.port));
    stopped_.store(true);
  });
  impl_->serial->on_error([this](wirestead::ErrorContext const& ctx) {
    emit(make_event(Direction::DeviceToApp, EventType::Error, {}, options_.port, "packet-probe",
                    std::string(ctx.message())));
    stopped_.store(true);
  });

  auto started = impl_->serial->start();
  if (!started.get()) {
    stopped_.store(true);
    throw std::runtime_error("failed to start serial capture");
  }
}

void SerialDirectCaptureSession::stop() {
  stopped_.store(true);
  if (impl_ && impl_->serial) {
    impl_->serial->stop();
    impl_->serial.reset();
  }
}

bool SerialDirectCaptureSession::stopped() const { return stopped_.load(); }

bool SerialDirectCaptureSession::send(std::vector<std::uint8_t> payload) {
  if (!impl_->serial || !impl_->serial->connected()) {
    return false;
  }

  auto const size = payload.size();
  auto sent_payload = payload;
  auto accepted = impl_->serial->send_move(std::move(payload));
  if (accepted) {
    emit(make_event(Direction::AppToDevice, EventType::RawBytes, std::move(sent_payload), "packet-probe", options_.port,
                    summary_for(Direction::AppToDevice, size)));
  }
  return static_cast<bool>(accepted);
}

PacketEvent SerialDirectCaptureSession::make_event(Direction direction, EventType type, std::vector<std::uint8_t> payload,
                                                   std::string source_endpoint, std::string destination_endpoint,
                                                   std::string summary) {
  PacketEvent event;
  event.sequence = seq_alloc_->next();
  event.timestamp_ns = now_ns();
  event.session_id = options_.session_id;
  event.transport = "serial";
  event.direction = direction;
  event.type = type;
  event.source_endpoint = std::move(source_endpoint);
  event.destination_endpoint = std::move(destination_endpoint);
  event.payload = std::move(payload);
  event.summary = std::move(summary);
  return event;
}

void SerialDirectCaptureSession::emit(PacketEvent const& event) {
  if (!on_event_) {
    return;
  }
  try {
    on_event_(event);
  } catch (...) {
  }
}

}  // namespace packet_probe
