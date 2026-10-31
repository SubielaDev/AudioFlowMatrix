#pragma once

#include <windows.h>

namespace afm::app::chrome {

// Borderless main window with a title bar drawn by the app: a plain dark bar with
// coloured circle buttons in the right corner (minimize, maximize/restore, close).
//
// The window keeps WS_OVERLAPPEDWINDOW, so Windows still provides moving, edge resizing,
// Aero Snap, the system menu and minimize/maximize animations; WM_NCCALCSIZE just hands
// the whole window to the client area and WM_NCHITTEST maps the drawn title bar and
// edges back to caption / border hit codes. The maximize button reports HTMAXBUTTON, which
// is what makes Windows 11 show its snap-layouts flyout on hover.

// Once, after CreateWindow: DWM shadow, dark system menu, rounded corners (Windows 11).
void install(HWND hwnd);

// Called first in the window procedure. Returns true when the message was handled and
// `result` is the value to return.
bool handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

// Draws the title bar across the top of the client area (in an ImGui window of its own)
// and records the regions the hit-testing above uses. Returns its height in pixels.
float drawTitleBar(HWND hwnd);

// Refresh rate of the monitor the window is on, in Hz; 0 if Windows does not say.
unsigned refreshRate(HWND hwnd);

} // namespace afm::app::chrome
