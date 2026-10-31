#include "Tray.h"

#include "I18n.h"
#include "resource.h"

#include "afm/platform/Win.h"

#include <shellapi.h>

#include <cwchar>
#include <iterator>

namespace afm::app::tray {

namespace {

constexpr UINT kCallbackMessage = WM_APP + 1;
constexpr UINT kIconId = 1;
constexpr UINT kMenuOpen = 1;
constexpr UINT kMenuExit = 2;

bool g_iconShown = false;
HICON g_icon = nullptr;

NOTIFYICONDATAW iconData(HWND hwnd) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = kIconId;
    return data;
}

void addIcon(HWND hwnd) {
    if (g_icon == nullptr) {
        g_icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                               LR_DEFAULTCOLOR));
    }
    NOTIFYICONDATAW data = iconData(hwnd);
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kCallbackMessage;
    data.hIcon = g_icon;
    std::wcsncpy(data.szTip, L"AudioFlow Matrix", std::size(data.szTip) - 1);
    g_iconShown = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void showMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, kMenuOpen, win::toWide(tr("Open AudioFlow Matrix")).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, win::toWide(tr("Exit")).c_str());
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);  // so the menu closes when the user clicks elsewhere
    const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0,
                                       hwnd, nullptr);
    DestroyMenu(menu);
    if (choice == kMenuOpen) restore(hwnd);
    if (choice == kMenuExit) PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

} // namespace

void hideToTray(HWND hwnd) {
    if (!g_iconShown) addIcon(hwnd);
    if (g_iconShown) ShowWindow(hwnd, SW_HIDE);  // without an icon the taskbar button stays
}

void restore(HWND hwnd) {
    removeIcon(hwnd);
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
}

void removeIcon(HWND hwnd) {
    if (!g_iconShown) return;
    NOTIFYICONDATAW data = iconData(hwnd);
    Shell_NotifyIconW(NIM_DELETE, &data);
    g_iconShown = false;
}

bool handleMessage(HWND hwnd, UINT msg, WPARAM, LPARAM lParam) {
    // Explorer restarted: its notification area forgot the icon.
    static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated && taskbarCreated != 0) {
        if (g_iconShown) {
            g_iconShown = false;
            addIcon(hwnd);
        }
        return true;
    }
    if (msg == kRestoreMessage) {
        restore(hwnd);
        return true;
    }
    if (msg != kCallbackMessage) return false;
    switch (LOWORD(lParam)) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: restore(hwnd); break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU: showMenu(hwnd); break;
    default: break;
    }
    return true;
}

} // namespace afm::app::tray
