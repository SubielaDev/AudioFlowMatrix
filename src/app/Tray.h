#pragma once

// "Minimize to the system tray": while minimized the window is hidden from the taskbar and an
// icon sits in the notification area. Click it to restore; right-click it for Open / Exit.

#include <windows.h>

namespace afm::app::tray {

// Posted by a second instance (see main.cpp) to bring this one back, also from the tray.
inline constexpr UINT kRestoreMessage = WM_APP + 2;

// Hides the (already minimized) window and shows the tray icon.
void hideToTray(HWND hwnd);
// Shows the window again, in front, and removes the tray icon.
void restore(HWND hwnd);
// Removes the tray icon if it is there (on exit).
void removeIcon(HWND hwnd);

// Tray icon clicks, its menu and Explorer restarts. True when the message was handled.
bool handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

} // namespace afm::app::tray
