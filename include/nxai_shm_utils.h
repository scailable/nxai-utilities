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
typedef struct {
    HANDLE up_pipe[2];
    HANDLE down_pipe[2];
} bidirectional_pipe_t;
typedef struct {
    wchar_t key[50];
    HANDLE id;
} nxai_shm_t;
#else
typedef struct bidirectional_pipe_t {
    int up_pipe[2];
    int down_pipe[2];
} bidirectional_pipe_t;
typedef struct {
    key_t key;
    int id;
} nxai_shm_t;
#endif

typedef enum {
    UP = 1,
    DOWN = 2
} PIPE_DIRECTION;

int nxai_pipe_get_write_fd( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

int nxai_pipe_get_read_fd( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

bidirectional_pipe_t nxai_initialize_pipe( int up_pipe_read, int up_pipe_write, int down_pipe_read, int down_pipe_write );

bidirectional_pipe_t nxai_create_pipe( int *error );

char nxai_pipe_read( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

char nxai_pipe_timed_read( bidirectional_pipe_t pipe, PIPE_DIRECTION direction, int timeout );

void nxai_pipe_close( bidirectional_pipe_t pipe, PIPE_DIRECTION direction );

ssize_t nxai_pipe_send( bidirectional_pipe_t pipe, PIPE_DIRECTION direction, char signal );

nxai_shm_t nxai_shm_create_random( size_t size );

nxai_shm_t nxai_shm_get( nxai_shm_t shm_data );

void *nxai_shm_attach( nxai_shm_t shm );

void nxai_shm_write_to_attached( void *shm_buffer, const char *data, uint32_t size );

nxai_shm_t nxai_shm_create( const char *path, int project_id, size_t size );

bool nxai_shm_write( int shm_id, const char *data, uint32_t size );

void nxai_shm_read_from_attached( void *shm_pointer, size_t *data_length, char **payload_data );

/**
 * @brief Reads data from shared memory.
 *
 * This function reads data from shared memory and returns a pointer to the shared memory block.
 * The first 4 bytes of the shared memory is always the size of the tensor.
 * The payload returned is a pointer to the shared memory block after the size header.
 * This process attaches the shared memory block to this process and keeps it attached.
 * Call `nxai_shm_close` when this memory is no longer in use.
 *
 * @param shm_id The shared memory ID.
 * @param data_length A pointer to a size_t variable where the function will store the size of the tensor.
 * @param payload_data A pointer to a char pointer where the function will store the pointer to the payload data.
 *
 * @return A pointer to the shared memory data.
 */
void *nxai_shm_read( int shm_id, size_t *data_length, char **payload_data );

/**
 * @brief Detaches shared memory from the current process.
 *
 * This function detaches the shared memory from the current process.
 *
 * @param memory_address A pointer to the shared memory.
 */
void nxai_shm_close( void *memory_address );

/**
 * @brief Destroys a shared memory segment.
 *
 * This function destroys a shared memory segment identified by shm_id.
 * It uses the shmctl system call with the IPC_RMID command to remove the shared memory segment.
 *
 * @param shm_id The identifier of the shared memory segment to be destroyed.
 *
 * @return The return value of the shmctl system call.
 *
 * @see shmctl
 */
int nxai_shm_destroy( int shm_id );

/**
 * \brief Reallocates shared memory.
 *
 * This function first destroys the old shared memory identified by `old_shm_id`, then creates a new shared memory with the given `shm_key` and `new_size`.
 * If the old shared memory cannot be destroyed, the function returns -1. Otherwise, it returns the identifier of the new shared memory.
 *
 * \param shm_key The key of the shared memory to be reallocated.
 * \param old_shm_id The identifier of the old shared memory to be destroyed.
 * \param new_size The size of the new shared memory to be created.
 *
 * \return The identifier of the new shared memory if successful, -1 if the old shared memory cannot be destroyed.
 */
int nxai_shm_realloc( nxai_shm_key_t shm_key, int old_shm_id, size_t new_size );

/**
 * @brief Get the size of shared memory segment
 *
 * This function retrieves the size of a shared memory segment identified by shm_id.
 * It subtracts HEADER_BYTES from the total size of the shared memory segment.
 *
 * @param shm_id Identifier of the shared memory segment
 * @return Size of the shared memory segment minus HEADER_BYTES
 */
size_t nxai_shm_get_size( int shm_id );

#ifdef __cplusplus
}
#endif