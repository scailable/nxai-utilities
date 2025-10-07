#if defined(_MSC_VER)
    // Windows stuff
    #include <windows.h>
#else
    #define _GNU_SOURCE
    #include <dlfcn.h>
#endif

#include "nxai_library_utils.h"

void* nxai_load_library(const char* filename)
{
#if defined(_MSC_VER)
    // Windows implementation
    return LoadLibraryA(filename);
#else
    // Linux implementation
    return dlopen(filename, RTLD_LAZY);
#endif
}

char* nxai_get_error(void)
{
#if defined(_MSC_VER)
    // Windows implementation using FormatMessage
    DWORD error = GetLastError();
    LPSTR messageBuffer = NULL;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR) &messageBuffer,
        0,
        NULL);

    return messageBuffer;
#else
    // Linux implementation using dlerror
    return dlerror();
#endif
}

void* nxai_load_library_with_namespace(const char* filename, int nsid)
{
#if defined(_MSC_VER)
    // Windows implementation using LoadLibraryEx
    return LoadLibraryEx(
        filename,
        NULL,
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    // Unix/Linux implementation using dlmopen
    return dlmopen(-1, filename, RTLD_LAZY);
#endif
}

void* nxai_get_library_symbol(void* handle, const char* symbol)
{
#if defined(_MSC_VER)
    // Windows implementation
    return GetProcAddress((HMODULE) handle, symbol);
#else
    // Linux implementation
    return dlsym(handle, symbol);
#endif
}

void nxai_free_library(void* handle)
{
#if defined(_MSC_VER)
    // Windows implementation
    FreeLibrary((HMODULE) handle);
#else
    // Linux implementation
    dlclose(handle);
#endif
}
