cmake_minimum_required(VERSION 3.18)

project(NxaiCUtilities)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(nxai-c-utilities
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nxai_socket_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nxai_shm_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nxai_process_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mpack.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/yyjson.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nxai_data_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nxai_library_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nxai_threading_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nxai_pipe_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nxai_utils.c
)