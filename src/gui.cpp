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
constexpr int IDC_TYPE_TABS  = 1004;
constexpr int IDC_DIR_TABS   = 1005;
constexpr int IDC_LIST_BASE  = 1100;

constexpr UINT WM_INTERCEPT_EVENT = WM_APP + 1;

constexpr size_t MAX_QUEUE_SIZE = 10000;
constexpr size_t MAX_EVENTS_PER_TICK = 250;

enum class EventType { Packet, Rpc };
enum class Direction { Incoming, Outgoing };

HWND g_hwnd = nullptr;
HWND g_filter = nullptr;
HWND g_autoscroll = nullptr;
HWND g_type_tabs = nullptr;
HWND g_dir_tabs = nullptr;

HWND g_lists[2][2]{};

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
int g_type_tab = 0;
int g_dir_tab = 0;

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void set_filter_text()
{
    if (!g_filter) return;

    char buffer[1024]{};
    GetWindowTextA(g_filter, buffer, static_cast<int>(sizeof(buffer)));
    g_filter_text = buffer;

    std::transform(
        g_filter_text.begin(),
        g_filter_text.end(),
        g_filter_text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
}

bool matches_filter(const std::string& event)
{
    if (g_filter_text.empty()) return true;

    std::string lower = event;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );

    return lower.find(g_filter_text) != std::string::npos;
}

HWND current_list()
{
    return g_lists[g_type_tab][g_dir_tab];
}

void show_active_list()
{
    for (int type = 0; type < 2; ++type)
    {
        for (int dir = 0; dir < 2; ++dir)
        {
            if (g_lists[type][dir])
            {
                ShowWindow(
                    g_lists[type][dir],
                    type == g_type_tab && dir == g_dir_tab ? SW_SHOW : SW_HIDE
                );
            }
        }
    }
}

void add_column(HWND list, int index, int width, const char* text)
{
    if (!list) return;

    LVCOLUMNA column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.cx = width;
    column.pszText = const_cast<char*>(text);
    column.iSubItem = index;

    SendMessageA(
        list,
        LVM_INSERTCOLUMNA,
        static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(&column)
    );
}

void add_columns(HWND list)
{
    add_column(list, 0, 100, "ID");
    add_column(list, 1, 90, "Size");
    add_column(list, 2, 260, "Name");
    add_column(list, 3, 700, "Data");
}

void set_item_text(HWND list, int row, int column, const char* text)
{
    if (!list) return;

    LVITEMA item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = column;
    item.pszText = const_cast<char*>(text);

    SendMessageA(
        list,
        LVM_SETITEMTEXTA,
        static_cast<WPARAM>(row),
        reinterpret_cast<LPARAM>(&item)
    );
}

void insert_row(
    EventType type,
    Direction direction,
    const char* id,
    const char* size,
    const char* name,
    const char* data
)
{
    HWND list = g_lists[
        type == EventType::Packet ? 0 : 1
    ][
        direction == Direction::Incoming ? 0 : 1
    ];

    if (!list) return;

    LVITEMA item{};
    item.mask = LVIF_TEXT;
    item.iItem = 0;
    item.iSubItem = 0;
    item.pszText = const_cast<char*>(id);

    const LRESULT row = SendMessageA(
        list,
        LVM_INSERTITEMA,
        0,
        reinterpret_cast<LPARAM>(&item)
    );

    if (row < 0) return;

    set_item_text(list, static_cast<int>(row), 1, size);
    set_item_text(list, static_cast<int>(row), 2, name);
    set_item_text(list, static_cast<int>(row), 3, data);

    if (g_auto_scroll && list == current_list())
    {
        SendMessageA(
            list,
            LVM_ENSUREVISIBLE,
            static_cast<WPARAM>(row),
            TRUE
        );
    }
}

void parse_event(const std::string& message)
{
    std::string parts[5];
    size_t start = 0;

    for (int i = 0; i < 4; ++i)
    {
        const size_t pos = message.find('|', start);

        if (pos == std::string::npos)
        {
            parts[i] = message.substr(start);
            start = message.size();
            break;
        }

        parts[i] = message.substr(start, pos - start);
        start = pos + 1;
    }

    if (start <= message.size())
        parts[4] = message.substr(start);

    EventType type;
    if (parts[0] == "RPC")
        type = EventType::Rpc;
    else if (parts[0] == "PACKET")
        type = EventType::Packet;
    else
        return;

    Direction direction;
    if (parts[3] == "IN")
        direction = Direction::Incoming;
    else if (parts[3] == "OUT")
        direction = Direction::Outgoing;
    else
        return;

    if (!matches_filter(message)) return;

    std::string name = parts[4];
    std::string data;

    const size_t detail_pos = name.find(" | ");
    if (detail_pos != std::string::npos)
    {
        data = name.substr(detail_pos + 3);
        name = name.substr(0, detail_pos);
    }

    insert_row(
        type,
        direction,
        parts[1].c_str(),
        parts[2].c_str(),
        name.c_str(),
        data.c_str()
    );
}

void clear_all_lists()
{
    for (auto& type : g_lists)
    {
        for (HWND list : type)
        {
            if (list)
                SendMessageA(list, LVM_DELETEALLITEMS, 0, 0);
        }
    }
}

void flush_pending()
{
    std::vector<std::string> events;

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        const size_t count = std::min(MAX_EVENTS_PER_TICK, g_pending.size());
        events.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            events.emplace_back(std::move(g_pending.front()));
            g_pending.pop_front();
        }
    }

    for (const auto& event : events)
        parse_event(event);

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (!g_pending.empty() && g_hwnd)
            PostMessageA(g_hwnd, WM_INTERCEPT_EVENT, 0, 0);
    }
}

void create_tabs(HWND hwnd)
{
    g_type_tabs = CreateWindowExA(
        0,
        WC_TABCONTROLA,
        "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        10, 42, 250, 32,
        hwnd,
        reinterpret_cast<HMENU>(IDC_TYPE_TABS),
        GetModuleHandleA(nullptr),
        nullptr
    );

    SendMessageA(g_type_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    TCITEMA item{};
    item.mask = TCIF_TEXT;

    item.pszText = const_cast<char*>("Packet");
    TabCtrl_InsertItem(g_type_tabs, 0, &item);

    item.pszText = const_cast<char*>("RPC");
    TabCtrl_InsertItem(g_type_tabs, 1, &item);

    g_dir_tabs = CreateWindowExA(
        0,
        WC_TABCONTROLA,
        "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        270, 42, 250, 32,
        hwnd,
        reinterpret_cast<HMENU>(IDC_DIR_TABS),
        GetModuleHandleA(nullptr),
        nullptr
    );

    SendMessageA(g_dir_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    item.pszText = const_cast<char*>("IN");
    TabCtrl_InsertItem(g_dir_tabs, 0, &item);

    item.pszText = const_cast<char*>("OUT");
    TabCtrl_InsertItem(g_dir_tabs, 1, &item);
}

HWND create_list(HWND hwnd, int type, int direction)
{
    const int id = IDC_LIST_BASE + type * 2 + direction;

    HWND list = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA,
        "",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        10, 78, 1000, 570,
        hwnd,
        reinterpret_cast<HMENU>(id),
        GetModuleHandleA(nullptr),
        nullptr
    );

    if (!list) return nullptr;

    SendMessageA(list, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    SendMessageA(
        list,
        LVM_SETEXTENDEDLISTVIEWSTYLE,
        0,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER
    );

    add_columns(list);
    return list;
}

void create_controls(HWND hwnd)
{
    g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    g_filter = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        10, 10, 300, 24,
        hwnd,
        reinterpret_cast<HMENU>(IDC_FILTER),
        GetModuleHandleA(nullptr),
        nullptr
    );

    SendMessageA(g_filter, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    HWND label = CreateWindowExA(
        0,
        "STATIC",
        "Filter:",
        WS_CHILD | WS_VISIBLE,
        320, 13, 50, 20,
        hwnd,
        nullptr,
        GetModuleHandleA(nullptr),
        nullptr
    );
    SendMessageA(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    g_autoscroll = CreateWindowExA(
        0,
        "BUTTON",
        "Auto Scroll",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        390, 10, 110, 24,
        hwnd,
        reinterpret_cast<HMENU>(IDC_AUTOSCROLL),
        GetModuleHandleA(nullptr),
        nullptr
    );
    SendMessageA(g_autoscroll, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SendMessageA(g_autoscroll, BM_SETCHECK, BST_CHECKED, 0);

    HWND clear = CreateWindowExA(
        0,
        "BUTTON",
        "Clear",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        510, 10, 80, 24,
        hwnd,
        reinterpret_cast<HMENU>(IDC_CLEAR),
        GetModuleHandleA(nullptr),
        nullptr
    );
    SendMessageA(clear, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    create_tabs(hwnd);

    for (int type = 0; type < 2; ++type)
    {
        for (int direction = 0; direction < 2; ++direction)
            g_lists[type][direction] = create_list(hwnd, type, direction);
    }

    show_active_list();
}

void resize_controls(int width, int height)
{
    if (g_type_tabs)
        MoveWindow(g_type_tabs, 10, 42, 250, 32, TRUE);

    if (g_dir_tabs)
        MoveWindow(g_dir_tabs, 270, 42, 250, 32, TRUE);

    for (auto& type : g_lists)
    {
        for (HWND list : type)
        {
            if (list)
            {
                MoveWindow(
                    list,
                    10, 78,
                    std::max(100, width - 20),
                    std::max(100, height - 88),
                    TRUE
                );
            }
        }
    }
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            create_controls(hwnd);
            SetTimer(hwnd, 1, 50, nullptr);
            return 0;
        }

        case WM_TIMER:
        {
            if (wParam == 1)
                flush_pending();
            return 0;
        }

        case WM_INTERCEPT_EVENT:
        {
            flush_pending();
            return 0;
        }

        case WM_NOTIFY:
        {
            const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);

            if (header && header->code == TCN_SELCHANGE)
            {
                if (header->idFrom == IDC_TYPE_TABS)
                {
                    g_type_tab = TabCtrl_GetCurSel(g_type_tabs);
                    show_active_list();
                }
                else if (header->idFrom == IDC_DIR_TABS)
                {
                    g_dir_tab = TabCtrl_GetCurSel(g_dir_tabs);
                    show_active_list();
                }
            }

            return 0;
        }

        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);

            if (id == IDC_CLEAR)
            {
                clear_all_lists();
                return 0;
            }

            if (id == IDC_AUTOSCROLL)
            {
                const LRESULT state = SendMessageA(g_autoscroll, BM_GETCHECK, 0, 0);
                g_auto_scroll = (state == BST_CHECKED);
                return 0;
            }

            if (id == IDC_FILTER && HIWORD(wParam) == EN_CHANGE)
            {
                set_filter_text();
                clear_all_lists();
                return 0;
            }

            return 0;
        }

        case WM_SIZE:
        {
            resize_controls(LOWORD(lParam), HIWORD(lParam));
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
        {
            KillTimer(hwnd, 1);

            g_hwnd = nullptr;
            g_filter = nullptr;
            g_autoscroll = nullptr;
            g_type_tabs = nullptr;
            g_dir_tabs = nullptr;

            for (auto& type : g_lists)
                for (HWND& list : type)
                    list = nullptr;

            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void gui_thread_proc()
{
    HINSTANCE instance = GetModuleHandleA(nullptr);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = "INTERCEPT_GUI";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExA(&wc))
    {
        const DWORD error = GetLastError();

        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            log::error(
                "Failed to register INTERCEPT GUI window class. error=" +
                std::to_string(error)
            );

            {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_ready = true;
            }
            g_state_cv.notify_all();
            return;
        }
    }

    HWND hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "INTERCEPT - Packet Monitor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1200,
        750,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!hwnd)
    {
        const DWORD error = GetLastError();

        log::error(
            "Failed to create INTERCEPT GUI. error=" +
            std::to_string(error)
        );

        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_ready = true;
        }

        g_state_cv.notify_all();
        return;
    }

    g_hwnd = hwnd;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    log::info("INTERCEPT GUI started.");

    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_ready = true;
    }
    g_state_cv.notify_all();

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_pending.empty())
            PostMessageA(g_hwnd, WM_INTERCEPT_EVENT, 0, 0);
    }

    MSG msg{};

    while (GetMessageA(&msg, nullptr, 0, 0) > 0)
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

    if (!g_running.compare_exchange_strong(expected, true))
        return;

    g_ready = false;

    g_gui_thread = std::thread(gui_thread_proc);

    std::unique_lock<std::mutex> lock(g_state_mutex);

    const bool ready = g_state_cv.wait_for(
        lock,
        std::chrono::seconds(5),
        [] { return g_ready.load(); }
    );

    if (!ready || !g_hwnd)
    {
        log::error("INTERCEPT GUI failed to initialize.");
        g_running = false;

        if (g_gui_thread.joinable())
            g_gui_thread.join();
    }
}

void stop()
{
    if (!g_running.load())
        return;

    HWND hwnd = g_hwnd;

    if (hwnd)
        PostMessageA(hwnd, WM_CLOSE, 0, 0);

    if (g_gui_thread.joinable())
        g_gui_thread.join();

    g_running = false;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending.clear();
    }

    log::info("INTERCEPT GUI stopped.");
}

void push_event(const std::string& event)
{
    if (!g_running.load())
        return;

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (g_pending.size() >= MAX_QUEUE_SIZE)
            g_pending.pop_front();

        g_pending.push_back(event);
    }

    HWND hwnd = g_hwnd;

    if (hwnd)
        PostMessageA(hwnd, WM_INTERCEPT_EVENT, 0, 0);
}

} // namespace intercept::gui
