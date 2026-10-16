# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

function(shadps4_optimize_host_target target)
    if(ENABLE_HOST_OPTIMIZATIONS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-O3>"
            "$<$<COMPILE_LANGUAGE:CXX>:-flto=thin>")
        target_link_options(${target} PRIVATE -flto=thin)
    endif()
endfunction()
