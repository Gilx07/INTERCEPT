#include "logger.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace {

std::mutex g_mutex;
std::ofstream g_file;

std::filesystem::path log_path()
{
    char buffer[MAX_PATH]{};

    const DWORD n =
        GetModuleFileNameA(
            nullptr,
            buffer,
            MAX_PATH
        );

    if (n == 0 || n >= MAX_PATH)
        return std::filesystem::current_path() / "INTERCEPT.log";

    return std::filesystem::path(buffer).parent_path()
        / "INTERCEPT.log";
}

std::string timestamp()
{
    SYSTEMTIME st{};

    GetLocalTime(&st);

    std::ostringstream ss;

    ss << std::setfill('0')
       << std::setw(2) << st.wHour << ':'
       << std::setw(2) << st.wMinute << ':'
       << std::setw(2) << st.wSecond << '.'
       << std::setw(3) << st.wMilliseconds;

    return ss.str();
}

void write(
    const char* level,
    const std::string& message
)
{
    std::lock_guard lock(g_mutex);

    if (!g_file.is_open())
        return;

    g_file
        << '['
        << timestamp()
        << "] ["
        << level
        << "] "
        << message
        << '\n';

    g_file.flush();
}

} // namespace

namespace intercept::log {

void init()
{
    std::lock_guard lock(g_mutex);

    if (g_file.is_open())
        return;

    g_file.open(
        log_path(),
        std::ios::out | std::ios::app
    );
}

void shutdown()
{
    std::lock_guard lock(g_mutex);

    if (g_file.is_open())
        g_file.flush();

    g_file.close();
}

void info(const std::string& message)
{
    write("INFO", message);
}

void warn(const std::string& message)
{
    write("WARN", message);
}

void error(const std::string& message)
{
    write("ERROR", message);
}

std::string hex_dump(
    const unsigned char* data,
    std::size_t size
)
{
    if (!data || size == 0)
        return {};

    std::ostringstream ss;

    ss << std::hex
       << std::setfill('0');

    for (std::size_t i = 0; i < size; ++i)
    {
        if (i)
            ss << ' ';

        ss << std::setw(2)
           << static_cast<unsigned int>(data[i]);
    }

    return ss.str();
}

} // namespace intercept::log