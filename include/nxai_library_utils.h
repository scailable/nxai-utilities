#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(EXPORT_MACRO)
#define EXPORT_MACRO 
#endif

EXPORT_MACRO void* nxai_load_library(const char* filename);

EXPORT_MACRO char* nxai_get_error(void);

EXPORT_MACRO void* nxai_load_library_with_namespace(const char* filename, int nsid);

EXPORT_MACRO void* nxai_get_library_symbol(void* handle, const char* symbol);

EXPORT_MACRO void nxai_free_library(void* handle);

#ifdef __cplusplus
}
#endif