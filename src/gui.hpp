#pragma once

#include <string>

namespace intercept::gui {

void start();
void stop();

void push_event(const std::string& event);

}