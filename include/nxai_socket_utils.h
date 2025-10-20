#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(EXPORT_MACRO)
#define EXPORT_MACRO 
#endif

#if defined(_MSC_VER)
    // Windows specific definitions
    #include <winsock2.h>
    #include <windows.h>
typedef SOCKET nxai_socket_t;
#else
// Linux specific definitions
typedef int nxai_socket_t;
#endif

extern bool nxai_socket_interrupt_signal;

EXPORT_MACRO nxai_socket_t nxai_socket_create_listener(const char* socket_path);

EXPORT_MACRO void nxai_socket_receive_on_connection(
    nxai_socket_t connection_fd,
    size_t* allocated_buffer_size,
    char** message_input_buffer,
    uint32_t* message_length);

EXPORT_MACRO nxai_socket_t nxai_socket_await_message(
    nxai_socket_t socket_fd,
    size_t* allocated_buffer_size,
    char** message_input_buffer,
    uint32_t* message_length);

EXPORT_MACRO int32_t nxai_socket_start_listener(
    const char* socket_path,
    void (*callback_function)(const char*, uint32_t, int));

EXPORT_MACRO void nxai_delete_socket_file(const char* socket_path);

EXPORT_MACRO nxai_socket_t nxai_socket_connect(const char* socket_path);

EXPORT_MACRO void nxai_socket_send(
    const char* socket_path,
    const char* string_to_send,
    uint32_t message_length);

EXPORT_MACRO int nxai_close_socket(nxai_socket_t connection_fd);

EXPORT_MACRO uint32_t nxai_socket_send_receive_message(
    const char* socket_path,
    const char* message_to_send,
    const uint32_t sending_message_length,
    char** return_message_buffer,
    size_t* allocated_message_length);

EXPORT_MACRO bool nxai_socket_is_valid(const nxai_socket_t* socket);

EXPORT_MACRO int nxai_socket_initialize_sockets();

EXPORT_MACRO bool nxai_socket_send_to_connection(
    const nxai_socket_t connection_fd,
    const char* message_to_send,
    uint32_t message_length);

#ifdef __cplusplus
}
#endif
