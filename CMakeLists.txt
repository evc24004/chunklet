cmake_minimum_required(VERSION 3.20)
project(chunklet VERSION 0.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(CHUNKLET_ENDSTONE_SOURCE "" CACHE PATH "Path to the pinned Endstone 0.11.13 source tree")
if(NOT TARGET endstone)
    if(NOT CHUNKLET_ENDSTONE_SOURCE OR
       NOT EXISTS "${CHUNKLET_ENDSTONE_SOURCE}/include/CMakeLists.txt")
        message(FATAL_ERROR "Standalone Chunklet builds require -DCHUNKLET_ENDSTONE_SOURCE=<Endstone 0.11.13 source>; see tools/README.md")
    endif()
    add_subdirectory("${CHUNKLET_ENDSTONE_SOURCE}/include"
                     "${CMAKE_BINARY_DIR}/endstone-include")
endif()

# These normal variables are local to this subdirectory (and zlib-ng's child
# directory). Do not FORCE cache options: that would change sibling plugins.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_TESTING OFF)
set(WITH_GTEST OFF)
set(WITH_FUZZERS OFF)
set(WITH_BENCHMARKS OFF)
set(WITH_NEW_STRATEGIES OFF)
set(ZLIB_COMPAT ON)
set(ZLIB_SYMBOL_PREFIX "chunklet_")
FetchContent_Declare(
    zlib_ng
    GIT_REPOSITORY https://github.com/zlib-ng/zlib-ng.git
    GIT_TAG 12731092979c6d07f42da27da673a9f6c7b13586
)
FetchContent_MakeAvailable(zlib_ng)

set_property(TARGET zlib-ng-static PROPERTY POSITION_INDEPENDENT_CODE ON)

set(CHUNKLET_DISTANCE_TABLE_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/distance_table.o")
add_custom_command(
    OUTPUT "${CHUNKLET_DISTANCE_TABLE_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/noise/distance/table.bin"
            "${CMAKE_CURRENT_BINARY_DIR}/distance_table.bin"
    COMMAND "${CMAKE_COMMAND}" -E touch distance_table.stack
    COMMAND "${CMAKE_OBJCOPY}" --input-target=binary
            --output-target=elf64-x86-64
            --binary-architecture=i386:x86-64
            --rename-section .data=.rodata,alloc,load,readonly,data,contents
            --add-section .note.GNU-stack=distance_table.stack
            --set-section-flags .note.GNU-stack=noload,readonly
            distance_table.bin distance_table.o
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/noise/distance/table.bin"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM)
set_source_files_properties("${CHUNKLET_DISTANCE_TABLE_OBJECT}"
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

endstone_add_plugin(chunklet
    src/allocation/transient/object_pool.cpp
    src/allocation/transient/object_copy.cpp
    src/compression/zlib/optimizer.cpp
    src/compression/zlib/stream_pool.cpp
    src/native/chunk_source.cpp
    src/native/layout.cpp
    src/cache/ownership/optimizer.cpp
    src/synchronization/shared_mutex/optimizer.cpp
    src/spatial/containment/kernel.cpp
    src/spatial/containment/optimizer.cpp
    src/timing/monotonic/optimizer.cpp
    src/noise/area/kernel.cpp
    src/noise/area/optimizer.cpp
    src/noise/construction/direct.cpp
    src/noise/construction/direct_hook.cpp
    src/noise/construction/direct_kernel.cpp
    src/noise/construction/legacy_random.cpp
    src/noise/distance/optimizer.cpp
    src/noise/construction/reserve.cpp
    src/noise/interpolation/kernel.cpp
    src/noise/interpolation/optimizer.cpp
    src/noise/interpolation/octaves.cpp
    src/noise/shuffle/optimizer.cpp
    src/noise/interpolation/sample_cache.cpp
    src/noise/perlin/kernel.cpp
    src/noise/perlin/octaves.cpp
    src/noise/perlin/octave_optimizer.cpp
    src/noise/perlin/optimizer.cpp
    src/plugin/chunklet_plugin.cpp
    src/plugin/commands.cpp
    src/render/job.cpp
    src/render/persistence.cpp
    src/render/plan.cpp
)
target_sources(chunklet PRIVATE "${CHUNKLET_DISTANCE_TABLE_OBJECT}")
target_include_directories(chunklet PRIVATE src)
set_target_properties(chunklet PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
get_target_property(CHUNKLET_ENDSTONE_INCLUDE_DIRS endstone INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(chunklet SYSTEM PRIVATE ${CHUNKLET_ENDSTONE_INCLUDE_DIRS})
target_link_libraries(chunklet PRIVATE zlibstatic)
target_compile_options(chunklet PRIVATE
    -Wall -Wextra -Wpedantic -mtune=znver3 -fno-plt)
set_property(TARGET chunklet PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

