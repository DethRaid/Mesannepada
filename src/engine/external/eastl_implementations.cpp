/*
 * Contains implementations of things that EASTL expects the user to provide
 */

#include <cstdint>
#include <stdio.h>

#if __linux__
#include <cinttypes>
#endif

#if _WIN32
#define SAH_CDECL __cdecl
#else
#define SAH_CDECL
#endif

void* SAH_CDECL operator new[](size_t size, const char* name, int flags, unsigned debugFlags, const char* file, int line) {
    return new uint8_t[size];
}

void* SAH_CDECL operator new[](uint64_t size, uint64_t alignment, uint64_t offset, char const* pName, int flags, unsigned int debugFlags, char const* file, int line) {
    return new uint8_t[size];
}

int Vsnprintf8(char* pDestination, size_t n, const char* pFormat, va_list arguments) {
#if _WIN32
    return vsnprintf_s(pDestination, n, n, pFormat, arguments);
#else
    return vsnprintf(pDestination, n, pFormat, arguments);
#endif
}

int Vsnprintf16(char16_t* pDestination, size_t n, const char16_t* pFormat, va_list arguments) {
    return 0;
}

int Vsnprintf32(char32_t* pDestination, size_t n, const char32_t* pFormat, va_list arguments) {
    return 0;
}

#if EA_CHAR8_UNIQUE
int Vsnprintf8(char8_t* pDestination, size_t n, const char8_t* pFormat, va_list arguments);
#endif
#if defined(EA_WCHAR_UNIQUE) && EA_WCHAR_UNIQUE
int VsnprintfW(wchar_t* pDestination, size_t n, const wchar_t* pFormat, va_list arguments);
#endif

