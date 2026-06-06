cmake_minimum_required(VERSION 3.24)

project(Muffin
  VERSION 0.1.0
  DESCRIPTION "A fast native Markdown WYSIWYG editor built with C++ and Qt 6 Widgets"
  LANGUAGES C CXX
)

set(MUFFIN_VERSION_OVERRIDE "" CACHE STRING "Override Muffin application version")
set(MUFFIN_EFFECTIVE_VERSION "${PROJECT_VERSION}")
if(MUFFIN_VERSION_OVERRIDE)
  if(NOT MUFFIN_VERSION_OVERRIDE MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "MUFFIN_VERSION_OVERRIDE must use x.y.z format")
  endif()
  set(MUFFIN_EFFECTIVE_VERSION "${MUFFIN_VERSION_OVERRIDE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
  add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
find_package(ICU REQUIRED COMPONENTS uc i18n)
set(MUFFIN_QT_LINGUIST_BIN "" CACHE PATH "Optional Qt Linguist tool directory")
set(MUFFIN_LOCAL_QT_LINGUIST_BIN "C:/Program Files/Qt Linguist/bin")
if(NOT MUFFIN_QT_LINGUIST_BIN AND WIN32 AND EXISTS "${MUFFIN_LOCAL_QT_LINGUIST_BIN}/lupdate.exe")
  set(MUFFIN_QT_LINGUIST_BIN "${MUFFIN_LOCAL_QT_LINGUIST_BIN}")
endif()
if(MUFFIN_QT_LINGUIST_BIN)
  find_program(MUFFIN_QT_LINGUIST_LUPDATE_EXECUTABLE NAMES lupdate lupdate6 HINTS "${MUFFIN_QT_LINGUIST_BIN}" NO_DEFAULT_PATH)
  find_program(MUFFIN_QT_LINGUIST_LRELEASE_EXECUTABLE NAMES lrelease lrelease6 HINTS "${MUFFIN_QT_LINGUIST_BIN}" NO_DEFAULT_PATH)
endif()
if(MUFFIN_QT_LINGUIST_LUPDATE_EXECUTABLE)
  set(MUFFIN_LUPDATE_EXECUTABLE "${MUFFIN_QT_LINGUIST_LUPDATE_EXECUTABLE}")
else()
  find_program(MUFFIN_LUPDATE_EXECUTABLE NAMES pyside6-lupdate lupdate lupdate6)
endif()
if(MUFFIN_QT_LINGUIST_LRELEASE_EXECUTABLE)
  set(MUFFIN_LRELEASE_EXECUTABLE "${MUFFIN_QT_LINGUIST_LRELEASE_EXECUTABLE}")
else()
  find_program(MUFFIN_LRELEASE_EXECUTABLE NAMES pyside6-lrelease lrelease lrelease6)
endif()

set(MUFFIN_TRANSLATION_SOURCES
  translations/muffin_ja.ts
  translations/muffin_zh_CN.ts
  translations/muffin_vi.ts
  translations/muffin_fr.ts
  translations/muffin_es.ts
  translations/muffin_ru.ts
  translations/muffin_de.ts
  translations/muffin_pt_BR.ts
  translations/muffin_ko.ts
  translations/muffin_it.ts
  translations/muffin_zh_TW.ts
  translations/muffin_tr.ts
  translations/muffin_pl.ts
  translations/muffin_nl.ts
)

set(MUFFIN_TRANSLATION_BINARY_SOURCES
  translations/muffin_ja.qm
  translations/muffin_zh_CN.qm
  translations/muffin_vi.qm
  translations/muffin_fr.qm
  translations/muffin_es.qm
  translations/muffin_ru.qm
  translations/muffin_de.qm
  translations/muffin_pt_BR.qm
  translations/muffin_ko.qm
  translations/muffin_it.qm
  translations/muffin_zh_TW.qm
  translations/muffin_tr.qm
  translations/muffin_pl.qm
  translations/muffin_nl.qm
)

set(MUFFIN_TRANSLATABLE_SOURCES
  src/main.cpp
  src/app/LanguageManager.cpp
  src/app/MainWindow.cpp
  src/app/PreferencesDialog.cpp
  src/app/SidebarWidget.cpp
  src/app/DocumentSession.cpp
  src/editor/SourceEditorWidget.cpp
  src/render/BlockLayoutBuilder.cpp
  src/io/FileController.cpp
)

if(MUFFIN_LUPDATE_EXECUTABLE)
  add_custom_target(update_translations
    COMMAND
      "${MUFFIN_LUPDATE_EXECUTABLE}"
      ${MUFFIN_TRANSLATABLE_SOURCES}
      -ts ${MUFFIN_TRANSLATION_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Update Qt Linguist translation source files"
    VERBATIM
  )
endif()

if(MUFFIN_LRELEASE_EXECUTABLE)
  add_custom_target(release_translations
    COMMAND
      "${MUFFIN_LRELEASE_EXECUTABLE}"
      ${MUFFIN_TRANSLATION_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Compile Qt Linguist translation files"
    VERBATIM
  )
endif()

foreach(MUFFIN_TRANSLATION_BINARY_SOURCE IN LISTS MUFFIN_TRANSLATION_BINARY_SOURCES)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${MUFFIN_TRANSLATION_BINARY_SOURCE}")
    if(MUFFIN_LRELEASE_EXECUTABLE)
      message(STATUS "Generating missing translation resource ${MUFFIN_TRANSLATION_BINARY_SOURCE}")
      execute_process(
        COMMAND
          "${MUFFIN_LRELEASE_EXECUTABLE}"
          ${MUFFIN_TRANSLATION_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
      )
    else()
      message(FATAL_ERROR
        "Missing ${MUFFIN_TRANSLATION_BINARY_SOURCE}. "
        "Install Qt Linguist tools, provide lrelease on PATH, or commit the generated .qm file.")
    endif()
  endif()
endforeach()

set(CMARK_TESTS OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
set(CMARK_LIB_FUZZER OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cmark-gfm EXCLUDE_FROM_ALL)
if(TARGET libcmark-gfm-extensions_static AND TARGET libcmark-gfm_static)
  # The vendored static extension archive references symbols from the cmark core
  # archive. Declare that edge so GNU ld sees the dependent archive first.
  target_link_libraries(libcmark-gfm-extensions_static
    INTERFACE
      libcmark-gfm_static
  )
endif()

add_library(tree-sitter-runtime STATIC
  third_party/tree-sitter/lib/src/lib.c
)
target_include_directories(tree-sitter-runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/lib/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/lib/src
)

set(TREE_SITTER_LANGUAGE_SOURCES)
set(TREE_SITTER_LANGUAGE_INCLUDE_DIRS)

function(add_tree_sitter_grammar GRAMMAR_DIR)
  set(GRAMMAR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${GRAMMAR_DIR}")
  list(APPEND TREE_SITTER_LANGUAGE_SOURCES "${GRAMMAR_PATH}/src/parser.c")
  foreach(SCANNER scanner.c scanner.cc scanner.cpp)
    if(EXISTS "${GRAMMAR_PATH}/src/${SCANNER}")
      list(APPEND TREE_SITTER_LANGUAGE_SOURCES "${GRAMMAR_PATH}/src/${SCANNER}")
    endif()
  endforeach()
  list(APPEND TREE_SITTER_LANGUAGE_INCLUDE_DIRS "${GRAMMAR_PATH}/src")
  set(TREE_SITTER_LANGUAGE_SOURCES "${TREE_SITTER_LANGUAGE_SOURCES}" PARENT_SCOPE)
  set(TREE_SITTER_LANGUAGE_INCLUDE_DIRS "${TREE_SITTER_LANGUAGE_INCLUDE_DIRS}" PARENT_SCOPE)
endfunction()

function(add_tree_sitter_nested_grammar GRAMMAR_DIR SUBDIR)
  set(GRAMMAR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${GRAMMAR_DIR}/${SUBDIR}")
  list(APPEND TREE_SITTER_LANGUAGE_SOURCES "${GRAMMAR_PATH}/src/parser.c")
  foreach(SCANNER scanner.c scanner.cc scanner.cpp)
    if(EXISTS "${GRAMMAR_PATH}/src/${SCANNER}")
      list(APPEND TREE_SITTER_LANGUAGE_SOURCES "${GRAMMAR_PATH}/src/${SCANNER}")
    endif()
  endforeach()
  list(APPEND TREE_SITTER_LANGUAGE_INCLUDE_DIRS "${GRAMMAR_PATH}/src")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${GRAMMAR_DIR}/common")
    list(APPEND TREE_SITTER_LANGUAGE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/${GRAMMAR_DIR}/common")
  endif()
  set(TREE_SITTER_LANGUAGE_SOURCES "${TREE_SITTER_LANGUAGE_SOURCES}" PARENT_SCOPE)
  set(TREE_SITTER_LANGUAGE_INCLUDE_DIRS "${TREE_SITTER_LANGUAGE_INCLUDE_DIRS}" PARENT_SCOPE)
endfunction()

add_tree_sitter_grammar(third_party/tree-sitter-bash)
add_tree_sitter_grammar(third_party/tree-sitter-c)
add_tree_sitter_grammar(third_party/tree-sitter-cpp)
add_tree_sitter_grammar(third_party/tree-sitter-c-sharp)
add_tree_sitter_grammar(third_party/tree-sitter-css)
add_tree_sitter_grammar(third_party/tree-sitter-go)
add_tree_sitter_grammar(third_party/tree-sitter-html)
add_tree_sitter_grammar(third_party/tree-sitter-ini)
add_tree_sitter_grammar(third_party/tree-sitter-java)
add_tree_sitter_grammar(third_party/tree-sitter-javascript)
add_tree_sitter_grammar(third_party/tree-sitter-json)
add_tree_sitter_grammar(third_party/tree-sitter-lua)
add_tree_sitter_nested_grammar(third_party/tree-sitter-markdown tree-sitter-markdown)
add_tree_sitter_grammar(third_party/tree-sitter-objc)
add_tree_sitter_nested_grammar(third_party/tree-sitter-php php_only)
add_tree_sitter_grammar(third_party/tree-sitter-powershell)
add_tree_sitter_grammar(third_party/tree-sitter-python)
add_tree_sitter_grammar(third_party/tree-sitter-qmljs)
add_tree_sitter_grammar(third_party/tree-sitter-r)
add_tree_sitter_grammar(third_party/tree-sitter-ruby)
add_tree_sitter_grammar(third_party/tree-sitter-rust)
add_tree_sitter_grammar(third_party/tree-sitter-toml)
add_tree_sitter_nested_grammar(third_party/tree-sitter-typescript typescript)
add_tree_sitter_nested_grammar(third_party/tree-sitter-typescript tsx)
add_tree_sitter_nested_grammar(third_party/tree-sitter-xml xml)
add_tree_sitter_grammar(third_party/tree-sitter-yaml)

add_library(tree-sitter-languages STATIC
  ${TREE_SITTER_LANGUAGE_SOURCES}
)
set_target_properties(tree-sitter-languages PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(tree-sitter-languages
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/lib/include
  PRIVATE
    ${TREE_SITTER_LANGUAGE_INCLUDE_DIRS}
)

set(MUFFIN_CORE_SOURCES
  src/app/DocumentSession.cpp
  src/app/DocumentSession.h
  src/document/NodeId.cpp
  src/document/NodeId.h
  src/document/SourceRange.h
  src/document/LineStartOffsetCache.cpp
  src/document/LineStartOffsetCache.h
  src/document/TopLevelRangeChange.h
  src/document/MarkdownTypes.h
  src/document/InlineNode.cpp
  src/document/InlineNode.h
  src/document/InlineProjection.cpp
  src/document/InlineProjection.h
  src/document/MarkdownNode.cpp
  src/document/MarkdownNode.h
  src/document/TextSelection.h
  src/document/SelectionSerializer.cpp
  src/document/SelectionSerializer.h
  src/document/NodeIndex.cpp
  src/document/NodeIndex.h
  src/document/MarkdownDocument.cpp
  src/document/MarkdownDocument.h
  src/document/OutlineBuilder.cpp
  src/document/OutlineBuilder.h
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
    libcmark-gfm-extensions_static
    libcmark-gfm_static
)

target_include_directories(MuffinCore
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/cmark-gfm/src
)

add_executable(Muffin
  src/translations.qrc
  src/app/CommandRegistry.cpp
  src/app/CommandRegistry.h
  src/app/LanguageManager.cpp
  src/app/LanguageManager.h
  src/app/MainWindow.cpp
  src/app/MainWindow.h
  src/app/PreferencesDialog.cpp
  src/app/PreferencesDialog.h
  src/io/FileController.cpp
  src/io/FileController.h
)

set(MUFFIN_UI_SOURCES
  src/katex_fonts.qrc
  src/table_toolbar_icons.qrc
  src/statusbar_icons.qrc
  third_party/katex/src/fontMetricsData.js
  third_party/katex/src/svgGeometry.ts
  third_party/katex/src/symbols.ts
  src/tree_sitter_queries.qrc
  src/blocks/code/CodeFenceController.cpp
  src/blocks/code/CodeFenceController.h
  src/blocks/frontmatter/FrontMatterController.cpp
  src/blocks/frontmatter/FrontMatterController.h
  src/blocks/html/HtmlBlockController.cpp
  src/blocks/html/HtmlBlockController.h
  src/blocks/html/HtmlSanitizer.cpp
  src/blocks/literal/LiteralBlockController.cpp
  src/blocks/literal/LiteralBlockController.h
  src/blocks/html/HtmlSanitizer.h
  src/blocks/math/MathBlockController.cpp
  src/blocks/math/MathBlockController.h
  src/blocks/math/MathRenderer.cpp
  src/blocks/math/MathRenderer.h
  src/blocks/table/TableController.cpp
  src/blocks/table/TableController.h
  src/blocks/table/TableCellSourceEdit.cpp
  src/blocks/table/TableCellSourceEdit.h
  src/app/SidebarWidget.cpp
  src/app/SidebarWidget.h
  src/editor/SourceEditorWidget.cpp
  src/editor/SourceEditorWidget.h
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
  src/editor/BlockEditContext.cpp
  src/editor/BlockEditContext.h
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
  src/editor/TextBlockCommandBuilder.cpp
  src/editor/TextBlockCommandBuilder.h
  src/math/MathBuilder.cpp
  src/math/MathBuilder.h
  src/math/MathDelimiter.cpp
  src/math/MathDelimiter.h
  src/math/MathFontMetrics.cpp
  src/math/MathFontMetrics.h
  src/math/MathFontRegistry.cpp
  src/math/MathFontRegistry.h
  src/math/MathFunctionRegistry.cpp
  src/math/MathFunctionRegistry.h
  src/math/MathLexer.cpp
  src/math/MathLexer.h
  src/math/MathLayoutTree.cpp
  src/math/MathLayoutTree.h
  src/math/MathMacroExpander.cpp
  src/math/MathMacroExpander.h
  src/math/MathOptions.cpp
  src/math/MathOptions.h
  src/math/MathParseNode.h
  src/math/MathParseError.cpp
  src/math/MathParseError.h
  src/math/MathParser.cpp
  src/math/MathParser.h
  src/math/MathRenderer.cpp
  src/math/MathRenderer.h
  src/math/MathRenderNode.cpp
  src/math/MathRenderNode.h
  src/math/MathSettings.cpp
  src/math/MathSettings.h
  src/math/MathStyle.cpp
  src/math/MathStyle.h
  src/math/MathSvgGeometry.cpp
  src/math/MathSvgGeometry.h
  src/math/MathSymbols.cpp
  src/math/MathSymbols.h
  src/render/BlockLayout.cpp
  src/render/BlockLayout.h
  src/render/CodeHighlight.h
  src/render/BlockLayoutBuilder.cpp
  src/render/BlockLayoutBuilder.h
  src/render/DocumentLayout.cpp
  src/render/DocumentLayout.h
  src/render/InlineLayout.cpp
  src/render/InlineLayout.h
  src/render/TreeSitterHighlighter.cpp
  src/render/TreeSitterHighlighter.h
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
    tree-sitter-runtime
    tree-sitter-languages
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
    Qt6::Svg
    ICU::uc
    ICU::i18n
    MuffinCore
    MuffinUi
)

target_include_directories(Muffin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(Muffin
  PRIVATE
    MUFFIN_VERSION="${MUFFIN_EFFECTIVE_VERSION}"
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

set(MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS)
if(MUFFIN_QT_CORE_LOCATION_RELEASE)
  get_filename_component(MUFFIN_QT_RUNTIME_DIR "${MUFFIN_QT_CORE_LOCATION_RELEASE}" DIRECTORY)
  list(APPEND MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS "PATH=path_list_prepend:${MUFFIN_QT_RUNTIME_DIR}")
endif()
set(MUFFIN_CONAN_QT_BUILD_BIN "C:/Users/jstzw/.conan2/p/b/qt584dfe634b273/b/build/qtbase/bin")
if(EXISTS "${MUFFIN_CONAN_QT_BUILD_BIN}")
  list(APPEND MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS "PATH=path_list_prepend:${MUFFIN_CONAN_QT_BUILD_BIN}")
endif()
if(MUFFIN_QT_PLUGIN_DIRS_ARG)
  list(APPEND MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS "QT_PLUGIN_PATH=set:${MUFFIN_QT_PLUGIN_DIRS_ARG}")
endif()
if(APPLE)
  list(APPEND MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS "QT_QPA_PLATFORM=set:offscreen")
endif()
add_executable(MuffinParserSmokeTest
  tests/parser/ParserSmokeTest.cpp
)

target_link_libraries(MuffinParserSmokeTest
  PRIVATE
    MuffinCore
)

add_test(NAME MuffinParserSmokeTest COMMAND MuffinParserSmokeTest)
set_tests_properties(MuffinParserSmokeTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinOutlineBuilderTest
  tests/document/OutlineBuilderTest.cpp
)

target_link_libraries(MuffinOutlineBuilderTest
  PRIVATE
    MuffinCore
)

add_test(NAME MuffinOutlineBuilderTest COMMAND MuffinOutlineBuilderTest)
set_tests_properties(MuffinOutlineBuilderTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

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
set_tests_properties(MuffinRenderLayoutTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorControllerTest
  tests/editor/EditorControllerTest.cpp
)

target_link_libraries(MuffinEditorControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorControllerTest COMMAND MuffinEditorControllerTest)
set_tests_properties(MuffinEditorControllerTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinTableModelOpsTest
  tests/blocks/TableModelOpsTest.cpp
)

target_link_libraries(MuffinTableModelOpsTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinTableModelOpsTest COMMAND MuffinTableModelOpsTest)
set_tests_properties(MuffinTableModelOpsTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinCodeFenceControllerTest
  tests/blocks/CodeFenceControllerTest.cpp
)

target_link_libraries(MuffinCodeFenceControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinCodeFenceControllerTest COMMAND MuffinCodeFenceControllerTest)
set_tests_properties(MuffinCodeFenceControllerTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinMathBlockControllerTest
  tests/blocks/MathBlockControllerTest.cpp
)

target_link_libraries(MuffinMathBlockControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinMathBlockControllerTest COMMAND MuffinMathBlockControllerTest)
set_tests_properties(MuffinMathBlockControllerTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinHtmlBlockControllerTest
  tests/blocks/HtmlBlockControllerTest.cpp
)

target_link_libraries(MuffinHtmlBlockControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinHtmlBlockControllerTest COMMAND MuffinHtmlBlockControllerTest)
set_tests_properties(MuffinHtmlBlockControllerTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinTranslationResourceTest
  src/translations.qrc
  tests/app/TranslationResourceTest.cpp
)

target_link_libraries(MuffinTranslationResourceTest
  PRIVATE
    Qt6::Widgets
)

add_test(NAME MuffinTranslationResourceTest COMMAND MuffinTranslationResourceTest)
set_tests_properties(MuffinTranslationResourceTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)
