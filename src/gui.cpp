#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gui.hpp"
#include "logger.hpp"

#pragma comment(lib, "comctl32.lib")

namespace intercept::gui {

namespace {

constexpr int IDC_FILTER     = 1001;
constexpr int IDC_AUTOSCROLL = 1002;
constexpr int IDC_CLEAR      = 1003;
constexpr int IDC_LIST       = 1004;

constexpr UINT WM_INTERCEPT_EVENT = WM_APP + 1;

constexpr size_t MAX_QUEUE_SIZE = 10000;
constexpr size_t MAX_EVENTS_PER_TICK = 250;

HWND g_hwnd = nullptr;
HWND g_filter = nullptr;
HWND g_list = nullptr;
HWND g_autoscroll = nullptr;

HFONT g_font = nullptr;

std::mutex g_mutex;
std::deque<std::string> g_pending;

std::mutex g_state_mutex;
std::condition_variable g_state_cv;

std::thread g_gui_thread;

std::atomic_bool g_running{false};
std::atomic_bool g_ready{false};

bool g_auto_scroll = true;
std::string g_filter_text;

LRESULT CALLBACK wnd_proc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

void set_filter_text()
{
    if (!g_filter)
        return;

    char buffer[1024]{};

    GetWindowTextA(
        g_filter,
        buffer,
        static_cast<int>(sizeof(buffer))
    );

    g_filter_text = buffer;

    std::transform(
        g_filter_text.begin(),
        g_filter_text.end(),
        g_filter_text.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );
}

bool matches_filter(const std::string& event)
{
    if (g_filter_text.empty())
        return true;

    std::string lower = event;

    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    return lower.find(g_filter_text) != std::string::npos;
}

void add_column(
    int index,
    int width,
    const char* text
)
{
    if (!g_list)
        return;

    LVCOLUMNA column{};

    column.mask =
        LVCF_TEXT |
        LVCF_WIDTH |
        LVCF_SUBITEM;

    column.cx = width;
    column.pszText = const_cast<char*>(text);
    column.iSubItem = index;

    SendMessageA(
        g_list,
        LVM_INSERTCOLUMNA,
        static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(&column)
    );
}

void add_columns()
{
    add_column(0, 100, "Type");
    add_column(1, 100, "ID");
    add_column(2, 80, "Size");
    add_column(3, 120, "Direction");
    add_column(4, 700, "Data");
}

void set_item_text(
    int row,
    int column,
    const char* text
)
{
    if (!g_list)
        return;

    LVITEMA item{};

    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = column;
    item.pszText = const_cast<char*>(text);

    SendMessageA(
        g_list,
        LVM_SETITEMTEXTA,
        static_cast<WPARAM>(row),
        reinterpret_cast<LPARAM>(&item)
    );
}

void insert_row(
    const char* type,
    const char* id,
    const char* size,
    const char* direction,
    const char* data
)
{
    if (!g_list)
        return;

    LVITEMA item{};

    item.mask = LVIF_TEXT;
    item.iItem = 0;
    item.iSubItem = 0;
    item.pszText = const_cast<char*>(type);

    const LRESULT row =
        SendMessageA(
            g_list,
            LVM_INSERTITEMA,
            0,
            reinterpret_cast<LPARAM>(&item)
        );

    if (row < 0)
        return;

    set_item_text(
        static_cast<int>(row),
        1,
        id
    );

    set_item_text(
        static_cast<int>(row),
        2,
        size
    );

    set_item_text(
        static_cast<int>(row),
        3,
        direction
    );

    set_item_text(
        static_cast<int>(row),
        4,
        data
    );

    if (g_auto_scroll)
    {
        SendMessageA(
            g_list,
            LVM_ENSUREVISIBLE,
            static_cast<WPARAM>(row),
            TRUE
        );
    }
}

void parse_event(const std::string& message)
{
    if (!matches_filter(message))
        return;

    std::string parts[5];

    size_t start = 0;

    for (int i = 0; i < 4; ++i)
    {
        const size_t pos =
            message.find('|', start);

        if (pos == std::string::npos)
        {
            parts[i] =
                message.substr(start);

            start = message.size();

            break;
        }

        parts[i] =
            message.substr(
                start,
                pos - start
            );

        start = pos + 1;
    }

    if (start <= message.size())
    {
        parts[4] =
            message.substr(start);
    }

    insert_row(
        parts[0].c_str(),
        parts[1].c_str(),
        parts[2].c_str(),
        parts[3].c_str(),
        parts[4].c_str()
    );
}

void flush_pending()
{
    std::vector<std::string> events;

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        const size_t count =
            std::min(
                MAX_EVENTS_PER_TICK,
                g_pending.size()
            );

        events.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            events.emplace_back(
                std::move(g_pending.front())
            );

            g_pending.pop_front();
        }
    }

    for (const auto& event : events)
    {
        parse_event(event);
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (!g_pending.empty() && g_hwnd)
        {
            PostMessageA(
                g_hwnd,
                WM_INTERCEPT_EVENT,
                0,
                0
            );
        }
    }
}

void create_controls(HWND hwnd)
{
    g_font =
        static_cast<HFONT>(
            GetStockObject(DEFAULT_GUI_FONT)
        );

    g_filter = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "",
        WS_CHILD |
        WS_VISIBLE |
        ES_AUTOHSCROLL,
        10,
        10,
        300,
        24,
        hwnd,
        reinterpret_cast<HMENU>(IDC_FILTER),
        GetModuleHandleA(nullptr),
        nullptr
    );

    SendMessageA(
        g_filter,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font),
        TRUE
    );

    HWND label = CreateWindowExA(
        0,
        "STATIC",
        "Filter:",
        WS_CHILD |
        WS_VISIBLE,
        320,
        13,
        50,
        20,
        hwnd,
        nullptr,
        GetModuleHandleA(nullptr),
        nullptr
    );

    SendMessageA(
        label,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font),
        TRUE
    );

    g_autoscroll = CreateWindowExA(
        0,
        "BUTTON",
        "Auto Scroll",
        WS_CHILD |
        WS_VISIBLE |
        BS_AUTOCHECKBOX,
        390,
        10,
        110,
        24,
        hwnd,
        reinterpret_cast<HMENU>(IDC_AUTOSCROLL),
        GetModuleHandleA(nullptr),
        nullptr
    );

    SendMessageA(
        g_autoscroll,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font),
        TRUE
    );

    SendMessageA(
        g_autoscroll,
        BM_SETCHECK,
        BST_CHECKED,
        0
    );

    HWND clear = CreateWindowExA(
        0,
        "BUTTON",
        "Clear",
        WS_CHILD |
        WS_VISIBLE |
        BS_PUSHBUTTON,
        510,
        10,
        80,
        24,
        hwnd,
        reinterpret_cast<HMENU>(IDC_CLEAR),
        GetModuleHandleA(nullptr),
        nullptr
    );

    SendMessageA(
        clear,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font),
        TRUE
    );

    g_list = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA,
        "",
        WS_CHILD |
        WS_VISIBLE |
        LVS_REPORT |
        LVS_SINGLESEL |
        LVS_SHOWSELALWAYS,
        10,
        45,
        1000,
        600,
        hwnd,
        reinterpret_cast<HMENU>(IDC_LIST),
        GetModuleHandleA(nullptr),
        nullptr
    );

    SendMessageA(
        g_list,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(g_font),
        TRUE
    );

    SendMessageA(
        g_list,
        LVM_SETEXTENDEDLISTVIEWSTYLE,
        0,
        LVS_EX_FULLROWSELECT |
        LVS_EX_GRIDLINES |
        LVS_EX_DOUBLEBUFFER
    );

    add_columns();
}

LRESULT CALLBACK wnd_proc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            create_controls(hwnd);

            SetTimer(
                hwnd,
                1,
                50,
                nullptr
            );

            {
                std::lock_guard<std::mutex> lock(
                    g_state_mutex
                );

                g_ready = true;
            }

            g_state_cv.notify_all();

            return 0;
        }

        case WM_TIMER:
        {
            if (wParam == 1)
            {
                flush_pending();
            }

            return 0;
        }

        case WM_INTERCEPT_EVENT:
        {
            flush_pending();
            return 0;
        }

        case WM_COMMAND:
        {
            const int id =
                LOWORD(wParam);

            if (id == IDC_CLEAR)
            {
                if (g_list)
                {
                    SendMessageA(
                        g_list,
                        LVM_DELETEALLITEMS,
                        0,
                        0
                    );
                }

                return 0;
            }

            if (id == IDC_AUTOSCROLL)
            {
                const LRESULT state =
                    SendMessageA(
                        g_autoscroll,
                        BM_GETCHECK,
                        0,
                        0
                    );

                g_auto_scroll =
                    (state == BST_CHECKED);

                return 0;
            }

            if (id == IDC_FILTER)
            {
                if (HIWORD(wParam) == EN_CHANGE)
                {
                    set_filter_text();

                    if (g_list)
                    {
                        SendMessageA(
                            g_list,
                            LVM_DELETEALLITEMS,
                            0,
                            0
                        );
                    }
                }

                return 0;
            }

            return 0;
        }

        case WM_SIZE:
        {
            const int width =
                LOWORD(lParam);

            const int height =
                HIWORD(lParam);

            if (g_list)
            {
                MoveWindow(
                    g_list,
                    10,
                    45,
                    std::max(100, width - 20),
                    std::max(100, height - 55),
                    TRUE
                );
            }

            return 0;
        }

        case WM_CLOSE:
        {
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY:
        {
            KillTimer(hwnd, 1);

            g_hwnd = nullptr;
            g_filter = nullptr;
            g_list = nullptr;
            g_autoscroll = nullptr;

            PostQuitMessage(0);

            return 0;
        }
    }

    return DefWindowProcA(
        hwnd,
        msg,
        wParam,
        lParam
    );
}

void gui_thread_proc()
{
    HINSTANCE instance =
        GetModuleHandleA(nullptr);

    INITCOMMONCONTROLSEX icc{};

    icc.dwSize = sizeof(icc);

    icc.dwICC =
        ICC_LISTVIEW_CLASSES |
        ICC_STANDARD_CLASSES;

    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc{};

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = "INTERCEPT_GUI";
    wc.hCursor =
        LoadCursorA(
            nullptr,
            IDC_ARROW
        );

    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1
        );

    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "INTERCEPT - Packet Monitor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1100,
        700,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!hwnd)
    {
        log::error(
            "Failed to create INTERCEPT GUI."
        );

        {
            std::lock_guard<std::mutex> lock(
                g_state_mutex
            );

            g_ready = true;
        }

        g_state_cv.notify_all();

        return;
    }

    g_hwnd = hwnd;

    ShowWindow(
        hwnd,
        SW_SHOW
    );

    UpdateWindow(hwnd);

    log::info(
        "INTERCEPT GUI started."
    );

    MSG msg{};

    while (GetMessageA(
        &msg,
        nullptr,
        0,
        0
    ) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_running = false;
    g_ready = false;
    g_hwnd = nullptr;
}

} // namespace

void start()
{
    bool expected = false;

    if (!g_running.compare_exchange_strong(
        expected,
        true))
    {
        return;
    }

    g_ready = false;

    g_gui_thread =
        std::thread(gui_thread_proc);

    std::unique_lock<std::mutex> lock(
        g_state_mutex
    );

    g_state_cv.wait_for(
        lock,
        std::chrono::seconds(5),
        [] {
            return g_ready.load();
        }
    );

    if (!g_hwnd)
    {
        log::error(
            "INTERCEPT GUI failed to initialize."
        );

        g_running = false;

        if (g_gui_thread.joinable())
        {
            g_gui_thread.join();
        }
    }
}

void stop()
{
    if (!g_running.load())
        return;

    HWND hwnd = g_hwnd;

    if (hwnd)
    {
        PostMessageA(
            hwnd,
            WM_CLOSE,
            0,
            0
        );
    }

    if (g_gui_thread.joinable())
    {
        g_gui_thread.join();
    }

    g_running = false;

    {
        std::lock_guard<std::mutex> lock(
            g_mutex
        );

        g_pending.clear();
    }

    log::info(
        "INTERCEPT GUI stopped."
    );
}

void push_event(const std::string& event)
{
    if (!g_running.load())
        return;

    {
        std::lock_guard<std::mutex> lock(
            g_mutex
        );

        if (g_pending.size() >= MAX_QUEUE_SIZE)
        {
            g_pending.pop_front();
        }

        g_pending.push_back(event);
    }

    HWND hwnd = g_hwnd;

    if (hwnd)
    {
        PostMessageA(
            hwnd,
            WM_INTERCEPT_EVENT,
            0,
            0
        );
    }
}

} // namespace intercept::gui