cmake_minimum_required(VERSION 3.24)
project(Muffin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Compatibility for CMake 4.x with older projects (cmark-gfm)
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

# cmark-gfm via FetchContent (avoids Conan availability issues)
include(FetchContent)
FetchContent_Declare(
    cmark-gfm
    GIT_REPOSITORY https://github.com/github/cmark-gfm.git
    GIT_TAG        0.29.0.gfm.13
    CMAKE_ARGS     -DCMARK_TESTS=OFF -DCMARK_STATIC=ON -DCMARK_SHARED=OFF
)
FetchContent_MakeAvailable(cmark-gfm)

# Expose cmark-gfm headers to our targets
set(CMARK_GFM_INCLUDE_DIRS
    "${cmark-gfm_SOURCE_DIR}/src"
    "${cmark-gfm_BINARY_DIR}/src"
    "${cmark-gfm_SOURCE_DIR}"
    "${cmark-gfm_SOURCE_DIR}/extensions"
)

add_subdirectory(src)

# Testing
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 COMPONENTS Test REQUIRED)
    add_subdirectory(tests)
endif()
