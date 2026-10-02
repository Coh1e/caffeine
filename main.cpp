// main.cpp — Caffeine
//
// Tiny Windows tray utility that keeps the system awake while it runs.
// Launch → system stays awake (display may still sleep). The right-click
// menu offers a "阻止息屏" toggle that additionally keeps the display on,
// plus Exit. Double-clicking the tray icon quits (restoring normal sleep
// and screen-off). Single source file, pure Win32, no third-party deps.

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00   // Windows 10+ (NOTIFYICON_VERSION_4 etc.)

#include <windows.h>
#include <shellapi.h>
#include <optional>

#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

// --------------------------------------------------------------------------
// Constants
// --------------------------------------------------------------------------

constexpr UINT    WM_APP_TRAY   = WM_APP + 1;   // tray icon callback message
constexpr UINT    TRAY_ICON_ID  = 1;
constexpr wchar_t kWindowClass[] = L"CaffeineTrayWindowClass";
constexpr wchar_t kMutexName[]   = L"Local\\CaffeineTraySingleton";

// --------------------------------------------------------------------------
// Localization — pick Chinese or English by the system UI language.
// --------------------------------------------------------------------------
struct Strings {
    const wchar_t* tipAwake;     // tooltip: system kept awake, display may sleep
    const wchar_t* tipDisplay;   // tooltip: system + display kept awake
    const wchar_t* menuDisplay;  // menu item: keep the display on (checkable)
    const wchar_t* menuExit;     // menu item: quit
};

const Strings kZh = {
    L"Caffeine：防休眠中（屏幕仍会息屏）",
    L"Caffeine：防休眠 + 防息屏",
    L"阻止息屏",
    L"退出",
};

const Strings kEn = {
    L"Caffeine: awake (display may still sleep)",
    L"Caffeine: awake + display kept on",
    L"Keep display on",
    L"Exit",
};

// Chosen once on first use, from the user's preferred UI language.
const Strings& Loc() {
    static const Strings& s =
        (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE) ? kZh : kEn;
    return s;
}

// --------------------------------------------------------------------------
// Globals (kept few and explicit)
// --------------------------------------------------------------------------

HINSTANCE g_hInst             = nullptr;
UINT      g_taskbarCreatedMsg = 0;   // RegisterWindowMessageW(L"TaskbarCreated")

// --------------------------------------------------------------------------
// AwakeGuard — RAII wrapper around SetThreadExecutionState.
//
// While running, the system is always kept awake. The display is only kept
// awake when the "阻止息屏" toggle is on:
//   Apply(false) → ES_CONTINUOUS | ES_SYSTEM_REQUIRED
//   Apply(true)  → ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
//
// Destructor unconditionally calls ES_CONTINUOUS so any exit path
// (normal, exception, std::exit) leaves Windows back on its default
// power management.
// --------------------------------------------------------------------------
class AwakeGuard {
public:
    void Apply(bool blockDisplay) noexcept {
        EXECUTION_STATE flags = ES_CONTINUOUS | ES_SYSTEM_REQUIRED;
        if (blockDisplay) flags |= ES_DISPLAY_REQUIRED;
        SetThreadExecutionState(flags);
        blockDisplay_ = blockDisplay;
    }
    bool BlocksDisplay() const noexcept { return blockDisplay_; }
    ~AwakeGuard() { SetThreadExecutionState(ES_CONTINUOUS); }

private:
    bool blockDisplay_ = false;
};

// --------------------------------------------------------------------------
// TrayIcon — RAII wrapper around Shell_NotifyIconW.
//
// Show(): NIM_ADD on first call, NIM_MODIFY thereafter.
// ReAdd(): used after the shell broadcasts TaskbarCreated (Explorer
//          restart) to register the icon again.
// Destructor: NIM_DELETE if we ever successfully added.
// --------------------------------------------------------------------------
class TrayIcon {
public:
    TrayIcon(HWND owner, UINT callbackMsg, UINT id) noexcept {
        nid_.cbSize           = sizeof(nid_);
        nid_.hWnd             = owner;
        nid_.uID              = id;
        nid_.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        nid_.uCallbackMessage = callbackMsg;
        nid_.uVersion         = NOTIFYICON_VERSION_4;
    }

    ~TrayIcon() {
        if (added_) {
            Shell_NotifyIconW(NIM_DELETE, &nid_);
        }
    }

    TrayIcon(const TrayIcon&)            = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    void Show(HICON icon, const wchar_t* tip) noexcept {
        nid_.hIcon = icon;
        wcsncpy_s(nid_.szTip, _countof(nid_.szTip), tip, _TRUNCATE);
        if (!added_) {
            if (Shell_NotifyIconW(NIM_ADD, &nid_)) {
                added_ = true;
                Shell_NotifyIconW(NIM_SETVERSION, &nid_);
            }
        }
        // Re-send the icon + tooltip once the icon is present. The tooltip set
        // during NIM_ADD is applied under the default icon version; after
        // NIM_SETVERSION switches the shell to v4 the tooltip is not re-rendered
        // until it is re-sent with NIM_MODIFY.
        if (added_) {
            Shell_NotifyIconW(NIM_MODIFY, &nid_);
        }
    }

    void ReAdd(HICON icon, const wchar_t* tip) noexcept {
        added_ = false;       // force NIM_ADD path
        Show(icon, tip);
    }

private:
    NOTIFYICONDATAW nid_{};
    bool            added_ = false;
};

// Globals owning the awake state and tray icon.
// AwakeGuard's static destructor is also a safety net beyond wWinMain.
AwakeGuard              g_awake;
std::optional<TrayIcon> g_tray;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

// Load the coffee-cup icon at the system small-icon size. If the resource
// is missing for any reason (e.g. PE was stripped), fall back to a stock
// system icon so we never end up with a blank tray slot.
HICON LoadAppIcon() {
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    HICON h = static_cast<HICON>(LoadImageW(
        g_hInst, MAKEINTRESOURCEW(IDI_CAFFEINE),
        IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    if (!h) {
        h = LoadIconW(nullptr, IDI_INFORMATION);
    }
    return h;
}

void RefreshTray() {
    if (!g_tray) return;
    g_tray->Show(LoadAppIcon(),
                 g_awake.BlocksDisplay() ? Loc().tipDisplay : Loc().tipAwake);
}

void ToggleDisplayBlock() {
    g_awake.Apply(!g_awake.BlocksDisplay());
    RefreshTray();
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu,
        MF_STRING | (g_awake.BlocksDisplay() ? MF_CHECKED : MF_UNCHECKED),
        ID_TRAY_DISPLAY, Loc().menuDisplay);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, Loc().menuExit);

    // Required so the menu dismisses correctly when the user clicks
    // outside it. Classic shell-tray pitfall.
    SetForegroundWindow(hwnd);

    TrackPopupMenu(menu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(menu);
}

// --------------------------------------------------------------------------
// Window procedure
// --------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // TaskbarCreated is broadcast by Explorer when the taskbar is first
    // created OR when explorer.exe is restarted; re-add the tray icon.
    if (msg == g_taskbarCreatedMsg && g_taskbarCreatedMsg != 0) {
        if (g_tray) {
            g_tray->ReAdd(LoadAppIcon(),
                          g_awake.BlocksDisplay() ? Loc().tipDisplay : Loc().tipAwake);
        }
        return 0;
    }

    switch (msg) {
    case WM_APP_TRAY:
        // With NOTIFYICON_VERSION_4, LOWORD(lParam) carries the actual
        // mouse / keyboard event.
        switch (LOWORD(lParam)) {
        case WM_LBUTTONDBLCLK:
            // Double-click quits — teardown restores normal sleep/screen-off.
            DestroyWindow(hwnd);
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu(hwnd);
            return 0;
        // Single left-click intentionally does nothing.
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_DISPLAY:
            ToggleDisplayBlock();
            return 0;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

// --------------------------------------------------------------------------
// Entry point
// --------------------------------------------------------------------------
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    g_hInst = hInstance;

    // Single-instance guard: bail out silently if another caffeine.exe is
    // already running in this user session. Local\\ namespace ensures we
    // don't collide with other sessions / users.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        CloseHandle(mutex);
        return 1;
    }

    // Hidden top-level window — NOT HWND_MESSAGE. TaskbarCreated is a
    // broadcast that only reaches top-level windows; a message-only
    // window would never see it and the icon wouldn't recover after
    // explorer.exe restart.
    HWND hwnd = CreateWindowExW(
        0, kWindowClass, L"Caffeine",
        WS_OVERLAPPED, 0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        CloseHandle(mutex);
        return 1;
    }
    // Intentionally no ShowWindow() — invisible by design.

    g_tray.emplace(hwnd, WM_APP_TRAY, TRAY_ICON_ID);

    // Keep the system awake from launch (display sleep still allowed until
    // the user enables "阻止息屏").
    g_awake.Apply(false);
    RefreshTray();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Deterministic teardown:
    //   1. NIM_DELETE the tray icon       (TrayIcon dtor via reset())
    //   2. ES_CONTINUOUS                  (AwakeGuard dtor; release hold)
    g_tray.reset();

    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
