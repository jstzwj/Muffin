# copy_dlls.cmake — Post-build script to collect all runtime DLLs
#
# Expected variables (passed via -D):
#   MUFFIN_SOURCE_DIR — project root
#   MUFFIN_BINARY_DIR — build root (e.g. build/dev)
#   MUFFIN_CONFIG     — build config (Release/Debug)

set(DIST_DIR "${MUFFIN_BINARY_DIR}/dist/${MUFFIN_CONFIG}")
set(EXECUTABLE "${MUFFIN_BINARY_DIR}/src/${MUFFIN_CONFIG}/muffin.exe")

file(MAKE_DIRECTORY "${DIST_DIR}")

# Find Qt bin dir by searching common Conan locations
set(QT_BIN_DIR "")
set(_search_paths
    "$ENV{HOME}/.conan2/p/b/qt584dfe634b273/p/bin"
    "C:/Users/$ENV{USERNAME}/.conan2/p/b/qt584dfe634b273/p/bin"
)
foreach(_p ${_search_paths})
    if(EXISTS "${_p}/Qt6Core.dll")
        set(QT_BIN_DIR "${_p}")
        break()
    endif()
endforeach()

# 1) Copy the executable itself
if(EXISTS "${EXECUTABLE}")
    file(COPY "${EXECUTABLE}" DESTINATION "${DIST_DIR}")
endif()

# 2) Copy Qt DLLs
if(QT_BIN_DIR AND IS_DIRECTORY "${QT_BIN_DIR}")
    file(GLOB _qt_dlls "${QT_BIN_DIR}/Qt6*.dll")
    foreach(_dll ${_qt_dlls})
        file(COPY "${_dll}" DESTINATION "${DIST_DIR}")
    endforeach()
endif()

# 3) Copy Qt plugins
set(_plugins_root "")
if(QT_BIN_DIR)
    get_filename_component(_qt_pkg "${QT_BIN_DIR}" DIRECTORY)
    set(_plugins_root "${_qt_pkg}/plugins")
endif()

if(IS_DIRECTORY "${_plugins_root}/platforms")
    file(MAKE_DIRECTORY "${DIST_DIR}/platforms")
    file(GLOB _platform_dlls "${_plugins_root}/platforms/*.dll")
    foreach(_dll ${_platform_dlls})
        file(COPY "${_dll}" DESTINATION "${DIST_DIR}/platforms")
    endforeach()
endif()

if(IS_DIRECTORY "${_plugins_root}/imageformats")
    file(MAKE_DIRECTORY "${DIST_DIR}/imageformats")
    file(GLOB _imgfmt_plugins "${_plugins_root}/imageformats/*.dll")
    foreach(_plugin ${_imgfmt_plugins})
        file(COPY "${_plugin}" DESTINATION "${DIST_DIR}/imageformats")
    endforeach()
endif()

if(IS_DIRECTORY "${_plugins_root}/styles")
    file(MAKE_DIRECTORY "${DIST_DIR}/styles")
    file(GLOB _style_plugins "${_plugins_root}/styles/*.dll")
    foreach(_plugin ${_style_plugins})
        file(COPY "${_plugin}" DESTINATION "${DIST_DIR}/styles")
    endforeach()
endif()

# 4) Copy MSVC runtime DLLs
set(_vc_dlls
    vcruntime140.dll
    vcruntime140_1.dll
    msvcp140.dll
    msvcp140_1.dll
    msvcp140_2.dll
    concrt140.dll
)
foreach(_dll ${_vc_dlls})
    if(EXISTS "C:/Windows/System32/${_dll}")
        file(COPY "C:/Windows/System32/${_dll}" DESTINATION "${DIST_DIR}")
    endif()
endforeach()

message(STATUS "Dist directory: ${DIST_DIR}")
