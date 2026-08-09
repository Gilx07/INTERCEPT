#include "session.hpp"
#include "logger.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace intercept::session {
namespace {

std::atomic_bool g_active{false};
std::atomic_bool g_stop{false};
std::thread g_hotkey_thread;
std::mutex g_mutex;
std::ofstream g_file;
std::chrono::steady_clock::time_point g_started{};
std::uint64_t g_sequence = 0;

std::string csv(const std::string& value)
{
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

const char* kind_name(events::Kind kind)
{
    return kind == events::Kind::Packet ? "PACKET" : "RPC";
}

const char* direction_name(events::Direction direction)
{
    return direction == events::Direction::Incoming ? "IN" : "OUT";
}

void write_row(const std::string& type, const std::string& direction, int id,
               std::size_t bytes, const std::string& name,
               const std::string& details, const std::string& hex,
               std::uint64_t sequence, std::uint64_t elapsed_ms,
               const std::string& marker)
{
    if (!g_file.is_open()) return;
    g_file << sequence << ',' << elapsed_ms << ','
           << csv(type) << ',' << csv(direction) << ',' << id << ',' << bytes << ','
           << csv(name) << ',' << csv(details) << ',' << csv(hex) << ',' << csv(marker) << '\n';
    g_file.flush();
}

void start_locked()
{
    if (g_active.load()) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char filename[128]{};
    _snprintf_s(filename, _TRUNCATE,
        "INTERCEPT_capture_%04u%02u%02u_%02u%02u%02u.csv",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    g_file.open(filename, std::ios::out | std::ios::trunc);
    if (!g_file.is_open()) {
        log::error("Capture session: failed to create CSV file.");
        return;
    }

    g_sequence = 0;
    g_started = std::chrono::steady_clock::now();
    g_active = true;
    g_file << "sequence,elapsed_ms,type,direction,id,bytes,name,details,hex,marker\n";
    g_file.flush();
    log::info(std::string("Capture session STARTED: ") + filename);
}

void stop_locked()
{
    if (!g_active.exchange(false)) return;
    if (g_file.is_open()) g_file.close();
    log::info("Capture session STOPPED. CSV capture saved.");
}

void toggle()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_active.load()) stop_locked();
    else start_locked();
}

void hotkey_loop()
{
    bool prev_f6 = false;
    bool prev_f7 = false;
    bool prev_f8 = false;
    bool prev_f9 = false;

    while (!g_stop.load()) {
        const bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        const bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        const bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        const bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;

        if (f6 && !prev_f6) toggle();
        if (f7 && !prev_f7) mark("ALT");
        if (f8 && !prev_f8) mark("JOB_START");
        if (f9 && !prev_f9) mark("REWARD");

        prev_f6 = f6;
        prev_f7 = f7;
        prev_f8 = f8;
        prev_f9 = f9;
        Sleep(25);
    }
}

} // namespace

void start()
{
    if (g_hotkey_thread.joinable()) return;
    g_stop = false;
    g_hotkey_thread = std::thread(hotkey_loop);
    log::info("Capture controls: F6 start/stop, F7 ALT, F8 JOB_START, F9 REWARD.");
}

void stop()
{
    g_stop = true;
    if (g_hotkey_thread.joinable()) g_hotkey_thread.join();
    std::lock_guard<std::mutex> lock(g_mutex);
    stop_locked();
}

bool active()
{
    return g_active.load();
}

void record(const events::Event& event)
{
    if (!g_active.load()) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active.load()) return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_started).count();
    ++g_sequence;
    write_row(kind_name(event.kind), direction_name(event.direction), event.id,
              event.bytes, event.name, event.details, event.hex,
              g_sequence, static_cast<std::uint64_t>(elapsed), "");
}

void mark(const std::string& label)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active.load()) return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_started).count();
    ++g_sequence;
    write_row("MARKER", "", -1, 0, label, "", "",
              g_sequence, static_cast<std::uint64_t>(elapsed), label);
    log::info("Capture marker: " + label);
}

} // namespace intercept::session
