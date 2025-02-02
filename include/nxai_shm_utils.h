#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <sys/shm.h>
#include <sys/types.h>

bool nxai_create_pipe( int pipefd[2] );

/**
 * @brief Reads a single character from a pipe.
 *
 * This function reads a single character from the pipe associated with the provided file descriptor.
 * If the reading fails, it returns -1. Otherwise, it returns the character read.
 *
 * @param fd The file descriptor of the pipe.
 * @return Returns the character read from the pipe, or -1 if an error occurred.
 */
char nxai_pipe_read( int fd );

/**
 * @brief Reads a single character from a pipe with a timeout.
 *
 * This function reads a single character from the pipe associated with the provided file descriptor within the specified timeout period.
 * If the reading fails, it returns -1. If the timeout expires without data available, it returns 0. Otherwise, it returns the character read.
 *
 * @param fd The file descriptor of the pipe.
 * @param timeout The timeout period in seconds.
 * @return Returns the character read from the pipe, -1 if an error occurred, or 0 if the timeout expired without data available.
 */
char nxai_pipe_timed_read( int fd, int timeout );

/**
 * @brief Closes a named pipe.
 *
 * This function closes the named pipe associated with the provided file descriptor.
 * If the closing fails, it prints an error message.
 *
 * @param fd The file descriptor of the pipe to close.
 */
void nxai_pipe_close( int semaphore );

key_t nxai_shm_create_random( size_t size, int *shm_id );

/**
 * @brief Sends a single character through a pipe.
 *
 * This function writes a single character to the pipe associated with the provided file descriptor.
 *
 * @param fd The file descriptor of the pipe.
 * @param signal The character to send through the pipe.
 * @return Returns the number of characters written, or -1 if an error occurred.
 */
ssize_t nxai_pipe_send( int fd, char signal );

/**
 * @brief Retrieves a shared memory segment.
 *
 * This function retrieves a shared memory segment with a given key.
 * If the retrieval fails, it prints an error message.
 *
 * @param shm_key The key of the shared memory segment.
 * @return The id of the shared memory segment.
 */
int nxai_shm_get( key_t shm_key );

void *nxai_shm_attach( int shm_id );

void nxai_shm_write_to_attached( void *shm_buffer, const char *data, uint32_t size );

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
key_t nxai_shm_create( char *path, int project_id, size_t size, int *shm_id );

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
int nxai_shm_realloc( key_t shm_key, int old_shm_id, size_t new_size );

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