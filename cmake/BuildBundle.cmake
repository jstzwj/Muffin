# BuildBundle.cmake — Build the Windows setup bundle (Burn exe wrapping the
# MSI behind the custom WPF bootstrapper application in installer/ba).
#
# Required variables:
#   DIST_DIR       - Path to the dist directory (populated by the 'dist' target)
#   WIX_COMMAND    - Path to the wix executable
#   DOTNET_COMMAND - Path to the dotnet executable (builds the BA)
#   MUFFIN_VERSION - Version string (e.g. "0.1.0")
#   MSI_ARCH       - WiX architecture (x64, arm64, ...) of the already-built MSI
#   SOURCE_DIR     - Project source root

if(NOT DEFINED WIX_COMMAND OR WIX_COMMAND STREQUAL "")
  message(FATAL_ERROR "WIX_COMMAND is required")
endif()
if(NOT DEFINED DOTNET_COMMAND OR DOTNET_COMMAND STREQUAL "")
  message(FATAL_ERROR "DOTNET_COMMAND is required")
endif()
if(NOT DEFINED MUFFIN_VERSION OR MUFFIN_VERSION STREQUAL "")
  message(FATAL_ERROR "MUFFIN_VERSION is required")
endif()
if(NOT DEFINED MSI_ARCH OR MSI_ARCH STREQUAL "")
  set(MSI_ARCH "x64")
endif()

# Pad to the four-part version the same way BuildMSI.cmake does.
set(_bundle_version "${MUFFIN_VERSION}")
string(REPLACE "." ";" _version_parts "${_bundle_version}")
list(LENGTH _version_parts _part_count)
while(_part_count LESS 4)
  string(APPEND _bundle_version ".0")
  math(EXPR _part_count "${_part_count} + 1")
endwhile()

set(_msi_path "${CMAKE_BINARY_DIR}/Muffin-${MUFFIN_VERSION}-windows-${MSI_ARCH}.msi")
if(NOT EXISTS "${_msi_path}")
  message(FATAL_ERROR "MSI not found at ${_msi_path}. Build the 'msi' target first (the 'bundle' target depends on it).")
endif()

# RID for mbanative.dll selection and the arch-suffixed managed-BA host symbol.
if(MSI_ARCH STREQUAL "arm64")
  set(_ba_rid "win-arm64")
  set(_ba_host_suffix "_A64")
elseif(MSI_ARCH STREQUAL "x86")
  set(_ba_rid "win-x86")
  set(_ba_host_suffix "_X86")
else()
  set(_ba_rid "win-x64")
  set(_ba_host_suffix "_X64")
endif()

set(_ba_dir "${SOURCE_DIR}/installer/ba/bin/Release/net48")

message(STATUS "Building Muffin bootstrapper application...")
message(STATUS "  dotnet:   ${DOTNET_COMMAND}")
message(STATUS "  BA arch:  ${_ba_rid}")

execute_process(
  COMMAND "${DOTNET_COMMAND}" build "${SOURCE_DIR}/installer/ba/MuffinBootstrapperUI.csproj"
    -c Release -p:MuffinBaArch=${_ba_rid}
  RESULT_VARIABLE dotnet_result
  OUTPUT_VARIABLE dotnet_output
  ERROR_VARIABLE dotnet_error
  WORKING_DIRECTORY "${SOURCE_DIR}/installer/ba"
)
if(dotnet_result)
  message(FATAL_ERROR "BA build failed:\n${dotnet_output}\n${dotnet_error}")
endif()

message(STATUS "Building setup bundle...")
message(STATUS "  MSI:      ${_msi_path}")
message(STATUS "  Version:  ${_bundle_version}")

set(BUNDLE_OUTPUT "${CMAKE_BINARY_DIR}/Muffin-Setup-${MUFFIN_VERSION}-windows-${MSI_ARCH}.exe")

execute_process(
  COMMAND "${WIX_COMMAND}" build
    -arch ${MSI_ARCH}
    -ext WixToolset.Bal.wixext
    -d BaHostSuffix=${_ba_host_suffix}
    -d MuffinVersion=${_bundle_version}
    -d MsiPath=${_msi_path}
    -d BaDir=${_ba_dir}
    -d SourceDir=${SOURCE_DIR}
    -out "${BUNDLE_OUTPUT}"
    "${SOURCE_DIR}/installer/MuffinBundle.wxs"
  RESULT_VARIABLE wix_result
  OUTPUT_VARIABLE wix_output
  ERROR_VARIABLE wix_error
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(wix_result)
  message(FATAL_ERROR "WiX bundle build failed:\n${wix_output}\n${wix_error}")
endif()

message(STATUS "Setup bundle written to ${BUNDLE_OUTPUT}")
