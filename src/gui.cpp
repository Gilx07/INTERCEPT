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
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gui.hpp"
#include "logger.hpp"
#include "monitor.hpp"

#pragma comment(lib, "comctl32.lib")

namespace intercept::gui {
namespace {

constexpr int IDC_FILTER = 1001;
constexpr int IDC_AUTOSCROLL = 1002;
constexpr int IDC_CLEAR = 1003;
constexpr int IDC_TYPE_TABS = 1004;
constexpr int IDC_DIR_TABS = 1005;
constexpr int IDC_LIST_BASE = 1100;
constexpr int IDC_DETAIL_HEX = 2001;
constexpr int IDC_DETAIL_COPY = 2002;
constexpr int IDC_DETAIL_SEND = 2003;
constexpr int IDC_DETAIL_CLOSE = 2004;
constexpr UINT WM_INTERCEPT_EVENT = WM_APP + 1;
constexpr size_t MAX_QUEUE_SIZE = 10000;
constexpr size_t MAX_EVENTS_PER_TICK = 250;
constexpr size_t MAX_ROWS_PER_LIST = 10000;

enum class EventType { Packet, Rpc };
enum class Direction { Incoming, Outgoing };

struct EventRecord {
    EventType type{};
    Direction direction{};
    int id = -1;
    std::string size;
    std::string name;
    std::string data;
    std::string hex;
};

struct DetailState {
    EventRecord event;
    HWND hex = nullptr;
};

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
std::deque<EventRecord> g_records[2][2];

LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK detail_proc(HWND, UINT, WPARAM, LPARAM);

int type_index(EventType type) { return type == EventType::Packet ? 0 : 1; }
int dir_index(Direction dir) { return dir == Direction::Incoming ? 0 : 1; }
HWND current_list() { return g_lists[g_type_tab][g_dir_tab]; }

bool matches_filter(const std::string& event)
{
    if (g_filter_text.empty()) return true;
    std::string lower = event;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find(g_filter_text) != std::string::npos;
}

void set_filter_text()
{
    if (!g_filter) return;
    char buffer[1024]{};
    GetWindowTextA(g_filter, buffer, static_cast<int>(sizeof(buffer)));
    g_filter_text = buffer;
    std::transform(g_filter_text.begin(), g_filter_text.end(), g_filter_text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
}

void show_active_list()
{
    for (int type = 0; type < 2; ++type)
        for (int dir = 0; dir < 2; ++dir)
            if (g_lists[type][dir])
                ShowWindow(g_lists[type][dir], type == g_type_tab && dir == g_dir_tab ? SW_SHOW : SW_HIDE);
}

void add_column(HWND list, int index, int width, const char* text)
{
    LVCOLUMNA column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.cx = width;
    column.pszText = const_cast<char*>(text);
    column.iSubItem = index;
    SendMessageA(list, LVM_INSERTCOLUMNA, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&column));
}

void add_columns(HWND list)
{
    add_column(list, 0, 80, "ID");
    add_column(list, 1, 80, "Size");
    add_column(list, 2, 260, "Name");
    add_column(list, 3, 900, "Data / HEX");
}

void set_item_text(HWND list, int row, int column, const char* text)
{
    LVITEMA item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = column;
    item.pszText = const_cast<char*>(text);
    SendMessageA(list, LVM_SETITEMTEXTA, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&item));
}

void insert_row(const EventRecord& event)
{
    const int ti = type_index(event.type);
    const int di = dir_index(event.direction);
    HWND list = g_lists[ti][di];
    if (!list) return;

    auto& records = g_records[ti][di];
    records.push_back(event);

    if (records.size() > MAX_ROWS_PER_LIST)
    {
        records.pop_front();
        const int count = static_cast<int>(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0));
        if (count > 0) SendMessageA(list, LVM_DELETEITEM, 0, 0);
    }

    const int row_index = static_cast<int>(records.size()) - 1;
    const std::string id = std::to_string(event.id);

    LVITEMA item{};
    item.mask = LVIF_TEXT;
    item.iItem = row_index;
    item.pszText = const_cast<char*>(id.c_str());

    const LRESULT row = SendMessageA(list, LVM_INSERTITEMA, static_cast<WPARAM>(row_index), reinterpret_cast<LPARAM>(&item));
    if (row < 0) return;

    set_item_text(list, row_index, 1, event.size.c_str());
    set_item_text(list, row_index, 2, event.name.c_str());

    std::string display = event.data;
    if (!event.hex.empty())
    {
        if (!display.empty()) display += " | ";
        display += "HEX: ";
        display += event.hex;
    }
    set_item_text(list, row_index, 3, display.c_str());

    if (g_auto_scroll && list == current_list())
        SendMessageA(list, LVM_ENSUREVISIBLE, static_cast<WPARAM>(row_index), TRUE);
}

EventRecord parse_event(const std::string& message, bool* accepted = nullptr)
{
    EventRecord event;
    if (accepted) *accepted = false;

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
    if (start <= message.size()) parts[4] = message.substr(start);

    if (parts[0] == "PACKET") event.type = EventType::Packet;
    else if (parts[0] == "RPC") event.type = EventType::Rpc;
    else return event;

    if (parts[3] == "IN") event.direction = Direction::Incoming;
    else if (parts[3] == "OUT") event.direction = Direction::Outgoing;
    else return event;

    if (!matches_filter(message)) return event;

    try { event.id = std::stoi(parts[1]); } catch (...) { event.id = -1; }
    event.size = parts[2];

    std::string payload = parts[4];
    const size_t detail_pos = payload.find(" | ");
    if (detail_pos == std::string::npos)
    {
        event.name = payload;
        if (accepted) *accepted = true;
        return event;
    }

    event.name = payload.substr(0, detail_pos);
    std::string detail = payload.substr(detail_pos + 3);
    const std::string hex_tag = "HEX: ";
    const size_t hex_pos = detail.rfind(hex_tag);
    if (hex_pos != std::string::npos)
    {
        event.hex = detail.substr(hex_pos + hex_tag.size());
        if (hex_pos > 0 && detail[hex_pos - 1] == '|') detail.erase(hex_pos - 1);
        else detail.erase(hex_pos);
        while (!detail.empty() && (detail.back() == ' ' || detail.back() == '|')) detail.pop_back();
    }
    event.data = detail;
    if (accepted) *accepted = true;
    return event;
}

void clear_all_lists()
{
    for (int type = 0; type < 2; ++type)
        for (int dir = 0; dir < 2; ++dir)
        {
            g_records[type][dir].clear();
            if (g_lists[type][dir]) SendMessageA(g_lists[type][dir], LVM_DELETEALLITEMS, 0, 0);
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

    for (const auto& raw : events)
    {
        bool accepted = false;
        EventRecord event = parse_event(raw, &accepted);
        if (accepted) insert_row(event);
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_pending.empty() && g_hwnd) PostMessageA(g_hwnd, WM_INTERCEPT_EVENT, 0, 0);
}

void set_clipboard_text(const std::string& text)
{
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!memory) { CloseClipboard(); return; }
    void* ptr = GlobalLock(memory);
    if (!ptr) { GlobalFree(memory); CloseClipboard(); return; }
    std::memcpy(ptr, text.c_str(), text.size() + 1);
    GlobalUnlock(memory);
    SetClipboardData(CF_TEXT, memory);
    CloseClipboard();
}

void send_detail(HWND hwnd, DetailState* state)
{
    char buffer[65536]{};
    GetWindowTextA(state->hex, buffer, static_cast<int>(sizeof(buffer)));
    const std::string hex = buffer;
    bool ok = false;
    if (state->event.type == EventType::Packet)
        ok = monitor::send_packet_hex(hex);
    else
        ok = monitor::send_rpc_hex(state->event.id, hex);

    MessageBoxA(hwnd, ok ? "Send successful." : "Send failed. Check HEX and packet/RPC state.",
        "INTERCEPT", ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
}

void create_detail_controls(HWND hwnd, DetailState* state)
{
    const char* type = state->event.type == EventType::Packet ? "PACKET" : "RPC";
    const char* direction = state->event.direction == Direction::Incoming ? "IN" : "OUT";

    auto static_text = [&](const char* text, int x, int y, int w, int h) {
        HWND control = CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE,
            x, y, w, h, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
        SendMessageA(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    };

    static_text((std::string("Type: ") + type).c_str(), 15, 15, 180, 22);
    static_text((std::string("Direction: ") + direction).c_str(), 210, 15, 180, 22);
    static_text((std::string("ID: ") + std::to_string(state->event.id)).c_str(), 405, 15, 120, 22);
    static_text((std::string("Name: ") + state->event.name).c_str(), 15, 42, 600, 22);

    HWND label = CreateWindowExA(0, "STATIC", "HEX (editable):", WS_CHILD | WS_VISIBLE,
        15, 72, 150, 22, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    SendMessageA(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    state->hex = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", state->event.hex.c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        15, 98, 680, 300, hwnd, reinterpret_cast<HMENU>(IDC_DETAIL_HEX), GetModuleHandleA(nullptr), nullptr);
    SendMessageA(state->hex, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SendMessageA(state->hex, EM_SETLIMITTEXT, 65535, 0);

    HWND copy = CreateWindowExA(0, "BUTTON", "Copy HEX", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        15, 415, 110, 30, hwnd, reinterpret_cast<HMENU>(IDC_DETAIL_COPY), GetModuleHandleA(nullptr), nullptr);
    HWND send = CreateWindowExA(0, "BUTTON", "Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        135, 415, 100, 30, hwnd, reinterpret_cast<HMENU>(IDC_DETAIL_SEND), GetModuleHandleA(nullptr), nullptr);
    HWND close = CreateWindowExA(0, "BUTTON", "Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        595, 415, 100, 30, hwnd, reinterpret_cast<HMENU>(IDC_DETAIL_CLOSE), GetModuleHandleA(nullptr), nullptr);
    SendMessageA(copy, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SendMessageA(send, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SendMessageA(close, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

LRESULT CALLBACK detail_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<DetailState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg)
    {
        case WM_CREATE:
        {
            auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
            state = static_cast<DetailState*>(cs->lpCreateParams);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            create_detail_controls(hwnd, state);
            return 0;
        }
        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);
            if (!state) return 0;
            if (id == IDC_DETAIL_COPY)
            {
                char buffer[65536]{};
                GetWindowTextA(state->hex, buffer, static_cast<int>(sizeof(buffer)));
                set_clipboard_text(buffer);
                return 0;
            }
            if (id == IDC_DETAIL_SEND)
            {
                send_detail(hwnd, state);
                return 0;
            }
            if (id == IDC_DETAIL_CLOSE)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            delete state;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void open_detail(const EventRecord& event)
{
    static bool registered = false;
    HINSTANCE instance = GetModuleHandleA(nullptr);
    const char* class_name = "INTERCEPT_DETAIL";
    if (!registered)
    {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = detail_proc;
        wc.hInstance = instance;
        wc.lpszClassName = class_name;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExA(&wc);
        registered = true;
    }

    auto* state = new DetailState(event);
    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, class_name,
        "INTERCEPT - Packet Detail",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 730, 490,
        g_hwnd, nullptr, instance, state);
    if (!hwnd) { delete state; return; }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
}

void create_tabs(HWND hwnd)
{
    g_type_tabs = CreateWindowExA(0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        10, 42, 250, 32, hwnd, reinterpret_cast<HMENU>(IDC_TYPE_TABS), GetModuleHandleA(nullptr), nullptr);
    SendMessageA(g_type_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    TCITEMA item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<char*>("Packet"); TabCtrl_InsertItem(g_type_tabs, 0, &item);
    item.pszText = const_cast<char*>("RPC"); TabCtrl_InsertItem(g_type_tabs, 1, &item);

    g_dir_tabs = CreateWindowExA(0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        270, 42, 250, 32, hwnd, reinterpret_cast<HMENU>(IDC_DIR_TABS), GetModuleHandleA(nullptr), nullptr);
    SendMessageA(g_dir_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    item.pszText = const_cast<char*>("IN"); TabCtrl_InsertItem(g_dir_tabs, 0, &item);
    item.pszText = const_cast<char*>("OUT"); TabCtrl_InsertItem(g_dir_tabs, 1, &item);
}

HWND create_list(HWND hwnd, int type, int direction)
{
    const int id = IDC_LIST_BASE + type * 2 + direction;
    HWND list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        10, 78, 1000, 570, hwnd, reinterpret_cast<HMENU>(id), GetModuleHandleA(nullptr), nullptr);
    if (!list) return nullptr;
    SendMessageA(list, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SendMessageA(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    add_columns(list);
    return list;
}

void create_controls(HWND hwnd)
{
    g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    g_filter = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        10, 10, 300, 24, hwnd, reinterpret_cast<HMENU>(IDC_FILTER), GetModuleHandleA(nullptr), nullptr);
    SendMessageA(g_filter, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    HWND label = CreateWindowExA(0, "STATIC", "Filter:", WS_CHILD | WS_VISIBLE,
        320, 13, 50, 20, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
    SendMessageA(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    g_autoscroll = CreateWindowExA(0, "BUTTON", "Auto Scroll", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        390, 10, 110, 24, hwnd, reinterpret_cast<HMENU>(IDC_AUTOSCROLL), GetModuleHandleA(nullptr), nullptr);
    SendMessageA(g_autoscroll, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SendMessageA(g_autoscroll, BM_SETCHECK, BST_CHECKED, 0);

    HWND clear = CreateWindowExA(0, "BUTTON", "Clear", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        510, 10, 80, 24, hwnd, reinterpret_cast<HMENU>(IDC_CLEAR), GetModuleHandleA(nullptr), nullptr);
    SendMessageA(clear, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    create_tabs(hwnd);
    for (int type = 0; type < 2; ++type)
        for (int direction = 0; direction < 2; ++direction)
            g_lists[type][direction] = create_list(hwnd, type, direction);
    show_active_list();
}

void resize_controls(int width, int height)
{
    if (g_type_tabs) MoveWindow(g_type_tabs, 10, 42, 250, 32, TRUE);
    if (g_dir_tabs) MoveWindow(g_dir_tabs, 270, 42, 250, 32, TRUE);
    for (auto& type : g_lists)
        for (HWND list : type)
            if (list) MoveWindow(list, 10, 78, std::max(100, width - 20), std::max(100, height - 88), TRUE);
}

LRESULT custom_tab_draw(const NMHDR* header)
{
    const auto* draw = reinterpret_cast<const NMCUSTOMDRAW*>(header);
    if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;

    COLORREF color;
    if (header->idFrom == IDC_TYPE_TABS)
        color = draw->dwItemSpec == 0 ? RGB(70, 130, 220) : RGB(135, 85, 190);
    else
        color = draw->dwItemSpec == 0 ? RGB(55, 160, 95) : RGB(210, 75, 75);

    HDC dc = draw->hdc;
    RECT rect = draw->rc;
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);

    char text[128]{};
    TCITEMA item{};
    item.mask = TCIF_TEXT;
    item.pszText = text;
    item.cchTextMax = static_cast<int>(sizeof(text));
    TabCtrl_GetItem(reinterpret_cast<HWND>(header->hwndFrom), static_cast<int>(draw->dwItemSpec), &item);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return CDRF_SKIPDEFAULT;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
            create_controls(hwnd);
            SetTimer(hwnd, 1, 50, nullptr);
            return 0;
        case WM_TIMER:
            if (wParam == 1) flush_pending();
            return 0;
        case WM_INTERCEPT_EVENT:
            flush_pending();
            return 0;
        case WM_NOTIFY:
        {
            const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
            if (!header) return 0;
            if (header->code == NM_CUSTOMDRAW &&
                (header->idFrom == IDC_TYPE_TABS || header->idFrom == IDC_DIR_TABS))
                return custom_tab_draw(header);
            if (header->code == TCN_SELCHANGE)
            {
                if (header->idFrom == IDC_TYPE_TABS) g_type_tab = TabCtrl_GetCurSel(g_type_tabs);
                else if (header->idFrom == IDC_DIR_TABS) g_dir_tab = TabCtrl_GetCurSel(g_dir_tabs);
                show_active_list();
                return 0;
            }
            if (header->code == NM_DBLCLK)
            {
                const int list_id = static_cast<int>(header->idFrom);
                if (list_id >= IDC_LIST_BASE && list_id < IDC_LIST_BASE + 4)
                {
                    const auto* info = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
                    if (info->iItem >= 0)
                    {
                        const int type = (list_id - IDC_LIST_BASE) / 2;
                        const int dir = (list_id - IDC_LIST_BASE) % 2;
                        if (static_cast<size_t>(info->iItem) < g_records[type][dir].size())
                            open_detail(g_records[type][dir][info->iItem]);
                    }
                }
                return 0;
            }
            return 0;
        }
        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);
            if (id == IDC_CLEAR) { clear_all_lists(); return 0; }
            if (id == IDC_AUTOSCROLL)
            {
                g_auto_scroll = SendMessageA(g_autoscroll, BM_GETCHECK, 0, 0) == BST_CHECKED;
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
            resize_controls(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            g_hwnd = nullptr;
            g_filter = nullptr;
            g_autoscroll = nullptr;
            g_type_tabs = nullptr;
            g_dir_tabs = nullptr;
            for (auto& type : g_lists)
                for (HWND& list : type) list = nullptr;
            PostQuitMessage(0);
            return 0;
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

    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        const DWORD error = GetLastError();
        log::error("Failed to register INTERCEPT GUI window class. error=" + std::to_string(error));
        g_ready = true;
        g_state_cv.notify_all();
        return;
    }

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "INTERCEPT - Packet Monitor",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 750,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd)
    {
        const DWORD error = GetLastError();
        log::error("Failed to create INTERCEPT GUI. error=" + std::to_string(error));
        g_ready = true;
        g_state_cv.notify_all();
        return;
    }

    g_hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    log::info("INTERCEPT GUI started.");
    g_ready = true;
    g_state_cv.notify_all();

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_pending.empty()) PostMessageA(g_hwnd, WM_INTERCEPT_EVENT, 0, 0);
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
    if (!g_running.compare_exchange_strong(expected, true)) return;
    g_ready = false;
    g_gui_thread = std::thread(gui_thread_proc);
    std::unique_lock<std::mutex> lock(g_state_mutex);
    const bool ready = g_state_cv.wait_for(lock, std::chrono::seconds(5), [] { return g_ready.load(); });
    if (!ready || !g_hwnd)
    {
        log::error("INTERCEPT GUI failed to initialize.");
        g_running = false;
        if (g_gui_thread.joinable()) g_gui_thread.join();
    }
}

void stop()
{
    if (!g_running.load()) return;
    HWND hwnd = g_hwnd;
    if (hwnd) PostMessageA(hwnd, WM_CLOSE, 0, 0);
    if (g_gui_thread.joinable()) g_gui_thread.join();
    g_running = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending.clear();
    }
    log::info("INTERCEPT GUI stopped.");
}

void push_event(const std::string& event)
{
    if (!g_running.load()) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_pending.size() >= MAX_QUEUE_SIZE) g_pending.pop_front();
        g_pending.push_back(event);
    }
    HWND hwnd = g_hwnd;
    if (hwnd) PostMessageA(hwnd, WM_INTERCEPT_EVENT, 0, 0);
}

} // namespace intercept::gui
