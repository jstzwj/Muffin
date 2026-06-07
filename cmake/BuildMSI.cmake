# BuildMSI.cmake — Build a Windows MSI installer using WiX Toolset
#
# Required variables:
#   DIST_DIR       - Path to the dist directory (populated by the 'dist' target)
#   WIX_COMMAND    - Path to the wix executable
#   MUFFIN_VERSION - Version string (e.g. "0.1.0")
#   SOURCE_DIR     - Project source root

if(NOT DEFINED DIST_DIR OR DIST_DIR STREQUAL "")
  message(FATAL_ERROR "DIST_DIR is required")
endif()
if(NOT DEFINED WIX_COMMAND OR WIX_COMMAND STREQUAL "")
  message(FATAL_ERROR "WIX_COMMAND is required")
endif()
if(NOT DEFINED MUFFIN_VERSION OR MUFFIN_VERSION STREQUAL "")
  message(FATAL_ERROR "MUFFIN_VERSION is required")
endif()

if(NOT EXISTS "${DIST_DIR}")
  message(FATAL_ERROR "Dist directory does not exist: ${DIST_DIR}. Run the 'dist' target first.")
endif()

# WiX requires a four-part version (x.x.x.x). Pad if needed.
set(_msi_version "${MUFFIN_VERSION}")
string(REPLACE "." ";" _version_parts "${_msi_version}")
list(LENGTH _version_parts _part_count)
while(_part_count LESS 4)
  string(APPEND _msi_version ".0")
  math(EXPR _part_count "${_part_count} + 1")
endwhile()

set(MSI_OUTPUT "${CMAKE_BINARY_DIR}/Muffin-${MUFFIN_VERSION}-windows-x64.msi")

message(STATUS "Building MSI installer...")
message(STATUS "  WiX:      ${WIX_COMMAND}")
message(STATUS "  DistDir:  ${DIST_DIR}")
message(STATUS "  Version:  ${_msi_version}")
message(STATUS "  Output:   ${MSI_OUTPUT}")

execute_process(
  COMMAND "${WIX_COMMAND}" build
    -arch x64
    -d MuffinVersion=${_msi_version}
    -d DistDir=${DIST_DIR}
    -out "${MSI_OUTPUT}"
    "${SOURCE_DIR}/cmake/Muffin.wxs"
  RESULT_VARIABLE wix_result
  OUTPUT_VARIABLE wix_output
  ERROR_VARIABLE wix_error
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(wix_result)
  message(FATAL_ERROR "WiX build failed:\n${wix_output}\n${wix_error}")
endif()

message(STATUS "MSI written to ${MSI_OUTPUT}")
