# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

add_definitions(-DBH_PLATFORM_LIONS)

set(PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${WAMR_ROOT_DIR}/core/shared/platform/include)

include (${WAMR_ROOT_DIR}/core/shared/platform/common/posix/platform_api_posix.cmake)

file (GLOB_RECURSE source_all ${PLATFORM_SHARED_DIR}/*.c)

set (PLATFORM_SHARED_SOURCE ${source_all} ${PLATFORM_COMMON_POSIX_SOURCE})

file (GLOB header ${WAMR_ROOT_DIR}/core/shared/platform/include/*.h)
LIST (APPEND RUNTIME_LIB_HEADER_LIST ${header})
