// main.cpp — Amped (满血)
//
// Tiny Windows tray utility that toggles SetThreadExecutionState to keep
// the system (and display) from going to sleep due to idleness. Single
// source file, pure Win32, no third-party dependencies.

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
constexpr wchar_t kWindowClass[] = L"AmpedTrayWindowClass";
constexpr wchar_t kMutexName[]   = L"Local\\AmpedTraySingleton";
constexpr wchar_t kTipFull[]     = L"Amped: full power, keeping PC awake";
constexpr wchar_t kTipEmpty[]    = L"Amped: empty, normal sleep allowed";

// --------------------------------------------------------------------------
// Globals (kept few and explicit)
// --------------------------------------------------------------------------

HINSTANCE g_hInst             = nullptr;
UINT      g_taskbarCreatedMsg = 0;   // RegisterWindowMessageW(L"TaskbarCreated")

// --------------------------------------------------------------------------
// AwakeGuard — RAII wrapper around SetThreadExecutionState.
//
// SetActive(true)  → ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
// SetActive(false) → ES_CONTINUOUS  (release, restore default policy)
//
// Destructor unconditionally calls ES_CONTINUOUS so any exit path
// (normal, exception, std::exit) leaves Windows back on its default
// power management.
// --------------------------------------------------------------------------
class AwakeGuard {
public:
    void SetActive(bool on) noexcept {
        const EXECUTION_STATE flags = on
            ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
            : ES_CONTINUOUS;
        SetThreadExecutionState(flags);
        active_ = on;
    }
    bool IsActive() const noexcept { return active_; }
    ~AwakeGuard() { SetThreadExecutionState(ES_CONTINUOUS); }

private:
    bool active_ = false;
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
        nid_.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
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
        } else {
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

// Load IDI_FULL or IDI_EMPTY at the system small-icon size. If the
// resource is missing for any reason (e.g. PE was stripped), fall back
// to an unmistakably distinct stock system icon so we never end up with
// a blank tray slot.
HICON LoadStateIcon(bool full) {
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    HICON h = static_cast<HICON>(LoadImageW(
        g_hInst,
        MAKEINTRESOURCEW(full ? IDI_FULL : IDI_EMPTY),
        IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    if (!h) {
        h = LoadIconW(nullptr, full ? IDI_WARNING : IDI_INFORMATION);
    }
    return h;
}

void RefreshTray() {
    if (!g_tray) return;
    const bool full = g_awake.IsActive();
    g_tray->Show(LoadStateIcon(full), full ? kTipFull : kTipEmpty);
}

void ToggleAmped() {
    g_awake.SetActive(!g_awake.IsActive());
    RefreshTray();
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu,
        MF_STRING | (g_awake.IsActive() ? MF_CHECKED : MF_UNCHECKED),
        ID_TRAY_TOGGLE, L"Toggle Amped");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

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
            const bool full = g_awake.IsActive();
            g_tray->ReAdd(LoadStateIcon(full), full ? kTipFull : kTipEmpty);
        }
        return 0;
    }

    switch (msg) {
    case WM_APP_TRAY:
        // With NOTIFYICON_VERSION_4, LOWORD(lParam) carries the actual
        // mouse / keyboard event.
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
            ToggleAmped();
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu(hwnd);
            return 0;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_TOGGLE:
            ToggleAmped();
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

    // Single-instance guard: bail out silently if another amped.exe is
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
        0, kWindowClass, L"Amped",
        WS_OVERLAPPED, 0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        CloseHandle(mutex);
        return 1;
    }
    // Intentionally no ShowWindow() — invisible by design.

    g_tray.emplace(hwnd, WM_APP_TRAY, TRAY_ICON_ID);

    // Default to "empty": don't surprise the user by holding their
    // machine awake at launch. They opt in with a left click.
    g_awake.SetActive(false);
    RefreshTray();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Deterministic teardown:
    //   1. NIM_DELETE the tray icon       (TrayIcon dtor via reset())
    //   2. ES_CONTINUOUS                  (explicit, plus AwakeGuard dtor)
    g_tray.reset();
    g_awake.SetActive(false);

    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
