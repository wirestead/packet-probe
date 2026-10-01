cmake_minimum_required(VERSION 3.16)

project(
  packet-probe
  VERSION 0.1.0
  DESCRIPTION "Lightweight device communication analyzer"
  LANGUAGES CXX
)

option(PACKET_PROBE_BUILD_TESTS "Build Packet Probe tests" ON)
set(PACKET_PROBE_WIRESTEAD_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../wirestead"
    CACHE PATH "Path to a local wirestead source tree"
)
set(PACKET_PROBE_INTERNAL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

if(EXISTS "${PACKET_PROBE_WIRESTEAD_SOURCE_DIR}/CMakeLists.txt")
  set(WIRESTEAD_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE
  )
  # Link wirestead statically: packet-probe then runs without a wirestead DLL next
  # to it (Windows has no rpath), for both the shipped binary and the tests.
  set(WIRESTEAD_BUILD_SHARED
      OFF
      CACHE BOOL "" FORCE
  )
  add_subdirectory("${PACKET_PROBE_WIRESTEAD_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/wirestead")
else()
  find_package(wirestead CONFIG REQUIRED)
endif()

add_library(
  packet_probe_core
  src/core/packet_event.cpp
  src/core/hex_dump.cpp
  src/core/endpoint.cpp
  src/core/jsonl_serializer.cpp
  src/core/latency_tracker.cpp
  src/core/serial_options.cpp
  src/core/send_input_parser.cpp
  src/core/event_pipeline.cpp
  src/ipc/ipc_event_server.cpp
  src/ipc/ipc_protocol.cpp
  src/decoder/frame_decoder.cpp
  src/decoder/fixed_size_frame_decoder.cpp
  src/decoder/delimiter_frame_decoder.cpp
  src/decoder/length_prefix_frame_decoder.cpp
  src/decoder/frame_decoder_factory.cpp
  src/recorder/jsonl_recorder.cpp
)
add_library(packet_probe::core ALIAS packet_probe_core)

target_compile_features(packet_probe_core PUBLIC cxx_std_20)
target_include_directories(
  packet_probe_core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${PACKET_PROBE_INTERNAL_INCLUDE_DIR}
)
target_link_libraries(packet_probe_core PUBLIC wirestead::wirestead)

add_library(
  packet_probe_capture
  src/capture/tcp_direct_capture_session.cpp
  src/capture/tcp_proxy_capture_session.cpp
  src/capture/tcp_server_capture_session.cpp
  src/capture/serial_direct_capture_session.cpp
  src/capture/udp_direct_capture_session.cpp
)
add_library(packet_probe::capture ALIAS packet_probe_capture)
target_compile_features(packet_probe_capture PUBLIC cxx_std_20)
target_include_directories(
  packet_probe_capture
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${PACKET_PROBE_INTERNAL_INCLUDE_DIR}
)
target_link_libraries(packet_probe_capture PUBLIC packet_probe::core wirestead::wirestead)

add_subdirectory(apps/packet-probe-cli)

if(PACKET_PROBE_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
