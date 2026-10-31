#include "UpdateCheck.h"

#include "Version.h"

#include <windows.h>

#include <winhttp.h>

#include <thread>

#ifndef AFM_VERSION
#define AFM_VERSION "0.0.0"
#endif

namespace afm::app {

namespace {

constexpr wchar_t kHost[] = L"api.github.com";
constexpr wchar_t kPath[] = L"/repos/SubielaDev/AudioFlowMatrix/releases/latest";
constexpr DWORD kTimeoutMs = 8000;
constexpr std::size_t kMaxResponse = 512 * 1024;  // a release JSON is a few KB

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h != nullptr) WinHttpCloseHandle(h);
    }
};

// The latest release tag, or empty with `error` set.
std::string fetchLatestTag(std::string& error) {
    // GitHub's API requires a User-Agent; it identifies the app and version, nothing else.
    const std::string appVersion = AFM_VERSION;
    const std::wstring agent = L"AudioFlowMatrix/" + std::wstring(appVersion.begin(), appVersion.end());
    Handle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (session.h == nullptr) {
        error = "WinHttpOpen failed (" + std::to_string(GetLastError()) + ")";
        return {};
    }
    WinHttpSetTimeouts(session.h, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
    Handle connection{WinHttpConnect(session.h, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0)};
    Handle request{connection.h ? WinHttpOpenRequest(connection.h, L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr};
    if (request.h == nullptr) {
        error = "could not open the request (" + std::to_string(GetLastError()) + ")";
        return {};
    }
    const wchar_t headers[] = L"Accept: application/vnd.github+json\r\n";
    if (!WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = "no response from GitHub (offline? error " + std::to_string(GetLastError()) + ")";
        return {};
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = "GitHub answered HTTP " + std::to_string(status);
        return {};
    }
    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available) || available == 0) break;
        if (body.size() + available > kMaxResponse) {
            error = "response too large";
            return {};
        }
        const std::size_t old = body.size();
        body.resize(old + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, body.data() + old, available, &read)) break;
        body.resize(old + read);
    }
    std::string tag = version::tagFromReleaseJson(body);
    if (tag.empty()) error = "no usable release tag in GitHub's answer";
    return tag;
}

} // namespace

void UpdateCheck::start() {
    if (shared_->state.load(std::memory_order_acquire) == State::Running) return;
    shared_->state.store(State::Running, std::memory_order_release);
    std::thread([shared = shared_] {
        std::string error;
        std::string tag = fetchLatestTag(error);
        {
            const std::lock_guard<std::mutex> lock(shared->mutex);
            shared->result = tag.empty() ? error : tag;
        }
        shared->state.store(tag.empty() ? State::Failed : State::Done, std::memory_order_release);
    }).detach();
}

std::string UpdateCheck::result() const {
    const std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->result;
}

std::wstring UpdateCheck::releasePage(const std::string& tag) {
    return L"https://github.com/SubielaDev/AudioFlowMatrix/releases/tag/" + std::wstring(tag.begin(), tag.end());
}

} // namespace afm::app
