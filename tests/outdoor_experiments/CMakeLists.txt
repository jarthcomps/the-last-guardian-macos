cmake_minimum_required(VERSION 3.25)
project(OutdoorExperiments LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
enable_testing()
get_filename_component(ENGINE "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_FMT "${ENGINE}/externals/fmt" CACHE PATH "" FORCE)
add_subdirectory("${ENGINE}/externals/spdlog" spdlog)
foreach(name render_state rendering_recorder dynamic_state_reuse compute_dispatch_labels range_protection hot_write_pages
             descriptor_push_memo pipeline_bind_cache push_constant_reuse push_constant_recorder
             backing_range download_overlap prefetch_unmark_policy readback_window)
    add_executable(${name} "${ENGINE}/tests/test_${name}.cpp")
    target_include_directories(${name} PRIVATE "${ENGINE}/src"
      "${ENGINE}/externals/Vulkan-Headers/include" "${ENGINE}/externals/tracy/public"
      "${ENGINE}/externals/json/include")
    target_compile_options(${name} PRIVATE -UNDEBUG)
    target_link_libraries(${name} PRIVATE spdlog::spdlog)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
add_test(NAME compute_labels_disabled COMMAND compute_dispatch_labels disabled)
add_executable(upload_cache_policy "${ENGINE}/tests/test_upload_cache_policy.c")
target_include_directories(upload_cache_policy PRIVATE
  "${ENGINE}/externals/mesa-kosmickrisp/externals/mesa/src/kosmickrisp/vulkan")
target_compile_options(upload_cache_policy PRIVATE -UNDEBUG)
add_test(NAME upload_cache_policy COMMAND upload_cache_policy)
