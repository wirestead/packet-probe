#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "recorder/jsonl_recorder.hpp"

namespace {

packet_probe::PacketEvent base_event() {
  packet_probe::PacketEvent event;
  event.sequence = 1;
  event.timestamp_ns = 1781234567890;
  event.session_id = "tcp-client-1";
  event.transport = "tcp";
  event.direction = packet_probe::Direction::Rx;
  event.type = packet_probe::EventType::RawBytes;
  return event;
}

}  // namespace

int main() {
  auto metadata_line = packet_probe::serialize_metadata_jsonl();
  assert(metadata_line.find("\"type\":\"metadata\"") != std::string::npos);
  assert(metadata_line.find("\"schema\":\"packet-probe.log.v1\"") != std::string::npos);
  assert(metadata_line.find("\"event_schema\":\"packet-probe.event.v1\"") != std::string::npos);

  auto event = base_event();
  event.payload = {0x02, 0x10, 0x01, 0x00, 0x03, 0xA7};
  event.summary = "RX 6 bytes";

  auto line = packet_probe::serialize_jsonl(event);
  assert(line ==
         "{\"seq\":1,\"parent_seq\":0,\"parent_seqs\":[],\"time_ns\":1781234567890,\"session\":\"tcp-client-1\",\"transport\":\"tcp\","
         "\"direction\":\"rx\",\"type\":\"raw_bytes\",\"size\":6,\"payload_hex\":\"0210010003A7\","
         "\"summary\":\"RX 6 bytes\"}");

  auto empty = base_event();
  empty.payload = {};
  empty.summary = "RX 0 bytes";
  auto empty_line = packet_probe::serialize_jsonl(empty);
  assert(empty_line.find("\"size\":0") != std::string::npos);
  assert(empty_line.find("\"payload_hex\":\"\"") != std::string::npos);

  auto binary = base_event();
  binary.payload = {0x00, 0x7F, 0x80, 0xFF};
  binary.summary = "binary \"payload\"";
  auto binary_line = packet_probe::serialize_jsonl(binary);
  assert(binary_line.find("\"payload_hex\":\"007F80FF\"") != std::string::npos);
  assert(binary_line.find("\"summary\":\"binary \\\"payload\\\"\"") != std::string::npos);

  auto error = base_event();
  error.type = packet_probe::EventType::Error;
  error.summary = "line\nbreak";
  auto error_line = packet_probe::serialize_jsonl(error);
  assert(error_line.find("\"type\":\"error\"") != std::string::npos);
  assert(error_line.find("\"summary\":\"line\\nbreak\"") != std::string::npos);

  auto proxied = base_event();
  proxied.direction = packet_probe::Direction::AppToDevice;
  proxied.source_endpoint = "127.0.0.1:53124";
  proxied.destination_endpoint = "192.168.0.10:9000";
  proxied.payload = {0xAA};
  proxied.summary = "APP -> DEVICE 1 bytes";
  auto proxied_line = packet_probe::serialize_jsonl(proxied);
  assert(proxied_line.find("\"direction\":\"app_to_device\"") != std::string::npos);
  assert(proxied_line.find("\"source\":\"127.0.0.1:53124\"") != std::string::npos);
  assert(proxied_line.find("\"destination\":\"192.168.0.10:9000\"") != std::string::npos);

  auto latency = base_event();
  latency.type = packet_probe::EventType::Latency;
  latency.direction = packet_probe::Direction::DeviceToApp;
  latency.summary = "response latency 940 us";
  latency.request_sequence = 12;
  latency.response_sequence = 13;
  latency.latency_ns = 940000;
  latency.request_size = 8;
  latency.response_size = 7;
  auto latency_line = packet_probe::serialize_jsonl(latency);
  assert(latency_line.find("\"type\":\"latency\"") != std::string::npos);
  assert(latency_line.find("\"request_seq\":12") != std::string::npos);
  assert(latency_line.find("\"response_seq\":13") != std::string::npos);
  assert(latency_line.find("\"latency_ns\":940000") != std::string::npos);
  assert(latency_line.find("\"latency_us\":940") != std::string::npos);

  auto serial = base_event();
  serial.session_id = "serial-1";
  serial.transport = "serial";
  serial.direction = packet_probe::Direction::DeviceToApp;
  serial.source_endpoint = "/dev/ttyUSB0";
  serial.destination_endpoint = "packet-probe";
  serial.payload = {0x02, 0x10, 0x01, 0x00, 0x03, 0xA7};
  serial.summary = "DEVICE -> APP 6 bytes";
  auto serial_line = packet_probe::serialize_jsonl(serial);
  assert(serial_line.find("\"transport\":\"serial\"") != std::string::npos);
  assert(serial_line.find("\"direction\":\"device_to_app\"") != std::string::npos);
  assert(serial_line.find("\"source\":\"/dev/ttyUSB0\"") != std::string::npos);
  assert(serial_line.find("\"destination\":\"packet-probe\"") != std::string::npos);
  assert(serial_line.find("\"payload_hex\":\"0210010003A7\"") != std::string::npos);

  auto frame = base_event();
  frame.sequence = 15;
  frame.parent_sequence = 14;
  frame.type = packet_probe::EventType::Frame;
  frame.payload = {0xAA, 0xBB};
  frame.summary = "FRAME 2 bytes";
  auto frame_line = packet_probe::serialize_jsonl(frame);
  assert(frame_line.find("\"seq\":15") != std::string::npos);
  assert(frame_line.find("\"parent_seq\":14") != std::string::npos);
  assert(frame_line.find("\"type\":\"frame\"") != std::string::npos);

  auto const path = std::filesystem::temp_directory_path() / "packet-probe-jsonl-recorder-test.jsonl";
  std::filesystem::remove(path);
  {
    packet_probe::JsonlRecorder recorder(path.string());
    recorder.record(event);
  }

  std::ifstream input(path);
  std::string first_line;
  std::string second_line;
  std::getline(input, first_line);
  std::getline(input, second_line);
  assert(first_line.find("\"type\":\"metadata\"") != std::string::npos);
  assert(first_line.find("\"schema\":\"packet-probe.log.v1\"") != std::string::npos);
  assert(second_line == line);
  std::filesystem::remove(path);

  return 0;
}
