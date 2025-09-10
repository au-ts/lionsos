# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

set(LIBC $ENV{LIBNFS_LIBC})
set(CC $ENV{CC})
set(TGT $ENV{TARGET})
set(CPU $ENV{CPU})

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSROOT ${LIBC})
set(CMAKE_C_COMPILER ${CC})
set(CMAKE_C_FLAGS "-nostdlib -mtune=${CPU} -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses" CACHE STRING "" FORCE)
set(CMAKE_C_COMPILER_TARGET ${TGT})
set(CMAKE_CROSSCOMPILING TRUE)

set(_LINK_BASE "-fuse-ld=lld --target=${TGT} --sysroot=${MUSL}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_LINK_BASE}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_LINK_BASE}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_LINK_BASE}")

# Prevent CMake from finding and using libraries or programs from the host system.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
