#pragma once

#ifdef __cplusplus
extern "C" {
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
    #include <sys/types.h>
#endif

// Platform types
#if defined(_MSC_VER)
typedef struct
{
    OVERLAPPED overlap;
    HANDLE event;
    HANDLE handle;
    char read_buffer;
    bool active;
    bool pending;
} _nxai_pipe_t;
typedef _nxai_pipe_t* nxai_pipe_t;
#else
typedef int nxai_pipe_t;
#endif

typedef struct bidirectional_pipe_t
{
    nxai_pipe_t up_pipe[2];
    nxai_pipe_t down_pipe[2];
} bidirectional_pipe_t;

typedef enum
{
    UP = 1,
    DOWN = 2
} PIPE_DIRECTION;

char* nxai_pipe_to_string(nxai_pipe_t pipe);

nxai_pipe_t nxai_string_to_pipe(const char* str);

nxai_pipe_t nxai_pipe_get_write_pipe(bidirectional_pipe_t pipe, PIPE_DIRECTION direction);

nxai_pipe_t nxai_pipe_get_read_pipe(bidirectional_pipe_t pipe, PIPE_DIRECTION direction);

bidirectional_pipe_t nxai_initialize_pipe(
    nxai_pipe_t up_pipe_read,
    nxai_pipe_t up_pipe_write,
    nxai_pipe_t down_pipe_read,
    nxai_pipe_t down_pipe_write);

#if defined(_MSC_VER)
nxai_pipe_t nxai_create_empty_pipe();
bool nxai_create_pipe_handles(HANDLE* read_handle, HANDLE* write_handle);
#endif

bidirectional_pipe_t nxai_create_pipe(int* error);

size_t nxai_pipe_timed_read_any(
    bidirectional_pipe_t* pipes_array,
    size_t pipes_length,
    PIPE_DIRECTION direction,
    int8_t* return_byte);

char nxai_pipe_read(bidirectional_pipe_t pipe, PIPE_DIRECTION direction);

char nxai_pipe_timed_read(bidirectional_pipe_t pipe, PIPE_DIRECTION direction, int timeout);

void nxai_pipe_close(bidirectional_pipe_t pipe, PIPE_DIRECTION direction);

char* nxai_read_pipe_to_string(nxai_pipe_t pipe);

ssize_t nxai_pipe_send(bidirectional_pipe_t pipe, PIPE_DIRECTION direction, char signal);

#ifdef __cplusplus
}
#endif
