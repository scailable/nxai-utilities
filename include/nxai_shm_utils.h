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
typedef struct {
    OVERLAPPED overlap;
    HANDLE event;
    HANDLE handle;
    char read_buffer;
    bool active;
} _nxai_pipe_t;
typedef _nxai_pipe_t *nxai_pipe_t;
#define NXAI_PIPE_INITIALIZER { { 0 }, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, 0, false }
typedef HANDLE shm_id_t;
typedef LPSTR shm_key_t;
#else
typedef int nxai_pipe_t;
typedef int shm_id_t;
typedef key_t shm_key_t;
#endif
typedef struct {
    shm_key_t key;
    shm_id_t id;
} nxai_shm_t;

typedef struct bidirectional_pipe_t {
    nxai_pipe_t up_pipe[2];
    nxai_pipe_t down_pipe[2];
} bidirectional_pipe_t;

typedef enum {
    UP = 1,
    DOWN = 2
} PIPE_DIRECTION;

char *nxai_shm_key_to_string( nxai_shm_t shm );

void nxai_shm_key_from_string( nxai_shm_t *shm, const char *str );

char *nxai_shm_id_to_string( nxai_shm_t shm );

nxai_shm_t nxai_shm_id_from_string( const char *str );

char *nxai_sprintf( size_t initial_size, char *fmt, ... );

char *nxai_pointer_to_string( void *pointer );

char *nxai_pipe_to_string( nxai_pipe_t pipe );

nxai_pipe_t nxai_string_to_pipe( const char *str );

nxai_pipe_t nxai_pipe_get_write_pipe( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

nxai_pipe_t nxai_pipe_get_read_pipe( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

bidirectional_pipe_t nxai_initialize_pipe( nxai_pipe_t up_pipe_read, nxai_pipe_t up_pipe_write, nxai_pipe_t down_pipe_read, nxai_pipe_t down_pipe_write );

#if defined( _MSC_VER )
nxai_pipe_t nxai_create_empty_pipe();
bool nxai_create_pipe_handles( HANDLE *read_handle, HANDLE *write_handle );
#endif

bidirectional_pipe_t nxai_create_pipe( int *error );

size_t nxai_pipe_timed_read_any( bidirectional_pipe_t *pipes_array, size_t pipes_length, PIPE_DIRECTION direction, int8_t *return_byte );

char nxai_pipe_read( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

char nxai_pipe_timed_read( bidirectional_pipe_t pipe, PIPE_DIRECTION direction, int timeout );

void nxai_pipe_close( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

ssize_t nxai_pipe_send( bidirectional_pipe_t pipe, PIPE_DIRECTION direction, char signal );

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