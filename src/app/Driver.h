#pragma once

#include <windows.h>

#include <string>

namespace afm::app::driver {

// The virtual ASIO driver ("AudioFlow Matrix ASIO").
//
// Every build of the executable embeds the driver DLL. Registering it is machine-wide and
// the DLL then gets loaded into any ASIO host, possibly an elevated one, so the registered
// copy must not be writable by an ordinary process. Registration therefore runs this same
// executable elevated (`--install-driver`): it copies the embedded DLL into
// %ProgramFiles%\AudioFlow Matrix\driver (admin-only), checks the copy byte for byte and
// registers it from there. Unregistering only deletes the driver's registry keys and never
// loads whatever DLL happens to be registered.

enum class Status {
    NotRegistered,
    Registered,       // the registered DLL is this build's driver
    RegisteredOther,  // registered, but another build, or the file is missing
};

[[nodiscard]] Status status();

// The DLL Windows has registered for the driver, or empty.
[[nodiscard]] std::wstring registeredPath();
// Where this build installs its driver.
[[nodiscard]] std::wstring installPath();

// Asks for elevation (UAC) and runs this executable with --install-driver or
// --uninstall-driver. False with `error` set if cancelled or failed.
[[nodiscard]] bool runElevated(bool install, HWND owner, std::string& error);

// The elevated side, called from wWinMain for those flags. Returns the process exit code.
int installElevated();
int uninstallElevated();

} // namespace afm::app::driver
