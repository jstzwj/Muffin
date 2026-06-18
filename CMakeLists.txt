cmake_minimum_required(VERSION 3.24)

project(Muffin
  VERSION 0.2.5
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
  src/app/CommandDeclarations.cpp
  src/app/MainWindowMenus.cpp
  src/app/MainWindowSignalBinder.cpp
  src/app/MainWindowActions.cpp
  src/app/MainWindowFileOps.cpp
  src/app/MainWindowFind.cpp
  src/app/PreferencesDialog.cpp
  src/app/HelpViewerDialog.cpp
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
  src/editor/EditorView.cpp
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

# Nuspell (vendored) — spell checking. Library-only: disable the CLI tool, tests and
# docs. When embedded via add_subdirectory, nuspell's own CMakeLists detects
# PARENT_DIRECTORY and returns() right after building src/nuspell, so its tests,
# docs and install rules are skipped automatically — but BUILD_TOOLS is consulted
# before that return (and pulls in getopt on MSVC), so it must be off. Built static
# to match cmark-gfm and the other vendored static libs. ICU (uc + data, which
# nuspell requires) is already provided by Conan and found transitively.
set(_muffin_prev_build_shared_libs "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/nuspell EXCLUDE_FROM_ALL)
# Treat nuspell's headers as system headers so /external:W0 (MSVC) / SYSTEM includes
# silence its third-party warnings in Muffin's own translation units, and mute its own
# compile to match the other vendored libraries.
get_target_property(_muffin_nuspell_includes nuspell INTERFACE_INCLUDE_DIRECTORIES)
if(_muffin_nuspell_includes)
  set_target_properties(nuspell PROPERTIES
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_muffin_nuspell_includes}")
endif()
if(MSVC)
  target_compile_options(nuspell PRIVATE /W0 /external:W0)
endif()
if(DEFINED _muffin_prev_build_shared_libs)
  set(BUILD_SHARED_LIBS "${_muffin_prev_build_shared_libs}" CACHE BOOL "" FORCE)
else()
  unset(BUILD_SHARED_LIBS CACHE)
endif()

# Lexbor and Yoga are provided by Conan
find_package(lexbor REQUIRED)
find_package(yoga REQUIRED)

add_library(tree-sitter-runtime STATIC
  third_party/tree-sitter/lib/src/lib.c
)
target_include_directories(tree-sitter-runtime
  PUBLIC SYSTEM
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
  PUBLIC SYSTEM
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/lib/include
  PRIVATE
    ${TREE_SITTER_LANGUAGE_INCLUDE_DIRS}
)

# Enable high-warning levels for our own code only. This call is placed AFTER
# the vendored cmark-gfm subdirectory and the inline tree-sitter C libraries so
# their directory-scoped options do not inherit it (they would flood the build).
# Warnings are deliberately non-fatal (no /WX/-Werror): this surfaces latent
# issues for triage without breaking the build. /external:W0 (MSVC) and SYSTEM
# include dirs (above) silence warnings from third-party headers (cmark-gfm,
# tree-sitter, and the find_package targets Qt/ICU/lexbor).
if(MSVC)
  add_compile_options(/W4 /permissive- /external:W0)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

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
  src/document/ImageSyntaxOps.cpp
  src/document/ImageSyntaxOps.h
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
  PUBLIC SYSTEM
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/cmark-gfm/src
)

# Help documentation: a set of GFM .md files per UI locale (English fallback at
# the root), embedded as resources. A .qrc is generated at configure time from a
# glob, so adding a locale or document needs no manual edits. CONFIGURE_DEPENDS
# re-runs the glob (and regenerates the .qrc) whenever the file set changes.
file(GLOB_RECURSE MUFFIN_HELP_DOCS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/help/*.md")
file(RELATIVE_PATH MUFFIN_HELP_QRC_REL "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/help")
set(MUFFIN_HELP_QRC "${CMAKE_CURRENT_BINARY_DIR}/help.qrc")
string(CONCAT MUFFIN_HELP_QRC_CONTENT
  "<RCC>\n"
  "  <qresource prefix=\"/help\">\n")
foreach(MUFFIN_HELP_DOC IN LISTS MUFFIN_HELP_DOCS)
  file(RELATIVE_PATH MUFFIN_HELP_DOC_REL
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/help" "${MUFFIN_HELP_DOC}")
  string(REPLACE "\\" "/" MUFFIN_HELP_DOC_REL "${MUFFIN_HELP_DOC_REL}")
  string(CONCAT MUFFIN_HELP_QRC_CONTENT "${MUFFIN_HELP_QRC_CONTENT}"
    "    <file alias=\"${MUFFIN_HELP_DOC_REL}\">${MUFFIN_HELP_QRC_REL}/${MUFFIN_HELP_DOC_REL}</file>\n")
endforeach()
string(CONCAT MUFFIN_HELP_QRC_CONTENT "${MUFFIN_HELP_QRC_CONTENT}"
  "  </qresource>\n"
  "</RCC>\n")
file(WRITE "${MUFFIN_HELP_QRC}" "${MUFFIN_HELP_QRC_CONTENT}")

# Spell-check dictionaries: hunspell .aff/.dic per locale, embedded as zlib-compressed
# resources (compress="9" shrinks the text dicts ~3-4x; Qt decompresses transparently on
# read, so nuspell still receives the raw bytes via QFile). Globbed so adding a locale is
# just dropping a file; en_US is the universal fallback. Mirrors the help-doc block above.
file(GLOB MUFFIN_DICT_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/dicts/*.dic"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/dicts/*.aff")
file(RELATIVE_PATH MUFFIN_DICTS_QRC_REL "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/dicts")
set(MUFFIN_DICTS_QRC "${CMAKE_CURRENT_BINARY_DIR}/dicts.qrc")
string(CONCAT MUFFIN_DICTS_QRC_CONTENT
  "<RCC>\n"
  "  <qresource prefix=\"/dicts\">\n")
foreach(MUFFIN_DICT_FILE IN LISTS MUFFIN_DICT_FILES)
  get_filename_component(MUFFIN_DICT_FILE_NAME "${MUFFIN_DICT_FILE}" NAME)
  string(REPLACE "\\" "/" MUFFIN_DICT_FILE_NAME "${MUFFIN_DICT_FILE_NAME}")
  string(CONCAT MUFFIN_DICTS_QRC_CONTENT "${MUFFIN_DICTS_QRC_CONTENT}"
    "    <file alias=\"${MUFFIN_DICT_FILE_NAME}\" compress=\"9\">${MUFFIN_DICTS_QRC_REL}/${MUFFIN_DICT_FILE_NAME}</file>\n")
endforeach()
string(CONCAT MUFFIN_DICTS_QRC_CONTENT "${MUFFIN_DICTS_QRC_CONTENT}"
  "  </qresource>\n"
  "</RCC>\n")
file(WRITE "${MUFFIN_DICTS_QRC}" "${MUFFIN_DICTS_QRC_CONTENT}")

# Emoji autocomplete dataset: a single shortcode->glyph table (one "shortcode<TAB>glyph" per line)
# embedded as a zlib-compressed resource so the BundledEmojiProvider can load it without disk I/O.
set(MUFFIN_EMOJI_QRC "${CMAKE_CURRENT_BINARY_DIR}/emoji.qrc")
file(RELATIVE_PATH MUFFIN_EMOJI_QRC_REL "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/emoji/emoji.txt")
file(WRITE "${MUFFIN_EMOJI_QRC}"
  "<RCC>\n"
  "  <qresource prefix=\"/emoji\">\n"
  "    <file alias=\"emoji.txt\" compress=\"9\">${MUFFIN_EMOJI_QRC_REL}</file>\n"
  "  </qresource>\n"
  "</RCC>\n")

add_executable(Muffin
  src/translations.qrc
  src/icons.qrc
  ${MUFFIN_HELP_QRC}
  ${MUFFIN_DICTS_QRC}
  ${MUFFIN_EMOJI_QRC}
  src/app/CommandRegistry.cpp
  src/app/CommandRegistry.h
  src/app/LanguageManager.cpp
  src/app/LanguageManager.h
  src/app/MainWindow.cpp
  src/app/MainWindow.h
  src/app/MainWindowActionBinder.cpp
  src/app/CommandDeclarations.cpp
  src/app/CommandDeclarations.h
  src/app/MainWindowMenus.cpp
  src/app/MainWindowActions.cpp
  src/app/MainWindowSignalBinder.cpp
  src/app/UpdateChecker.cpp
  src/app/UpdateChecker.h
  src/app/MainWindowFileOps.cpp
  src/app/MainWindowFind.cpp
  src/app/PreferencesDialog.cpp
  src/app/PreferencesDialog.h
  src/app/HelpViewerDialog.cpp
  src/app/HelpViewerDialog.h
  src/app/RenderCommandFacade.cpp
  src/app/RenderCommandFacade.h
  src/app/PreferencesPage.cpp
  src/app/PreferencesPage.h
  src/app/MarkdownSettings.cpp
  src/app/MarkdownSettings.h
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
  src/spellcheck/SpellChecker.cpp
  src/spellcheck/SpellChecker.h
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
  src/app/EditorBackend.h
  src/app/RenderEditorBackend.cpp
  src/app/RenderEditorBackend.h
  src/app/SourceEditorBackend.cpp
  src/app/SourceEditorBackend.h
  src/editor/EditorControllerUndo.cpp
  src/editor/EditorView.cpp
  src/editor/EditorView.h
  src/editor/EditorViewGeometry.cpp
  src/editor/EditorViewGeometry.h
  src/editor/HtmlBlockHoverController.cpp
  src/editor/HtmlBlockHoverController.h
  src/editor/CodeLanguageEditor.cpp
  src/editor/CodeLanguageEditor.h
  src/editor/EmojiCompleter.cpp
  src/editor/EmojiCompleter.h
  src/editor/EmojiProvider.h
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
    Nuspell::nuspell
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

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MuffinTests.cmake)
