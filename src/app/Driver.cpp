#include "Driver.h"

#include "resource.h"

#include "afm/platform/Win.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace afm::app::driver {

namespace {

// Matches DllRegisterServer in src/asio/virtual/DllMain.cpp.
constexpr wchar_t kAsioKey[] = L"SOFTWARE\\ASIO\\AudioFlow Matrix ASIO";
constexpr wchar_t kInprocKey[] = L"SOFTWARE\\Classes\\CLSID\\{7C3E1A52-9B4D-4F6E-8A21-3D5C9E0B7F14}\\InprocServer32";
constexpr wchar_t kDllName[] = L"AudioFlowMatrixASIO.dll";

std::wstring exeDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir = path;
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

bool fileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 1099511628211ull;
    return h;
}

std::wstring fullPath(const std::wstring& path) {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf) : path;
}

} // namespace

std::wstring dllPath(std::string& error) {
    const std::wstring beside = exeDirectory() + L"\\" + kDllName;
    if (fileExists(beside)) return beside;

    // Portable: extract the copy embedded in the executable.
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_ASIO_DLL), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL loaded = res != nullptr ? LoadResource(nullptr, res) : nullptr;
    const auto* bytes = loaded != nullptr ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
    const DWORD size = res != nullptr ? SizeofResource(nullptr, res) : 0;
    if (bytes == nullptr || size == 0) {
        error = "AudioFlowMatrixASIO.dll was not found next to the executable.";
        return {};
    }

    wchar_t localAppData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) {
        error = "LOCALAPPDATA is not set; cannot unpack the driver.";
        return {};
    }
    const std::wstring root = std::wstring(localAppData) + L"\\AudioFlowMatrix";
    const std::wstring dir = root + L"\\driver";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);

    wchar_t name[64];
    std::swprintf(name, 64, L"AudioFlowMatrixASIO-%016llx.dll", static_cast<unsigned long long>(fnv1a(bytes, size)));
    const std::wstring target = dir + L"\\" + name;
    if (fileExists(target)) return target;  // same contents, already unpacked

    // Write to a temporary name and rename, so a half-written DLL is never registered.
    const std::wstring temp = target + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "Could not unpack the driver to " + win::toUtf8(target);
        return {};
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes, size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (!ok || !MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        error = "Could not unpack the driver (disk full or no permission?)";
        return {};
    }
    return target;
}

std::wstring registeredPath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD bytes = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kInprocKey, nullptr, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf,
                     &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return buf;
}

Status status(const std::wstring& ourDll) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kAsioKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return Status::NotRegistered;
    }
    RegCloseKey(key);
    const std::wstring registered = registeredPath();
    if (registered.empty() || !fileExists(registered)) return Status::RegisteredOther;
    if (!ourDll.empty() && _wcsicmp(fullPath(registered).c_str(), fullPath(ourDll).c_str()) != 0) {
        return Status::RegisteredOther;
    }
    return Status::Registered;
}

} // namespace afm::app::driver
