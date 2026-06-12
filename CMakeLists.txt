cmake_minimum_required(VERSION 3.24)

project(Muffin
  VERSION 0.2.1
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

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg PrintSupport Network)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(WebP REQUIRED)
find_package(libavif REQUIRED)
find_package(Python3 COMPONENTS Interpreter QUIET)
set(MUFFIN_QT_LINGUIST_BIN "" CACHE PATH "Optional Qt Linguist tool directory")
set(MUFFIN_QT_TRANSLATION_SOURCE_DIR "" CACHE PATH "Optional directory containing Qt qtbase_*.qm translation files")
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

# Qt base translations for standard dialog buttons (Save, Discard, Cancel, etc.)
set(MUFFIN_QT_TRANSLATION_BINARY_SOURCES
  translations/qtbase_ja.qm
  translations/qtbase_zh_CN.qm
  translations/qtbase_fr.qm
  translations/qtbase_es.qm
  translations/qtbase_ru.qm
  translations/qtbase_de.qm
  translations/qtbase_pt_BR.qm
  translations/qtbase_ko.qm
  translations/qtbase_it.qm
  translations/qtbase_zh_TW.qm
  translations/qtbase_tr.qm
  translations/qtbase_pl.qm
  translations/qtbase_nl.qm
)

set(MUFFIN_TRANSLATABLE_SOURCES
  src/main.cpp
  src/app/LanguageManager.cpp
  src/app/MainWindow.cpp
  src/app/MainWindowActionBinder.cpp
  src/app/MainWindowMenus.cpp
  src/app/MainWindowSignalBinder.cpp
  src/app/MainWindowActions.cpp
  src/app/MainWindowFileOps.cpp
  src/app/MainWindowFind.cpp
  src/app/PreferencesDialog.cpp
  src/app/PreferencesPage.cpp
  src/app/PrefsFilesPage.cpp
  src/app/PrefsEditorPage.cpp
  src/app/PrefsImagePage.cpp
  src/app/PrefsMarkdownPage.cpp
  src/app/PrefsExportPage.cpp
  src/app/PrefsAppearancePage.cpp
  src/app/PrefsGeneralPage.cpp
  src/app/UpdateChecker.cpp
  src/app/SidebarWidget.cpp
  src/document/DocumentSession.cpp
  src/editor/SourceEditorWidget.cpp
  src/editor/FindBarWidget.cpp
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

set(MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS)
if(MUFFIN_QT_TRANSLATION_SOURCE_DIR)
  list(APPEND MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS "${MUFFIN_QT_TRANSLATION_SOURCE_DIR}")
endif()
if(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_TRANSLATIONS)
  list(APPEND MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
endif()
if(DEFINED QT_HOST_PATH)
  list(APPEND MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS "${QT_HOST_PATH}/translations")
endif()
if(DEFINED ENV{QT_HOST_PATH})
  list(APPEND MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS "$ENV{QT_HOST_PATH}/translations")
endif()
if(Qt6_DIR)
  get_filename_component(MUFFIN_QT_PREFIX_FROM_QT6_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
  list(APPEND MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS "${MUFFIN_QT_PREFIX_FROM_QT6_DIR}/translations")
endif()
if(Qt6Core_DIR)
  get_filename_component(MUFFIN_QT_PREFIX_FROM_QT6_CORE_DIR "${Qt6Core_DIR}/../../.." ABSOLUTE)
  list(APPEND MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS "${MUFFIN_QT_PREFIX_FROM_QT6_CORE_DIR}/translations")
endif()
if(Python3_Interpreter_FOUND)
  execute_process(
    COMMAND
      "${Python3_EXECUTABLE}"
      -c "from pathlib import Path; import PySide6; p = Path(PySide6.__file__).resolve().parent; print(p / 'translations'); print(p / 'Qt' / 'translations')"
    RESULT_VARIABLE MUFFIN_PYSIDE6_TRANSLATIONS_RESULT
    OUTPUT_VARIABLE MUFFIN_PYSIDE6_TRANSLATIONS_DIR
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(MUFFIN_PYSIDE6_TRANSLATIONS_RESULT EQUAL 0)
    string(REPLACE "\n" ";" MUFFIN_PYSIDE6_TRANSLATIONS_DIRS "${MUFFIN_PYSIDE6_TRANSLATIONS_DIR}")
    list(APPEND MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS ${MUFFIN_PYSIDE6_TRANSLATIONS_DIRS})
  endif()
endif()
list(REMOVE_DUPLICATES MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS)

foreach(MUFFIN_QT_TRANSLATION_BINARY_SOURCE IN LISTS MUFFIN_QT_TRANSLATION_BINARY_SOURCES)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${MUFFIN_QT_TRANSLATION_BINARY_SOURCE}")
    set(MUFFIN_QT_TRANSLATION_COPIED FALSE)
    get_filename_component(MUFFIN_QT_TRANSLATION_FILE_NAME "${MUFFIN_QT_TRANSLATION_BINARY_SOURCE}" NAME)
    foreach(MUFFIN_QT_TRANSLATION_CANDIDATE_DIR IN LISTS MUFFIN_QT_TRANSLATION_CANDIDATE_DIRS)
      if(EXISTS "${MUFFIN_QT_TRANSLATION_CANDIDATE_DIR}/${MUFFIN_QT_TRANSLATION_FILE_NAME}")
        message(STATUS
          "Copying Qt translation ${MUFFIN_QT_TRANSLATION_FILE_NAME} "
          "from ${MUFFIN_QT_TRANSLATION_CANDIDATE_DIR}")
        file(COPY_FILE
          "${MUFFIN_QT_TRANSLATION_CANDIDATE_DIR}/${MUFFIN_QT_TRANSLATION_FILE_NAME}"
          "${CMAKE_CURRENT_SOURCE_DIR}/${MUFFIN_QT_TRANSLATION_BINARY_SOURCE}"
          ONLY_IF_DIFFERENT
        )
        set(MUFFIN_QT_TRANSLATION_COPIED TRUE)
        break()
      endif()
    endforeach()
    if(NOT MUFFIN_QT_TRANSLATION_COPIED)
      message(FATAL_ERROR
        "Missing ${MUFFIN_QT_TRANSLATION_BINARY_SOURCE}, and no Qt translation source "
        "directory contained ${MUFFIN_QT_TRANSLATION_FILE_NAME}. "
        "Install PySide6, provide a Qt installation with translations, or set "
        "MUFFIN_QT_TRANSLATION_SOURCE_DIR.")
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

# Lexbor and Yoga are provided by Conan
find_package(lexbor REQUIRED)
find_package(yoga REQUIRED)

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
  src/document/DocumentSession.cpp
  src/document/DocumentSession.h
  src/document/NodeId.cpp
  src/document/NodeId.h
  src/document/SourceRange.h
  src/document/PendingBlockMarker.cpp
  src/document/PendingBlockMarker.h
  src/document/SourceRangeUtil.cpp
  src/document/SourceRangeUtil.h
  src/document/LineStartOffsetCache.cpp
  src/document/LineStartOffsetCache.h
  src/document/TopLevelRangeChange.h
  src/document/MarkdownTypes.h
  src/document/DefinitionBlock.cpp
  src/document/DefinitionBlock.h
  src/document/InlineNode.cpp
  src/document/InlineNode.h
  src/document/MarkdownNode.cpp
  src/document/MarkdownNode.h
  src/document/TextSelection.h
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
    Qt6::Core
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
  src/icons.qrc
  src/app/CommandRegistry.cpp
  src/app/CommandRegistry.h
  src/app/EditorBackend.h
  src/app/LanguageManager.cpp
  src/app/LanguageManager.h
  src/app/MainWindow.cpp
  src/app/MainWindow.h
  src/app/MainWindowActionBinder.cpp
  src/app/MainWindowActionBinder.h
  src/app/MainWindowMenus.cpp
  src/app/MainWindowActions.cpp
  src/app/MainWindowSignalBinder.cpp
  src/app/MainWindowSignalBinder.h
  src/app/UpdateChecker.cpp
  src/app/UpdateChecker.h
  src/app/MainWindowFileOps.cpp
  src/app/MainWindowFind.cpp
  src/app/RenderEditorBackend.cpp
  src/app/RenderEditorBackend.h
  src/app/SourceEditorBackend.cpp
  src/app/SourceEditorBackend.h
  src/app/PreferencesDialog.cpp
  src/app/PreferencesDialog.h
  src/app/RenderCommandFacade.cpp
  src/app/RenderCommandFacade.h
  src/app/PreferencesPage.cpp
  src/app/PreferencesPage.h
  src/app/PrefsFilesPage.cpp
  src/app/PrefsFilesPage.h
  src/app/PrefsEditorPage.cpp
  src/app/PrefsEditorPage.h
  src/app/PrefsImagePage.cpp
  src/app/PrefsImagePage.h
  src/app/PrefsMarkdownPage.cpp
  src/app/PrefsMarkdownPage.h
  src/app/PrefsExportPage.cpp
  src/app/PrefsExportPage.h
  src/app/PrefsAppearancePage.cpp
  src/app/PrefsAppearancePage.h
  src/app/PrefsGeneralPage.cpp
  src/app/PrefsGeneralPage.h
  src/io/FileController.cpp
  src/io/FileController.h
)

set(MUFFIN_UI_SOURCES
  src/katex_fonts.qrc
  third_party/katex/src/fontMetricsData.js
  third_party/katex/src/svgGeometry.ts
  third_party/katex/src/symbols.ts
  src/tree_sitter_queries.qrc
  src/projection/InlineProjection.cpp
  src/projection/InlineProjection.h
  src/projection/SelectionSerializer.cpp
  src/projection/SelectionSerializer.h
  src/blocks/code/CodeFenceController.cpp
  src/blocks/code/CodeFenceController.h
  src/blocks/html/HtmlSanitizer.cpp
  src/blocks/literal/LiteralBlockController.cpp
  src/blocks/literal/LiteralBlockController.h
  src/blocks/literal/LiteralBlockUtil.cpp
  src/blocks/literal/LiteralBlockUtil.h
  src/blocks/html/HtmlSanitizer.h
  src/html/HtmlBox.cpp
  src/html/HtmlBox.h
  src/html/HtmlParser.cpp
  src/html/HtmlParser.h
  src/html/HtmlBoxBuilder.cpp
  src/html/HtmlBoxBuilder.h
  src/html/HtmlStyleResolver.cpp
  src/html/HtmlStyleResolver.h
  src/html/HtmlLayoutEngine.cpp
  src/html/HtmlLayoutEngine.h
  src/html/HtmlTextMeasurer.cpp
  src/html/HtmlTextMeasurer.h
  src/html/HtmlLayoutResult.cpp
  src/html/HtmlLayoutResult.h
  src/html/HtmlRenderer.cpp
  src/html/HtmlRenderer.h
  src/html/InlineHtmlRenderer.cpp
  src/html/InlineHtmlRenderer.h
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
  src/editor/FindBarWidget.cpp
  src/editor/FindBarWidget.h
  src/blocks/table/TableModelOps.cpp
  src/blocks/table/TableModelOps.h
  src/commands/ParagraphController.cpp
  src/commands/ParagraphController.h
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
  src/editor/EditorContext.cpp
  src/editor/EditorContext.h
  src/editor/EditorContextHolder.h
  src/editor/CursorPosition.h
  src/editor/EditorController.cpp
  src/editor/EditorController.h
  src/editor/EditorControllerUndo.cpp
  src/editor/EditorView.cpp
  src/editor/EditorView.h
  src/editor/CodeLanguageEditor.cpp
  src/editor/CodeLanguageEditor.h
  src/editor/TableToolbar.cpp
  src/editor/TableToolbar.h
  src/editor/InputController.cpp
  src/editor/InputController.h
  src/editor/InputControllerEdit.cpp
  src/io/ImageFileOps.cpp
  src/io/ImageFileOps.h
  src/editor/SelectionController.cpp
  src/editor/SelectionController.h
  src/editor/TextBlockCommandBuilder.cpp
  src/editor/TextBlockCommandBuilder.h
  src/math/MathBuilder.cpp
  src/math/MathBuilder.h
  src/math/MathBuilderArray.cpp
  src/math/MathBuilderSpacing.cpp
  src/math/MathDelimiter.cpp
  src/math/MathDelimiter.h
  src/math/MathFontMetrics.cpp
  src/math/MathFontMetrics.h
  src/math/MathFontRegistry.cpp
  src/math/MathFontRegistry.h
  src/math/MathDimension.cpp
  src/math/MathDimension.h
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
  src/math/MathParserEnvironment.cpp
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
  src/render/ImageDecoder.cpp
  src/render/ImageDecoder.h
  src/render/ImageLoader.cpp
  src/render/ImageLoader.h
  src/render/ImagePlaceholder.cpp
  src/render/ImagePlaceholder.h
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
    Qt6::Network
    MuffinCore
    tree-sitter-runtime
    tree-sitter-languages
    ICU::uc
    ICU::i18n
  PRIVATE
    WebP::webp
    avif
    Qt6::Svg
    lexbor::lexbor_static
    yoga::yoga
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
    Qt6::PrintSupport
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

set(MUFFIN_APP_ICONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/app-icons")

if(WIN32)
  set_target_properties(Muffin PROPERTIES WIN32_EXECUTABLE TRUE)
  # Embed the application icon into the .exe so it shows in Explorer, the
  # taskbar, and Start Menu shortcuts. Use forward slashes in the RC file
  # because backslashes collide with C escapes (e.g. \r in \resources).
  set(MUFFIN_APP_ICON_ICO "${MUFFIN_APP_ICONS_DIR}/muffin.ico")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/muffin.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/muffin.rc"
    @ONLY
  )
  target_sources(Muffin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/muffin.rc")
endif()

if(APPLE)
  set_target_properties(Muffin PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE "muffin"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
  )
  # Place the icon set in Muffin.app/Contents/Resources/ (CFBundleIconFile = muffin).
  set(MUFFIN_APP_ICON_ICNS "${MUFFIN_APP_ICONS_DIR}/muffin.icns")
  set_source_files_properties("${MUFFIN_APP_ICON_ICNS}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
  target_sources(Muffin PRIVATE "${MUFFIN_APP_ICON_ICNS}")
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
  foreach(MUFFIN_QT_PLUGIN_SUBDIR platforms styles imageformats tls networkinformation)
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

# --- Windows MSI installer target ---
if(WIN32)
  find_program(WIX_COMMAND wix)
  if(WIX_COMMAND)
    add_custom_target(msi
      COMMAND ${CMAKE_COMMAND}
        "-DDIST_DIR=${MUFFIN_DIST_DIR}"
        "-DWIX_COMMAND=${WIX_COMMAND}"
        "-DMUFFIN_VERSION=${MUFFIN_EFFECTIVE_VERSION}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildMSI.cmake"
      DEPENDS dist
      COMMENT "Build Muffin MSI installer"
      VERBATIM
    )
  endif()
endif()

# --- macOS DMG target ---
if(APPLE)
  # Locate macdeployqt relative to Qt6::Core
  # On macOS frameworks, Qt6::Core is at <prefix>/lib/QtCore.framework/Versions/A/QtCore
  # and macdeployqt is at <prefix>/bin/macdeployqt or <prefix>/libexec/macdeployqt.
  set(_muffin_macdeployqt "${MUFFIN_MACDEPLOYQT}" CACHE FILEPATH "Path to macdeployqt")
  set(_muffin_qt_hints "")
  if(NOT _muffin_macdeployqt AND MUFFIN_QT_CORE_LOCATION_RELEASE)
    # Walk up from the library location to find the Qt prefix
    get_filename_component(_qt_candidate "${MUFFIN_QT_CORE_LOCATION_RELEASE}" DIRECTORY)
    while(_qt_candidate AND NOT _muffin_macdeployqt)
      foreach(_subdir bin libexec)
        if(EXISTS "${_qt_candidate}/${_subdir}/macdeployqt")
          set(_muffin_macdeployqt "${_qt_candidate}/${_subdir}/macdeployqt")
          break()
        endif()
      endforeach()
      if(NOT _muffin_macdeployqt)
        list(APPEND _muffin_qt_hints "${_qt_candidate}/bin" "${_qt_candidate}/libexec")
      endif()
      get_filename_component(_qt_parent "${_qt_candidate}" DIRECTORY)
      if(_qt_parent STREQUAL _qt_candidate)
        break()
      endif()
      set(_qt_candidate "${_qt_parent}")
    endwhile()
  endif()
  if(NOT _muffin_macdeployqt)
    # Also try from Qt6::qmake location
    if(TARGET Qt6::qmake)
      get_target_property(_qt_qmake_location Qt6::qmake IMPORTED_LOCATION)
      if(NOT _qt_qmake_location)
        get_target_property(_qt_qmake_location Qt6::qmake LOCATION)
      endif()
      if(_qt_qmake_location)
        get_filename_component(_qt_bin_dir "${_qt_qmake_location}" DIRECTORY)
        list(APPEND _muffin_qt_hints "${_qt_bin_dir}")
      endif()
    endif()
    find_program(_muffin_macdeployqt macdeployqt HINTS ${_muffin_qt_hints})
  endif()

  if(_muffin_macdeployqt)
    message(STATUS "Found macdeployqt: ${_muffin_macdeployqt}")
    add_custom_target(dmg
      COMMAND ${CMAKE_COMMAND}
        "-DDIST_DIR=${MUFFIN_DIST_DIR}"
        "-DMUFFIN_VERSION=${MUFFIN_EFFECTIVE_VERSION}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DMACDEPLOYQT=${_muffin_macdeployqt}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildDMG.cmake"
      DEPENDS dist
      COMMENT "Build Muffin DMG disk image"
      VERBATIM
    )
  else()
    message(WARNING "macdeployqt not found — DMG target will not be available")
  endif()
endif()

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
add_executable(MuffinParserBasicTest
  tests/parser/ParserBasicTest.cpp
)

target_link_libraries(MuffinParserBasicTest
  PRIVATE
    MuffinCore
)

add_test(NAME MuffinParserBasicTest COMMAND MuffinParserBasicTest)
set_tests_properties(MuffinParserBasicTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinParserDefinitionTest
  tests/parser/ParserDefinitionTest.cpp
)

target_link_libraries(MuffinParserDefinitionTest
  PRIVATE
    MuffinCore
)

add_test(NAME MuffinParserDefinitionTest COMMAND MuffinParserDefinitionTest)
set_tests_properties(MuffinParserDefinitionTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinParserMathCodeTest
  tests/parser/ParserMathCodeTest.cpp
)

target_link_libraries(MuffinParserMathCodeTest
  PRIVATE
    MuffinCore
)

add_test(NAME MuffinParserMathCodeTest COMMAND MuffinParserMathCodeTest)
set_tests_properties(MuffinParserMathCodeTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinOutlineBuilderTest
  tests/document/OutlineBuilderTest.cpp
)

target_link_libraries(MuffinOutlineBuilderTest
  PRIVATE
    MuffinCore
)

add_test(NAME MuffinOutlineBuilderTest COMMAND MuffinOutlineBuilderTest)
set_tests_properties(MuffinOutlineBuilderTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinRenderThemeTest
  tests/render/RenderThemeTest.cpp
)

target_link_libraries(MuffinRenderThemeTest
  PRIVATE
    MuffinUi
)

add_test(
  NAME MuffinRenderThemeTest
  COMMAND MuffinRenderThemeTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md"
)
set_tests_properties(MuffinRenderThemeTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)
if(APPLE)
  set_tests_properties(MuffinRenderThemeTest PROPERTIES DISABLED YES)
endif()

add_executable(MuffinRenderIncrementalTest
  tests/render/RenderIncrementalTest.cpp
)

target_link_libraries(MuffinRenderIncrementalTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderIncrementalTest COMMAND MuffinRenderIncrementalTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderIncrementalTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderListMarkerTest
  tests/render/RenderListMarkerTest.cpp
)

target_link_libraries(MuffinRenderListMarkerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderListMarkerTest COMMAND MuffinRenderListMarkerTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderListMarkerTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderInlineProjectionTest
  tests/render/RenderInlineProjectionTest.cpp
)

target_link_libraries(MuffinRenderInlineProjectionTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderInlineProjectionTest COMMAND MuffinRenderInlineProjectionTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderInlineProjectionTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderInlineGeometryTest
  tests/render/RenderInlineGeometryTest.cpp
)

target_link_libraries(MuffinRenderInlineGeometryTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderInlineGeometryTest COMMAND MuffinRenderInlineGeometryTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderInlineGeometryTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderInlineHitTestTest
  tests/render/RenderInlineHitTest.cpp
)

target_link_libraries(MuffinRenderInlineHitTestTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderInlineHitTestTest COMMAND MuffinRenderInlineHitTestTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderInlineHitTestTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderMathLayoutTest
  tests/render/RenderMathLayoutTest.cpp
)

target_link_libraries(MuffinRenderMathLayoutTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderMathLayoutTest COMMAND MuffinRenderMathLayoutTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderMathLayoutTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderMathGeometryTest
  tests/render/RenderMathGeometryTest.cpp
)

target_link_libraries(MuffinRenderMathGeometryTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderMathGeometryTest COMMAND MuffinRenderMathGeometryTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderMathGeometryTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderMathAuditTest
  tests/render/RenderMathAuditTest.cpp
)

target_link_libraries(MuffinRenderMathAuditTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderMathAuditTest COMMAND MuffinRenderMathAuditTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderMathAuditTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderMathFunctions1Test
  tests/render/RenderMathFunctions1Test.cpp
)

target_link_libraries(MuffinRenderMathFunctions1Test
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderMathFunctions1Test COMMAND MuffinRenderMathFunctions1Test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderMathFunctions1Test PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderMathFunctions2Test
  tests/render/RenderMathFunctions2Test.cpp
)

target_link_libraries(MuffinRenderMathFunctions2Test
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderMathFunctions2Test COMMAND MuffinRenderMathFunctions2Test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderMathFunctions2Test PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)
if(APPLE)
  set_tests_properties(
    MuffinRenderMathLayoutTest
    MuffinRenderMathGeometryTest
    MuffinRenderMathAuditTest
    MuffinRenderMathFunctions1Test
    MuffinRenderMathFunctions2Test
    PROPERTIES DISABLED YES)
endif()

add_executable(MuffinRenderHtmlLayoutTest
  tests/render/RenderHtmlLayoutTest.cpp
)

target_link_libraries(MuffinRenderHtmlLayoutTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderHtmlLayoutTest COMMAND MuffinRenderHtmlLayoutTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderHtmlLayoutTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderHtmlPaintTest
  tests/render/RenderHtmlPaintTest.cpp
)

target_link_libraries(MuffinRenderHtmlPaintTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderHtmlPaintTest COMMAND MuffinRenderHtmlPaintTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderHtmlPaintTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinRenderTreeSitterTest
  tests/render/RenderTreeSitterTest.cpp
)

target_link_libraries(MuffinRenderTreeSitterTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinRenderTreeSitterTest COMMAND MuffinRenderTreeSitterTest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/render_smoke.md")
set_tests_properties(MuffinRenderTreeSitterTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorViewProjectionSourceTest
  tests/render/EditorViewProjectionSourceTest.cpp
)

target_link_libraries(MuffinEditorViewProjectionSourceTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorViewProjectionSourceTest COMMAND MuffinEditorViewProjectionSourceTest)
set_tests_properties(MuffinEditorViewProjectionSourceTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorViewProjectionMappingTest
  tests/render/EditorViewProjectionMappingTest.cpp
)

target_link_libraries(MuffinEditorViewProjectionMappingTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorViewProjectionMappingTest COMMAND MuffinEditorViewProjectionMappingTest)
set_tests_properties(MuffinEditorViewProjectionMappingTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorViewHitTestTest
  tests/render/EditorViewHitTestTest.cpp
)

target_link_libraries(MuffinEditorViewHitTestTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorViewHitTestTest COMMAND MuffinEditorViewHitTestTest)
set_tests_properties(MuffinEditorViewHitTestTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorViewSelectionTest
  tests/render/EditorViewSelectionTest.cpp
)

target_link_libraries(MuffinEditorViewSelectionTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorViewSelectionTest COMMAND MuffinEditorViewSelectionTest)
set_tests_properties(MuffinEditorViewSelectionTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorViewLayoutTest
  tests/render/EditorViewLayoutTest.cpp
)

target_link_libraries(MuffinEditorViewLayoutTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorViewLayoutTest COMMAND MuffinEditorViewLayoutTest)
set_tests_properties(MuffinEditorViewLayoutTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorCoreTest
  tests/editor/EditorCoreTest.cpp
)

target_link_libraries(MuffinEditorCoreTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorCoreTest COMMAND MuffinEditorCoreTest)
set_tests_properties(MuffinEditorCoreTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorStyleClipboardTest
  tests/editor/EditorStyleClipboardTest.cpp
)

target_link_libraries(MuffinEditorStyleClipboardTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorStyleClipboardTest COMMAND MuffinEditorStyleClipboardTest)
set_tests_properties(MuffinEditorStyleClipboardTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinEditorCodeFenceSelectionTest
  tests/editor/EditorCodeFenceSelectionTest.cpp
)

target_link_libraries(MuffinEditorCodeFenceSelectionTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinEditorCodeFenceSelectionTest COMMAND MuffinEditorCodeFenceSelectionTest)
set_tests_properties(MuffinEditorCodeFenceSelectionTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinInputTextEditingTest
  tests/editor/InputTextEditingTest.cpp
)

target_link_libraries(MuffinInputTextEditingTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinInputTextEditingTest COMMAND MuffinInputTextEditingTest)
set_tests_properties(MuffinInputTextEditingTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinInputParagraphTest
  tests/editor/InputParagraphTest.cpp
)

target_link_libraries(MuffinInputParagraphTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinInputParagraphTest COMMAND MuffinInputParagraphTest)
set_tests_properties(MuffinInputParagraphTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinInputUndoSelectionTest
  tests/editor/InputUndoSelectionTest.cpp
)

target_link_libraries(MuffinInputUndoSelectionTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinInputUndoSelectionTest COMMAND MuffinInputUndoSelectionTest)
set_tests_properties(MuffinInputUndoSelectionTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinInputListTabTest
  tests/editor/InputListTabTest.cpp
)

target_link_libraries(MuffinInputListTabTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinInputListTabTest COMMAND MuffinInputListTabTest)
set_tests_properties(MuffinInputListTabTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinInputLazyBlockTest
  tests/editor/InputLazyBlockTest.cpp
)

target_link_libraries(MuffinInputLazyBlockTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinInputLazyBlockTest COMMAND MuffinInputLazyBlockTest)
set_tests_properties(MuffinInputLazyBlockTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinInputLiteralMergeTest
  tests/editor/InputLiteralMergeTest.cpp
)

target_link_libraries(MuffinInputLiteralMergeTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinInputLiteralMergeTest COMMAND MuffinInputLiteralMergeTest)
set_tests_properties(MuffinInputLiteralMergeTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinCodeFenceCommitCursorTest
  tests/editor/CodeFenceCommitCursorTest.cpp
)

target_link_libraries(MuffinCodeFenceCommitCursorTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinCodeFenceCommitCursorTest COMMAND MuffinCodeFenceCommitCursorTest)
set_tests_properties(MuffinCodeFenceCommitCursorTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinInputBlockEditingTest
  tests/editor/InputBlockEditingTest.cpp
)

target_link_libraries(MuffinInputBlockEditingTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinInputBlockEditingTest COMMAND MuffinInputBlockEditingTest)
set_tests_properties(MuffinInputBlockEditingTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinInputDefinitionHeadingTest
  tests/editor/InputDefinitionHeadingTest.cpp
)

target_link_libraries(MuffinInputDefinitionHeadingTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinInputDefinitionHeadingTest COMMAND MuffinInputDefinitionHeadingTest)
set_tests_properties(MuffinInputDefinitionHeadingTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinParagraphHeadingTest
  tests/commands/ParagraphHeadingTest.cpp
)

target_link_libraries(MuffinParagraphHeadingTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinParagraphHeadingTest COMMAND MuffinParagraphHeadingTest)
set_tests_properties(MuffinParagraphHeadingTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinParagraphBlockInsertTest
  tests/commands/ParagraphBlockInsertTest.cpp
)

target_link_libraries(MuffinParagraphBlockInsertTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinParagraphBlockInsertTest COMMAND MuffinParagraphBlockInsertTest)
set_tests_properties(MuffinParagraphBlockInsertTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinParagraphToggleBlockTest
  tests/commands/ParagraphToggleBlockTest.cpp
)

target_link_libraries(MuffinParagraphToggleBlockTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinParagraphToggleBlockTest COMMAND MuffinParagraphToggleBlockTest)
set_tests_properties(MuffinParagraphToggleBlockTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinParagraphToggleUndoTest
  tests/commands/ParagraphToggleUndoTest.cpp
)

target_link_libraries(MuffinParagraphToggleUndoTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinParagraphToggleUndoTest COMMAND MuffinParagraphToggleUndoTest)
set_tests_properties(MuffinParagraphToggleUndoTest PROPERTIES
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

add_executable(MuffinTableControllerTest
  tests/blocks/TableControllerTest.cpp
)

target_link_libraries(MuffinTableControllerTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinTableControllerTest COMMAND MuffinTableControllerTest)
set_tests_properties(MuffinTableControllerTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

add_executable(MuffinTableCellEditingTest
  tests/blocks/TableCellEditingTest.cpp
)

target_link_libraries(MuffinTableCellEditingTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinTableCellEditingTest COMMAND MuffinTableCellEditingTest)
set_tests_properties(MuffinTableCellEditingTest PROPERTIES ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}")

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

add_executable(MuffinBlockTableUndoTest
  tests/blocks/BlockTableUndoTest.cpp
)

target_link_libraries(MuffinBlockTableUndoTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinBlockTableUndoTest COMMAND MuffinBlockTableUndoTest)
set_tests_properties(MuffinBlockTableUndoTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

add_executable(MuffinBlockCodeFrontMatterTest
  tests/blocks/BlockCodeFrontMatterTest.cpp
)

target_link_libraries(MuffinBlockCodeFrontMatterTest
  PRIVATE
    MuffinUi
)

add_test(NAME MuffinBlockCodeFrontMatterTest COMMAND MuffinBlockCodeFrontMatterTest)
set_tests_properties(MuffinBlockCodeFrontMatterTest PROPERTIES
  ENVIRONMENT_MODIFICATION "${MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS}"
  RESOURCE_LOCK MuffinQtGui
)

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
