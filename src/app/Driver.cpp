#include "Driver.h"

#include "resource.h"

#include <shellapi.h>
#include <shlobj.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace afm::app::driver {

namespace {

// Must match DllRegisterServer in src/asio/virtual/DllMain.cpp.
constexpr wchar_t kAsioKey[] = L"SOFTWARE\\ASIO\\AudioFlow Matrix ASIO";
constexpr wchar_t kClsidKey[] = L"SOFTWARE\\Classes\\CLSID\\{7C3E1A52-9B4D-4F6E-8A21-3D5C9E0B7F14}";

// Exit codes of the elevated helper.
enum : int { kOk = 0, kNoEmbeddedDriver = 2, kNoProgramFiles = 3, kWriteFailed = 4, kVerifyFailed = 5, kRegisterFailed = 6 };

struct Bytes {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

Bytes embedded() {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_ASIO_DLL), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL loaded = res != nullptr ? LoadResource(nullptr, res) : nullptr;
    const auto* data = loaded != nullptr ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
    return data != nullptr ? Bytes{data, SizeofResource(nullptr, res)} : Bytes{};
}

std::uint64_t fnv1a(Bytes b) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < b.size; ++i) h = (h ^ b.data[i]) * 1099511628211ull;
    return h;
}

std::wstring programFiles() {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

std::wstring driverDirectory() {
    const std::wstring pf = programFiles();
    return pf.empty() ? std::wstring{} : pf + L"\\AudioFlow Matrix\\driver";
}

// Whole-file comparison with the embedded DLL.
bool fileEquals(const std::wstring& path, Bytes b) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool same = GetFileSizeEx(f, &size) && static_cast<std::uint64_t>(size.QuadPart) == b.size;
    if (same) {
        std::vector<std::uint8_t> buf(b.size);
        DWORD read = 0;
        same = ReadFile(f, buf.data(), static_cast<DWORD>(b.size), &read, nullptr) && read == b.size &&
               std::memcmp(buf.data(), b.data, b.size) == 0;
    }
    CloseHandle(f);
    return same;
}

bool writeFile(const std::wstring& path, Bytes b) {
    const std::wstring temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, b.data, static_cast<DWORD>(b.size), &written, nullptr) && written == b.size;
    CloseHandle(f);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

void deleteKey(const wchar_t* path) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE |
                                                         KEY_WOW64_64KEY,
                      &key) != ERROR_SUCCESS) {
        return;
    }
    RegDeleteTreeW(key, nullptr);
    RegCloseKey(key);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, path, KEY_WOW64_64KEY, 0);
}

// Old per-user copies unpacked by 0.3.0 portables; harmless once nothing points at them.
void removeLegacyUserCopies() {
    wchar_t localAppData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) return;
    const std::wstring dir = std::wstring(localAppData) + L"\\AudioFlowMatrix\\driver";
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\AudioFlowMatrixASIO-*.dll").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        DeleteFileW((dir + L"\\" + fd.cFileName).c_str());  // fails harmlessly if a host still has it loaded
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    RemoveDirectoryW(dir.c_str());
}

} // namespace

std::wstring installPath() {
    const std::wstring dir = driverDirectory();
    if (dir.empty()) return {};
    wchar_t name[64];
    std::swprintf(name, 64, L"\\AudioFlowMatrixASIO-%016llx.dll", static_cast<unsigned long long>(fnv1a(embedded())));
    return dir + name;
}

std::wstring registeredPath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD bytes = sizeof(buf);
    const std::wstring key = std::wstring(kClsidKey) + L"\\InprocServer32";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf,
                     &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return buf;
}

Status status() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kAsioKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return Status::NotRegistered;
    }
    RegCloseKey(key);
    const std::wstring registered = registeredPath();
    return !registered.empty() && fileEquals(registered, embedded()) ? Status::Registered : Status::RegisteredOther;
}

bool runElevated(bool install, HWND owner, std::string& error) {
    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) {
        error = "Could not locate AudioFlowMatrix.exe.";
        return false;
    }
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";  // UAC prompt: registering a driver writes HKLM and Program Files
    sei.lpFile = exe;
    sei.lpParameters = install ? L"--install-driver" : L"--uninstall-driver";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        error = GetLastError() == ERROR_CANCELLED ? "Driver registration was cancelled." : "Could not start the driver setup.";
        return false;
    }
    DWORD code = 1;
    if (sei.hProcess != nullptr) {
        WaitForSingleObject(sei.hProcess, 60000);
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
    }
    if (code != kOk) {
        error = "Driver setup failed (code " + std::to_string(code) + ").";
        return false;
    }
    if (install) removeLegacyUserCopies();
    return true;
}

int installElevated() {
    const Bytes dll = embedded();
    if (dll.data == nullptr || dll.size == 0) return kNoEmbeddedDriver;
    const std::wstring dir = driverDirectory();
    if (dir.empty()) return kNoProgramFiles;
    CreateDirectoryW(dir.substr(0, dir.find_last_of(L'\\')).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);

    const std::wstring target = installPath();
    if (!fileEquals(target, dll) && !writeFile(target, dll)) return kWriteFailed;
    if (!fileEquals(target, dll)) return kVerifyFailed;

    // Register by calling the DLL's own entry point (no regsvr32), with its imports resolved
    // from System32 and its own admin-only folder only.
    HMODULE module = LoadLibraryExW(target.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) return kRegisterFailed;
    using RegisterFn = HRESULT(STDAPICALLTYPE*)();
    auto reg = reinterpret_cast<RegisterFn>(reinterpret_cast<void*>(GetProcAddress(module, "DllRegisterServer")));
    const HRESULT hr = reg != nullptr ? reg() : E_FAIL;
    FreeLibrary(module);
    if (FAILED(hr)) return kRegisterFailed;

    // Older builds' copies in the same folder; ones a host still has loaded stay until next time.
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\AudioFlowMatrixASIO-*.dll").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring path = dir + L"\\" + fd.cFileName;
            if (_wcsicmp(path.c_str(), target.c_str()) != 0) DeleteFileW(path.c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    return kOk;
}

int uninstallElevated() {
    deleteKey(kAsioKey);
    deleteKey(kClsidKey);
    return kOk;
}

} // namespace afm::app::driver
