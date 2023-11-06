#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern bool sclbl_socket_interrupt_signal;

void sclbl_socket_start_listener( const char *socket_path, void ( *callback_function )( const char *, uint32_t , int) );

void sclbl_socket_send( const char *socket_path, const char *string_to_send, uint32_t message_length );

char* sclbl_socket_send_receive_message(const char *socket_path, const char *string_to_send, const uint32_t output_message_length);

#ifdef __cplusplus
}
#endif
