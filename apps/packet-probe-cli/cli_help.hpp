#pragma once

#include <iosfwd>

#include "cli_options.hpp"

namespace packet_probe::cli {

void print_help(std::ostream& out);
void print_tcp_client_help(std::ostream& out);
void print_tcp_server_help(std::ostream& out);
void print_tcp_proxy_help(std::ostream& out);
void print_serial_help(std::ostream& out);
void print_udp_help(std::ostream& out);
void print_engine_help(std::ostream& out);
void print_help_for_mode(std::ostream& out, CliOptions const& options);

}  // namespace packet_probe::cli
