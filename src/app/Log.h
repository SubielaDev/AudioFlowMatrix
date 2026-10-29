#pragma once

#include <cstdio>
#include <string>

namespace afm::app {

// Plain-text diagnostics log, %APPDATA%\AudioFlowMatrix\logs\AudioFlowMatrix.log, meant to
// be sent back by testers. Each run starts a new file and keeps the previous one as
// AudioFlowMatrix.previous.log. Lines are timestamped and flushed immediately so nothing
// is lost if the app crashes. GUI thread only.
class Log {
public:
    Log() = default;
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    void open();
    void write(const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    [[nodiscard]] const std::wstring& directory() const noexcept { return directory_; }

private:
    std::FILE* file_ = nullptr;
    std::wstring directory_;
};

} // namespace afm::app
