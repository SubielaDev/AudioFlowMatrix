#include "WindowChrome.h"

#include "imgui.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace afm::app::chrome {

namespace {

// Regions of the drawn title bar in client pixels, refreshed every frame by drawTitleBar
// and read by WM_NCHITTEST (same thread).
struct Layout {
    int titleHeight = 0;
    int buttonsLeft = 0;  // [buttonsLeft, buttonsRight): close | minimize | maximize, on the left
    int buttonsRight = 0;
    int maxLeft = 0;
    int maxRight = 0;
};

Layout g_layout;
bool g_maxHovered = false;
bool g_maxPressed = false;
bool g_active = true;

int frameThickness(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

LRESULT hitTest(HWND hwnd, LPARAM lParam) {
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &pt);
    RECT rc;
    GetClientRect(hwnd, &rc);

    if (!IsZoomed(hwnd)) {
        const int b = frameThickness(hwnd);
        const bool left = pt.x < b, right = pt.x >= rc.right - b;
        const bool top = pt.y < b, bottom = pt.y >= rc.bottom - b;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (bottom) return HTBOTTOM;
        // The top edge resizes only outside the window buttons.
        if (top && (pt.x < g_layout.buttonsLeft || pt.x >= g_layout.buttonsRight)) return HTTOP;
    }
    if (pt.y >= 0 && pt.y < g_layout.titleHeight) {
        if (pt.x >= g_layout.maxLeft && pt.x < g_layout.maxRight) return HTMAXBUTTON;
        if (pt.x >= g_layout.buttonsLeft && pt.x < g_layout.buttonsRight) return HTCLIENT;  // close / minimize: ImGui
        return HTCAPTION;
    }
    return HTCLIENT;
}

void setMaxHovered(HWND hwnd, bool hovered) {
    if (hovered && !g_maxHovered) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
        TrackMouseEvent(&tme);
    }
    g_maxHovered = hovered;
    if (!hovered) g_maxPressed = false;
}

// Coloured circle window buttons: the circle always shows, its symbol only while the pointer
// is over the group (close ×, minimize −, maximize + / restore ⤡).
enum class Glyph { Close, Minimize, Maximize, Restore };

struct CircleStyle {
    ImU32 fill;
    ImU32 fillPressed;
};

constexpr CircleStyle kClose{IM_COL32(255, 95, 87, 255), IM_COL32(191, 64, 58, 255)};
constexpr CircleStyle kMinimize{IM_COL32(254, 188, 46, 255), IM_COL32(190, 136, 26, 255)};
constexpr CircleStyle kMaximize{IM_COL32(40, 200, 64, 255), IM_COL32(28, 148, 46, 255)};
constexpr ImU32 kInactive = IM_COL32(72, 79, 92, 255);
constexpr ImU32 kSymbol = IM_COL32(60, 16, 12, 200);

void drawCircleButton(ImDrawList* dl, ImVec2 c, float r, Glyph g, const CircleStyle& style, bool active,
                      bool showSymbol, bool pressed, float s) {
    dl->AddCircleFilled(c, r, !active ? kInactive : pressed ? style.fillPressed : style.fill, 24);
    dl->AddCircle(c, r, IM_COL32(0, 0, 0, 60), 24, 1.0f * s);
    if (!showSymbol) return;
    const float h = r * 0.48f;
    const float t = 1.4f * s;
    switch (g) {
    case Glyph::Close:
        dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), kSymbol, t);
        dl->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), kSymbol, t);
        break;
    case Glyph::Minimize:
        dl->AddLine(ImVec2(c.x - h * 1.15f, c.y), ImVec2(c.x + h * 1.15f, c.y), kSymbol, t);
        break;
    case Glyph::Maximize:
        dl->AddLine(ImVec2(c.x - h * 1.15f, c.y), ImVec2(c.x + h * 1.15f, c.y), kSymbol, t);
        dl->AddLine(ImVec2(c.x, c.y - h * 1.15f), ImVec2(c.x, c.y + h * 1.15f), kSymbol, t);
        break;
    case Glyph::Restore:
        // Two small inward-pointing triangles, like "exit full screen".
        dl->AddTriangleFilled(ImVec2(c.x - h, c.y - 0.15f * h), ImVec2(c.x - 0.15f * h, c.y - 0.15f * h),
                              ImVec2(c.x - 0.15f * h, c.y - h), kSymbol);
        dl->AddTriangleFilled(ImVec2(c.x + h, c.y + 0.15f * h), ImVec2(c.x + 0.15f * h, c.y + 0.15f * h),
                              ImVec2(c.x + 0.15f * h, c.y + h), kSymbol);
        break;
    }
}

} // namespace

void install(HWND hwnd) {
    // A 1 px frame margin keeps the DWM drop shadow on a frameless window.
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const int round = 2;  // DWMWCP_ROUND; ignored before Windows 11
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            // No non-client area. Maximized windows overhang the monitor by the frame
            // thickness, so pull the client back inside the work area.
            if (IsZoomed(hwnd)) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                const int f = frameThickness(hwnd);
                p->rgrc[0].left += f;
                p->rgrc[0].top += f;
                p->rgrc[0].right -= f;
                p->rgrc[0].bottom -= f;
            }
            result = 0;
            return true;
        }
        return false;
    case WM_NCHITTEST:
        result = hitTest(hwnd, lParam);
        return true;
    case WM_NCACTIVATE:
        g_active = wParam != FALSE;
        // -1: don't let DefWindowProc repaint a classic frame over ours.
        result = DefWindowProcW(hwnd, msg, wParam, -1);
        return true;
    case WM_NCMOUSEMOVE:
        setMaxHovered(hwnd, wParam == HTMAXBUTTON);
        return false;
    case WM_MOUSEMOVE:
    case WM_NCMOUSELEAVE:
        setMaxHovered(hwnd, false);
        return false;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTMAXBUTTON) {
            g_maxPressed = true;
            result = 0;
            return true;
        }
        return false;
    case WM_NCLBUTTONUP:
        if (wParam == HTMAXBUTTON) {
            if (g_maxPressed) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            g_maxPressed = false;
            result = 0;
            return true;
        }
        return false;
    default:
        return false;
    }
}

float drawTitleBar(HWND hwnd) {
    const float s = ImGui::GetFontSize() / 16.0f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float height = std::round(30.0f * s);
    const float slot = std::round(20.0f * s);  // hit area per circle (full title height)
    const float radius = 6.0f * s;
    const float margin = std::round(10.0f * s);  // gap between the circles and the right edge

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    // Same background as the rest of the interface, so the bar blends in.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    ImGui::Begin("##titlebar", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float w = vp->Size.x;
    const float x0 = w - 3 * slot - margin;  // right corner: minimize | maximize | close

    // Minimize and close are ImGui buttons; act on release over the button, posted so the
    // window change happens after this frame. Maximize is a non-client button (HTMAXBUTTON),
    // handled in handleMessage, so Windows 11 can show snap layouts on it.
    ImGui::SetCursorPos(ImVec2(x0, 0));
    ImGui::InvisibleButton("##minimize", ImVec2(slot, height));
    const bool minHovered = ImGui::IsItemHovered(), minHeld = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivated() && ImGui::IsItemHovered()) PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    ImGui::SetCursorPos(ImVec2(x0 + 2 * slot, 0));
    ImGui::InvisibleButton("##close", ImVec2(slot, height));
    const bool closeHovered = ImGui::IsItemHovered(), closeHeld = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivated() && ImGui::IsItemHovered()) PostMessageW(hwnd, WM_CLOSE, 0, 0);

    // Symbols show on all three while the pointer is over any of them.
    const bool symbols = closeHovered || minHovered || g_maxHovered || closeHeld || minHeld || g_maxPressed;
    const float cy = origin.y + height * 0.5f;
    const auto centre = [&](int i) { return ImVec2(origin.x + x0 + slot * (static_cast<float>(i) + 0.5f), cy); };
    drawCircleButton(dl, centre(0), radius, Glyph::Minimize, kMinimize, g_active || symbols, symbols, minHeld, s);
    drawCircleButton(dl, centre(1), radius, IsZoomed(hwnd) ? Glyph::Restore : Glyph::Maximize, kMaximize,
                     g_active || symbols, symbols, g_maxPressed, s);
    drawCircleButton(dl, centre(2), radius, Glyph::Close, kClose, g_active || symbols, symbols, closeHeld, s);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    g_layout.titleHeight = static_cast<int>(height);
    g_layout.buttonsLeft = static_cast<int>(x0);
    g_layout.buttonsRight = static_cast<int>(x0 + 3 * slot);
    g_layout.maxLeft = static_cast<int>(x0 + slot);
    g_layout.maxRight = static_cast<int>(x0 + 2 * slot);
    return height;
}

unsigned refreshRate(HWND hwnd) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) return 0;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) return 0;
    return mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 0;  // 0 and 1 mean "hardware default"
}

} // namespace afm::app::chrome
