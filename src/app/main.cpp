// AudioFlow Matrix entry point: Win32 window + OpenGL 3 + Dear ImGui.
//
//   AudioFlowMatrix.exe          GUI
//   AudioFlowMatrix.exe --list   print ASIO drivers and WASAPI devices, then exit

#include "App.h"

#include "afm/asio/BridgeProtocol.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include <GL/gl.h>

#include <cstdio>
#include <cstring>
#include <memory>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

HDC g_dc = nullptr;
HGLRC g_rc = nullptr;
int g_width = 0;
int g_height = 0;

bool createGl(HWND hwnd) {
    g_dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    const int format = ChoosePixelFormat(g_dc, &pfd);
    if (format == 0 || !SetPixelFormat(g_dc, format, &pfd)) return false;
    g_rc = wglCreateContext(g_dc);
    if (g_rc == nullptr || !wglMakeCurrent(g_dc, g_rc)) return false;

    // VSync keeps the GUI at the display rate instead of spinning a CPU core.
    using SwapIntervalFn = BOOL(WINAPI*)(int);
    if (auto swapInterval = reinterpret_cast<SwapIntervalFn>(reinterpret_cast<void*>(wglGetProcAddress("wglSwapIntervalEXT")))) {
        swapInterval(1);
    }
    return true;
}

void destroyGl(HWND hwnd) {
    wglMakeCurrent(nullptr, nullptr);
    if (g_rc != nullptr) wglDeleteContext(g_rc);
    if (g_dc != nullptr) ReleaseDC(hwnd, g_dc);
}

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_width = LOWORD(lParam);
            g_height = HIWORD(lParam);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {900, 560};
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // no ALT application menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void applyTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.GrabMinSize = 14.0f;
    style.TableAngledHeadersAngle = 50.0f * 3.14159265f / 180.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.11f, 0.125f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.125f, 0.14f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.135f, 0.155f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.18f, 0.21f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.28f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.34f, 1);
    c[ImGuiCol_Button] = ImVec4(0.2f, 0.22f, 0.26f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.3f, 0.36f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.36f, 0.44f, 1);
    c[ImGuiCol_Header] = ImVec4(0.2f, 0.22f, 0.26f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.3f, 0.36f, 1);
    c[ImGuiCol_Tab] = ImVec4(0.15f, 0.16f, 0.19f, 1);
    c[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.42f, 0.62f, 1);
    c[ImGuiCol_TabSelected] = ImVec4(0.2f, 0.34f, 0.52f, 1);
    c[ImGuiCol_SliderGrab] = ImVec4(0.75f, 0.78f, 0.82f, 1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.15f, 0.18f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(0.4f, 0.75f, 1.0f, 1);
    style.ScaleAllSizes(scale);
}

int listDevices() {
    AttachConsole(ATTACH_PARENT_PROCESS);
    std::printf("ASIO drivers:\n");
    for (const auto& d : afm::asio::listDrivers()) std::printf("  %s\n", d.name.c_str());
    std::printf("WASAPI capture devices:\n");
    for (const auto& d : afm::wasapi::listDevices(true)) std::printf("  %s\n", d.name.c_str());
    std::printf("WASAPI render devices:\n");
    for (const auto& d : afm::wasapi::listDevices(false)) std::printf("  %s\n", d.name.c_str());
    std::fflush(stdout);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // ASIO drivers expect an STA thread

    if (cmdLine != nullptr && std::wcsstr(cmdLine, L"--list") != nullptr) {
        const int rc = listDevices();
        CoUninitialize();
        return rc;
    }

    // Single instance: the bridge shared memory and the master driver can only be owned once.
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\AudioFlowMatrix.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(afm::bridge::kAppWindowClass, nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = afm::bridge::kAppWindowClass;
    RegisterClassExW(&wc);

    const float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"AudioFlow Matrix", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, static_cast<int>(1360 * dpiScale), static_cast<int>(820 * dpiScale),
                                nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr || !createGl(hwnd)) {
        MessageBoxW(nullptr, L"Could not create an OpenGL window.", L"AudioFlow Matrix", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f * scale, &fontConfig);
    } else {
        fontConfig.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&fontConfig);
    }
    applyTheme(scale);

    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL3_Init();

    {
        auto app = std::make_unique<afm::app::App>(hwnd);

        bool done = false;
        while (!done) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT) done = true;
            }
            if (done) break;
            if (IsIconic(hwnd)) {
                Sleep(16);  // audio keeps running; just don't draw
                continue;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            app->frame();
            ImGui::Render();

            glViewport(0, 0, g_width, g_height);
            glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SwapBuffers(g_dc);
        }
    }  // App shuts audio down here, before the window goes away

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyGl(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    if (instanceMutex != nullptr) CloseHandle(instanceMutex);
    CoUninitialize();
    return 0;
}
