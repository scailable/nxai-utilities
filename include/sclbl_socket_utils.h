#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern bool sclbl_socket_interrupt_signal;

void sclbl_socket_start_listener( const char *socket_path, void ( *callback_function )( const char *, uint32_t ) );

void sclbl_socket_send( const char *socket_path, const char *string_to_send );

#ifdef __cplusplus
}
#endif
