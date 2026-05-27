# deploy.cmake — Cross-platform post-build deployment script
#
# Usage:
#   cmake -DEXECUTABLE=<path> -DDEPLOY_DIR=<dir> -DCMAKE_BUILD_TYPE=<cfg>
#         -DCMAKE_SOURCE_DIR=<dir> -DCMAKE_BINARY_DIR=<dir> -P deploy.cmake
#
# Copies the executable and all runtime dependencies (DLLs, plugins, resources)
# into DEPLOY_DIR so the result is a self-contained distribution folder.

cmake_minimum_required(VERSION 3.24)

if(NOT EXECUTABLE OR NOT DEPLOY_DIR)
    message(FATAL_ERROR "EXECUTABLE and DEPLOY_DIR are required")
endif()
if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required (pass -DSOURCE_DIR=...)")
endif()

message(STATUS "=== Muffin Deploy ===")
message(STATUS "  Executable : ${EXECUTABLE}")
message(STATUS "  Deploy dir : ${DEPLOY_DIR}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")

file(MAKE_DIRECTORY "${DEPLOY_DIR}")

# ------------------------------------------------------------------
# Copy the main executable
# ------------------------------------------------------------------
get_filename_component(EXE_NAME "${EXECUTABLE}" NAME)
file(COPY "${EXECUTABLE}" DESTINATION "${DEPLOY_DIR}")
message(STATUS "  Copied: ${EXE_NAME}")

# ------------------------------------------------------------------
# Copy resources (themes, etc.)
# ------------------------------------------------------------------
if(EXISTS "${SOURCE_DIR}/resources/themes")
    file(COPY "${SOURCE_DIR}/resources/themes" DESTINATION "${DEPLOY_DIR}")
    message(STATUS "  Copied: resources/themes/")
endif()

# ------------------------------------------------------------------
# Platform-specific deployment
# ------------------------------------------------------------------
if(WIN32 OR (NOT APPLE AND NOT UNIX))
    include("${SOURCE_DIR}/scripts/deploy_windows.cmake")
elseif(APPLE)
    include("${SOURCE_DIR}/scripts/deploy_macos.cmake")
else()
    include("${SOURCE_DIR}/scripts/deploy_linux.cmake")
endif()

message(STATUS "=== Deploy complete ===")
