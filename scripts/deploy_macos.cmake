# deploy_macos.cmake — macOS deployment
# Creates an .app bundle and runs macdeployqt.

find_program(MACDEPLOYQT macdeployqt HINTS
    "${CMAKE_BINARY_DIR}/generators"
    "${CMAKE_BINARY_DIR}/build/generators"
    "/usr/local/opt/qt6/bin"
    "/opt/homebrew/opt/qt6/bin"
)

# Check if this is a .app bundle build
set(APP_BUNDLE "${DEPLOY_DIR}/Muffin.app")
if(EXISTS "${APP_BUNDLE}")
    if(MACDEPLOYQT)
        message(STATUS "  Running macdeployqt...")
        execute_process(
            COMMAND "${MACDEPLOYQT}" "${APP_BUNDLE}" -verbose=1
            RESULT_VARIABLE RESULT
            ERROR_VARIABLE ERR
        )
        if(NOT RESULT EQUAL 0)
            message(WARNING "  macdeployqt failed: ${ERR}")
        else()
            message(STATUS "  macdeployqt succeeded")
        endif()
    else()
        message(WARNING "  macdeployqt not found. Qt frameworks may not be bundled.")
    endif()
else()
    message(STATUS "  Not an .app bundle, copying executable directly")

    # Copy cmark-gfm dylib if present
    file(GLOB CMARK_DYLIB "${CMAKE_BINARY_DIR}/_deps/cmark-gfm-build/src/*.dylib")
    foreach(DYLIB ${CMARK_DYLIB})
        get_filename_component(DYLIB_NAME "${DYLIB}" NAME)
        file(COPY "${DYLIB}" DESTINATION "${DEPLOY_DIR}")
        message(STATUS "  Copied: ${DYLIB_NAME}")
    endforeach()

    # Fix rpath for standalone binary
    execute_process(
        COMMAND install_name_tool -add_rpath "@executable_path/../lib"
                "${DEPLOY_DIR}/muffin"
        ERROR_QUIET
    )
endif()
