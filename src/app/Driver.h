#pragma once

#include <string>

namespace afm::app::driver {

// Where the virtual ASIO driver DLL ("AudioFlow Matrix ASIO") lives.
//
// Installed or unzipped: AudioFlowMatrixASIO.dll next to the executable.
// Portable (the .exe alone): the DLL is embedded in the executable and extracted to
// %LOCALAPPDATA%\AudioFlowMatrix\driver\AudioFlowMatrixASIO-<content hash>.dll. The name
// changes with the contents, so a newer build never has to overwrite a DLL that Traktor may
// still have loaded; the old one just stops being registered.

enum class Status {
    NotRegistered,
    Registered,       // registered and pointing at the DLL this executable uses
    RegisteredOther,  // registered, but from another copy (another folder, version or a missing file)
};

// The DLL this executable uses, extracting the embedded copy if needed. Empty + `error` on failure.
[[nodiscard]] std::wstring dllPath(std::string& error);

// The DLL Windows has registered for the driver's CLSID, or empty.
[[nodiscard]] std::wstring registeredPath();

[[nodiscard]] Status status(const std::wstring& ourDll);

} // namespace afm::app::driver
