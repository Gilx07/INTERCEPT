#pragma once

#include <string>

namespace intercept::monitor {

void install();
void uninstall();

bool send_packet_hex(const std::string& hex);
bool send_rpc_hex(int id, const std::string& hex);

} // namespace intercept::monitor
