cmake_minimum_required(VERSION 3.20)
project(chunklet VERSION 0.1.0 LANGUAGES CXX)

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
get_target_property(CHUNKLET_ENDSTONE_INCLUDE_DIRS endstone INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET endstone PROPERTY
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${CHUNKLET_ENDSTONE_INCLUDE_DIRS}")

endstone_add_plugin(chunklet
    src/native/chunk_source.cpp
    src/native/layout.cpp
    src/plugin/chunklet_plugin.cpp
    src/plugin/commands.cpp
    src/render/job.cpp
    src/render/plan.cpp
)
target_include_directories(chunklet PRIVATE src)
target_compile_options(chunklet PRIVATE -Wall -Wextra -Wpedantic)

