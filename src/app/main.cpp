// AudioFlow Matrix entry point: Win32 window + OpenGL 3 + Dear ImGui.
//
//   AudioFlowMatrix.exe          GUI
//   AudioFlowMatrix.exe --list   print ASIO drivers and WASAPI devices, then exit

#include "App.h"
#include "Brand.h"
#include "CrashReport.h"
#include "PerfTest.h"
#include "Tray.h"
#include "WindowChrome.h"
#include "resource.h"

#include "afm/asio/BridgeProtocol.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include <GL/gl.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <functional>
#include <memory>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002  // Windows 10 1803+, missing from older SDKs
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

HDC g_dc = nullptr;
HGLRC g_rc = nullptr;
int g_width = 0;
int g_height = 0;

// Draws and presents one frame. Also called from WM_SIZE while the user drags a window
// edge (Windows runs its own modal loop then), so the layout follows the size live.
std::function<void()> g_renderFrame;
bool g_inSizeMove = false;

// Sleeps until `period` seconds after `frameStart` (QueryPerformanceCounter ticks). A
// high-resolution waitable timer is precise to well under a millisecond without raising the
// system timer resolution.
void waitForNextFrame(HANDLE timer, LONGLONG frameStart, double period) {
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    const double remaining = period - static_cast<double>(now.QuadPart - frameStart) / static_cast<double>(frequency.QuadPart);
    if (remaining <= 0.0) return;
    if (timer == nullptr) {
        Sleep(static_cast<DWORD>(remaining * 1000.0));
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(remaining * 1e7);  // relative, in 100 ns units
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
}
// Settings > Minimize to the system tray; read when the window is minimized.
std::function<bool()> g_minimizeToTray;

bool createGl(HWND hwnd, bool vsync) {
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

    // No VSync: the main loop paces frames itself (Settings > Window, App::uiFrameRate). VSync tied the UI to the
    // display rate, a whole CPU core on a 360 Hz monitor, and some drivers spin while waiting.
    // Only the performance test turns it back on (--render=vsync) for comparison.
    using SwapIntervalFn = BOOL(WINAPI*)(int);
    if (auto swapInterval = reinterpret_cast<SwapIntervalFn>(reinterpret_cast<void*>(wglGetProcAddress("wglSwapIntervalEXT")))) {
        swapInterval(vsync ? 1 : 0);
    }
    return true;
}

void destroyGl(HWND hwnd) {
    wglMakeCurrent(nullptr, nullptr);
    if (g_rc != nullptr) wglDeleteContext(g_rc);
    if (g_dc != nullptr) ReleaseDC(hwnd, g_dc);
}

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT chromeResult = 0;
    if (afm::app::chrome::handleMessage(hwnd, msg, wParam, lParam, chromeResult)) return chromeResult;
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    if (afm::app::tray::handleMessage(hwnd, msg, wParam, lParam)) return 0;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            if (g_minimizeToTray && g_minimizeToTray()) afm::app::tray::hideToTray(hwnd);
        } else {
            g_width = LOWORD(lParam);
            g_height = HIWORD(lParam);
            if (g_inSizeMove && g_renderFrame) g_renderFrame();
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        g_inSizeMove = true;
        return 0;
    case WM_EXITSIZEMOVE:
        g_inSizeMove = false;
        return 0;
    case WM_GETMINMAXINFO: {
        // Below this the layout cannot fit; scaled with the window's DPI.
        const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {static_cast<LONG>(760 * scale), static_cast<LONG>(520 * scale)};
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // no ALT application menu
        break;
    case WM_DESTROY:
        afm::app::tray::removeIcon(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int listDevices() {
    // A GUI-subsystem app has no console of its own: print into the caller's, unless the
    // output is already redirected to a file or pipe.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
    }
    std::printf("ASIO drivers:\n");
    for (const auto& d : afm::asio::listDrivers()) std::printf("  %s\n", d.name.c_str());
    std::printf("WASAPI capture devices:\n");
    for (const auto& d : afm::wasapi::listDevices(true)) std::printf("  %s\n", d.name.c_str());
    std::printf("WASAPI render devices:\n");
    for (const auto& d : afm::wasapi::listDevices(false)) std::printf("  %s\n", d.name.c_str());
    const auto status = afm::app::driver::status();
    std::printf("Virtual ASIO driver installs to: %s\n", afm::win::toUtf8(afm::app::driver::installPath()).c_str());
    std::printf("Registered from: %s (%s)\n", afm::win::toUtf8(afm::app::driver::registeredPath()).c_str(),
                status == afm::app::driver::Status::Registered        ? "this build"
                : status == afm::app::driver::Status::NotRegistered ? "not registered"
                                                                     : "another build");
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
    // Elevated helper started by Devices > Register / Unregister (see Driver.h). Exact match
    // only: these change machine-wide state and must not trigger on a substring.
    if (cmdLine != nullptr && (std::wcscmp(cmdLine, L"--install-driver") == 0 || std::wcscmp(cmdLine, L"--uninstall-driver") == 0)) {
        const int rc = std::wcscmp(cmdLine, L"--install-driver") == 0 ? afm::app::driver::installElevated()
                                                                       : afm::app::driver::uninstallElevated();
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
            // It may be hidden in the tray: let it restore itself (and drop its tray icon).
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(existing, afm::app::tray::kRestoreMessage, 0, 0);
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

    // Performance test options (see PerfTest.h). Without them: 60 fps full redraws.
    using afm::app::RenderMode;
    RenderMode renderMode = RenderMode::Cap60;
    double perfSeconds = 0.0;
    bool perfMoving = true;
    const char* perfTab = "mixer";
    if (cmdLine != nullptr) {
        if (std::wcsstr(cmdLine, L"--render=vsync") != nullptr) renderMode = RenderMode::VSync;
        if (std::wcsstr(cmdLine, L"--render=display") != nullptr) renderMode = RenderMode::Display;
        if (const wchar_t* p = std::wcsstr(cmdLine, L"--perf-test=")) perfSeconds = std::wcstod(p + 12, nullptr);
        perfMoving = std::wcsstr(cmdLine, L"--perf-scene=silent") == nullptr;
        if (std::wcsstr(cmdLine, L"--perf-tab=matrix") != nullptr) perfTab = "matrix";
        if (std::wcsstr(cmdLine, L"--perf-tab=devices") != nullptr) perfTab = "devices";
        if (std::wcsstr(cmdLine, L"--perf-tab=midi") != nullptr) perfTab = "midi";
    }

    const float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"AudioFlow Matrix", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, static_cast<int>(1360 * dpiScale), static_cast<int>(820 * dpiScale),
                                nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr || !createGl(hwnd, renderMode == RenderMode::VSync)) {
        MessageBoxW(nullptr, L"Could not create an OpenGL window.", L"AudioFlow Matrix", MB_ICONERROR);
        return 1;
    }
    afm::app::chrome::install(hwnd);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef NDEBUG
    io.ConfigDebugHighlightIdConflicts = false;  // a developer message, not for users; debug builds keep it
#endif

    const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES) {
        // Latin-1 plus Cyrillic: every UI language (see I18n.cpp).
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f * scale, &fontConfig,
                                     io.Fonts->GetGlyphRangesCyrillic());
    } else {
        fontConfig.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&fontConfig);
    }
    afm::app::brand::applyTheme(scale);

    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL3_Init();
    afm::app::brand::loadTextures();

    {
        afm::app::PerfScene perfScene;
        perfScene.enabled = perfSeconds > 0.0;
        perfScene.active = perfMoving;
        perfScene.tab = perfTab;
        auto app = std::make_unique<afm::app::App>(hwnd, perfScene);
        g_minimizeToTray = [&app] { return app->minimizeToTray(); };

        std::unique_ptr<afm::app::PerfRecorder> perf;
        if (perfScene.enabled) perf = std::make_unique<afm::app::PerfRecorder>(renderMode, perfMoving, perfSeconds, hwnd);

        g_renderFrame = [&] {
            LARGE_INTEGER t0, t1, t2, frequency;
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&t0);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            app->frame();
            ImGui::Render();
            QueryPerformanceCounter(&t1);

            glViewport(0, 0, g_width, g_height);
            const ImVec4 bg = afm::app::brand::kBackground;
            glClearColor(bg.x, bg.y, bg.z, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SwapBuffers(g_dc);
            QueryPerformanceCounter(&t2);
            if (perf) {
                const double ms = 1000.0 / static_cast<double>(frequency.QuadPart);
                perf->frame((t1.QuadPart - t0.QuadPart) * ms, (t2.QuadPart - t1.QuadPart) * ms);
            }
        };

        HANDLE frameTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (frameTimer == nullptr) frameTimer = CreateWaitableTimerW(nullptr, TRUE, nullptr);  // before Windows 10 1803

        bool done = false;
        while (!done) {
            LARGE_INTEGER frameStart;
            QueryPerformanceCounter(&frameStart);
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
            g_renderFrame();
            if (perf && perf->finished()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            if (renderMode != RenderMode::VSync) {
                // The user's choice in Settings; the performance test pins its own mode instead.
                double rate = app->uiFrameRate();
                if (perf) {
                    const unsigned hz = afm::app::chrome::refreshRate(hwnd);
                    rate = renderMode == RenderMode::Display && hz > 0 ? hz : 60.0;
                }
                waitForNextFrame(frameTimer, frameStart.QuadPart, 1.0 / rate);
            }
        }
        if (frameTimer != nullptr) CloseHandle(frameTimer);
        g_renderFrame = nullptr;
        g_minimizeToTray = nullptr;
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
