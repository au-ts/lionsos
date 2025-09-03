# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

set(MUSL $ENV{MUSL})
set(CC $ENV{CC})
set(TGT $ENV{TARGET})
set(CPU $ENV{CPU})

if (TGT MATCHES "^x86_64")
  set(SYS_PROC x86_64)
elseif (TGT MATCHES "^aarch64")
  set(SYS_PROC aarch64)
else()
  set(SYS_PROC unknown)
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ${SYS_PROC})
set(CMAKE_SYSROOT ${MUSL})
set(CMAKE_C_COMPILER ${CC})
set(CMAKE_C_FLAGS_INIT "-nostdlib -mtune=${CPU}")
set(CMAKE_C_COMPILER_TARGET ${TGT})
set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_LINK_BASE "-fuse-ld=lld --target=${TGT} --sysroot=${MUSL}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_LINK_BASE}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_LINK_BASE}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_LINK_BASE}")
add_compile_options(--target=${TGT} --sysroot=${MUSL})

# Prevent CMake from finding and using libraries or programs from the host system.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
