#pragma once

#include <cstddef>
#include <string>

namespace intercept::log {

void init();
void shutdown();

void info(const std::string& message);
void warn(const std::string& message);
void error(const std::string& message);

std::string hex_dump(
    const unsigned char* data,
    std::size_t size
);

} // namespace intercept::log