#include "console.hpp"

#include <Windows.h>

#include <cstdio>
#include <iostream>
#include <mutex>

namespace {

std::mutex g_mutex;
bool g_initialized = false;

} // namespace

namespace intercept::console {

void init()
{
    std::lock_guard lock(g_mutex);

    if (g_initialized)
        return;

    if (!AllocConsole())
    {
        if (GetLastError() != ERROR_ACCESS_DENIED)
            return;
    }

    FILE* fp = nullptr;

    freopen_s(
        &fp,
        "CONOUT$",
        "w",
        stdout
    );

    freopen_s(
        &fp,
        "CONOUT$",
        "w",
        stderr
    );

    freopen_s(
        &fp,
        "CONIN$",
        "r",
        stdin
    );

    SetConsoleTitleA(
        "INTERCEPT v0.2 - SA-MP Monitor"
    );

    g_initialized = true;

    std::cout
        << "\n"
        << "========================================\n"
        << " INTERCEPT v0.2\n"
        << " GTA SA 1.0 US\n"
        << " SA-MP 0.3.DL-R1\n"
        << "========================================\n"
        << std::flush;
}

void shutdown()
{
    std::lock_guard lock(g_mutex);

    if (!g_initialized)
        return;

    std::cout
        << "\n"
        << "INTERCEPT console shutting down.\n"
        << std::flush;

    g_initialized = false;

    FreeConsole();
}

void print(const std::string& text)
{
    std::lock_guard lock(g_mutex);

    if (!g_initialized)
        return;

    std::cout
        << text
        << std::endl;
}

} // namespace intercept::console