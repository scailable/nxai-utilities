#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// Platform imports
#if defined( _MSC_VER )
#include "winsock2.h"
#include "windows.h"
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/shm.h>
#include <sys/types.h>
#endif

// Platform types
#if defined( _MSC_VER )
typedef HANDLE shm_id_t;
typedef LPSTR shm_key_t;
#else
typedef int shm_id_t;
typedef key_t shm_key_t;
#endif
typedef struct {
    shm_key_t key;
    shm_id_t id;
} nxai_shm_t;

char *nxai_shm_key_to_string( nxai_shm_t shm );

void nxai_shm_key_from_string( nxai_shm_t *shm, const char *str );

char *nxai_shm_id_to_string( nxai_shm_t shm );

nxai_shm_t nxai_shm_id_from_string( const char *str );

nxai_shm_t nxai_shm_create_random( size_t size );

bool nxai_shm_get_id( nxai_shm_t *shm );

bool nxai_shm_valid( void *shm_buffer );

void *nxai_shm_attach( nxai_shm_t shm );

void nxai_shm_write_to_attached( void *shm_buffer, const char *data, uint32_t size );

nxai_shm_t nxai_shm_create( const char *path, int project_id, size_t size );

bool nxai_shm_write( const nxai_shm_t *shm, const char *data, uint32_t size );

void nxai_shm_read_from_attached( void *shm_pointer, size_t *data_length, char **payload_data );

void *nxai_shm_read( nxai_shm_t *shm, size_t *data_length, char **payload_data );

/**
 * @brief Detaches shared memory from the current process.
 *
 * This function detaches the shared memory from the current process.
 *
 * @param memory_address A pointer to the shared memory.
 */
void nxai_shm_close( void *memory_address );

int nxai_shm_destroy( const nxai_shm_t *shm );

bool nxai_shm_realloc( nxai_shm_t *shm, size_t new_size );

size_t nxai_shm_get_size( nxai_shm_t *shm );

#ifdef __cplusplus
}
#endif