#include "engine_controller.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "capture/serial_direct_capture_session.hpp"
#include "capture/tcp_direct_capture_session.hpp"
#include "capture/tcp_proxy_capture_session.hpp"
#include "capture/tcp_server_capture_session.hpp"
#include "capture/udp_direct_capture_session.hpp"
#include "core/hex_dump.hpp"
#include "core/send_input_parser.hpp"
#include "decoder/frame_decoder_factory.hpp"
#include "engine_config.hpp"
#include "packet_probe/ipc/ipc_protocol.hpp"
#include "run_common.hpp"

namespace packet_probe::cli {

namespace {

// Adapts a capture session type whose public API includes send() to IEngineSession.
template <typename Session>
class SendableSessionAdapter : public IEngineSession {
 public:
  explicit SendableSessionAdapter(std::unique_ptr<Session> session) : session_(std::move(session)) {}
  void stop() override { session_->stop(); }
  bool stopped() const override { return session_->stopped(); }
  bool send(std::vector<std::uint8_t> payload) override { return session_->send(std::move(payload)); }
  bool supports_send() const override { return true; }

 private:
  std::unique_ptr<Session> session_;
};

// tcp-proxy has no send() (see docs/ipc-protocol.md: "Not supported in tcp-proxy mode").
class ProxySessionAdapter : public IEngineSession {
 public:
  explicit ProxySessionAdapter(std::unique_ptr<TcpProxyCaptureSession> session) : session_(std::move(session)) {}
  void stop() override { session_->stop(); }
  bool stopped() const override { return session_->stopped(); }
  bool send(std::vector<std::uint8_t> /*payload*/) override { return false; }
  bool supports_send() const override { return false; }

 private:
  std::unique_ptr<TcpProxyCaptureSession> session_;
};

}  // namespace

EngineController::EngineController(IpcEventServer* ipc_server, SharedSequenceAllocator seq_alloc)
    : ipc_server_(ipc_server), seq_alloc_(std::move(seq_alloc)) {}

EngineController::~EngineController() { shutdown(); }

void EngineController::install() {
  ipc_server_->set_command_handler(
      [this](IpcClientId client_id, std::string_view line) { handle_command(client_id, line); });
}

void EngineController::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (capturing_) {
    stop_capture_locked();
  }
  ipc_server_->set_command_handler(nullptr);
}

void EngineController::handle_command(IpcClientId client_id, std::string_view line) {
  nlohmann::json msg;
  try {
    msg = nlohmann::json::parse(std::string(line));
  } catch (nlohmann::json::parse_error const&) {
    return;
  }
  if (!msg.is_object()) return;

  auto const type_it = msg.find("type");
  if (type_it != msg.end() && type_it->is_string() && *type_it != kIpcMessageTypeCommand) {
    return;
  }

  auto const command_it = msg.find("command");
  if (command_it == msg.end() || !command_it->is_string()) return;
  std::string const command = command_it->get<std::string>();

  std::string id;
  auto const id_it = msg.find("id");
  if (id_it != msg.end() && id_it->is_string()) {
    id = id_it->get<std::string>();
  }

  try {
    if (command == kIpcCommandConfigure) {
      handle_configure(client_id, id, msg);
    } else if (command == kIpcCommandStartCapture) {
      handle_start_capture(client_id, id);
    } else if (command == kIpcCommandStopCapture) {
      handle_stop_capture(client_id, id);
    } else if (command == kIpcCommandGetStatus) {
      handle_get_status(client_id, id);
    } else if (command == kIpcCommandListSerialPorts) {
      handle_list_serial_ports(client_id, id);
    } else if (command == kIpcCommandSend) {
      handle_send(client_id, id, msg);
    } else {
      reply_result(client_id, id, false, "unknown command: " + command);
    }
  } catch (std::exception const& ex) {
    reply_result(client_id, id, false, ex.what());
  } catch (...) {
    reply_result(client_id, id, false, "unknown error");
  }
}

void EngineController::handle_configure(IpcClientId client_id, std::string const& id, nlohmann::json const& msg) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (capturing_) {
    reply_result(client_id, id, false, "cannot configure while capturing; stop_capture first");
    return;
  }

  auto const config_it = msg.find("config");
  if (config_it == msg.end() || !config_it->is_object()) {
    reply_result(client_id, id, false, "configure requires a \"config\" object");
    return;
  }

  auto new_options = engine_config_from_json(*config_it);
  validate_options(new_options);

  config_ = std::move(new_options);
  has_config_ = true;
  reply_result(client_id, id, true);
  broadcast_status_locked();
}

void EngineController::handle_start_capture(IpcClientId client_id, std::string const& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (capturing_) {
    reply_result(client_id, id, false, "already capturing");
    return;
  }
  if (!has_config_) {
    reply_result(client_id, id, false, "not configured; call configure first");
    return;
  }

  try {
    start_session_locked();
  } catch (std::exception const& ex) {
    session_.reset();
    pipeline_.reset();
    recorder_.reset();
    reply_result(client_id, id, false, ex.what());
    return;
  }

  capturing_ = true;
  reply_result(client_id, id, true);
  broadcast_status_locked();
}

void EngineController::handle_stop_capture(IpcClientId client_id, std::string const& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!capturing_) {
    reply_result(client_id, id, false, "not capturing");
    return;
  }
  stop_capture_locked();
  reply_result(client_id, id, true);
  broadcast_status_locked();
}

void EngineController::handle_get_status(IpcClientId client_id, std::string const& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  nlohmann::json result;
  result["type"] = kIpcMessageTypeResult;
  result["id"] = id;
  result["ok"] = true;
  result["engine_state"] = capturing_ ? kEngineStateCapturing : kEngineStateIdle;
  if (has_config_) {
    result["config"] = engine_config_to_json(config_);
  }
  result["counters"] = {{"events_seen", events_seen_.load(std::memory_order_relaxed)}};
  ipc_server_->send_to_client(client_id, result.dump());
}

void EngineController::handle_list_serial_ports(IpcClientId client_id, std::string const& id) {
  std::vector<std::string> ports;
  std::error_code ec;

  for (char const* dir : {"/dev/serial/by-id", "/dev"}) {
    if (!std::filesystem::exists(dir, ec) || ec) {
      ec.clear();
      continue;
    }
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    for (auto const& entry : it) {
      auto const name = entry.path().filename().string();
      bool const in_dev_root = std::string(dir) == "/dev";
      if (in_dev_root && name.rfind("ttyUSB", 0) != 0 && name.rfind("ttyACM", 0) != 0) {
        continue;
      }
      ports.push_back(entry.path().string());
    }
  }

  std::sort(ports.begin(), ports.end());
  ports.erase(std::unique(ports.begin(), ports.end()), ports.end());

  nlohmann::json result;
  result["type"] = kIpcMessageTypeResult;
  result["id"] = id;
  result["ok"] = true;
  result["ports"] = ports;
  ipc_server_->send_to_client(client_id, result.dump());
}

void EngineController::handle_send(IpcClientId client_id, std::string const& id, nlohmann::json const& msg) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!capturing_ || !session_) {
    reply_result(client_id, id, false, "not capturing");
    return;
  }
  if (!session_->supports_send()) {
    reply_result(client_id, id, false, "send is not supported in " + config_.mode + " mode");
    return;
  }

  auto const payload_it = msg.find("payload_hex");
  if (payload_it == msg.end() || !payload_it->is_string()) {
    reply_result(client_id, id, false, "send requires \"payload_hex\"");
    return;
  }

  std::vector<std::uint8_t> payload;
  try {
    payload = parse_hex_payload(payload_it->get<std::string>());
  } catch (std::exception const& ex) {
    reply_result(client_id, id, false, std::string("invalid payload_hex: ") + ex.what());
    return;
  }

  bool const sent = session_->send(std::move(payload));
  reply_result(client_id, id, sent, sent ? "" : "send failed");
}

void EngineController::reply_result(IpcClientId client_id, std::string const& id, bool ok, std::string const& error) {
  nlohmann::json result;
  result["type"] = kIpcMessageTypeResult;
  result["id"] = id;
  result["ok"] = ok;
  if (!ok && !error.empty()) {
    result["error"] = error;
  }
  ipc_server_->send_to_client(client_id, result.dump());
}

void EngineController::broadcast_status_locked() {
  nlohmann::json status;
  status["type"] = kIpcMessageTypeStatus;
  status["engine_state"] = capturing_ ? kEngineStateCapturing : kEngineStateIdle;
  if (has_config_) {
    status["config"] = engine_config_to_json(config_);
  }
  status["counters"] = {{"events_seen", events_seen_.load(std::memory_order_relaxed)}};
  ipc_server_->broadcast_raw(status.dump());
}

void EngineController::start_session_locked() {
  recorder_ = make_recorder(config_);

  // Eagerly validate the decoder config so a bad "configure" surfaces as a
  // start_capture error instead of silently building a broken pipeline.
  (void)create_frame_decoder(config_.decoder_config);

  // EventPipeline holds a std::mutex and is therefore non-movable, so it must be
  // constructed in place (std::optional::emplace / new) rather than assigned from
  // a temporary returned by value.
  auto* recorder_ptr = recorder_.get();
  auto* ipc_server = ipc_server_;
  auto const hex_raw = config_.hex_raw;
  auto const hex_frame = config_.hex_frame;
  pipeline_ = std::make_unique<EventPipeline>(
      make_frame_decoder_factory(config_.decoder_config),
      [recorder_ptr, ipc_server, hex_raw, hex_frame](PacketEvent const& event) {
        recorder_ptr->record(event);
        if (ipc_server != nullptr) {
          ipc_server->broadcast(event);
        }
        print_event(event, hex_raw, hex_frame);
      },
      seq_alloc_);

  auto on_event = [this](PacketEvent const& event) {
    events_seen_.fetch_add(1, std::memory_order_relaxed);
    pipeline_->consume(event);
  };

  if (config_.mode == "udp") {
    UdpDirectCaptureOptions opts;
    opts.bind_host = config_.bind_host;
    opts.bind_port = config_.bind_port;
    opts.target_host = config_.target_host;
    opts.target_port = config_.target_port;
    auto session = std::make_unique<UdpDirectCaptureSession>(opts, on_event, seq_alloc_);
    session->start();
    session_ = std::make_unique<SendableSessionAdapter<UdpDirectCaptureSession>>(std::move(session));
  } else if (config_.mode == "tcp-client") {
    TcpDirectCaptureOptions opts;
    opts.host = config_.host;
    opts.port = config_.port;
    auto session = std::make_unique<TcpDirectCaptureSession>(opts, on_event, seq_alloc_);
    session->start();
    session_ = std::make_unique<SendableSessionAdapter<TcpDirectCaptureSession>>(std::move(session));
  } else if (config_.mode == "tcp-server") {
    TcpServerCaptureOptions opts;
    opts.listen_host = config_.listen_host;
    opts.listen_port = config_.listen_port;
    auto session = std::make_unique<TcpServerCaptureSession>(opts, on_event, seq_alloc_);
    session->start();
    session_ = std::make_unique<SendableSessionAdapter<TcpServerCaptureSession>>(std::move(session));
  } else if (config_.mode == "tcp-proxy") {
    TcpProxyConfig cfg;
    cfg.listen_host = config_.listen_host;
    cfg.listen_port = config_.listen_port;
    cfg.target_host = config_.target_host;
    cfg.target_port = config_.target_port;
    cfg.latency_enabled = config_.latency;
    auto session = std::make_unique<TcpProxyCaptureSession>(cfg, on_event, seq_alloc_);
    session->start();
    session_ = std::make_unique<ProxySessionAdapter>(std::move(session));
  } else if (config_.mode == "serial") {
    SerialCaptureOptions opts;
    opts.port = config_.serial_port;
    opts.baudrate = config_.baudrate;
    opts.data_bits = config_.data_bits;
    opts.stop_bits = config_.stop_bits;
    opts.parity = config_.parity;
    opts.flow_control = config_.flow_control;
    auto session = std::make_unique<SerialDirectCaptureSession>(opts, on_event, seq_alloc_);
    session->start();
    session_ = std::make_unique<SendableSessionAdapter<SerialDirectCaptureSession>>(std::move(session));
  } else {
    throw std::invalid_argument("unknown or missing mode: " + config_.mode);
  }
}

void EngineController::stop_capture_locked() {
  if (session_) {
    session_->stop();
  }
  session_.reset();
  pipeline_.reset();
  recorder_.reset();
  // config_/has_config_ are retained so a restart doesn't require re-configuring.
  capturing_ = false;
}

}  // namespace packet_probe::cli
