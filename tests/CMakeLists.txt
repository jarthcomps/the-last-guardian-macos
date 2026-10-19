add_executable(shadps4_push_constant_recorder_test test_push_constant_recorder.cpp)
target_include_directories(shadps4_push_constant_recorder_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(shadps4_push_constant_recorder_test PRIVATE Vulkan::Headers spdlog::spdlog)
add_test(NAME push_constant_recorder COMMAND shadps4_push_constant_recorder_test)
add_executable(shadps4_push_constant_reuse_test test_push_constant_reuse.cpp)
target_include_directories(shadps4_push_constant_reuse_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME push_constant_reuse COMMAND shadps4_push_constant_reuse_test)
add_executable(shadps4_coherent_read_test test_coherent_read.cpp)
target_include_directories(shadps4_coherent_read_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME coherent_read COMMAND shadps4_coherent_read_test)
add_executable(shadps4_combined_readback_test test_combined_readback.cpp)
target_include_directories(shadps4_combined_readback_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME combined_readback COMMAND shadps4_combined_readback_test)
add_executable(shadps4_lookup_reuse_test test_lookup_reuse.cpp)
target_include_directories(shadps4_lookup_reuse_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lookup_reuse COMMAND shadps4_lookup_reuse_test)
add_executable(shadps4_download_rescan_test test_download_rescan.cpp)
target_include_directories(shadps4_download_rescan_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME download_rescan COMMAND shadps4_download_rescan_test)
# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Find or download Google Test
add_executable(shadps4_readback_window_test test_readback_window.cpp)
target_include_directories(shadps4_readback_window_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME readback_window COMMAND shadps4_readback_window_test)
add_executable(shadps4_backing_range_test test_backing_range.cpp)
target_include_directories(shadps4_backing_range_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME backing_range COMMAND shadps4_backing_range_test)
add_executable(shadps4_download_overlap_test test_download_overlap.cpp)
target_include_directories(shadps4_download_overlap_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME download_overlap COMMAND shadps4_download_overlap_test)
add_executable(shadps4_shader_diagnostic_names_test test_shader_diagnostic_names.cpp)
target_include_directories(shadps4_shader_diagnostic_names_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME shader_diagnostic_names COMMAND shadps4_shader_diagnostic_names_test)

add_executable(shadps4_fence_submission_test test_fence_submissions.cpp)
target_include_directories(shadps4_fence_submission_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME fence_submissions COMMAND shadps4_fence_submission_test)
add_executable(shadps4_encoder_batch_test test_encoder_batch.cpp)
target_include_directories(shadps4_encoder_batch_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME encoder_batch COMMAND shadps4_encoder_batch_test)
add_executable(shadps4_pipeline_bind_cache_test test_pipeline_bind_cache.cpp)
target_include_directories(shadps4_pipeline_bind_cache_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(shadps4_pipeline_bind_cache_test PRIVATE Vulkan::Headers spdlog::spdlog)
add_test(NAME pipeline_bind_cache COMMAND shadps4_pipeline_bind_cache_test)
add_executable(shadps4_descriptor_push_memo_test test_descriptor_push_memo.cpp)
target_include_directories(shadps4_descriptor_push_memo_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(shadps4_descriptor_push_memo_test PRIVATE Vulkan::Headers spdlog::spdlog)
add_test(NAME descriptor_push_memo COMMAND shadps4_descriptor_push_memo_test)
add_executable(shadps4_copy_shader_hle_plan_test test_copy_shader_hle_plan.cpp)
target_include_directories(shadps4_copy_shader_hle_plan_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME copy_shader_hle_plan COMMAND shadps4_copy_shader_hle_plan_test)
add_executable(shadps4_single_mip_tiling_test test_single_mip_tiling.cpp)
target_include_directories(shadps4_single_mip_tiling_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME single_mip_tiling COMMAND shadps4_single_mip_tiling_test)
add_executable(shadps4_ipc_test_control_test test_ipc_test_control.cpp)
target_include_directories(shadps4_ipc_test_control_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ipc_test_control COMMAND shadps4_ipc_test_control_test)
add_executable(shadps4_fence_deferral_test test_fence_deferral.cpp)
target_include_directories(shadps4_fence_deferral_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME fence_deferral COMMAND shadps4_fence_deferral_test)
add_executable(shadps4_buffer_barrier_policy_test test_buffer_barrier_policy.cpp)
target_include_directories(shadps4_buffer_barrier_policy_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME buffer_barrier_policy COMMAND shadps4_buffer_barrier_policy_test)
add_executable(shadps4_screenshot_burst_test test_screenshot_burst.cpp)
target_include_directories(shadps4_screenshot_burst_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME screenshot_burst COMMAND shadps4_screenshot_burst_test)
add_executable(shadps4_prefetch_unmark_policy_test test_prefetch_unmark_policy.cpp)
target_include_directories(shadps4_prefetch_unmark_policy_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME prefetch_unmark_policy COMMAND shadps4_prefetch_unmark_policy_test)
add_executable(shadps4_image_upload_policy_test test_image_upload_policy.cpp)
target_include_directories(shadps4_image_upload_policy_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME image_upload_policy COMMAND shadps4_image_upload_policy_test)
add_executable(shadps4_wait_stats_test test_wait_stats.cpp)
target_include_directories(shadps4_wait_stats_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME wait_stats COMMAND shadps4_wait_stats_test)
add_executable(shadps4_eop_latency_stats_test test_eop_latency_stats.cpp)
target_include_directories(shadps4_eop_latency_stats_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME eop_latency_stats COMMAND shadps4_eop_latency_stats_test)
add_executable(shadps4_lazy_fault_context_test test_lazy_fault_context.cpp)
target_include_directories(shadps4_lazy_fault_context_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lazy_fault_context COMMAND shadps4_lazy_fault_context_test)
add_executable(shadps4_hot_write_pages_test test_hot_write_pages.cpp)
target_include_directories(shadps4_hot_write_pages_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(shadps4_hot_write_pages_test PRIVATE
    spdlog::spdlog nlohmann_json::nlohmann_json Tracy::TracyClient)
add_test(NAME hot_write_pages COMMAND shadps4_hot_write_pages_test)
add_test(NAME host_optimization_flags COMMAND ${CMAKE_COMMAND}
    -S ${CMAKE_CURRENT_SOURCE_DIR}/host_optimization_flags
    -B ${CMAKE_CURRENT_BINARY_DIR}/host_optimization_flags
    -DOPTIMIZATION_MODULE=${CMAKE_SOURCE_DIR}/cmake/HostOptimizations.cmake)

if (APPLE AND ARCHITECTURE STREQUAL "x86_64")
    add_executable(shadps4_apple_thread_priority_test test_apple_thread_priority.cpp)
    target_include_directories(shadps4_apple_thread_priority_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME apple_thread_priority COMMAND shadps4_apple_thread_priority_test)
    add_executable(shadps4_rosetta_avx_store_test test_rosetta_avx_store.cpp)
    target_include_directories(shadps4_rosetta_avx_store_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(shadps4_rosetta_avx_store_test PRIVATE xbyak::xbyak)
    add_test(NAME rosetta_avx_store COMMAND shadps4_rosetta_avx_store_test)
    add_executable(shadps4_pm4_address_test test_pm4_addresses.cpp)
    target_include_directories(shadps4_pm4_address_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(shadps4_pm4_address_test PRIVATE
        Boost::headers spdlog::spdlog magic_enum::magic_enum)
    add_test(NAME pm4_addresses COMMAND shadps4_pm4_address_test)
endif()

if (APPLE)
    add_executable(shadps4_apple_guest_thread_qos_test test_apple_guest_thread_qos.cpp)
    target_include_directories(shadps4_apple_guest_thread_qos_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME apple_guest_thread_qos COMMAND shadps4_apple_guest_thread_qos_test)
endif()

include(FetchContent)
    FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)

set(TEST_TARGETS
    shadps4_settings_test
    shadps4_ngs2_test
    shadps4_gcn_test
)

set(SETTINGS_TEST_SOURCES
    # Under test
    ${CMAKE_SOURCE_DIR}/src/core/emulator_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/emulator_state.cpp

    # Minimal common support
    ${CMAKE_SOURCE_DIR}/src/common/path_util.cpp
    ${CMAKE_SOURCE_DIR}/src/common/assert.cpp
    ${CMAKE_SOURCE_DIR}/src/common/error.cpp
    ${CMAKE_SOURCE_DIR}/src/common/string_util.cpp
    ${CMAKE_SOURCE_DIR}/src/common/logging/log.cpp

    # Stubs that replace dependencies
    stubs/common_stub.cpp
    stubs/core_stub.cpp
    stubs/signals_stub.cpp
    stubs/file_sys_stub.cpp
    stubs/scm_rev_stub.cpp
    stubs/sdl_stub.cpp

    # Tests
    test_emulator_settings.cpp
    # Windows static guest red-zone protection
    test_windows_guest_red_zone_protection_settings.cpp
)

set(NGS2_TEST_SOURCES
    # Under test
    ${CMAKE_SOURCE_DIR}/src/core/libraries/ngs2/ngs2_impl.cpp

    # Minimal common and kernel support
    ${CMAKE_SOURCE_DIR}/src/core/emulator_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/emulator_state.cpp
    ${CMAKE_SOURCE_DIR}/src/core/libraries/kernel/sync/mutex.cpp
    ${CMAKE_SOURCE_DIR}/src/common/path_util.cpp
    ${CMAKE_SOURCE_DIR}/src/common/assert.cpp
    ${CMAKE_SOURCE_DIR}/src/common/error.cpp
    ${CMAKE_SOURCE_DIR}/src/common/string_util.cpp
    ${CMAKE_SOURCE_DIR}/src/common/logging/log.cpp

    # Stubs that replace dependencies
    stubs/common_stub.cpp
    stubs/core_stub.cpp
    stubs/signals_stub.cpp
    stubs/file_sys_stub.cpp
    stubs/scm_rev_stub.cpp
    stubs/sdl_stub.cpp

    # Tests
    test_ngs2.cpp
)
set(GCN_TEST_SOURCES
    # Under test
    ${CMAKE_SOURCE_DIR}/src/core/emulator_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/emulator_state.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_atomic.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_barriers.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_bitwise_conversion.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_composite.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_context_get_set.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_convert.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_floating_point.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_image.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_integer.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_logical.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_select.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_shared_memory.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_special.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_undefined.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_warp.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/emit_spirv_instructions.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/spirv_emit_context.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/backend/spirv/spirv_emit_context.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/format.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/decode.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/decode.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/fetch_shader.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/fetch_shader.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/data_share.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/export.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/translate.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/translate.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/scalar_alu.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/scalar_flow.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/scalar_memory.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/vector_alu.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/vector_interpolation.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/frontend/translate/vector_memory.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/abstract_syntax_list.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/abstract_syntax_list.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/basic_block.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/basic_block.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/microinstruction.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/attribute.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/attribute.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/ir_emitter.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/ir_emitter.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/opcodes.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/opcodes.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/patch.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/patch.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/program.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/program.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/passes/ir_passes.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/passes/constant_propagation_pass.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/passes/dead_code_elimination_pass.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/passes/shader_info_collection_pass.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/passes/ssa_rewrite_pass.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/post_order.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/post_order.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/type.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/type.h
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/value.cpp
    ${CMAKE_SOURCE_DIR}/src/shader_recompiler/ir/value.h
    ${CMAKE_SOURCE_DIR}/src/video_core/amdgpu/pixel_format.cpp
    ${CMAKE_SOURCE_DIR}/src/video_core/amdgpu/pixel_format.h
    ${CMAKE_SOURCE_DIR}/src/video_core/amdgpu/resource.h

    # Minimal common support
    ${CMAKE_SOURCE_DIR}/src/common/path_util.cpp
    ${CMAKE_SOURCE_DIR}/src/common/assert.cpp
    ${CMAKE_SOURCE_DIR}/src/common/error.cpp
    ${CMAKE_SOURCE_DIR}/src/common/io_file.cpp
    ${CMAKE_SOURCE_DIR}/src/common/string_util.cpp
    ${CMAKE_SOURCE_DIR}/src/common/logging/log.cpp
    ${CMAKE_SOURCE_DIR}/src/common/ntapi.cpp

    # Stubs that replace dependencies
    stubs/common_stub.cpp
    stubs/resource_tracking_pass_stub.cpp
    stubs/scm_rev_stub.cpp
    stubs/sdl_stub.cpp
    stubs/core_stub.cpp
    stubs/signals_stub.cpp
    stubs/file_sys_stub.cpp

    gcn/gcn_test_runner.hpp
    gcn/gcn_test_runner.cpp
    gcn/translator.hpp
    gcn/translator.cpp

    # Tests
    gcn/test_gcn_instructions.cpp
)

add_executable(shadps4_settings_test ${SETTINGS_TEST_SOURCES})
add_executable(shadps4_ngs2_test ${NGS2_TEST_SOURCES})
add_executable(shadps4_gcn_test ${GCN_TEST_SOURCES})

foreach(t ${TEST_TARGETS})
    target_include_directories(${t} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
    )
    target_compile_features(${t} PRIVATE cxx_std_23)
    target_compile_definitions(${t} PRIVATE BOOST_ASIO_STANDALONE)
endforeach()

target_link_libraries(shadps4_settings_test PRIVATE
    GTest::gtest_main
    fmt::fmt
    nlohmann_json::nlohmann_json
    toml11::toml11
    SDL3::SDL3
    spdlog::spdlog
)
target_link_libraries(shadps4_ngs2_test PRIVATE
    GTest::gtest_main
    fmt::fmt
    nlohmann_json::nlohmann_json
    toml11::toml11
    SDL3::SDL3
    spdlog::spdlog
)
target_link_libraries(shadps4_gcn_test PRIVATE
    GTest::gtest_main
    fmt::fmt
    nlohmann_json::nlohmann_json
    magic_enum::magic_enum
    toml11::toml11
    Boost::headers
    Vulkan::Headers
    sirit
    SDL3::SDL3
    spdlog::spdlog
    half::half
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
    CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    include(CheckCXXSymbolExists)
    check_cxx_symbol_exists(_LIBCPP_VERSION version LIBCPP)
    if (LIBCPP)
        foreach(t ${TEST_TARGETS})
            target_compile_options(${t} PRIVATE -fexperimental-library)
        endforeach()
    endif()
endif()

foreach(t ${TEST_TARGETS})
    if (WIN32)
        target_link_libraries(${t} PRIVATE onecore)
        target_compile_definitions(${t} PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            NTDDI_VERSION=0x0A000006
            _WIN32_WINNT=0x0A00
            WINVER=0x0A00
        )
        if (MSVC)
            target_compile_definitions(${t} PRIVATE
                _CRT_SECURE_NO_WARNINGS
                _CRT_NONSTDC_NO_DEPRECATE
                _SCL_SECURE_NO_WARNINGS
                _TIMESPEC_DEFINED
            )
        endif()
    endif()
endforeach()

include(GoogleTest)

foreach(t ${TEST_TARGETS})
    gtest_discover_tests(${t}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_TIMEOUT 60
    )
endforeach()

# ===========================================================================
# HTTP tests (libSceHttp - URI helpers and status-line parsing)
# ===========================================================================
# Self-contained block - mirrors the structure of the targets above so the
# new test does not depend on any earlier foreach iterations.

set(HTTP_TEST_SOURCES
    # Under test
    ${CMAKE_SOURCE_DIR}/src/core/libraries/network/http.cpp
    # Required to link RegisterLib's LIB_FUNCTION calls and the logger's
    # access to EmulatorSettings.
    ${CMAKE_SOURCE_DIR}/src/core/emulator_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/emulator_state.cpp

    # Minimal common support
    ${CMAKE_SOURCE_DIR}/src/common/path_util.cpp
    ${CMAKE_SOURCE_DIR}/src/common/assert.cpp
    ${CMAKE_SOURCE_DIR}/src/common/error.cpp
    ${CMAKE_SOURCE_DIR}/src/common/string_util.cpp
    ${CMAKE_SOURCE_DIR}/src/common/logging/log.cpp

    # Stubs that replace dependencies
    stubs/common_stub.cpp
    stubs/scm_rev_stub.cpp
    stubs/sdl_stub.cpp
    stubs/loader_stub.cpp
    stubs/core_stub.cpp
    stubs/signals_stub.cpp
    stubs/file_sys_stub.cpp
    stubs/kernel_stub.cpp

    # Tests
    network/test_http_uri.cpp
    network/test_http_status_line.cpp
    network/test_http_parse_response_header.cpp
    network/test_http_lifecycle.cpp
)

add_executable(shadps4_http_test ${HTTP_TEST_SOURCES})

list(APPEND TEST_TARGETS shadps4_http_test)

target_include_directories(shadps4_http_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
)
target_compile_features(shadps4_http_test PRIVATE cxx_std_23)
target_compile_definitions(shadps4_http_test PRIVATE BOOST_ASIO_STANDALONE)

target_link_libraries(shadps4_http_test PRIVATE
    GTest::gtest_main
    fmt::fmt
    magic_enum::magic_enum
    nlohmann_json::nlohmann_json
    toml11::toml11
    SDL3::SDL3
    spdlog::spdlog
    ZLIB::ZLIB
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
    CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    include(CheckCXXSymbolExists)
    check_cxx_symbol_exists(_LIBCPP_VERSION version LIBCPP)
    if (LIBCPP)
        target_compile_options(shadps4_http_test PRIVATE -fexperimental-library)
    endif()
endif()

if (WIN32)
    target_link_libraries(shadps4_http_test PRIVATE onecore)
    target_compile_definitions(shadps4_http_test PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        NTDDI_VERSION=0x0A000006
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
    if (MSVC)
        target_compile_definitions(shadps4_http_test PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
            _SCL_SECURE_NO_WARNINGS
            _TIMESPEC_DEFINED
        )
    endif()
endif()

gtest_discover_tests(shadps4_http_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
)
add_executable(shadps4_barrier_stats_test test_barrier_stats.cpp)
target_include_directories(shadps4_barrier_stats_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME barrier_stats COMMAND shadps4_barrier_stats_test)
add_executable(shadps4_image_barrier_policy_test test_image_barrier_policy.cpp)
target_include_directories(shadps4_image_barrier_policy_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME image_barrier_policy COMMAND shadps4_image_barrier_policy_test)
add_executable(shadps4_buffer_hazard_tracker_test test_buffer_hazard_tracker.cpp)
target_include_directories(shadps4_buffer_hazard_tracker_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME buffer_hazard_tracker COMMAND shadps4_buffer_hazard_tracker_test)
add_executable(shadps4_bit_array_model_test test_bit_array_model.cpp)
target_include_directories(shadps4_bit_array_model_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME bit_array_model COMMAND shadps4_bit_array_model_test)

foreach(experiment render_state rendering_recorder dynamic_state_reuse compute_dispatch_labels range_protection)
    add_executable(shadps4_${experiment}_test test_${experiment}.cpp)
    target_include_directories(shadps4_${experiment}_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(shadps4_${experiment}_test PRIVATE
        Vulkan::Headers spdlog::spdlog nlohmann_json::nlohmann_json Tracy::TracyClient)
    target_compile_options(shadps4_${experiment}_test PRIVATE -UNDEBUG)
    add_test(NAME ${experiment} COMMAND shadps4_${experiment}_test)
endforeach()
add_test(NAME compute_dispatch_labels_disabled COMMAND shadps4_compute_dispatch_labels_test disabled)
if(EXISTS "${CMAKE_SOURCE_DIR}/externals/mesa-kosmickrisp/externals/mesa/src/kosmickrisp/vulkan/kk_upload_cache_policy.h")
    add_executable(shadps4_upload_cache_policy_test test_upload_cache_policy.c)
    target_include_directories(shadps4_upload_cache_policy_test PRIVATE
        ${CMAKE_SOURCE_DIR}/externals/mesa-kosmickrisp/externals/mesa/src/kosmickrisp/vulkan)
    target_compile_options(shadps4_upload_cache_policy_test PRIVATE -UNDEBUG)
    add_test(NAME upload_cache_policy COMMAND shadps4_upload_cache_policy_test)
endif()
add_executable(shadps4_frame_path_stats_test test_frame_path_stats.cpp)
target_include_directories(shadps4_frame_path_stats_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME frame_path_stats COMMAND shadps4_frame_path_stats_test)
add_executable(shadps4_early_label_release_test test_early_label_release.cpp)
target_include_directories(shadps4_early_label_release_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME early_label_release COMMAND shadps4_early_label_release_test)
add_executable(shadps4_env_switch_test test_env_switch.cpp)
target_include_directories(shadps4_env_switch_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME env_switch COMMAND shadps4_env_switch_test)
