#include "nxai_shm_utils.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef NXAI_DEBUG
#include "memory_leak_detector.h"
#endif

#ifdef __MUSL__
// musl crosscompiler doesn't find time.h otherwise
#include "musl_time.h"
#else
#include <time.h>
#endif

// Pipe stuff
#include <sys/select.h>

// SHM stuff
#include <fcntl.h>
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/stat.h>

#define HEADER_BYTES 4

bool nxai_create_pipe( int pipefd[2] ) {
    int result = pipe( pipefd );
    if ( result == -1 ) {
        perror( "pipe failed" );
        return false;
    }
    return true;
}

ssize_t nxai_pipe_send( int fd, char signal ) {
    return write( fd, &signal, 1 );
}

char nxai_pipe_read( int fd ) {
    // Read from FIFO
    char response;
    ssize_t bytes_read = read( fd, &response, 1 );
    if ( bytes_read == -1 ) {
        return -1;
    }
    return response;
}

char nxai_pipe_timed_read( int fd, int timeout ) {
    fd_set set;
    struct timeval tv;
    int rv;
    char buff[1];

    FD_ZERO( &set );
    FD_SET( fd, &set );

    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    rv = select( fd + 1, &set, NULL, NULL, &tv );
    if ( rv == -1 ) {
        perror( "select" );
        return -1;
    } else if ( rv == 0 ) {
        return 0;
    }
    read( fd, buff, 1 );
    return buff[0];
}

void nxai_pipe_close( int fd ) {
    if ( close( fd ) == -1 ) {
        printf( "Could not close pipe: %s\n", strerror( errno ) );
    }
}

key_t nxai_shm_create_random( size_t size, int *shm_id ) {
    int new_id = -1;
    key_t shm_key;
    // Keep trying random keys until unused is found
    while ( new_id == -1 ) {
        shm_key = rand();
        new_id = shmget( shm_key, size + HEADER_BYTES, 0666 | IPC_CREAT | IPC_EXCL );
    }
    *shm_id = new_id;
    return shm_key;
}

key_t nxai_shm_create( char *path, int project_id, size_t size, int *shm_id ) {
    key_t shm_key = ftok( path, project_id );
    *shm_id = shmget( shm_key, size + HEADER_BYTES, 0666 | IPC_CREAT );
    if ( *shm_id == -1 ) {
        perror( "Failed to create SHM:" );
    }
    return shm_key;
}

int nxai_shm_get( key_t shm_key ) {
    int shm_id = shmget( shm_key, 0, 0 );
    if ( shm_id == -1 ) {
        printf( "Could not get SHM %d : %s\n", __LINE__, strerror( errno ) );
    }
    return shm_id;
}

void *nxai_shm_attach( int shm_id ) {
    // Attach the shared memory segment to the process's address space.
    // This is done by calling the shmat() function with the shared memory ID.
    // The function returns a pointer to the attached shared memory segment.
    void *result = shmat( shm_id, NULL, 0 );
    return result;
}

void nxai_shm_write_to_attached( void *shm_buffer, const char *data, uint32_t size ) {
    // Write the size of the data to the beginning of the shared memory segment.
    // This is done by copying the size (which is an integer) to the shared memory segment.
    // The size is copied as a 4-byte value, as the size is represented as a 32-bit unsigned integer.
    memcpy( shm_buffer, &size, HEADER_BYTES );

    // Write the data to the shared memory segment.
    // This is done by copying the data to the shared memory segment, starting from the 4th byte,
    // as the first 4 bytes are used to store the size of the data.
    memcpy( shm_buffer + HEADER_BYTES, data, size );
}

bool nxai_shm_write( int shm_id, const char *data, uint32_t size ) {

    void *result = nxai_shm_attach( shm_id );
    if ( result == (void *) -1 ) {
        return false;
    }

    nxai_shm_write_to_attached( result, data, size );

    // Detach the shared memory segment from the process's address space.
    // This is done by calling the shmdt() function with the pointer to the shared memory segment.
    shmdt( result );

    return true;
}

void nxai_shm_read_from_attached( void *shm_pointer, size_t *data_length, char **payload_data ) {
    // The first 4 bytes of the shared memory is always the size of the tensor
    uint32_t size;
    memcpy( &size, shm_pointer, HEADER_BYTES );
    *data_length = (size_t) size;
    // Return pointer to the payload data after the size header
    *payload_data = (char *) shm_pointer + HEADER_BYTES;
}

void *nxai_shm_read( int shm_id, size_t *data_length, char **payload_data ) {
    void *shm_pointer = shmat( shm_id, NULL, 0 );
    if ( shm_pointer == (void *) -1 ) {
        return NULL;
    }
    nxai_shm_read_from_attached( shm_pointer, data_length, payload_data );
    // Return pointer to data after size
    return shm_pointer;
}

void nxai_shm_close( void *memory_address ) {
    // Detach memory from this process
    shmdt( memory_address );
}

int nxai_shm_destroy( int shm_id ) {
    return shmctl( shm_id, IPC_RMID, NULL );
}

int nxai_shm_realloc( key_t shm_key, int old_shm_id, size_t new_size ) {

    // Remove old SHM
    if ( nxai_shm_destroy( old_shm_id ) != 0 ) {
        return -1;
    }

    int new_shm_id = shmget( shm_key, new_size + HEADER_BYTES, 0666 | IPC_CREAT );

    return new_shm_id;
}

size_t nxai_shm_get_size( int shm_id ) {
    struct shmid_ds buf;
    shmctl( shm_id, IPC_STAT, &buf );
    return buf.shm_segsz - HEADER_BYTES;
}