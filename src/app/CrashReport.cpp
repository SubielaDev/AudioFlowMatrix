#include "CrashReport.h"

#include <windows.h>

#include <dbghelp.h>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>

#ifndef AFM_VERSION_STRING
#define AFM_VERSION_STRING "dev"
#endif

namespace afm::app {

namespace {

constexpr DWORD kTerminateCode = 0xE0F0A001;  // our own code for std::terminate
std::atomic<bool> g_reported{false};
DWORD g_mainThread = 0;

// Everything below runs after a crash: fixed buffers, Win32 calls only, no C++ allocation.

void appendf(char* buf, std::size_t cap, std::size_t& len, const char* fmt, ...) {
    if (len >= cap) return;
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf + len, cap - len, fmt, args);
    va_end(args);
    if (n > 0) len = std::min(cap - 1, len + static_cast<std::size_t>(n));
}

bool g_symbols = false;  // DbgHelp found the PDBs shipped next to the binaries

// "Name.dll+0x1234  function  (file:line)" for an address; symbols only when the PDB is there.
void describeAddress(const void* addr, char* out, std::size_t cap) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH) > 0) {
        const char* name = std::strrchr(path, '\\');
        name = name ? name + 1 : path;
        std::snprintf(out, cap, "%s+0x%llx", name,
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(addr) -
                                                      reinterpret_cast<std::uintptr_t>(module)));
    } else {
        std::snprintf(out, cap, "0x%p", addr);
    }
    if (!g_symbols) return;

    alignas(SYMBOL_INFO) static char symbolStorage[sizeof(SYMBOL_INFO) + 256];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    const auto address = static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(addr));
    DWORD64 symbolOffset = 0;
    if (!SymFromAddr(GetCurrentProcess(), address, &symbolOffset, symbol)) return;
    std::size_t len = std::strlen(out);
    appendf(out, cap, len, "  %s", symbol->Name);
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD lineOffset = 0;
    if (SymGetLineFromAddr64(GetCurrentProcess(), address, &lineOffset, &line)) {
        const char* file = std::strrchr(line.FileName, '\\');
        if (file == nullptr) file = std::strrchr(line.FileName, '/');
        appendf(out, cap, len, "  (%s:%lu)", file ? file + 1 : line.FileName, line.LineNumber);
    }
}

const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction (CPU feature missing?)";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case 0xC0000409: return "stack buffer overrun / fail-fast";
    case kTerminateCode: return "std::terminate (unhandled C++ exception or noexcept violation)";
    default: return "unknown";
    }
}

bool hasAvx2() {
#if defined(_MSC_VER)
    int regs[4] = {};
    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0;
#else
    unsigned a = 0, b = 0, c = 0, d = 0;
    return __get_cpuid_count(7, 0, &a, &b, &c, &d) != 0 && (b & (1u << 5)) != 0;
#endif
}

bool crashDirectory(wchar_t* dir, std::size_t cap) {
    wchar_t appData[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0) return false;
    std::swprintf(dir, cap, L"%ls\\AudioFlowMatrix", appData);
    CreateDirectoryW(dir, nullptr);
    std::swprintf(dir, cap, L"%ls\\AudioFlowMatrix\\crashes", appData);
    CreateDirectoryW(dir, nullptr);
    return true;
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    if (g_reported.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;  // only the first crash

    // AudioFlowMatrix.pdb and AudioFlowMatrixASIO.pdb ship next to the binaries.
    wchar_t exeDir[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        if (wchar_t* slash = std::wcsrchr(exeDir, L'\\')) *slash = L'\0';
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
        g_symbols = SymInitializeW(GetCurrentProcess(), exeDir, TRUE) != FALSE;
    }

    wchar_t dir[MAX_PATH];
    if (!crashDirectory(dir, MAX_PATH)) return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t base[MAX_PATH];
    std::swprintf(base, MAX_PATH, L"%ls\\crash-%04u%02u%02u-%02u%02u%02u", dir, t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond);

    static char text[16384];
    std::size_t len = 0;
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;
    char where[MAX_PATH + 32];
    describeAddress(rec->ExceptionAddress, where, sizeof(where));

    OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))) {
        fn(&os);
    }

    appendf(text, sizeof(text), len, "AudioFlow Matrix crash report\r\n");
    appendf(text, sizeof(text), len, "Version:   %s\r\n", AFM_VERSION_STRING);
    appendf(text, sizeof(text), len, "Windows:   %lu.%lu.%lu\r\n", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);
    appendf(text, sizeof(text), len, "CPU AVX2:  %s\r\n", hasAvx2() ? "yes" : "no");
    appendf(text, sizeof(text), len, "Symbols:   %s\r\n", g_symbols ? "loaded" : "not found (PDB missing?)");
    appendf(text, sizeof(text), len, "Thread:    %s (id %lu)\r\n",
            GetCurrentThreadId() == g_mainThread ? "GUI" : "audio/device", GetCurrentThreadId());
    appendf(text, sizeof(text), len, "Exception: 0x%08lX %s\r\n", rec->ExceptionCode, exceptionName(rec->ExceptionCode));
    appendf(text, sizeof(text), len, "At:        %s\r\n", where);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        appendf(text, sizeof(text), len, "Access:    %s 0x%llx\r\n",
                rec->ExceptionInformation[0] == 0 ? "read of" : rec->ExceptionInformation[0] == 1 ? "write to" : "execute",
                static_cast<unsigned long long>(rec->ExceptionInformation[1]));
    }
    appendf(text, sizeof(text), len, "\r\nStack:\r\n");
    void* frames[48];
    const USHORT count = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        describeAddress(frames[i], where, sizeof(where));
        appendf(text, sizeof(text), len, "  %2u  %s\r\n", i, where);
    }

    wchar_t path[MAX_PATH];
    std::swprintf(path, MAX_PATH, L"%ls.txt", base);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, text, static_cast<DWORD>(len), &written, nullptr);
        CloseHandle(file);
    }

    std::swprintf(path, MAX_PATH, L"%ls.dmp", base);
    file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION ex{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory), &ex,
                          nullptr, nullptr);
        CloseHandle(file);
    }

    wchar_t message[MAX_PATH + 160];
    std::swprintf(message, MAX_PATH + 160,
                  L"AudioFlow Matrix has crashed.\n\nPlease send the developer the two \"crash-...\" files in:\n%ls",
                  dir);
    MessageBoxW(nullptr, message, L"AudioFlow Matrix", MB_ICONERROR | MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void installCrashHandler() {
    g_mainThread = GetCurrentThreadId();
    SetUnhandledExceptionFilter(onCrash);
    std::set_terminate([] {
        RaiseException(kTerminateCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        std::abort();
    });
}

namespace {

[[gnu::noinline]] void writeThroughNull(volatile int* p) { *p = 42; }

DWORD WINAPI crashThread(void*) {
    writeThroughNull(nullptr);
    return 0;
}

} // namespace

void crashForTesting(bool onOtherThread) {
    if (onOtherThread) {
        HANDLE thread = CreateThread(nullptr, 0, crashThread, nullptr, 0, nullptr);
        WaitForSingleObject(thread, INFINITE);
    } else {
        writeThroughNull(nullptr);
    }
    std::abort();
}

} // namespace afm::app
