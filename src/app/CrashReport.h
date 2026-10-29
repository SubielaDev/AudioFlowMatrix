#pragma once

namespace afm::app {

// Writes %APPDATA%\AudioFlowMatrix\crashes\crash-<time>.txt (exception, faulting module and
// offset, stack as module+offset) plus a minidump when the process crashes on any thread,
// then tells the user where the files are. Install once, first thing in wWinMain.
void installCrashHandler();

// Deliberately crashes (null write), on this thread or on a new one.
[[noreturn]] void crashForTesting(bool onOtherThread);

} // namespace afm::app
