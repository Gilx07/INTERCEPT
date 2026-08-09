#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gui.hpp"
#include "logger.hpp"
#include "monitor.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

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
constexpr size_t MAX_QUEUE_SIZE = 20000;
constexpr size_t MAX_EVENTS_PER_TICK = 100;
constexpr size_t MAX_ROWS_PER_LIST = 5000;

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
std::atomic_bool g_event_message_pending{false};

bool g_auto_scroll = true;
std::string g_filter_text;
int g_type_tab = 0;
int g_dir_tab = 0;
std::deque<EventRecord> g_records[2][2];

constexpr COLORREF C_BG = RGB(24, 24, 28);
constexpr COLORREF C_INPUT = RGB(39, 39, 46);
constexpr COLORREF C_TEXT = RGB(232, 232, 238);
constexpr COLORREF C_MUTED = RGB(165, 165, 178);
constexpr COLORREF C_IN = RGB(45, 160, 90);
constexpr COLORREF C_OUT = RGB(205, 65, 65);
constexpr COLORREF C_PACKET = RGB(55, 125, 220);
constexpr COLORREF C_RPC = RGB(135, 75, 190);

LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK detail_proc(HWND, UINT, WPARAM, LPARAM);

int type_index(EventType type) { return type == EventType::Packet ? 0 : 1; }
int dir_index(Direction dir) { return dir == Direction::Incoming ? 0 : 1; }
HWND current_list() { return g_lists[g_type_tab][g_dir_tab]; }

void apply_dark_theme(HWND hwnd)
{
    if (!hwnd) return;
    SetWindowTheme(hwnd, L"", L"");
    SetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND,
        reinterpret_cast<LONG_PTR>(CreateSolidBrush(C_BG)));
}

void apply_dark_control(HWND hwnd)
{
    if (hwnd) SetWindowTheme(hwnd, L"", L"");
}

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
    char buffer[512]{};
    GetWindowTextA(g_filter, buffer, sizeof(buffer));
    g_filter_text = buffer;
    std::transform(g_filter_text.begin(), g_filter_text.end(), g_filter_text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
}

std::string trim_copy(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

EventRecord parse_event(const std::string& event)
{
    EventRecord record;
    std::vector<std::string> parts;
    size_t start = 0;

    while (start <= event.size()) {
        const size_t pos = event.find('|', start);
        if (pos == std::string::npos) {
            parts.push_back(event.substr(start));
            break;
        }
        parts.push_back(event.substr(start, pos - start));
        start = pos + 1;
    }

    if (!parts.empty())
        record.type = parts[0] == "RPC" ? EventType::Rpc : EventType::Packet;
    if (parts.size() > 1)
        record.id = std::atoi(parts[1].c_str());
    if (parts.size() > 2)
        record.size = parts[2];
    if (parts.size() > 3)
        record.direction = parts[3] == "OUT" ? Direction::Outgoing : Direction::Incoming;
    if (parts.size() > 4)
        record.name = trim_copy(parts[4]);

    // monitor.cpp serializes the payload as:
    // TYPE|ID|SIZE|DIR|NAME | DETAILS | HEX: xx xx xx
    // Therefore HEX is normally in a later pipe-separated field, not p[5].
    std::string payload;
    for (size_t i = 5; i < parts.size(); ++i) {
        const std::string part = trim_copy(parts[i]);
        if (part.empty()) continue;
        if (!payload.empty()) payload += " | ";
        payload += part;
    }

    const std::string hex_marker = "HEX:";
    const size_t hex_pos = payload.find(hex_marker);
    if (hex_pos != std::string::npos) {
        record.data = trim_copy(payload.substr(0, hex_pos));
        record.hex = trim_copy(payload.substr(hex_pos + hex_marker.size()));
        if (!record.data.empty() && record.data.size() >= 1 &&
            record.data.back() == '|') {
            record.data.pop_back();
            record.data = trim_copy(record.data);
        }
    } else {
        record.data = payload;
    }

    return record;
}

void add_record_to_list(const EventRecord& record)
{
    const int type = type_index(record.type);
    const int dir = dir_index(record.direction);
    auto& records = g_records[type][dir];
    records.push_back(record);
    while (records.size() > MAX_ROWS_PER_LIST)
        records.pop_front();

    HWND list = g_lists[type][dir];
    if (!list || !matches_filter(record.name + " " + record.data)) return;

    while (ListView_GetItemCount(list) >= static_cast<int>(MAX_ROWS_PER_LIST))
        ListView_DeleteItem(list, 0);

    char id_buf[32]{};
    _snprintf_s(id_buf, _TRUNCATE, "%d", record.id);

    const int item = ListView_GetItemCount(list);
    LVITEMA lv{};
    lv.mask = LVIF_TEXT;
    lv.iItem = item;
    lv.iSubItem = 0;
    lv.pszText = id_buf;

    SendMessageA(list, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&lv));
    ListView_SetItemText(list, item, 1, const_cast<char*>(record.size.c_str()));
    ListView_SetItemText(list, item, 2, const_cast<char*>(record.name.c_str()));
    ListView_SetItemText(list, item, 3, const_cast<char*>(record.data.c_str()));
}

void flush_pending()
{
    std::deque<std::string> local;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const size_t count = std::min(MAX_EVENTS_PER_TICK, g_pending.size());
        for (size_t i = 0; i < count; ++i) {
            local.push_back(std::move(g_pending.front()));
            g_pending.pop_front();
        }
        g_event_message_pending = !g_pending.empty();
    }

    HWND list = current_list();
    if (list && !local.empty())
        SendMessageA(list, WM_SETREDRAW, FALSE, 0);

    for (const auto& event : local)
        add_record_to_list(parse_event(event));

    if (list && !local.empty()) {
        SendMessageA(list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list, nullptr, TRUE);
        if (g_auto_scroll && ListView_GetItemCount(list) > 0)
            ListView_EnsureVisible(list, ListView_GetItemCount(list) - 1, FALSE);
    }

    if (g_event_message_pending && g_hwnd)
        PostMessageA(g_hwnd, WM_INTERCEPT_EVENT, 0, 0);
}

void clear_all_lists()
{
    for (auto& by_type : g_records)
        for (auto& by_dir : by_type)
            by_dir.clear();

    for (auto& by_type : g_lists)
        for (HWND list : by_type)
            if (list) ListView_DeleteAllItems(list);
}

void show_active_list()
{
    for (int type = 0; type < 2; ++type)
        for (int dir = 0; dir < 2; ++dir)
            if (g_lists[type][dir])
                ShowWindow(g_lists[type][dir],
                    type == g_type_tab && dir == g_dir_tab ? SW_SHOW : SW_HIDE);
}

void configure_list(HWND list)
{
    ListView_SetExtendedListViewStyle(list,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    const char* columns[] = { "ID", "Size", "Name", "Data" };
    const int widths[] = { 70, 80, 220, 520 };

    for (int i = 0; i < 4; ++i) {
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<char*>(columns[i]);
        col.cx = widths[i];
        ListView_InsertColumn(list, i, &col);
    }
}

void set_font(HWND hwnd)
{
    if (hwnd && g_font)
        SendMessageA(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

void create_tabs(HWND hwnd)
{
    g_type_tabs = CreateWindowExA(0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | TCS_OWNERDRAWFIXED,
        12, 54, 180, 30, hwnd,
        reinterpret_cast<HMENU>(IDC_TYPE_TABS), GetModuleHandleA(nullptr), nullptr);

    g_dir_tabs = CreateWindowExA(0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | TCS_OWNERDRAWFIXED,
        202, 54, 180, 30, hwnd,
        reinterpret_cast<HMENU>(IDC_DIR_TABS), GetModuleHandleA(nullptr), nullptr);

    TCITEMA item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<char*>("PACKET");
    TabCtrl_InsertItem(g_type_tabs, 0, &item);
    item.pszText = const_cast<char*>("RPC");
    TabCtrl_InsertItem(g_type_tabs, 1, &item);
    item.pszText = const_cast<char*>("IN");
    TabCtrl_InsertItem(g_dir_tabs, 0, &item);
    item.pszText = const_cast<char*>("OUT");
    TabCtrl_InsertItem(g_dir_tabs, 1, &item);
}

void create_lists(HWND hwnd)
{
    for (int type = 0; type < 2; ++type) {
        for (int dir = 0; dir < 2; ++dir) {
            g_lists[type][dir] = CreateWindowExA(
                WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | WS_VSCROLL,
                12, 90, 980, 520, hwnd,
                reinterpret_cast<HMENU>(IDC_LIST_BASE + type * 2 + dir),
                GetModuleHandleA(nullptr), nullptr);
            configure_list(g_lists[type][dir]);
            apply_dark_control(g_lists[type][dir]);
            set_font(g_lists[type][dir]);
        }
    }
}

void draw_tab(const DRAWITEMSTRUCT* dis)
{
    if (!dis || !dis->hwndItem) return;

    HWND tabs = dis->hwndItem;
    const bool type_tabs = tabs == g_type_tabs;
    const int selected = TabCtrl_GetCurSel(tabs);
    const int index = static_cast<int>(dis->itemID);

    COLORREF bg = type_tabs
        ? (index == 0 ? C_PACKET : C_RPC)
        : (index == 0 ? C_IN : C_OUT);
    COLORREF text = C_TEXT;

    if (index != selected) {
        bg = RGB((GetRValue(bg) + 30) / 2,
                  (GetGValue(bg) + 30) / 2,
                  (GetBValue(bg) + 30) / 2);
        text = C_MUTED;
    }

    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, brush);
    DeleteObject(brush);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text);
    const char* label = index == 0
        ? (type_tabs ? "PACKET" : "IN")
        : (type_tabs ? "RPC" : "OUT");
    RECT rc = dis->rcItem;
    DrawTextA(dis->hDC, label, -1, &rc,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void open_detail(const EventRecord& event)
{
    auto* state = new DetailState{ event };

    WNDCLASSA wc{};
    wc.lpfnWndProc = detail_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(C_BG);
    wc.lpszClassName = "INTERCEPT_PACKET_DETAIL";

    static std::once_flag once;
    std::call_once(once, [&]() { RegisterClassA(&wc); });

    HWND detail = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        event.type == EventType::Packet ? "Packet Detail" : "RPC Detail",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 560,
        g_hwnd, nullptr, GetModuleHandleA(nullptr), state);

    if (!detail) delete state;
}

void set_clipboard_text(HWND owner, const std::string& text)
{
    if (!OpenClipboard(owner)) return;

    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (mem) {
        void* ptr = GlobalLock(mem);
        if (ptr) {
            memcpy(ptr, text.c_str(), text.size() + 1);
            GlobalUnlock(mem);
            SetClipboardData(CF_TEXT, mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

std::string get_edit_text(HWND edit)
{
    if (!edit) return {};
    const int len = GetWindowTextLengthA(edit);
    if (len <= 0) return {};
    std::string text(static_cast<size_t>(len), '\0');
    GetWindowTextA(edit, text.data(), len + 1);
    return text;
}

LRESULT CALLBACK detail_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<DetailState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        state = reinterpret_cast<DetailState*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (msg) {
    case WM_CREATE:
    {
        apply_dark_theme(hwnd);
        const EventRecord& e = state->event;

        std::string title =
            "ID: " + std::to_string(e.id) +
            "    Size: " + e.size +
            "    Name: " + e.name;

        HWND title_hwnd = CreateWindowExA(
            0, "STATIC", title.c_str(), WS_CHILD | WS_VISIBLE,
            12, 12, 680, 24, hwnd, nullptr,
            GetModuleHandleA(nullptr), nullptr);
        set_font(title_hwnd);

        state->hex = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", e.hex.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            12, 44, 680, 400, hwnd,
            reinterpret_cast<HMENU>(IDC_DETAIL_HEX),
            GetModuleHandleA(nullptr), nullptr);
        apply_dark_control(state->hex);
        set_font(state->hex);

        HWND copy = CreateWindowExA(
            0, "BUTTON", "Copy HEX", WS_CHILD | WS_VISIBLE,
            12, 458, 110, 32, hwnd,
            reinterpret_cast<HMENU>(IDC_DETAIL_COPY),
            GetModuleHandleA(nullptr), nullptr);
        HWND send = CreateWindowExA(
            0, "BUTTON", "Send", WS_CHILD | WS_VISIBLE,
            132, 458, 110, 32, hwnd,
            reinterpret_cast<HMENU>(IDC_DETAIL_SEND),
            GetModuleHandleA(nullptr), nullptr);
        HWND close = CreateWindowExA(
            0, "BUTTON", "Close", WS_CHILD | WS_VISIBLE,
            582, 458, 110, 32, hwnd,
            reinterpret_cast<HMENU>(IDC_DETAIL_CLOSE),
            GetModuleHandleA(nullptr), nullptr);
        set_font(copy);
        set_font(send);
        set_font(close);
        return 0;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_DETAIL_CLOSE) {
            DestroyWindow(hwnd);
            return 0;
        }

        if (LOWORD(wParam) == IDC_DETAIL_COPY) {
            set_clipboard_text(hwnd, get_edit_text(state->hex));
            return 0;
        }

        if (LOWORD(wParam) == IDC_DETAIL_SEND) {
            const std::string hex = get_edit_text(state->hex);
            if (hex.empty()) {
                MessageBoxA(hwnd,
                    "HEX field is empty. Enter packet bytes first.",
                    "INTERCEPT", MB_OK | MB_ICONWARNING);
                return 0;
            }

            bool ok = false;
            if (state->event.type == EventType::Packet)
                ok = intercept::monitor::send_packet_hex(hex);
            else
                ok = intercept::monitor::send_rpc_hex(state->event.id, hex);

            MessageBoxA(hwnd,
                ok ? "Packet/RPC sent successfully." :
                     "Send failed. Check the HEX data and RakHook state.",
                "INTERCEPT",
                ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_INPUT);
        static HBRUSH brush = CreateSolidBrush(C_INPUT);
        return reinterpret_cast<LRESULT>(brush);
    }

    case WM_DESTROY:
        delete state;
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        apply_dark_theme(hwnd);
        g_filter = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            12, 12, 500, 30, hwnd,
            reinterpret_cast<HMENU>(IDC_FILTER),
            GetModuleHandleA(nullptr), nullptr);
        g_autoscroll = CreateWindowExA(
            0, "BUTTON", "Auto Scroll",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            520, 12, 120, 30, hwnd,
            reinterpret_cast<HMENU>(IDC_AUTOSCROLL),
            GetModuleHandleA(nullptr), nullptr);
        SendMessageA(g_autoscroll, BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowExA(
            0, "BUTTON", "Clear", WS_CHILD | WS_VISIBLE,
            650, 12, 90, 30, hwnd,
            reinterpret_cast<HMENU>(IDC_CLEAR),
            GetModuleHandleA(nullptr), nullptr);
        create_tabs(hwnd);
        create_lists(hwnd);
        apply_dark_control(g_filter);
        apply_dark_control(g_autoscroll);
        apply_dark_control(GetDlgItem(hwnd, IDC_CLEAR));
        set_font(g_filter);
        set_font(g_autoscroll);
        set_font(GetDlgItem(hwnd, IDC_CLEAR));
        show_active_list();
        return 0;

    case WM_SIZE:
    {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        if (g_filter)
            MoveWindow(g_filter, 12, 12, width - 260, 30, TRUE);
        if (g_autoscroll)
            MoveWindow(g_autoscroll, width - 240, 12, 120, 30, TRUE);
        HWND clear = GetDlgItem(hwnd, IDC_CLEAR);
        if (clear)
            MoveWindow(clear, width - 110, 12, 90, 30, TRUE);
        if (g_type_tabs)
            MoveWindow(g_type_tabs, 12, 54, 180, 30, TRUE);
        if (g_dir_tabs)
            MoveWindow(g_dir_tabs, 202, 54, 180, 30, TRUE);
        for (auto& by_type : g_lists)
            for (HWND list : by_type)
                if (list)
                    MoveWindow(list, 12, 90, width - 24, height - 102, TRUE);
        return 0;
    }

    case WM_DRAWITEM:
        draw_tab(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;

    case WM_INTERCEPT_EVENT:
        flush_pending();
        return 0;

    case WM_NOTIFY:
    {
        const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
        if (!header) return 0;

        if (header->code == TCN_SELCHANGE) {
            if (header->idFrom == IDC_TYPE_TABS)
                g_type_tab = TabCtrl_GetCurSel(g_type_tabs);
            else if (header->idFrom == IDC_DIR_TABS)
                g_dir_tab = TabCtrl_GetCurSel(g_dir_tabs);
            show_active_list();
            InvalidateRect(header->hwndFrom, nullptr, TRUE);
            return 0;
        }

        if (header->code == NM_DBLCLK) {
            const int list_id = static_cast<int>(header->idFrom);
            if (list_id >= IDC_LIST_BASE && list_id < IDC_LIST_BASE + 4) {
                const auto* info = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
                if (info && info->iItem >= 0) {
                    const int type = (list_id - IDC_LIST_BASE) / 2;
                    const int dir = (list_id - IDC_LIST_BASE) % 2;
                    const size_t index = static_cast<size_t>(info->iItem);
                    if (index < g_records[type][dir].size())
                        open_detail(g_records[type][dir][index]);
                }
            }
            return 0;
        }

        if (header->code == NM_CUSTOMDRAW) {
            const auto* draw = reinterpret_cast<const NMLVCUSTOMDRAW*>(lParam);
            if (header->idFrom >= IDC_LIST_BASE &&
                header->idFrom < IDC_LIST_BASE + 4) {
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                    return CDRF_DODEFAULT;
            }
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CLEAR) {
            clear_all_lists();
            return 0;
        }
        if (LOWORD(wParam) == IDC_AUTOSCROLL) {
            g_auto_scroll = SendMessageA(
                g_autoscroll, BM_GETCHECK, 0, 0) == BST_CHECKED;
            return 0;
        }
        if (LOWORD(wParam) == IDC_FILTER) {
            if (HIWORD(wParam) == EN_CHANGE)
                set_filter_text();
            return 0;
        }
        return 0;

    case WM_DESTROY:
        g_running = false;
        g_ready = false;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void gui_thread_main()
{
    INITCOMMONCONTROLSEX icc{
        sizeof(icc),
        ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES
    };
    InitCommonControlsEx(&icc);

    WNDCLASSA wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(C_BG);
    wc.lpszClassName = "INTERCEPT_GUI";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, wc.lpszClassName, "INTERCEPT - Realtime Monitor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1040, 680,
        nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);

    if (!hwnd) {
        log::error("INTERCEPT GUI failed to initialize.");
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_ready = false;
        }
        g_state_cv.notify_all();
        return;
    }

    g_hwnd = hwnd;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_ready = true;
    }
    g_state_cv.notify_all();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_hwnd = nullptr;
}

} // namespace

void start()
{
    if (g_running.exchange(true)) return;
    g_gui_thread = std::thread(gui_thread_main);
    std::unique_lock<std::mutex> lock(g_state_mutex);
    g_state_cv.wait_for(lock, std::chrono::seconds(5),
        [] { return g_ready.load(); });
    if (!g_ready.load())
        log::error("INTERCEPT GUI failed to initialize.");
    else
        log::info("INTERCEPT GUI started.");
}

void stop()
{
    if (!g_running.exchange(false)) return;
    if (g_hwnd) PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
    if (g_gui_thread.joinable()) g_gui_thread.join();
}

void push_event(const std::string& event)
{
    if (!g_running.load()) return;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_pending.size() >= MAX_QUEUE_SIZE)
            g_pending.pop_front();
        g_pending.push_back(event);

        if (!g_event_message_pending.exchange(true) && g_hwnd)
            PostMessageA(g_hwnd, WM_INTERCEPT_EVENT, 0, 0);
    }
}

} // namespace intercept::gui
