# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

set(SHADPS4_PGO "OFF" CACHE STRING
    "Profile-guided build of the emulator target (Clang): OFF, GENERATE or USE")
set_property(CACHE SHADPS4_PGO PROPERTY STRINGS OFF GENERATE USE)
set(SHADPS4_PGO_PROFILE "" CACHE FILEPATH
    "Merged .profdata for SHADPS4_PGO=USE (llvm-profdata merge of the GENERATE run's .profraw)")

function(shadps4_optimize_host_target target)
    if(ENABLE_HOST_OPTIMIZATIONS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-O3>"
            "$<$<COMPILE_LANGUAGE:CXX>:-flto=thin>")
        target_link_options(${target} PRIVATE -flto=thin)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(SHADPS4_PGO STREQUAL "GENERATE")
            # Run with LLVM_PROFILE_FILE=<dir>/%c%m.profraw: continuous mode keeps the counters
            # current even when the emulator is killed instead of exiting.
            target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fprofile-generate>")
            target_link_options(${target} PRIVATE -fprofile-generate)
        elseif(SHADPS4_PGO STREQUAL "USE")
            if(NOT EXISTS "${SHADPS4_PGO_PROFILE}")
                message(FATAL_ERROR "SHADPS4_PGO=USE needs SHADPS4_PGO_PROFILE (a .profdata file)")
            endif()
            target_compile_options(${target} PRIVATE
                "$<$<COMPILE_LANGUAGE:CXX>:-fprofile-use=${SHADPS4_PGO_PROFILE}>"
                "$<$<COMPILE_LANGUAGE:CXX>:-Wno-profile-instr-unprofiled>"
                "$<$<COMPILE_LANGUAGE:CXX>:-Wno-profile-instr-out-of-date>")
            target_link_options(${target} PRIVATE "-fprofile-use=${SHADPS4_PGO_PROFILE}")
        endif()
    endif()
endfunction()
