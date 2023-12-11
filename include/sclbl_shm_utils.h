#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/shm.h>

#ifdef __cplusplus
}
#endif

/**
 * @brief Creates a shared memory segment.
 *
 * This function generates a shared memory segment with the specified size.
 * It uses the ftok function to generate a unique key for the shared memory segment
 * and the shmget function to create the shared memory segment.
 *
 * @param path The pathname to be used in generating the key.
 * @param project_id The project identifier to be used in generating the key.
 * @param size The size of the shared memory segment to be created.
 * @param shm_id A pointer to an integer where the shared memory segment ID will be stored.
 *
 * @return The key used to create the shared memory segment.
 */
key_t sclbl_shm_create( char *path, int project_id, size_t size, int *shm_id );

/**
 * @brief Writes data to a shared memory segment.
 *
 * This function attaches a shared memory segment to the process's address space, writes the size of the data and the data itself to the shared memory segment, and then detaches the shared memory segment from the process's address space.
 *
 * @param shm_id The ID of the shared memory segment.
 * @param data The data to be written to the shared memory segment.
 * @param size The size of the data to be written to the shared memory segment.
 *
 * @note This function does not check if the shared memory segment is large enough to hold the data. It is the responsibility of the caller to ensure this.
 */
void sclbl_shm_write( int shm_id, char *data, uint32_t size );

/**
 * @brief Reads data from shared memory.
 *
 * This function reads data from shared memory and returns a pointer to the shared memory block.
 * The first 4 bytes of the shared memory is always the size of the tensor.
 * The payload returned is a pointer to the shared memory block after the size header.
 * This process attaches the shared memory block to this process and keeps it attached.
 * Call `sclbl_shm_close` when this memory is no longer in use.
 *
 * @param shm_id The shared memory ID.
 * @param data_length A pointer to a size_t variable where the function will store the size of the tensor.
 * @param payload_data A pointer to a char pointer where the function will store the pointer to the payload data.
 *
 * @return A pointer to the shared memory data.
 */
void *sclbl_shm_read( int shm_id, size_t *data_length, char **payload_data );

/**
 * @brief Detaches shared memory from the current process.
 *
 * This function detaches the shared memory from the current process.
 *
 * @param memory_address A pointer to the shared memory.
 */
void sclbl_shm_close( void *memory_address );

int sclbl_shm_destroy( int shm_id );

int sclbl_shm_realloc( key_t shm_key, int old_shm_id, size_t new_size );
