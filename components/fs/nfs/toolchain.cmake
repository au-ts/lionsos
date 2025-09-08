# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

set(MUSL $ENV{MUSL})
set(CC $ENV{CC})
set(TGT $ENV{TARGET})
set(CPU $ENV{CPU})

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSROOT ${MUSL})
set(CMAKE_C_COMPILER ${CC})
set(CMAKE_C_FLAGS "-nostdlib -mtune=${CPU} -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses" CACHE STRING "" FORCE)
set(CMAKE_C_COMPILER_TARGET ${TGT})
set(CMAKE_CROSSCOMPILING TRUE)

# Prevent CMake from finding and using libraries or programs from the host system.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
