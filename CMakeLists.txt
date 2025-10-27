cmake_minimum_required(VERSION 3.18)

project(NxaiCUtilities)

# -----------------------------------------------------------------------------
# Project Sources
# -----------------------------------------------------------------------------
set(NXAI_SOURCES
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

# -----------------------------------------------------------------------------
# Library Targets
# -----------------------------------------------------------------------------
# This is the "object library" target: compiles the sources only once
add_library(object_library OBJECT ${NXAI_SOURCES})

# Shared libraries need Position Independent Code (PIC)
set_property(TARGET object_library PROPERTY POSITION_INDEPENDENT_CODE 1)

# Set include directories for any target that uses these objects
target_include_directories(object_library PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(WIN32)
    set(EXPORT_MACRO "__declspec(dllexport)")
else()
    set(EXPORT_MACRO "__attribute__((visibility(\"default\")))")
endif()
target_compile_definitions(object_library PRIVATE EXPORT_MACRO=${EXPORT_MACRO}) # < for nxLibContext()

# Create the shared and static libraries from the same compiled object files
add_library(nxai-c-utilities-shared SHARED $<TARGET_OBJECTS:object_library>)
add_library(nxai-c-utilities STATIC $<TARGET_OBJECTS:object_library>)

