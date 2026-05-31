cmake_minimum_required(VERSION 3.24)

project(Muffin
  VERSION 0.1.0
  DESCRIPTION "A fast native Markdown WYSIWYG editor built with C++ and Qt 6 Widgets"
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
  add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMARK_TESTS OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
set(CMARK_LIB_FUZZER OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cmark-gfm EXCLUDE_FROM_ALL)

set(MUFFIN_CORE_SOURCES
  src/app/DocumentSession.cpp
  src/app/DocumentSession.h
  src/document/NodeId.cpp
  src/document/NodeId.h
  src/document/SourceRange.h
  src/document/MarkdownTypes.h
  src/document/InlineNode.cpp
  src/document/InlineNode.h
  src/document/MarkdownNode.cpp
  src/document/MarkdownNode.h
  src/document/TextSelection.h
  src/document/SelectionSerializer.cpp
  src/document/SelectionSerializer.h
  src/document/NodeIndex.cpp
  src/document/NodeIndex.h
  src/document/MarkdownDocument.cpp
  src/document/MarkdownDocument.h
  src/parser/MarkdownParser.h
  src/parser/CmarkNodeAdapter.cpp
  src/parser/CmarkNodeAdapter.h
  src/parser/CmarkGfmParser.cpp
  src/parser/CmarkGfmParser.h
  src/parser/MarkdownSerializer.cpp
  src/parser/MarkdownSerializer.h
)

add_library(MuffinCore STATIC
  ${MUFFIN_CORE_SOURCES}
)

target_link_libraries(MuffinCore
  PUBLIC
    Qt6::Widgets
  PRIVATE
    libcmark-gfm_static
    libcmark-gfm-extensions_static
)

target_include_directories(MuffinCore
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/cmark-gfm/src
)

add_executable(Muffin
  src/app/CommandRegistry.cpp
  src/app/CommandRegistry.h
  src/app/MainWindow.cpp
  src/app/MainWindow.h
  src/editor/SourceEditorWidget.cpp
  src/editor/SourceEditorWidget.h
  src/io/FileController.cpp
  src/io/FileController.h
)

set(MUFFIN_UI_SOURCES
  src/blocks/code/CodeFenceController.cpp
  src/blocks/code/CodeFenceController.h
  src/blocks/html/HtmlBlockController.cpp
  src/blocks/html/HtmlBlockController.h
  src/blocks/html/HtmlSanitizer.cpp
  src/blocks/html/HtmlSanitizer.h
  src/blocks/math/MathBlockController.cpp
  src/blocks/math/MathBlockController.h
  src/blocks/math/MathRenderer.cpp
  src/blocks/math/MathRenderer.h
  src/blocks/table/TableController.cpp
  src/blocks/table/TableController.h
  src/blocks/table/TableModelOps.cpp
  src/blocks/table/TableModelOps.h
  src/commands/StylizeController.cpp
  src/commands/StylizeController.h
  src/edit/EditTransaction.cpp
  src/edit/EditTransaction.h
  src/edit/UndoStack.cpp
  src/edit/UndoStack.h
  src/editor/BrushQueue.cpp
  src/editor/BrushQueue.h
  src/editor/ClipboardController.cpp
  src/editor/ClipboardController.h
  src/editor/CursorPosition.h
  src/editor/EditorController.cpp
  src/editor/EditorController.h
  src/editor/EditorView.cpp
  src/editor/EditorView.h
  src/editor/InputController.cpp
  src/editor/InputController.h
  src/editor/SelectionController.cpp
  src/editor/SelectionController.h
  src/render/BlockLayout.cpp
  src/render/BlockLayout.h
  src/render/BlockLayoutBuilder.cpp
  src/render/BlockLayoutBuilder.h
  src/render/DocumentLayout.cpp
  src/render/DocumentLayout.h
  src/render/InlineLayout.cpp
  src/render/InlineLayout.h
  src/theme/RenderTheme.cpp
  src/theme/RenderTheme.h
  src/theme/ThemeManager.cpp
  src/theme/ThemeManager.h
)

add_library(MuffinUi STATIC
  ${MUFFIN_UI_SOURCES}
)

target_link_libraries(MuffinUi
  PUBLIC
    Qt6::Widgets
    MuffinCore
)

target_include_directories(MuffinUi
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(Muffin
  PRIVATE
    src/main.cpp
)

target_link_libraries(Muffin
  PRIVATE
    Qt6::Widgets
    MuffinCore
    MuffinUi
)

target_include_directories(Muffin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
  set_target_properties(Muffin PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set(MUFFIN_DIST_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Muffin distribution output directory")

set(MUFFIN_QT_PLUGIN_DIRS)
get_target_property(MUFFIN_QT_CORE_LOCATION_RELEASE Qt6::Core IMPORTED_LOCATION_RELEASE)
get_target_property(MUFFIN_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION)
if(NOT MUFFIN_QT_CORE_LOCATION_RELEASE)
  set(MUFFIN_QT_CORE_LOCATION_RELEASE "${MUFFIN_QT_CORE_LOCATION}")
endif()

if(MUFFIN_QT_CORE_LOCATION_RELEASE)
  get_filename_component(MUFFIN_QT_LIB_OR_BIN_DIR "${MUFFIN_QT_CORE_LOCATION_RELEASE}" DIRECTORY)
  get_filename_component(MUFFIN_QT_PACKAGE_DIR "${MUFFIN_QT_LIB_OR_BIN_DIR}" DIRECTORY)
  foreach(MUFFIN_QT_PLUGIN_SUBDIR platforms styles imageformats)
    if(EXISTS "${MUFFIN_QT_PACKAGE_DIR}/plugins/${MUFFIN_QT_PLUGIN_SUBDIR}")
      list(APPEND MUFFIN_QT_PLUGIN_DIRS "${MUFFIN_QT_PACKAGE_DIR}/plugins/${MUFFIN_QT_PLUGIN_SUBDIR}")
    endif()
  endforeach()
endif()

string(JOIN "|" MUFFIN_QT_PLUGIN_DIRS_ARG ${MUFFIN_QT_PLUGIN_DIRS})

add_custom_target(dist
  COMMAND
    ${CMAKE_COMMAND}
      "-DAPP_FILE=$<TARGET_FILE:Muffin>"
      "-DDIST_DIR=${MUFFIN_DIST_DIR}"
      "-DRUNTIME_DLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:Muffin>,|>"
      "-DQT_PLUGIN_DIRS=${MUFFIN_QT_PLUGIN_DIRS_ARG}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MuffinDeploy.cmake"
  DEPENDS Muffin
  COMMENT "Create Muffin distribution directory"
  VERBATIM
)

enable_testing()

add_executable(MuffinParserSmokeTest
  tests/parser/ParserSmokeTest.cpp
)

target_link_libraries(MuffinParserSmokeTest
  PRIVATE
    MuffinCore
)

add_test(NAME MuffinParserSmokeTest COMMAND MuffinParserSmokeTest)

add_executable(MuffinRenderLayoutTest
  tests/render/RenderLayoutTest.cpp
)

target_link_libraries(MuffinRenderLayoutTest
  PRIVATE
    MuffinUi
)

add_test(
  NAME MuffinRenderLayoutTest
  COMMAND MuffinRenderLayoutTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md"
)

add_executable(MuffinEditorControllerTest
  tests/editor/EditorControllerTest.cpp
)

target_link_libraries(MuffinEditorControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorControllerTest COMMAND MuffinEditorControllerTest)

add_executable(MuffinTableModelOpsTest
  tests/blocks/TableModelOpsTest.cpp
)

target_link_libraries(MuffinTableModelOpsTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinTableModelOpsTest COMMAND MuffinTableModelOpsTest)

add_executable(MuffinCodeFenceControllerTest
  tests/blocks/CodeFenceControllerTest.cpp
)

target_link_libraries(MuffinCodeFenceControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinCodeFenceControllerTest COMMAND MuffinCodeFenceControllerTest)

add_executable(MuffinMathBlockControllerTest
  tests/blocks/MathBlockControllerTest.cpp
)

target_link_libraries(MuffinMathBlockControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinMathBlockControllerTest COMMAND MuffinMathBlockControllerTest)

add_executable(MuffinHtmlBlockControllerTest
  tests/blocks/HtmlBlockControllerTest.cpp
)

target_link_libraries(MuffinHtmlBlockControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinHtmlBlockControllerTest COMMAND MuffinHtmlBlockControllerTest)
