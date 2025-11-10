# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required (VERSION 3.14)
include (CheckPIESupported)
project (iwasm)

option (BUILD_SHARED_LIBS "Build using shared libraries" OFF)

set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wformat -Wformat-security -Wshadow")
set (CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -Wno-unused-command-line-argument")
set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set (CMAKE_C_STANDARD 99)

set (WAMR_BUILD_TARGET "AARCH64")

set (WAMR_BUILD_INTERP 1)
set (WAMR_DISABLE_HW_BOUND_CHECK 1)

set (WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../dep/wasm-micro-runtime)

include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

check_pie_supported()

add_library (vmlib ${WAMR_RUNTIME_LIB_SOURCE})
set_version_info (vmlib)
target_include_directories (vmlib INTERFACE
  $<INSTALL_INTERFACE:include>
)
set_target_properties (vmlib PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
