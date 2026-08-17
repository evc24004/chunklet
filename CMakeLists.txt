cmake_minimum_required(VERSION 3.20)
project(chunklet VERSION 0.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(CHUNKLET_ENDSTONE_SOURCE "" CACHE PATH "Path to an Endstone source tree")
if(CHUNKLET_ENDSTONE_SOURCE)
    add_subdirectory("${CHUNKLET_ENDSTONE_SOURCE}/include"
                     "${CMAKE_BINARY_DIR}/endstone-include")
else()
    FetchContent_Declare(
        endstone_source
        GIT_REPOSITORY https://github.com/EndstoneMC/endstone.git
        GIT_TAG v0.11.7
        SOURCE_SUBDIR include
    )
    FetchContent_MakeAvailable(endstone_source)
endif()

set(WITH_GTEST OFF CACHE BOOL "" FORCE)
set(WITH_FUZZERS OFF CACHE BOOL "" FORCE)
set(WITH_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(WITH_GZFILEOP OFF CACHE BOOL "" FORCE)
set(WITH_NEW_STRATEGIES OFF CACHE BOOL "" FORCE)
set(ZLIB_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ZLIBNG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)
set(ZLIB_SYMBOL_PREFIX "chunklet_" CACHE STRING "" FORCE)
FetchContent_Declare(
    zlib_ng
    GIT_REPOSITORY https://github.com/zlib-ng/zlib-ng.git
    GIT_TAG 2.3.3
)
FetchContent_MakeAvailable(zlib_ng)

set_property(TARGET zlib-ng-static PROPERTY POSITION_INDEPENDENT_CODE ON)
get_target_property(CHUNKLET_ENDSTONE_INCLUDE_DIRS endstone INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET endstone PROPERTY
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${CHUNKLET_ENDSTONE_INCLUDE_DIRS}")

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
    src/spatial/proximity/optimizer.cpp
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
target_link_libraries(chunklet PRIVATE zlibstatic)
target_compile_options(chunklet PRIVATE
    -Wall -Wextra -Wpedantic -mtune=znver3 -fno-plt)
set_property(TARGET chunklet PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

