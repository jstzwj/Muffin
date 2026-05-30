cmake_minimum_required(VERSION 3.24)
project(Muffin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(BUILD_TESTING "Build tests" ON)

# Compatibility for CMake 4.x with older projects (cmark-gfm)
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

# --- Vendored cmark-gfm with math extension ---
set(MUFFIN_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "Disable third-party dependency tests" FORCE)
set(CMARK_TESTS OFF CACHE BOOL "Disable cmark-gfm tests" FORCE)
set(CMARK_STATIC ON CACHE BOOL "Build cmark-gfm static libraries" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "Do not build cmark-gfm shared libraries" FORCE)
set(CMARK_PROGRAMS OFF CACHE BOOL "Do not build cmark-gfm tools" FORCE)
add_subdirectory(third_party/cmark-gfm)
set(BUILD_TESTING ${MUFFIN_BUILD_TESTING} CACHE BOOL "Build tests" FORCE)

# tinyxml2 via FetchContent
include(FetchContent)
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "Disable tinyxml2 tests" FORCE)
set(tinyxml2_INSTALL OFF CACHE BOOL "Disable tinyxml2 install rules" FORCE)
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG        11.0.0
)
FetchContent_MakeAvailable(tinyxml2)

if(TARGET tinyxml2 AND NOT TARGET tinyxml2::tinyxml2)
    add_library(tinyxml2::tinyxml2 ALIAS tinyxml2)
endif()

# Expose cmark-gfm headers to our targets
set(CMARK_GFM_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/third_party/cmark-gfm/src"
    "${CMAKE_BINARY_DIR}/third_party/cmark-gfm/src"
    "${CMAKE_SOURCE_DIR}/third_party/cmark-gfm"
    "${CMAKE_SOURCE_DIR}/third_party/cmark-gfm/extensions"
)

add_subdirectory(src)

# Testing
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 COMPONENTS Test REQUIRED)
    add_subdirectory(tests)
endif()
