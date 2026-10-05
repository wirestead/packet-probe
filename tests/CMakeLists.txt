# The tests are assert()-based; keep them checking in Release builds too (the
# vendored wirestead defaults CMAKE_BUILD_TYPE to Release, which adds NDEBUG).
add_compile_options($<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
if(MSVC)
  add_library(test_msvc_no_dialogs OBJECT msvc_no_dialogs.cpp)
  link_libraries(test_msvc_no_dialogs)
endif()

add_executable(test_hex_dump test_hex_dump.cpp)
target_include_directories(test_hex_dump PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_hex_dump PRIVATE packet_probe::core)
add_test(NAME test_hex_dump COMMAND test_hex_dump)

add_executable(test_jsonl_recorder test_jsonl_recorder.cpp)
target_include_directories(test_jsonl_recorder PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_jsonl_recorder PRIVATE packet_probe::core)
add_test(NAME test_jsonl_recorder COMMAND test_jsonl_recorder)

add_executable(test_latency_tracker test_latency_tracker.cpp)
target_include_directories(test_latency_tracker PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_latency_tracker PRIVATE packet_probe::core)
add_test(NAME test_latency_tracker COMMAND test_latency_tracker)

add_executable(test_packet_event_direction test_packet_event_direction.cpp)
target_link_libraries(test_packet_event_direction PRIVATE packet_probe::core)
add_test(NAME test_packet_event_direction COMMAND test_packet_event_direction)

add_executable(test_serial_options test_serial_options.cpp)
target_include_directories(test_serial_options PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_serial_options PRIVATE packet_probe::core)
add_test(NAME test_serial_options COMMAND test_serial_options)

add_executable(test_frame_decoders test_frame_decoders.cpp)
target_include_directories(test_frame_decoders PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_frame_decoders PRIVATE packet_probe::core)
add_test(NAME test_frame_decoders COMMAND test_frame_decoders)

add_executable(test_event_pipeline test_event_pipeline.cpp)
target_include_directories(test_event_pipeline PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_event_pipeline PRIVATE packet_probe::core)
add_test(NAME test_event_pipeline COMMAND test_event_pipeline)

add_executable(test_send_input_parser test_send_input_parser.cpp)
target_include_directories(test_send_input_parser PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_send_input_parser PRIVATE packet_probe::core)
add_test(NAME test_send_input_parser COMMAND test_send_input_parser)

add_executable(test_ipc_protocol test_ipc_protocol.cpp)
target_link_libraries(test_ipc_protocol PRIVATE packet_probe::core)
add_test(NAME test_ipc_protocol COMMAND test_ipc_protocol)

add_executable(test_ipc_event_server test_ipc_event_server.cpp)
target_include_directories(test_ipc_event_server PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_ipc_event_server PRIVATE packet_probe::core wirestead::wirestead)
add_test(NAME test_ipc_event_server COMMAND test_ipc_event_server)

add_executable(test_public_headers test_public_headers.cpp)
target_link_libraries(test_public_headers PRIVATE packet_probe::core)
add_test(NAME test_public_headers COMMAND test_public_headers)

add_executable(
  test_cli_options
  test_cli_options.cpp
  ../apps/packet-probe-cli/cli_decoder_options.cpp
  ../apps/packet-probe-cli/cli_send_options.cpp
  ../apps/packet-probe-cli/cli_options.cpp
)
target_include_directories(test_cli_options PRIVATE ../apps/packet-probe-cli)
target_include_directories(test_cli_options PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_cli_options PRIVATE packet_probe::core)
add_test(NAME test_cli_options COMMAND test_cli_options)

add_executable(
  test_engine_config
  test_engine_config.cpp
  ../apps/packet-probe-cli/engine_config.cpp
)
target_include_directories(test_engine_config PRIVATE ../apps/packet-probe-cli)
target_include_directories(test_engine_config PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_engine_config PRIVATE packet_probe::core nlohmann_json::nlohmann_json)
add_test(NAME test_engine_config COMMAND test_engine_config)

add_executable(
  test_engine_controller
  test_engine_controller.cpp
  ../apps/packet-probe-cli/engine_controller.cpp
  ../apps/packet-probe-cli/serial_ports.cpp
  ../apps/packet-probe-cli/engine_config.cpp
  ../apps/packet-probe-cli/run_common.cpp
  ../apps/packet-probe-cli/cli_options.cpp
  ../apps/packet-probe-cli/cli_decoder_options.cpp
  ../apps/packet-probe-cli/cli_send_options.cpp
)
target_include_directories(test_engine_controller PRIVATE ../apps/packet-probe-cli)
target_include_directories(test_engine_controller PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_engine_controller PRIVATE packet_probe::core packet_probe::capture wirestead::wirestead nlohmann_json::nlohmann_json)
add_test(NAME test_engine_controller COMMAND test_engine_controller)

add_executable(test_tcp_server_capture_session test_tcp_server_capture_session.cpp)
target_include_directories(test_tcp_server_capture_session PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_tcp_server_capture_session PRIVATE packet_probe::capture)
add_test(NAME test_tcp_server_capture_session COMMAND test_tcp_server_capture_session)

add_executable(test_tcp_capture_session test_tcp_capture_session.cpp)
target_include_directories(test_tcp_capture_session PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_tcp_capture_session PRIVATE packet_probe::capture)
add_test(NAME test_tcp_capture_session COMMAND test_tcp_capture_session)

add_executable(test_udp_capture_session test_udp_capture_session.cpp)
target_include_directories(test_udp_capture_session PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(test_udp_capture_session PRIVATE packet_probe::capture)
add_test(NAME test_udp_capture_session COMMAND test_udp_capture_session)
