#include "engine_controller.hpp"

#include "wirestead/wirestead.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define TEST_ASSERT(cond, msg)                                                                    \
  if (!(cond)) {                                                                                  \
    std::cerr << "Assertion failed: " << #cond << " - " << msg << std::endl;                      \
    std::exit(1);                                                                                 \
  }

namespace {

std::filesystem::path make_temp_socket_path() {
  auto base = std::filesystem::temp_directory_path();
  auto name = "pp-engine-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sock";
  return base / name;
}

void wait_until(std::function<bool()> const& predicate, std::string const& description,
                std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  TEST_ASSERT(predicate(), "Timeout waiting for: " + description);
}

}  // namespace

int main() {
  std::cout << "Starting EngineController integration test..." << std::endl;

  auto socket_path = make_temp_socket_path();

  packet_probe::IpcServerOptions server_options;
  server_options.socket_path = socket_path.string();
  packet_probe::IpcEventServer ipc_server(server_options);
  ipc_server.start();
  TEST_ASSERT(ipc_server.running(), "IPC server should be running");

  auto seq_alloc = packet_probe::make_sequence_allocator();
  packet_probe::cli::EngineController controller(&ipc_server, seq_alloc);
  controller.install();

  std::mutex mutex;
  std::vector<nlohmann::json> messages;

  wirestead::UdsClient client(server_options.socket_path);
  client.framer(std::make_unique<wirestead::framer::LineFramer>("\n", false, 65536));
  client.on_message([&](wirestead::MessageContext const& ctx) {
    std::lock_guard<std::mutex> lock(mutex);
    messages.push_back(nlohmann::json::parse(std::string(ctx.data())));
  });

  TEST_ASSERT(client.start_sync(), "client should connect");

  auto find_matching = [&](std::function<bool(nlohmann::json const&)> const& pred,
                           std::string const& label) -> nlohmann::json {
    std::cerr << "[waiting] " << label << std::endl;
    nlohmann::json found;
    wait_until(
        [&] {
          std::lock_guard<std::mutex> lock(mutex);
          for (auto const& msg : messages) {
            if (pred(msg)) {
              found = msg;
              return true;
            }
          }
          return false;
        },
        label);
    std::cerr << "[found]   " << label << " -> " << found.dump() << std::endl;
    return found;
  };

  auto send = [&](nlohmann::json const& msg) { client.send_line(msg.dump()); };

  // metadata is sent immediately on connect
  find_matching([](nlohmann::json const& m) { return m.value("type", "") == "metadata"; }, "metadata");

  // get_status while idle
  send({{"type", "command"}, {"id", "s1"}, {"command", "get_status"}});
  {
    auto const r = find_matching([](nlohmann::json const& m) { return m.value("id", "") == "s1"; }, "s1 result");
    TEST_ASSERT(r.value("ok", false), "get_status should ack ok");
    TEST_ASSERT(r.value("engine_state", "") == "idle", "engine should start idle");
  }

  // configure udp mode
  nlohmann::json config;
  config["mode"] = "udp";
  config["bind_host"] = "127.0.0.1";
  config["bind_port"] = 19412;
  send({{"type", "command"}, {"id", "s2"}, {"command", "configure"}, {"config", config}});
  {
    auto const r = find_matching([](nlohmann::json const& m) { return m.value("id", "") == "s2"; }, "s2 result");
    TEST_ASSERT(r.value("ok", false), "configure should ack ok");
  }
  find_matching(
      [](nlohmann::json const& m) { return m.value("type", "") == "status" && m.value("engine_state", "") == "idle"; },
      "status idle after configure");

  // start_capture
  send({{"type", "command"}, {"id", "s3"}, {"command", "start_capture"}});
  {
    auto const r = find_matching([](nlohmann::json const& m) { return m.value("id", "") == "s3"; }, "s3 result");
    TEST_ASSERT(r.value("ok", false), "start_capture should ack ok");
  }
  find_matching(
      [](nlohmann::json const& m) {
        return m.value("type", "") == "status" && m.value("engine_state", "") == "capturing";
      },
      "status capturing after start_capture");

  // A real UDP datagram should flow through as a raw_bytes event. UDP delivery isn't
  // guaranteed even on loopback, so retry sending until the event shows up instead of
  // trusting a single datagram under parallel test-suite load.
  {
    wirestead::config::UdpConfig udp_cfg;
    udp_cfg.bind_address = "0.0.0.0";
    udp_cfg.local_port = 0;
    udp_cfg.remote_address = "127.0.0.1";
    udp_cfg.remote_port = 19412;
    wirestead::UdpClient sender(udp_cfg);
    TEST_ASSERT(sender.start_sync(), "udp sender should start");

    std::cerr << "[waiting] raw_bytes ping event" << std::endl;
    bool delivered = false;
    for (int attempt = 0; attempt < 20 && !delivered; ++attempt) {
      sender.send(std::string_view("ping"));
      auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
      while (std::chrono::steady_clock::now() < deadline) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto const& msg : messages) {
          if (msg.value("type", "") == "raw_bytes" && msg.value("payload_hex", "") == "70696E67") {
            delivered = true;
            break;
          }
        }
        if (delivered) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    TEST_ASSERT(delivered, "raw_bytes ping event");
    std::cerr << "[found]   raw_bytes ping event" << std::endl;

    sender.stop();
  }

  // start_capture again should fail (already capturing)
  send({{"type", "command"}, {"id", "s4"}, {"command", "start_capture"}});
  {
    auto const r = find_matching([](nlohmann::json const& m) { return m.value("id", "") == "s4"; }, "s4 result");
    TEST_ASSERT(!r.value("ok", true), "double start_capture should fail");
  }

  // list_serial_ports never fails
  send({{"type", "command"}, {"id", "s5"}, {"command", "list_serial_ports"}});
  {
    auto const r = find_matching([](nlohmann::json const& m) { return m.value("id", "") == "s5"; }, "s5 result");
    TEST_ASSERT(r.value("ok", false), "list_serial_ports should ack ok");
    TEST_ASSERT(r.contains("ports") && r["ports"].is_array(), "list_serial_ports should return a ports array");
  }

  // unknown command is rejected with an error, not silently dropped
  send({{"type", "command"}, {"id", "s6"}, {"command", "not_a_real_command"}});
  {
    auto const r = find_matching([](nlohmann::json const& m) { return m.value("id", "") == "s6"; }, "s6 result");
    TEST_ASSERT(!r.value("ok", true), "unknown command should fail");
    TEST_ASSERT(r.contains("error"), "unknown command should report an error");
  }

  // stop_capture
  send({{"type", "command"}, {"id", "s7"}, {"command", "stop_capture"}});
  {
    auto const r = find_matching([](nlohmann::json const& m) { return m.value("id", "") == "s7"; }, "s7 result");
    TEST_ASSERT(r.value("ok", false), "stop_capture should ack ok");
  }
  find_matching(
      [](nlohmann::json const& m) { return m.value("type", "") == "status" && m.value("engine_state", "") == "idle"; },
      "status idle after stop_capture");

  // configuring again after stop (without needing a fresh process/connection) should succeed
  send({{"type", "command"}, {"id", "s8"}, {"command", "configure"}, {"config", config}});
  {
    auto const r = find_matching([&](nlohmann::json const& m) { return m.value("id", "") == "s8"; }, "s8 result");
    TEST_ASSERT(r.value("ok", false), "reconfigure after stop should ack ok");
  }

  client.stop();
  controller.shutdown();
  ipc_server.stop();

  std::error_code ignored;
  std::filesystem::remove(socket_path, ignored);

  std::cout << "All EngineController checks passed!" << std::endl;
  return 0;
}
