#include "gui.hpp"
#include "event_queue.hpp"

#include <Windows.h>
#include <CommCtrl.h>

#include <algorithm>
#include <atomic>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr UINT WM_REFRESH = WM_APP + 1;
constexpr UINT IDC_FILTER = 1001;
constexpr UINT IDC_HIDE_SYNC = 1002;
constexpr UINT IDC_CLEAR = 1003;
constexpr UINT IDC_PAUSE = 1004;
constexpr UINT IDC_TABS = 1005;
constexpr UINT IDC_LIST = 1006;
constexpr UINT IDC_INSPECTOR = 1007;
constexpr UINT TIMER_ID = 1;

HWND g_hwnd = nullptr;
HWND g_filter = nullptr;
HWND g_hide_sync = nullptr;
HWND g_pause = nullptr;
HWND g_list = nullptr;
HWND g_tabs = nullptr;
HWND g_inspector = nullptr;
HFONT g_font = nullptr;
std::atomic_bool g_running{false};
std::atomic_bool g_stop{false};
std::vector<intercept::events::Event> g_visible;
std::vector<intercept::events::Event> g_history;

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool is_sync(const intercept::events::Event& e)
{
    if (e.kind != intercept::events::Kind::Packet) return false;
    const std::string n = lower(e.name);
    return n.find("sync") != std::string::npos;
}

std::string get_filter()
{
    char buf[256]{};
    GetWindowTextA(g_filter, buf, sizeof(buf));
    return lower(buf);
}

int current_tab()
{
    return TabCtrl_GetCurSel(g_tabs);
}

bool matches(const intercept::events::Event& e)
{
    const int tab = current_tab();
    if (tab == 1 && e.kind != intercept::events::Kind::Packet) return false;
    if (tab == 2 && e.kind != intercept::events::Kind::Rpc) return false;
    if (Button_GetCheck(g_hide_sync) == BST_CHECKED && is_sync(e)) return false;

    const std::string f = get_filter();
    if (f.empty()) return true;

    std::ostringstream ss;
    ss << e.id << ' ' << e.name << ' ' << e.details << ' ' << e.hex;
    return lower(ss.str()).find(f) != std::string::npos;
}

const char* kind_name(const intercept::events::Event& e)
{
    return e.kind == intercept::events::Kind::Packet ? "PACKET" : "RPC";
}

const char* direction_name(const intercept::events::Event& e)
{
    return e.direction == intercept::events::Direction::Incoming ? "IN" : "OUT";
}

void set_text(HWND h, const std::string& s)
{
    SetWindowTextA(h, s.c_str());
}

void refresh_list()
{
    if (!g_list) return;
    const auto incoming = intercept::events::drain();
    g_history.insert(g_history.end(), incoming.begin(), incoming.end());
    constexpr std::size_t kHistory = 20000;
    if (g_history.size() > kHistory)
        g_history.erase(g_history.begin(), g_history.begin() + (g_history.size() - kHistory));

    ListView_DeleteAllItems(g_list);
    g_visible.clear();

    for (const auto& e : g_history) {
        if (!matches(e)) continue;
        g_visible.push_back(e);
        const int row = ListView_GetItemCount(g_list);
        LVITEMA item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        std::string seq = std::to_string(e.sequence);
        item.pszText = seq.data();
        ListView_InsertItemA(g_list, &item);

        std::string values[] = {
            direction_name(e), kind_name(e), std::to_string(e.id), e.name,
            std::to_string(e.bytes), e.details
        };
        for (int col = 0; col < 6; ++col)
            ListView_SetItemTextA(g_list, row, col + 1, values[col].data());
    }

    std::ostringstream status;
    status << "Events " << intercept::events::total_events()
           << " | Packets " << intercept::events::total_packets()
           << " | RPC " << intercept::events::total_rpcs()
           << " | Queue drops " << intercept::events::dropped_events()
           << " | Visible " << g_visible.size();
    set_text(g_inspector, status.str());
}

void inspect_selected()
{
    const int row = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (row < 0 || row >= static_cast<int>(g_visible.size())) return;
    const auto& e = g_visible[row];

    std::ostringstream ss;
    ss << "Sequence: " << e.sequence << "\r\n"
       << "Direction: " << direction_name(e) << "\r\n"
       << "Type: " << kind_name(e) << "\r\n"
       << "ID: " << e.id << "\r\n"
       << "Name: " << e.name << "\r\n"
       << "Bytes: " << e.bytes << "\r\n"
       << "Details: " << e.details << "\r\n\r\n"
       << "HEX:\r\n" << e.hex;
    set_text(g_inspector, ss.str());
}

void layout(HWND hwnd)
{
    RECT r{};
    GetClientRect(hwnd, &r);
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;

    SetWindowPos(g_filter, nullptr, 10, 10, 330, 24, SWP_NOZORDER);
    SetWindowPos(g_hide_sync, nullptr, 350, 10, 130, 24, SWP_NOZORDER);
    SetWindowPos(g_pause, nullptr, 490, 10, 90, 24, SWP_NOZORDER);
    SetWindowPos(g_tabs, nullptr, 10, 42, w - 20, h - 52, SWP_NOZORDER);

    RECT tr{};
    GetClientRect(g_tabs, &tr);
    TabCtrl_AdjustRect(g_tabs, FALSE, &tr);
    const int x = 10 + tr.left;
    const int y = 42 + tr.top;
    const int tw = (w - 20) - tr.left - (10 + (tr.right - tr.left));
    const int th = (h - 52) - tr.top - (10 + (tr.bottom - tr.top));
    const int inspector_h = 150;

    SetWindowPos(g_list, nullptr, x, y, tw, std::max(100, th - inspector_h - 8), SWP_NOZORDER);
    SetWindowPos(g_inspector, nullptr, x, y + std::max(100, th - inspector_h - 8) + 8, tw, inspector_h, SWP_NOZORDER);
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_font = CreateFontA(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        g_filter = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_FILTER), GetModuleHandleA(nullptr), nullptr);
        g_hide_sync = CreateWindowA("BUTTON", "Hide Sync", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_HIDE_SYNC), GetModuleHandleA(nullptr), nullptr);
        g_pause = CreateWindowA("BUTTON", "Pause", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_PAUSE), GetModuleHandleA(nullptr), nullptr);

        g_tabs = CreateWindowExA(0, WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_TABS), GetModuleHandleA(nullptr), nullptr);
        const char* names[] = {"All", "Packets", "RPC"};
        for (int i = 0; i < 3; ++i) {
            TCITEMA ti{};
            ti.mask = TCIF_TEXT;
            ti.pszText = const_cast<char*>(names[i]);
            TabCtrl_InsertItem(g_tabs, i, &ti);
        }

        g_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LIST), GetModuleHandleA(nullptr), nullptr);
        const char* cols[] = {"#", "Dir", "Type", "ID", "Name", "Bytes", "Details"};
        const int widths[] = {70, 55, 75, 55, 170, 70, 420};
        for (int i = 0; i < 7; ++i) {
            LVCOLUMNA c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.cx = widths[i];
            c.pszText = const_cast<char*>(cols[i]);
            ListView_InsertColumn(g_list, i, &c);
        }

        g_inspector = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_INSPECTOR), GetModuleHandleA(nullptr), nullptr);

        SendMessageA(g_filter, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageA(g_hide_sync, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageA(g_pause, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageA(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageA(g_inspector, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

        SetTimer(hwnd, TIMER_ID, 100, nullptr);
        layout(hwnd);
        return 0;
    }
    case WM_SIZE:
        layout(hwnd);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_ID && Button_GetCheck(g_pause) != BST_CHECKED)
            refresh_list();
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lp)->idFrom == IDC_LIST &&
            reinterpret_cast<LPNMHDR>(lp)->code == LVN_ITEMCHANGED)
            inspect_selected();
        else if (reinterpret_cast<LPNMHDR>(lp)->idFrom == IDC_TABS &&
                 reinterpret_cast<LPNMHDR>(lp)->code == TCN_SELCHANGE)
            refresh_list();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_FILTER || LOWORD(wp) == IDC_HIDE_SYNC)
            refresh_list();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        g_running = false;
        g_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

DWORD WINAPI gui_thread(LPVOID)
{
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSA wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = "INTERCEPT_GUI_V02";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(0, wc.lpszClassName, "INTERCEPT v0.2 - Realtime Monitor",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) {
        g_running = false;
        return 0;
    }

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    g_running = true;

    MSG msg{};
    while (!g_stop && GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

HANDLE g_thread = nullptr;

} // namespace

namespace intercept::gui {

void start()
{
    if (g_running || g_thread) return;
    g_stop = false;
    g_thread = CreateThread(nullptr, 0, gui_thread, nullptr, 0, nullptr);
}

void stop()
{
    g_stop = true;
    if (g_hwnd) PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
    if (g_thread) {
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

} // namespace intercept::gui
