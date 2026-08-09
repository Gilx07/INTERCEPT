#include "logger.hpp"
#include "monitor.hpp"
#include "console.hpp"

#include <RakHook/rakhook.hpp>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>

namespace {

HMODULE g_module = nullptr;
HANDLE g_thread = nullptr;

std::atomic_bool g_stop{false};

DWORD WINAPI worker_thread(
    LPVOID
)
{
    // ========================================================
    // Logger
    // ========================================================

    intercept::log::init();

    // ========================================================
    // Console
    // ========================================================

    intercept::console::init();

    intercept::log::info(
        "========================================"
    );

    intercept::log::info(
        "INTERCEPT v0.2 starting"
    );

    intercept::log::info(
        "Target: GTA SA 1.0 US / SA-MP 0.3.DL-R1"
    );

    intercept::log::info(
        "Mode: realtime packet/RPC monitor"
    );

    intercept::log::info(
        "========================================"
    );

    intercept::console::print(
        "[INIT] Waiting for samp.dll..."
    );

    // ========================================================
    // Wait for SA-MP
    // ========================================================

    while (!g_stop.load())
    {
        if (GetModuleHandleA("samp.dll"))
            break;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(250)
        );
    }

    if (g_stop.load())
    {
        intercept::log::info(
            "Startup cancelled."
        );

        intercept::console::print(
            "[INIT] Startup cancelled."
        );

        intercept::console::shutdown();
        intercept::log::shutdown();

        return 0;
    }

    intercept::log::info(
        "samp.dll detected."
    );

    intercept::console::print(
        "[INIT] samp.dll detected."
    );

    // ========================================================
    // Install monitor
    // ========================================================

    intercept::monitor::install();

    // ========================================================
    // Initialize RakHook
    // ========================================================

    bool initialized = false;

    for (
        int attempt = 1;
        attempt <= 120 && !g_stop.load();
        ++attempt
    )
    {
        if (rakhook::initialize())
        {
            initialized = true;

            std::ostringstream ss;

            ss
                << "RakHook initialized."
                << " attempt="
                << attempt
                << " samp_version="
                << static_cast<int>(
                    rakhook::samp_version()
                );

            intercept::log::info(
                ss.str()
            );

            intercept::console::print(
                "[INIT] " + ss.str()
            );

            intercept::log::info(
                "Realtime monitor is active."
            );

            intercept::console::print(
                "[READY] Realtime monitor is active."
            );

            break;
        }

        if (attempt == 120)
        {
            intercept::log::error(
                "RakHook initialization failed "
                "after 120 attempts."
            );

            intercept::console::print(
                "[ERROR] RakHook initialization failed."
            );
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(250)
        );
    }

    // ========================================================
    // Main worker loop
    // ========================================================

    while (!g_stop.load())
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500)
        );
    }

    // ========================================================
    // Shutdown
    // ========================================================

    if (initialized &&
        rakhook::initialized)
    {
        intercept::log::info(
            "Destroying RakHook."
        );

        rakhook::destroy();
    }

    intercept::log::info(
        "INTERCEPT stopped."
    );

    intercept::console::print(
        "[STOP] INTERCEPT stopped."
    );

    intercept::console::shutdown();
    intercept::log::shutdown();

    return 0;
}

} // namespace

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID
)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = hModule;

        DisableThreadLibraryCalls(
            hModule
        );

        g_stop = false;

        g_thread = CreateThread(
            nullptr,
            0,
            worker_thread,
            nullptr,
            0,
            nullptr
        );

        return g_thread != nullptr;
    }

    if (reason == DLL_PROCESS_DETACH)
    {
        /*
         * Do not wait for the worker thread here.
         *
         * Windows may already be tearing down
         * the process and its loaded modules.
         */

        g_stop = true;

        if (g_thread)
        {
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
    }

    return TRUE;
}