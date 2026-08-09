#pragma once

#include "event_queue.hpp"

#include <cstdint>
#include <string>

namespace intercept::session {

void start();
void stop();
void record(const events::Event& event);
bool active();
void mark(const std::string& label);

} // namespace intercept::session
