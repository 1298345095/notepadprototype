#include <windows.h>
#include <cstddef>

// Host services and defaults imported by Notepad4's Win32 editor sources.
HANDLE g_hDefaultHeap = GetProcessHeap();
UINT g_uSystemDPI = USER_DEFAULT_SCREEN_DPI;
unsigned int dwUrlThreshold = 256;
DWORD kSystemLibraryLoadFlags = LOAD_LIBRARY_SEARCH_SYSTEM32;
WCHAR defaultTextFontName[LF_FACESIZE] = L"Consolas";

// Additional case mappings belong to Notepad4's application menu.
char *EditMapTextCase(int, const char *, std::size_t &, UINT) noexcept {
    return nullptr;
}
