#include "Keyboard.h"

#include "afm/platform/Win.h"

#include <string>

namespace afm::app {

namespace {

constexpr UINT_PTR kCheckTimer = 1;
constexpr UINT kCheckMs = 250;

// Raw Input reports the generic VK_SHIFT / VK_CONTROL / VK_MENU; tell the sides apart, so a
// shortcut can use one of them on its own. 0 = not a key to report.
std::uint8_t sidedKey(const RAWKEYBOARD& kb) {
    if (kb.VKey == 0 || kb.VKey >= 0xFF) return 0;  // 0xFF: fake key of an escape sequence
    switch (kb.VKey) {
    case VK_SHIFT: return kb.MakeCode == 0x36 ? midi::vk::kRShift : midi::vk::kLShift;
    case VK_CONTROL:
        if (kb.Flags & RI_KEY_E1) return 0;  // first half of Pause
        return (kb.Flags & RI_KEY_E0) ? midi::vk::kRControl : midi::vk::kLControl;
    case VK_MENU: return (kb.Flags & RI_KEY_E0) ? midi::vk::kRMenu : midi::vk::kLMenu;
    default: return static_cast<std::uint8_t>(kb.VKey);
    }
}

struct WindowState {
    KeyboardInput::Handler handler = nullptr;
    void* user = nullptr;
    midi::KeyTracker tracker;
};

void report(WindowState& s, std::uint8_t key, bool down) {
    midi::KeyEvent e;
    if (s.tracker.update(key, down, e)) s.handler(s.user, e);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* s = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (s != nullptr && msg == WM_INPUT) {
        RAWINPUT input{};
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) !=
                static_cast<UINT>(-1) &&
            input.header.dwType == RIM_TYPEKEYBOARD) {
            if (const std::uint8_t key = sidedKey(input.data.keyboard); key != 0) {
                report(*s, key, (input.data.keyboard.Flags & RI_KEY_BREAK) == 0);
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);  // lets Windows clean up the input
    }
    // A key let go while a secure desktop (UAC, lock screen) had the input never reaches us:
    // without this a held push-to-talk key would stay open.
    if (s != nullptr && msg == WM_TIMER && wParam == kCheckTimer && s->tracker.anyDown()) {
        for (int key = 1; key < 0xFF; ++key) {
            const auto k = static_cast<std::uint8_t>(key);
            if (s->tracker.isDown(k) && (GetAsyncKeyState(key) & 0x8000) == 0) report(*s, k, false);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool KeyboardInput::start(Handler handler, void* user, std::string& error) {
    stop();
    handler_ = handler;
    user_ = user;
    std::atomic<int> state{0};  // 0 starting, 1 running, -1 failed
    thread_ = std::thread([this, &state, &error] { run(state, error); });
    while (state.load(std::memory_order_acquire) == 0) Sleep(1);
    if (state.load(std::memory_order_acquire) < 0) {
        thread_.join();
        return false;
    }
    return true;
}

void KeyboardInput::stop() {
    if (!thread_.joinable()) return;
    PostThreadMessageW(threadId_.load(), WM_QUIT, 0, 0);
    thread_.join();
    threadId_.store(0);
}

void KeyboardInput::run(std::atomic<int>& state, std::string& error) {
    threadId_.store(GetCurrentThreadId());
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"AudioFlowMatrixKeyboard";
    RegisterClassW(&wc);  // fails harmlessly when a previous start already registered it
    const HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        error = "Keyboard: could not create the input window (error " + std::to_string(GetLastError()) + ")";
        state.store(-1, std::memory_order_release);
        return;
    }
    WindowState s{handler_, user_, {}};
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));

    // INPUTSINK: keys arrive also while another program is in front.
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01;  // generic desktop
    device.usUsage = 0x06;      // keyboard
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = hwnd;
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        error = "Keyboard: Raw Input registration failed (error " + std::to_string(GetLastError()) + ")";
        DestroyWindow(hwnd);
        state.store(-1, std::memory_order_release);
        return;
    }
    SetTimer(hwnd, kCheckTimer, kCheckMs, nullptr);
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // creates the queue before stop() can post to it
    state.store(1, std::memory_order_release);

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Report every key still down as released, so nothing (push-to-talk) stays held.
    for (int key = 1; key < 0xFF; ++key) {
        if (s.tracker.isDown(static_cast<std::uint8_t>(key))) report(s, static_cast<std::uint8_t>(key), false);
    }
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = nullptr;
    RegisterRawInputDevices(&device, 1, sizeof(device));
    KillTimer(hwnd, kCheckTimer);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    DestroyWindow(hwnd);
}

} // namespace afm::app
