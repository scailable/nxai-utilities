#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#if defined( __WIN32__ )
// Windows stuff
#include "windows.h"
#else
#include <sys/shm.h>
#include <sys/types.h>
#endif

#if defined( __WIN32__ )
typedef HANDLE nxai_pipe_t;
typedef struct {
    wchar_t key[MAX_PATH];
    HANDLE id;
} nxai_shm_t;
#else
typedef int nxai_pipe_t;
typedef struct {
    key_t key;
    int id;
} nxai_shm_t;
#endif

typedef struct bidirectional_pipe_t {
    nxai_pipe_t up_pipe[2];
    nxai_pipe_t down_pipe[2];
} bidirectional_pipe_t;

typedef enum {
    UP = 1,
    DOWN = 2
} PIPE_DIRECTION;

nxai_pipe_t nxai_pipe_get_write_pipe( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

nxai_pipe_t nxai_pipe_get_read_pipe( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

bidirectional_pipe_t nxai_initialize_pipe( nxai_pipe_t up_pipe_read, nxai_pipe_t up_pipe_write, nxai_pipe_t down_pipe_read, nxai_pipe_t down_pipe_write );

bidirectional_pipe_t nxai_create_pipe( int *error );

char nxai_pipe_read( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

char nxai_pipe_timed_read( bidirectional_pipe_t pipe, PIPE_DIRECTION direction, int timeout );

void nxai_pipe_close( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

ssize_t nxai_pipe_send( bidirectional_pipe_t pipe, PIPE_DIRECTION direction, char signal );

nxai_shm_t nxai_shm_create_random( size_t size );

bool nxai_shm_get_id( nxai_shm_t *shm );

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