/* Load-config directory whose DependentLoadFlags make Windows resolve this module's
 * static imports from System32 only, never from the module's own folder. Without it, a
 * dbghelp.dll / winmm.dll / dwmapi.dll / opengl32.dll dropped next to the portable .exe
 * (say, in Downloads) would be loaded and run. Honoured since Windows 10 1607.
 *
 * MinGW toolchains (zig, clang, gcc) only: lld-link picks up `_load_config_used` the same
 * way MSVC's CRT provides it. MSVC builds use /DEPENDENTLOADFLAG instead (see
 * afm_safe_dll_search in cmake/AfmCompilerOptions.cmake). */

#include <windows.h>

const IMAGE_LOAD_CONFIG_DIRECTORY64 _load_config_used = {
    .Size = sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64),
    .DependentLoadFlags = LOAD_LIBRARY_SEARCH_SYSTEM32,
};
