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

## Licence

Copyright 2023, Scailable, All rights reserved.