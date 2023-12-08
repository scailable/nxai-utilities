#include "sclbl_shm_utils.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Socket stuff
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/shm.h>


key_t sclbl_shm_create(char* path, int project_id, size_t size, int* shm_id){
    key_t shm_key = ftok(path, project_id);
    *shm_id = shmget(shm_key, size + 4, 0666 | IPC_CREAT);
    return shm_key;
}

void sclbl_shm_write(int shm_id, char* data, uint32_t size){
    // Attach the shared memory segment to the process's address space.
   // This is done by calling the shmat() function with the shared memory ID.
   // The function returns a pointer to the attached shared memory segment.
   // The returned pointer is cast to a uint8_t pointer, as the shared memory segment is treated as an array of bytes.
   uint8_t* shm_buffer = (uint8_t *) shmat(shm_id, NULL, 0);

   // Write the size of the data to the beginning of the shared memory segment.
   // This is done by copying the size (which is an integer) to the shared memory segment.
   // The size is copied as a 4-byte value, as the size is represented as a 32-bit unsigned integer.
   memcpy(shm_buffer, &size, 4);

   // Write the data to the shared memory segment.
   // This is done by copying the data to the shared memory segment, starting from the 4th byte,
   // as the first 4 bytes are used to store the size of the data.
   memcpy(shm_buffer + 4,data,size);

   // Detach the shared memory segment from the process's address space.
   // This is done by calling the shmdt() function with the pointer to the shared memory segment.
   shmdt(shm_buffer);
}