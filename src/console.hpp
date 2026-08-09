#pragma once

#include <string>

namespace intercept::console {

void init();
void shutdown();

void print(const std::string& text);

} // namespace intercept::console