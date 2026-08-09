#include "event_queue.hpp"

#include <atomic>
#include <deque>
#include <mutex>

namespace {
constexpr std::size_t kMaxPending = 4096;
std::mutex g_mutex;
std::deque<intercept::events::Event> g_queue;
std::atomic_uint64_t g_sequence{0};
std::atomic_uint64_t g_total_events{0};
std::atomic_uint64_t g_total_packets{0};
std::atomic_uint64_t g_total_rpcs{0};
std::atomic_uint64_t g_dropped{0};
}

namespace intercept::events {

void push(Event event)
{
    event.sequence = ++g_sequence;
    ++g_total_events;
    if (event.kind == Kind::Packet) ++g_total_packets;
    else ++g_total_rpcs;

    std::lock_guard lock(g_mutex);
    if (g_queue.size() >= kMaxPending) {
        g_queue.pop_front();
        ++g_dropped;
    }
    g_queue.push_back(std::move(event));
}

std::vector<Event> drain()
{
    std::vector<Event> result;
    std::lock_guard lock(g_mutex);
    result.reserve(g_queue.size());
    while (!g_queue.empty()) {
        result.push_back(std::move(g_queue.front()));
        g_queue.pop_front();
    }
    return result;
}

void clear()
{
    std::lock_guard lock(g_mutex);
    g_queue.clear();
}

std::uint64_t total_events() { return g_total_events.load(); }
std::uint64_t total_packets() { return g_total_packets.load(); }
std::uint64_t total_rpcs() { return g_total_rpcs.load(); }
std::uint64_t dropped_events() { return g_dropped.load(); }

} // namespace intercept::events
