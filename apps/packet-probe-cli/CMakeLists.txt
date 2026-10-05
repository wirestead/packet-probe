set(PACKET_PROBE_CLI_COMMON_SOURCES
    cli_decoder_options.cpp
    cli_send_options.cpp
    cli_options.cpp
    cli_help.cpp
    engine_config.cpp
)

set(PACKET_PROBE_CLI_RUN_SOURCES
    run_common.cpp
    run_tcp_client.cpp
    run_tcp_proxy.cpp
    run_tcp_server.cpp
    run_serial.cpp
    run_udp.cpp
    run_engine.cpp
    engine_controller.cpp
)

add_executable(packet-probe main.cpp ${PACKET_PROBE_CLI_COMMON_SOURCES} ${PACKET_PROBE_CLI_RUN_SOURCES})

target_compile_features(packet-probe PRIVATE cxx_std_20)
target_include_directories(packet-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(packet-probe PRIVATE packet_probe::capture nlohmann_json::nlohmann_json)
set_target_properties(packet-probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
