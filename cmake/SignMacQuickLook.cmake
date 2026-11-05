# Run after macdeployqt has finished changing load commands and nested code.
# Ad-hoc signing needs no developer certificate; it does not provide notarization.
if(NOT DEFINED APP_BUNDLE OR NOT DEFINED ENTITLEMENTS)
  message(FATAL_ERROR "APP_BUNDLE and ENTITLEMENTS are required")
endif()
find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
set(extension "${APP_BUNDLE}/Contents/PlugIns/MuffinQuickLook.appex")
if(NOT EXISTS "${extension}/Contents/MacOS/MuffinQuickLook")
  message(FATAL_ERROR "Quick Look extension is missing from ${APP_BUNDLE}")
endif()
execute_process(COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none
  --entitlements "${ENTITLEMENTS}" "${extension}"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CODESIGN_EXECUTABLE}" --verify --strict "${extension}"
  COMMAND_ERROR_IS_FATAL ANY)
# Seal the containing bundle last so it records the final extension signature.
execute_process(COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${APP_BUNDLE}"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CODESIGN_EXECUTABLE}" --verify --deep --strict "${APP_BUNDLE}"
  COMMAND_ERROR_IS_FATAL ANY)
