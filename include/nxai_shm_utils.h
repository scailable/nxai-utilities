#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(EXPORT_MACRO)
#define EXPORT_MACRO 
#endif

#include <stdbool.h>
#include <stdint.h>

// Platform imports
#if defined(_MSC_VER)
    #include <basetsd.h>

    #include "winsock2.h"
    #include "windows.h"
typedef SSIZE_T ssize_t;
#else
    #include <sys/shm.h>
    #include <sys/types.h>
#endif

// Platform types
#if defined(_MSC_VER)
typedef HANDLE shm_id_t;
typedef LPSTR shm_key_t;
#else
typedef int shm_id_t;
typedef key_t shm_key_t;
#endif
typedef struct
{
    shm_key_t key;
    shm_id_t id;
} nxai_shm_t;

EXPORT_MACRO char* nxai_shm_key_to_string(nxai_shm_t shm);

EXPORT_MACRO void nxai_shm_key_from_string(nxai_shm_t* shm, const char* str);

EXPORT_MACRO char* nxai_shm_id_to_string(nxai_shm_t shm);

EXPORT_MACRO nxai_shm_t nxai_shm_id_from_string(const char* str);

EXPORT_MACRO bool nxai_shm_is_valid(const nxai_shm_t* shm);

EXPORT_MACRO nxai_shm_t nxai_shm_create_random(size_t size);

EXPORT_MACRO bool nxai_shm_get_id(nxai_shm_t* shm);

EXPORT_MACRO bool nxai_shm_pointer_valid(void* shm_buffer);

EXPORT_MACRO void* nxai_shm_attach(nxai_shm_t shm);

EXPORT_MACRO void nxai_shm_write_to_attached(void* shm_buffer, const char* data, uint32_t size);

EXPORT_MACRO nxai_shm_t nxai_shm_create(const char* path, int project_id, size_t size);

EXPORT_MACRO bool nxai_shm_write(const nxai_shm_t* shm, const char* data, uint32_t size);

EXPORT_MACRO void nxai_shm_read_from_attached(void* shm_pointer, size_t* data_length, char** payload_data);

EXPORT_MACRO void* nxai_shm_read(nxai_shm_t* shm, size_t* data_length, char** payload_data);

EXPORT_MACRO void nxai_shm_close(void* memory_address);

EXPORT_MACRO int nxai_shm_destroy(const nxai_shm_t* shm);

EXPORT_MACRO bool nxai_shm_realloc(nxai_shm_t* shm, size_t new_size);

EXPORT_MACRO size_t nxai_shm_get_size(nxai_shm_t* shm);

#ifdef __cplusplus
}
#endif
