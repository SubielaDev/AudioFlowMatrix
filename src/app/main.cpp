// AudioFlow Matrix entry point: Win32 window + OpenGL 3 + Dear ImGui.
//
//   AudioFlowMatrix.exe          GUI
//   AudioFlowMatrix.exe --list   print ASIO drivers and WASAPI devices, then exit

#include "App.h"
#include "Brand.h"
#include "CrashReport.h"
#include "resource.h"

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
    afm::app::installCrashHandler();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // ASIO drivers expect an STA thread

    if (cmdLine != nullptr && std::wcsstr(cmdLine, L"--list") != nullptr) {
        const int rc = listDevices();
        CoUninitialize();
        return rc;
    }
    // Exercises the crash reporter: --crash-test on this thread, --crash-test-thread on another.
    if (cmdLine != nullptr && std::wcsstr(cmdLine, L"--crash-test") != nullptr) {
        afm::app::crashForTesting(std::wcsstr(cmdLine, L"--crash-test-thread") != nullptr);
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
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
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
    afm::app::brand::applyTheme(scale);

    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL3_Init();
    afm::app::brand::loadTextures();

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
                app->idle();  // audio and MIDI mappings keep running; just don't draw
                Sleep(16);
                continue;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            app->frame();
            ImGui::Render();

            glViewport(0, 0, g_width, g_height);
            const ImVec4 bg = afm::app::brand::kBackground;
            glClearColor(bg.x, bg.y, bg.z, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SwapBuffers(g_dc);
        }
    }  // App shuts audio down here, before the window goes away

    afm::app::brand::releaseTextures();
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
