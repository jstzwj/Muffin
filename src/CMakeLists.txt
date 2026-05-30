set(MUFFIN_LIB_SOURCES
    app/Application.cpp
    app/MainWindow.cpp
    core/MdNode.cpp
    core/MdDocument.cpp
    core/MdParser.cpp
    core/MdSerializer.cpp
    core/SourceMap.cpp
    renderer/DocumentRenderer.cpp
    renderer/Theme.cpp
)

set(MUFFIN_LIB_HEADERS
    app/Application.h
    app/MainWindow.h
    core/MdNode.h
    core/MdDocument.h
    core/MdParser.h
    core/MdSerializer.h
    core/SourceMap.h
    renderer/DocumentRenderer.h
    renderer/Theme.h
)

# Static library for sharing code between executable and tests
add_library(muffin_lib STATIC ${MUFFIN_LIB_SOURCES} ${MUFFIN_LIB_HEADERS})

# MSVC: Qt requires /Zc:__cplusplus and /permissive-
if(MSVC)
    target_compile_options(muffin_lib PRIVATE /Zc:__cplusplus /permissive- /utf-8)
endif()

target_include_directories(muffin_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMARK_GFM_INCLUDE_DIRS}
)

target_link_libraries(muffin_lib PUBLIC
    Qt6::Widgets
    Qt6::Gui
    libcmark-gfm_static
    libcmark-gfm-extensions_static
)

target_compile_definitions(muffin_lib PUBLIC
    CMARK_GFM_STATIC_DEFINE
    CMARK_GFM_EXTENSIONS_STATIC_DEFINE
)

# Conan Qt packages only define release config targets, so Debug builds miss the include dirs.
if(TARGET Qt6::Core AND NOT DEFINED _MUFFIN_QT_INCLUDE_DIR)
    get_target_property(_qt_core_imported_location Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _qt_core_imported_location)
        get_target_property(_qt_core_imported_location Qt6::Core IMPORTED_LOCATION)
    endif()
    if(_qt_core_imported_location)
        get_filename_component(_qt_lib_dir "${_qt_core_imported_location}" DIRECTORY)
        get_filename_component(_qt_pkg_dir "${_qt_lib_dir}" DIRECTORY)
        set(_qt_include_dir "${_qt_pkg_dir}/include")
    endif()
    if(NOT IS_DIRECTORY "${_qt_include_dir}")
        find_path(_qt_include_dir
            NAMES "QtCore/QtCore"
            PATHS "${_qt_lib_dir}/../include"
                  "$ENV{HOME}/.conan2/p/b/qt584dfe634b273/p/include"
                  "C:/Users/jstzw/.conan2/p/b/qt584dfe634b273/p/include"
        )
    endif()
    set(_MUFFIN_QT_INCLUDE_DIR "${_qt_include_dir}" CACHE INTERNAL "Qt include dir")
endif()

if(_MUFFIN_QT_INCLUDE_DIR AND IS_DIRECTORY "${_MUFFIN_QT_INCLUDE_DIR}")
    target_include_directories(muffin_lib PUBLIC
        "${_MUFFIN_QT_INCLUDE_DIR}"
        "${_MUFFIN_QT_INCLUDE_DIR}/QtCore"
        "${_MUFFIN_QT_INCLUDE_DIR}/QtGui"
        "${_MUFFIN_QT_INCLUDE_DIR}/QtWidgets"
    )
endif()

# Executable
add_executable(muffin main.cpp)
target_link_libraries(muffin PRIVATE muffin_lib)

# --- Post-build: copy all runtime DLLs to dist directory ---
if(WIN32)
    set(_muffin_binary_dir "${CMAKE_BINARY_DIR}")
    set(_muffin_source_dir "${CMAKE_SOURCE_DIR}")
    add_custom_command(TARGET muffin POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D "MUFFIN_SOURCE_DIR=${_muffin_source_dir}"
            -D "MUFFIN_BINARY_DIR=${_muffin_binary_dir}"
            -D "MUFFIN_CONFIG=$<CONFIG>"
            -P "${_muffin_source_dir}/cmake/copy_dlls.cmake"
        COMMENT "Copying runtime DLLs to dist directory"
    )
endif()
