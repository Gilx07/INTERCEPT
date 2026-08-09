#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace intercept::events {

enum class Kind { Packet, Rpc };
enum class Direction { Incoming, Outgoing };

struct Event {
    std::uint64_t sequence = 0;
    Kind kind = Kind::Packet;
    Direction direction = Direction::Incoming;
    int id = -1;
    std::size_t bytes = 0;
    std::string name;
    std::string details;
    std::string hex;
};

void push(Event event);
std::vector<Event> drain();
void clear();
std::uint64_t total_events();
std::uint64_t total_packets();
std::uint64_t total_rpcs();
std::uint64_t dropped_events();

} // namespace intercept::events
