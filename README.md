# sclbl-utilities

## Context

This project serves to provide common used utility functions across multiple Scailable applications. Placing these commonly used functions in a single source library reduces code duplication, bugs and maintenance costs.

The design is to have multiple libraries which can be used from multiple projects, and can be extended in the future.

Currently the projects include:

- Sclbl C Utilities. A C/C++ based library which can be imported in any C/C++ application to provide convenience functions as well as interface functions for the Scailable Edge AI Manager.

## Requirements

### To Compile

The application can be compiled on any architecture in a Linux environment.

For easy compilation, ensure `CMake` is installed. On Debian based systems:

```sudo apt install cmake```

In the source directory,

Create folder for build:\
```mkdir build && cd build```\
Configure build:\
```cmake ..```\
Compile library:\
```cmake --build .```

## Add to your project

To add this library to your CMake project, add the following to the `CMakeLists.txt`:

``` cmake
# Add Scailable C Utilities library
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/path/to/sclbl-utilities)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/path/to/sclbl-utilities/include)
target_link_libraries(<target> sclbl-c-utilities)
```

- `add_subdirectory` tells your project where to find the Scailable C Utilities library.
- `include_directores` tells your project where to find the header files. This allows you to call the utility functions from your source files.
- `target_link_libraries` links the library to your target. This also ensures that the library will be built when you build your target.

## Licence

Copyright 2023, Scailable, All rights reserved.