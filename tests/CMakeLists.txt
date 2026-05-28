set(TEST_SOURCES
    parser/test_cmark_parser.cpp
    parser/test_math_delimiter_scanner.cpp
    renderer/test_render_source_map.cpp
    renderer/test_document_renderer.cpp
    renderer/test_math_rendering.cpp
    editor/test_markdown_patch.cpp
    editor/test_markdown_command.cpp
)

set(MUFFIN_TEST_PATH "$ENV{PATH}")
if(WIN32)
    set(QT_RUNTIME_DIR "")
    if(DEFINED QT_BIN_DIR AND EXISTS "${QT_BIN_DIR}")
        set(QT_RUNTIME_DIR "${QT_BIN_DIR}")
    elseif(DEFINED Qt6_DIR)
        get_filename_component(_qt6_config_dir "${Qt6_DIR}" DIRECTORY)
        get_filename_component(_qt6_lib_dir "${_qt6_config_dir}" DIRECTORY)
        get_filename_component(_qt6_prefix "${_qt6_lib_dir}" DIRECTORY)
        if(EXISTS "${_qt6_prefix}/bin")
            set(QT_RUNTIME_DIR "${_qt6_prefix}/bin")
        endif()
    endif()
    if(NOT QT_RUNTIME_DIR)
        file(GLOB _conan_qt_core_candidates
            "$ENV{USERPROFILE}/.conan2/p/b/qt*/p/bin/Qt6Core.dll"
            "$ENV{USERPROFILE}/.conan2/p/qt*/p/bin/Qt6Core.dll")
        list(GET _conan_qt_core_candidates 0 _conan_qt_core)
        if(_conan_qt_core)
            get_filename_component(QT_RUNTIME_DIR "${_conan_qt_core}" DIRECTORY)
        endif()
    endif()

    if(QT_RUNTIME_DIR)
        set(MUFFIN_TEST_PATH "${QT_RUNTIME_DIR};${CMAKE_BINARY_DIR}/Muffin-dist;$ENV{PATH}")
    else()
        set(MUFFIN_TEST_PATH "${CMAKE_BINARY_DIR}/Muffin-dist;$ENV{PATH}")
    endif()
endif()

foreach(TEST_FILE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_FILE})
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMARK_GFM_INCLUDE_DIRS})
    target_link_libraries(${TEST_NAME} PRIVATE Qt6::Test libcmark-gfm_static libcmark-gfm-extensions_static)
    target_compile_definitions(${TEST_NAME} PRIVATE CMARK_GFM_STATIC_DEFINE)
    if(TEST_FILE MATCHES "parser/test_cmark_parser.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp)
    endif()
    if(TEST_FILE MATCHES "parser/test_math_delimiter_scanner.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp)
    endif()
    if(TEST_FILE MATCHES "renderer/test_render_source_map.cpp")
        target_sources(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp)
    endif()
    if(TEST_FILE MATCHES "renderer/test_document_renderer.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "renderer/test_math_rendering.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "editor/test_markdown_patch.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownPatch.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp)
    endif()
    if(TEST_FILE MATCHES "editor/test_markdown_command.cpp")
        target_sources(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets)
    endif()
    if(WIN32)
        add_test(NAME ${TEST_NAME}
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${MUFFIN_TEST_PATH}"
                "QT_QPA_PLATFORM=offscreen"
                $<TARGET_FILE:${TEST_NAME}>)
    else()
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endif()
endforeach()
