# deploy_windows.cmake — Windows deployment
# 1. VC runtime  2. windeployqt (Qt DLLs + plugins)  3. Conan deps  4. cmark-gfm

# Resolve Conan cache location
if(WIN32)
    set(CONAN_HOME "$ENV{USERPROFILE}/.conan2")
else()
    set(CONAN_HOME "$ENV{HOME}/.conan2")
endif()

# ------------------------------------------------------------------
# 1. VC Runtime (MSVC redistributable DLLs)
# ------------------------------------------------------------------
set(VCRT_SEARCH_DIRS)
if(DEFINED ENV{VCToolsRedistDir})
    list(APPEND VCRT_SEARCH_DIRS
        "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT"
        "$ENV{VCToolsRedistDir}/onecore/x64/Microsoft.VC143.CRT"
    )
endif()
file(GLOB VS_REDIST_DIRS
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
    "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
)
list(APPEND VCRT_SEARCH_DIRS ${VS_REDIST_DIRS})

set(VCRT_DLLS
    msvcp140.dll msvcp140_1.dll msvcp140_2.dll
    msvcp140_atomic_wait.dll msvcp140_codecvt_ids.dll
    vcruntime140.dll vcruntime140_1.dll vcruntime140_threads.dll concrt140.dll
)

set(VCRT_FOUND FALSE)
foreach(VCRT_DIR ${VCRT_SEARCH_DIRS})
    if(EXISTS "${VCRT_DIR}/vcruntime140.dll")
        set(VCRT_FOUND TRUE)
        foreach(DLL ${VCRT_DLLS})
            if(EXISTS "${VCRT_DIR}/${DLL}")
                file(COPY "${VCRT_DIR}/${DLL}" DESTINATION "${DEPLOY_DIR}")
                message(STATUS "  Copied: ${DLL}")
            endif()
        endforeach()
        break()
    endif()
endforeach()
if(NOT VCRT_FOUND)
    message(WARNING "  VC runtime not found.")
endif()

# ------------------------------------------------------------------
# 2. windeployqt — copies Qt DLLs, plugins, platforms, styles
# ------------------------------------------------------------------
# Find the correct Qt package by looking for Qt6Core.dll
file(GLOB QT6CORE_CANDIDATES "${CONAN_HOME}/p/b/qt*/p/bin/Qt6Core.dll")
set(QT_BIN_DIR)
foreach(COREDLL ${QT6CORE_CANDIDATES})
    get_filename_component(BINDIR "${COREDLL}" DIRECTORY)
    if(EXISTS "${BINDIR}/windeployqt.exe")
        set(QT_BIN_DIR "${BINDIR}")
        break()
    endif()
endforeach()

if(QT_BIN_DIR)
    set(WINDEPLOYQT "${QT_BIN_DIR}/windeployqt.exe")
    message(STATUS "  windeployqt: ${WINDEPLOYQT}")

    # windeployqt needs Qt6Core.dll on PATH to detect Qt version.
    set(ENV{PATH} "${QT_BIN_DIR};$ENV{PATH}")

    execute_process(
        COMMAND "${WINDEPLOYQT}"
            --release
            --no-translations
            --no-opengl-sw
            --dir "${DEPLOY_DIR}"
            "${DEPLOY_DIR}/${EXE_NAME}"
        RESULT_VARIABLE WDQT_RESULT
        OUTPUT_VARIABLE WDQT_OUTPUT
        ERROR_VARIABLE WDQT_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(WDQT_RESULT EQUAL 0)
        message(STATUS "  windeployqt succeeded")
    else()
        message(WARNING "  windeployqt failed (${WDQT_RESULT}):\n  ${WDQT_ERROR}")
        # Fallback: copy Qt DLLs manually
        file(GLOB QT_DLLS "${QT_BIN_DIR}/Qt6*.dll")
        foreach(DLL ${QT_DLLS})
            get_filename_component(N "${DLL}" NAME)
            file(COPY "${DLL}" DESTINATION "${DEPLOY_DIR}")
            message(STATUS "  Fallback copy: ${N}")
        endforeach()
        # Copy plugins
        file(COPY "${QT_BIN_DIR}/../plugins/platforms" DESTINATION "${DEPLOY_DIR}" PATTERN "*.dll")
        file(COPY "${QT_BIN_DIR}/../plugins/styles" DESTINATION "${DEPLOY_DIR}" PATTERN "*.dll")
    endif()
else()
    message(WARNING "  windeployqt not found — Qt DLLs will NOT be deployed.")
endif()

# ------------------------------------------------------------------
# 3. Conan runtime dependency DLLs
# ------------------------------------------------------------------
# Collect shared libs from the Conan dependency graph (libpq, openssl, etc.)
# These live in <conan_pkg>/p/bin/ or <conan_pkg>/p/lib/
file(GLOB CONAN_DLLS
    "${CONAN_HOME}/p/b/opens*/p/bin/*.dll"
    "${CONAN_HOME}/p/b/opens*/p/lib/ossl-modules/*.dll"
    "${CONAN_HOME}/p/b/libpq*/p/bin/*.dll"
    "${CONAN_HOME}/p/b/sqlit*/p/bin/*.dll"
    "${CONAN_HOME}/p/b/harfb*/p/bin/*.dll"
    "${CONAN_HOME}/p/b/freet*/p/bin/*.dll"
    "${CONAN_HOME}/p/b/glib*/p/bin/*.dll"
    "${CONAN_HOME}/p/b/pcre*/p/bin/*.dll"
)
foreach(DLL ${CONAN_DLLS})
    get_filename_component(DLL_NAME "${DLL}" NAME)
    if(NOT DLL_NAME MATCHES "Qt6.*")  # Already handled by windeployqt
        if(DLL_NAME MATCHES "ossl-modules")
            file(MAKE_DIRECTORY "${DEPLOY_DIR}/ossl-modules")
            file(COPY "${DLL}" DESTINATION "${DEPLOY_DIR}/ossl-modules")
        else()
            file(COPY "${DLL}" DESTINATION "${DEPLOY_DIR}")
        endif()
        message(STATUS "  Copied: ${DLL_NAME}")
    endif()
endforeach()

# ------------------------------------------------------------------
# 4. cmark-gfm DLL (if shared)
# ------------------------------------------------------------------
file(GLOB CMARK_DLL "${CMAKE_BINARY_DIR}/_deps/cmark-gfm-build/src/Release/cmark-gfm.dll"
                     "${CMAKE_BINARY_DIR}/_deps/cmark-gfm-build/src/*.dll")
foreach(DLL ${CMARK_DLL})
    get_filename_component(N "${DLL}" NAME)
    file(COPY "${DLL}" DESTINATION "${DEPLOY_DIR}")
    message(STATUS "  Copied: ${N}")
endforeach()
