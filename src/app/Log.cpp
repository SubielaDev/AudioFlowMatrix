#include "Log.h"

#include <windows.h>

#include <cstdarg>

namespace afm::app {

Log::~Log() {
    if (file_ != nullptr) std::fclose(file_);
}

void Log::open() {
    wchar_t appData[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0) return;
    const std::wstring root = std::wstring(appData) + L"\\AudioFlowMatrix";
    directory_ = root + L"\\logs";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(directory_.c_str(), nullptr);

    const std::wstring current = directory_ + L"\\AudioFlowMatrix.log";
    const std::wstring previous = directory_ + L"\\AudioFlowMatrix.previous.log";
    MoveFileExW(current.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
    file_ = _wfopen(current.c_str(), L"wb");
}

void Log::write(const char* format, ...) {
    if (file_ == nullptr) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(file_, "%04u-%02u-%02u %02u:%02u:%02u.%03u  ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                 t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, format);
    std::vfprintf(file_, format, args);
    va_end(args);
    std::fputs("\r\n", file_);
    std::fflush(file_);
}

} // namespace afm::app
