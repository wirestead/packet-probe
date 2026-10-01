#include "ipc/ipc_event_server.hpp"

#include "packet_probe/core/jsonl_serializer.hpp"
#include "wirestead/wirestead.hpp"
#include "wirestead/framer/line_framer.hpp"

#include <atomic>
#include <cstdint>
#include <string_view>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace packet_probe {

namespace {

constexpr std::string_view kTcpPrefix = "tcp:";

std::unique_ptr<wirestead::wrapper::ServerInterface> make_uds_server(std::string const& socket_path) {
  auto const path = std::filesystem::path(socket_path);
  auto const parent = path.parent_path();
  if (!parent.empty() && !std::filesystem::exists(parent)) {
    throw std::runtime_error("IPC socket parent directory does not exist: " + parent.string());
  }
  if (std::filesystem::is_directory(path)) {
    throw std::runtime_error("IPC socket path is a directory: " + socket_path);
  }

  // Stale socket cleanup: UDS in Unix requires removing the socket file before binding.
  std::error_code remove_err;
  if (std::filesystem::exists(path)) {
    std::filesystem::remove(path, remove_err);
  }

  auto server = std::make_unique<wirestead::UdsServer>(socket_path);
  server->max_clients(16);
  server->auto_start(false);
  // IPC is a local, single-host control channel; restrict it to the owner to prevent
  // other local users from observing captured traffic or issuing commands.
  server->socket_permissions(0600);
  return server;
}

// "tcp:<host>:<port>". The IPC channel is unauthenticated, so the host must be a
// loopback address; remote access belongs to a front end (e.g. the web gateway).
std::unique_ptr<wirestead::wrapper::ServerInterface> make_tcp_server(std::string const& address) {
  auto const host_port = address.substr(kTcpPrefix.size());
  auto const colon = host_port.rfind(':');
  if (colon == std::string::npos || colon == 0) {
    throw std::invalid_argument("IPC TCP address must be tcp:<host>:<port>: " + address);
  }
  auto const host = host_port.substr(0, colon);
  auto const port_str = host_port.substr(colon + 1);
  std::size_t parsed = 0;
  unsigned long port = 0;
  try {
    port = std::stoul(port_str, &parsed);
  } catch (std::exception const&) {
  }
  if (parsed != port_str.size() || port == 0 || port > 65535) {
    throw std::invalid_argument("invalid IPC TCP port: " + port_str);
  }
  if (host.rfind("127.", 0) != 0 && host != "::1") {
    throw std::invalid_argument("IPC TCP host must be a loopback address (127.x.x.x or ::1): " + host);
  }

  auto server = std::make_unique<wirestead::TcpServer>(static_cast<std::uint16_t>(port));
  server->bind_address(host);
  server->max_clients(16);
  server->auto_start(false);
  return server;
}

}  // namespace

struct IpcEventServer::Impl {
  explicit Impl(IpcServerOptions server_options) : options(std::move(server_options)) {}

  IpcServerOptions options;
  std::unique_ptr<wirestead::wrapper::ServerInterface> server;
  std::atomic<bool> is_running{false};
  CommandHandler command_handler;
  std::mutex command_handler_mutex;
  bool is_tcp = false;

  void start() {
    if (options.socket_path.empty()) {
      throw std::invalid_argument("--ipc requires a socket path");
    }
    if (is_running.load()) {
      return;
    }

    is_tcp = options.socket_path.rfind(kTcpPrefix, 0) == 0;
    server = is_tcp ? make_tcp_server(options.socket_path) : make_uds_server(options.socket_path);

    // Enable line framing so on_message delivers complete newline-terminated commands
    server->framer([]() {
      return std::make_unique<wirestead::framer::LineFramer>("\n", false, 65536);
    });

    server->on_connect([this](wirestead::ConnectionContext const& ctx) {
      if (server) {
        auto metadata = serialize_metadata_jsonl() + '\n';
        server->send_to(ctx.client_id(), metadata);
      }
    });

    server->on_error([this](wirestead::ErrorContext const& ctx) {
      // Do not stop the whole IPC server for a single client error.
    });

    server->on_message([this](wirestead::MessageContext const& ctx) {
      CommandHandler handler_copy;
      {
        std::lock_guard<std::mutex> lock(command_handler_mutex);
        handler_copy = command_handler;
      }
      if (handler_copy) {
        handler_copy(static_cast<IpcClientId>(ctx.client_id()), ctx.data());
      }
    });

    auto started = server->start_sync();
    if (!started) {
      server.reset();
      is_running.store(false);
      throw std::runtime_error("failed to start IPC server on " + options.socket_path);
    }

    is_running.store(true);
  }

  void stop() {
    is_running.store(false);

    if (server) {
      server->stop();
      server.reset();
    }

    if (!is_tcp) {
      std::error_code ignored;
      std::filesystem::remove(options.socket_path, ignored);
    }
  }

  bool running() const { return is_running.load(); }

  void broadcast_metadata() {
    if (!server || !is_running.load()) {
      return;
    }
    server->broadcast(serialize_metadata_jsonl() + '\n');
  }

  void broadcast(PacketEvent const& event) {
    if (!server || !is_running.load()) {
      return;
    }
    server->broadcast(serialize_event_jsonl(event) + '\n');
  }

  bool send_to_client(IpcClientId client_id, std::string const& line) {
    if (!server || !is_running.load()) {
      return false;
    }
    return static_cast<bool>(server->send_to(static_cast<wirestead::ClientId>(client_id), line + '\n'));
  }

  void broadcast_raw(std::string const& line) {
    if (!server || !is_running.load()) {
      return;
    }
    server->broadcast(line + '\n');
  }
};

IpcEventServer::IpcEventServer(IpcServerOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}

IpcEventServer::~IpcEventServer() { stop(); }

void IpcEventServer::start() { impl_->start(); }

void IpcEventServer::stop() { impl_->stop(); }

bool IpcEventServer::running() const { return impl_->running(); }

void IpcEventServer::broadcast_metadata() {
  try {
    impl_->broadcast_metadata();
  } catch (...) {
  }
}

void IpcEventServer::broadcast(PacketEvent const& event) {
  try {
    impl_->broadcast(event);
  } catch (...) {
  }
}

bool IpcEventServer::send_to_client(IpcClientId client_id, std::string const& line) {
  try {
    return impl_->send_to_client(client_id, line);
  } catch (...) {
    return false;
  }
}

void IpcEventServer::broadcast_raw(std::string const& line) {
  try {
    impl_->broadcast_raw(line);
  } catch (...) {
  }
}

void IpcEventServer::set_command_handler(CommandHandler handler) {
  std::lock_guard<std::mutex> lock(impl_->command_handler_mutex);
  impl_->command_handler = std::move(handler);
}

}  // namespace packet_probe
