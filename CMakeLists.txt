cmake_minimum_required(VERSION 3.18)

project(NxaiCUtilities)

# Include GNUInstallDirs to get standard installation paths like lib, bin, etc.
# This makes the install logic cross-platform by default.
include(GNUInstallDirs)

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
    $<INSTALL_INTERFACE:include>  # This line is important for the install target
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

# =============================================================================
# INSTALLATION LOGIC
# =============================================================================

# 1. Define a configurable variable for the Python installation directory.
#    This allows the user to specify a custom path during configuration.
#    The default is "", relative to the CMAKE_INSTALL_PREFIX.
set(INSTALL_DIR "." CACHE PATH "Directory to install the Python bindings into")
message(STATUS "Python bindings will be installed to: ${CMAKE_INSTALL_PREFIX}/${INSTALL_DIR}")

# 2. Install the library targets.
#    Using TARGETS is the modern way to do this. It correctly handles different
#    file names and locations across platforms (e.g., .dll, .so, .dylib) and
#    build configurations.

install(TARGETS nxai-c-utilities-shared DESTINATION ${INSTALL_DIR})

# 3. Install the Python file.
set(PYTHON_BINDING_FILE ${CMAKE_CURRENT_SOURCE_DIR}/python-utilities/nxai_communication_utils.py)

if(EXISTS ${PYTHON_BINDING_FILE})
    install(FILES ${PYTHON_BINDING_FILE}
        DESTINATION ${CMAKE_INSTALL_PREFIX}/${INSTALL_DIR}
    )
    message(STATUS "Found Python binding file to install: ${PYTHON_BINDING_FILE}")
else()
    message(WARNING "Python binding file not found at: ${PYTHON_BINDING_FILE}. It will not be installed.")
endif()
