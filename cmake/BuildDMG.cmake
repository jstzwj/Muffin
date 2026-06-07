# BuildDMG.cmake — Create a macOS DMG disk image from the .app bundle
#
# Required variables:
#   DIST_DIR        - Path to the dist directory containing Muffin.app
#   MUFFIN_VERSION  - Version string (e.g. "0.1.0")
#   SOURCE_DIR      - Project source root (unused but kept for consistency)
#   MACDEPLOYQT     - Path to macdeployqt executable

if(NOT DEFINED DIST_DIR OR DIST_DIR STREQUAL "")
  message(FATAL_ERROR "DIST_DIR is required")
endif()
if(NOT DEFINED MUFFIN_VERSION OR MUFFIN_VERSION STREQUAL "")
  message(FATAL_ERROR "MUFFIN_VERSION is required")
endif()
if(NOT DEFINED MACDEPLOYQT OR MACDEPLOYQT STREQUAL "")
  message(FATAL_ERROR "MACDEPLOYQT is required")
endif()

set(APP_BUNDLE "${DIST_DIR}/Muffin.app")
if(NOT EXISTS "${APP_BUNDLE}")
  message(FATAL_ERROR
    "App bundle not found at ${APP_BUNDLE}. "
    "Ensure the 'dist' target was built with MACOSX_BUNDLE enabled.")
endif()

message(STATUS "Using macdeployqt: ${MACDEPLOYQT}")

# Run macdeployqt to deploy Qt frameworks into the app bundle
execute_process(
  COMMAND "${MACDEPLOYQT}" "${APP_BUNDLE}" -verbose=1
  RESULT_VARIABLE mdqt_result
  OUTPUT_VARIABLE mdqt_output
  ERROR_VARIABLE mdqt_error
)
if(mdqt_result)
  message(FATAL_ERROR
    "macdeployqt failed:\n${mdqt_output}\n${mdqt_error}")
endif()
message(STATUS "macdeployqt completed successfully")

# Create DMG staging area
set(DMG_STAGING "${CMAKE_BINARY_DIR}/dmg-staging")
file(REMOVE_RECURSE "${DMG_STAGING}")
file(MAKE_DIRECTORY "${DMG_STAGING}")

# Copy app bundle into staging
file(COPY "${APP_BUNDLE}" DESTINATION "${DMG_STAGING}")

# Create Applications symlink for drag-to-install UX
execute_process(
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    /Applications "${DMG_STAGING}/Applications"
)

# Create the DMG
set(DMG_OUTPUT "${CMAKE_BINARY_DIR}/Muffin-${MUFFIN_VERSION}-macos-arm64.dmg")
execute_process(
  COMMAND hdiutil create
    -volname "Muffin"
    -srcfolder "${DMG_STAGING}"
    -ov
    -format UDZO
    "${DMG_OUTPUT}"
  RESULT_VARIABLE hdiutil_result
  OUTPUT_VARIABLE hdiutil_output
  ERROR_VARIABLE hdiutil_error
)
if(hdiutil_result)
  message(FATAL_ERROR
    "hdiutil failed:\n${hdiutil_output}\n${hdiutil_error}")
endif()

# Clean up staging
file(REMOVE_RECURSE "${DMG_STAGING}")

message(STATUS "DMG written to ${DMG_OUTPUT}")
