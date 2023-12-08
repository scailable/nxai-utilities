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
