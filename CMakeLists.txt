cmake_minimum_required(VERSION 3.24)

project(Muffin
  VERSION 0.5.5
  DESCRIPTION "A fast native Markdown WYSIWYG editor built with C++ and Qt 6 Widgets"
  LANGUAGES C CXX
)

# STATIC by default. The CI AddressSanitizer job overrides this to SHARED:
# ~320 ASan-instrumented test executables each embedding a full static copy of
# the instrumented MuffinCore/MuffinUi exhausts the runner's disk, while a
# single instrumented .so per library keeps the whole suite within budget.
set(MUFFIN_LIB_TYPE STATIC CACHE STRING "Link type of the MuffinCore/MuffinUi libraries")

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

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg PrintSupport Network Concurrent)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(WebP REQUIRED)
# dav1d is libavif's AV1 decoder; found here (Conan provides dav1d::dav1d) so libavif's own
# find_package(dav1d) below reuses it. libavif itself is built from vendored source (see the
# third_party/libavif block below) rather than pulled from Conan, to avoid its libyuv dependency.
find_package(dav1d REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(harfbuzz REQUIRED)
set(_muffin_harfbuzz_include_hints ${harfbuzz_INCLUDE_DIRS})
if(DEFINED harfbuzz_PACKAGE_FOLDER_RELEASE)
  list(APPEND _muffin_harfbuzz_include_hints
    "${harfbuzz_PACKAGE_FOLDER_RELEASE}/include/harfbuzz")
endif()
find_path(MUFFIN_HARFBUZZ_INCLUDE_DIR
  NAMES hb.h
  HINTS ${_muffin_harfbuzz_include_hints}
  PATH_SUFFIXES harfbuzz
  REQUIRED
)
unset(_muffin_harfbuzz_include_hints)
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
  src/app/MainWindowContextMenu.cpp
  src/app/MainWindowSignalBinder.cpp
  src/app/MainWindowActions.cpp
  src/app/MainWindowFileOps.cpp
  src/app/MainWindowFind.cpp
  src/app/PreferencesDialog.cpp
  src/app/HelpViewerDialog.cpp
  src/app/QuickOpenDialog.cpp
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
  src/app/StatusBarWidget.cpp
  src/document/DocumentSession.cpp
  src/editor/EditorView.cpp
  src/editor/EditorViewPaint.cpp
  src/editor/EditorViewViewport.cpp
  src/editor/SourceEditorWidget.cpp
  src/editor/VirtualSourceEdit.cpp
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

# libavif (vendored) — AVIF decoder. Built from source instead of pulled from Conan because the
# ConanCenter libavif/1.4.1 recipe hard-requires libyuv, whose only source
# (chromium.googlesource.com) is unreachable from GitHub Actions IPs. Decode-only: dav1d from
# Conan (reliable VideoLAN source; found above), libyuv OFF (libavif uses its internal
# YUV->RGB), aom OFF, no apps/tests/examples. Static to match the other vendored libs. The
# external static target is `avif_static` (the `avif`/`avif_internal` targets are not for
# external use). See third_party/libavif/VENDORED.md.
set(_muffin_prev_build_shared_libs_avif "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(AVIF_CODEC_DAV1D  SYSTEM CACHE STRING "" FORCE)
set(AVIF_CODEC_AOM    OFF     CACHE STRING "" FORCE)
set(AVIF_LIBYUV       OFF     CACHE STRING "" FORCE)
set(AVIF_LIBSHARPYUV  OFF     CACHE STRING "" FORCE)
set(AVIF_BUILD_APPS     OFF CACHE BOOL "" FORCE)
set(AVIF_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(AVIF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/libavif EXCLUDE_FROM_ALL)
# Ensure the avif headers are visible as system includes for Muffin's own translation units
# (ImageDecoder.cpp #includes <avif/avif.h>); merge_static_libs may not forward avif's includes.
if(TARGET avif_static)
  target_include_directories(avif_static SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libavif/include)
endif()
if(DEFINED _muffin_prev_build_shared_libs_avif)
  set(BUILD_SHARED_LIBS "${_muffin_prev_build_shared_libs_avif}" CACHE BOOL "" FORCE)
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
  src/katex_fonts.qrc
  src/mermaid_fonts.qrc
  src/math/MathBuilder.cpp
  src/math/MathBuilderArray.cpp
  src/math/MathBuilderSpacing.cpp
  src/math/MathDelimiter.cpp
  src/math/MathFontMetrics.cpp
  src/math/MathFontRegistry.cpp
  src/math/MathDimension.cpp
  src/math/MathFunctionRegistry.cpp
  src/math/MathLexer.cpp
  src/math/MathLayoutTree.cpp
  src/math/MathMacroExpander.cpp
  src/math/MathOptions.cpp
  src/math/MathParseError.cpp
  src/math/MathParser.cpp
  src/math/MathParserEnvironment.cpp
  src/math/MathRenderer.cpp
  src/mermaid/math/MathMlCssLayout.cpp
  src/mermaid/math/MathGlyphRasterizer.cpp
  src/mermaid/math/MathGlyphRasterizer.h
  src/mermaid/math/MathMlCssPainter.cpp
  src/mermaid/math/MathMlCssPainter.h
  src/math/OpenTypeMathFont.cpp
  src/math/MathRenderNode.cpp
  src/math/MathSettings.cpp
  src/math/MathStyle.cpp
  src/math/MathSvgGeometry.cpp
  src/math/MathSymbols.cpp
  src/mermaid/MermaidDiagramDetector.cpp
  src/mermaid/MermaidDiagramDetector.h
  src/mermaid/MermaidDiagnostic.cpp
  src/mermaid/MermaidDiagnostic.h
  src/mermaid/MermaidDiagram.h
  src/mermaid/MermaidPaintOptions.h
  src/mermaid/MermaidPreprocessor.cpp
  src/mermaid/MermaidPreprocessor.h
  src/mermaid/MermaidRenderMetadata.cpp
  src/mermaid/MermaidRenderMetadata.h
  src/mermaid/MermaidScene.h
  src/mermaid/editor/MermaidSvgExporter.cpp
  src/mermaid/editor/MermaidSvgExporter.h
  src/mermaid/editor/MermaidRenderSupport.cpp
  src/mermaid/editor/MermaidRenderSupport.h
  src/mermaid/editor/MermaidDiagrams.cpp
  src/mermaid/editor/MermaidDiagrams.h
  src/mermaid/editor/FlowchartDiagramAdapter.cpp
  src/mermaid/editor/SwimlaneDiagramAdapter.cpp
  src/mermaid/editor/SequenceDiagramAdapter.cpp
  src/mermaid/editor/ClassDiagramAdapter.cpp
  src/mermaid/editor/StateDiagramAdapter.cpp
  src/mermaid/editor/ErDiagramAdapter.cpp
  src/mermaid/editor/RequirementDiagramAdapter.cpp
  src/mermaid/editor/PieDiagramAdapter.cpp
  src/mermaid/editor/QuadrantDiagramAdapter.cpp
  src/mermaid/editor/JourneyDiagramAdapter.cpp
  src/mermaid/editor/RadarDiagramAdapter.cpp
  src/mermaid/editor/XYChartDiagramAdapter.cpp
  src/mermaid/editor/TimelineDiagramAdapter.cpp
  src/mermaid/editor/PacketDiagramAdapter.cpp
  src/mermaid/editor/KanbanDiagramAdapter.cpp
  src/mermaid/editor/MindmapDiagramAdapter.cpp
  src/mermaid/editor/GitGraphDiagramAdapter.cpp
  src/mermaid/editor/C4DiagramAdapter.cpp
  src/mermaid/editor/GanttDiagramAdapter.cpp
  src/mermaid/editor/InfoDiagramAdapter.cpp
  src/mermaid/editor/TreeViewDiagramAdapter.cpp
  src/mermaid/editor/EventModelingDiagramAdapter.cpp
  src/mermaid/editor/IshikawaDiagramAdapter.cpp
  src/mermaid/editor/VennDiagramAdapter.cpp
  src/mermaid/editor/SankeyDiagramAdapter.cpp
  src/mermaid/editor/TreemapDiagramAdapter.cpp
  src/mermaid/editor/CynefinDiagramAdapter.cpp
  src/mermaid/editor/WardleyDiagramAdapter.cpp
  src/mermaid/editor/ArchitectureDiagramAdapter.cpp
  src/mermaid/editor/BlockDiagramAdapter.cpp
  src/mermaid/editor/RailroadDiagramAdapter.cpp
  src/mermaid/editor/ErrorDiagramAdapter.cpp
  src/mermaid/MermaidFontRegistry.cpp
  src/mermaid/MermaidFontRegistry.h
  src/mermaid/flowchart/Flowchart.cpp
  src/mermaid/flowchart/Flowchart.h
  src/mermaid/flowchart/FlowLabel.cpp
  src/mermaid/flowchart/BlinkSvgPathMetrics.cpp
  src/mermaid/flowchart/FlowLabel.h
  src/mermaid/flowchart/FlowchartTokenizer.cpp
  src/mermaid/flowchart/FlowchartTokenizer.h
  src/mermaid/flowchart/FlowchartLayout.cpp
  src/mermaid/flowchart/SwimlaneLayout.cpp
  src/mermaid/flowchart/FlowchartLayout.h
  src/mermaid/flowchart/FlowchartShapes.cpp
  src/mermaid/flowchart/FlowchartShapes.h
  src/mermaid/graphlib/Graph.h
  src/mermaid/dagre/DagreLabels.h
  src/mermaid/dagre/DagreUtil.cpp
  src/mermaid/dagre/DagreUtil.h
  src/mermaid/dagre/NestingGraph.cpp
  src/mermaid/dagre/NestingGraph.h
  src/mermaid/dagre/AddBorderSegments.cpp
  src/mermaid/dagre/AddBorderSegments.h
  src/mermaid/dagre/ParentDummyChains.cpp
  src/mermaid/dagre/ParentDummyChains.h
  src/mermaid/dagre/Acyclic.cpp
  src/mermaid/dagre/Acyclic.h
  src/mermaid/dagre/Normalize.cpp
  src/mermaid/dagre/Normalize.h
  src/mermaid/dagre/CoordinateSystem.cpp
  src/mermaid/dagre/CoordinateSystem.h
  src/mermaid/dagre/Rank.cpp
  src/mermaid/dagre/Rank.h
  src/mermaid/dagre/Order.cpp
  src/mermaid/dagre/Order.h
  src/mermaid/dagre/Position.cpp
  src/mermaid/dagre/Position.h
  src/mermaid/dagre/Layout.cpp
  src/mermaid/dagre/Layout.h
  src/mermaid/theme/MermaidColor.cpp
  src/mermaid/theme/MermaidColor.h
  src/mermaid/theme/FlowTheme.cpp
  src/mermaid/theme/FlowTheme.h
  src/mermaid/theme/FlowStyleResolve.cpp
  src/mermaid/theme/FlowStyleResolve.h
  src/mermaid/theme/MermaidStyleResolve.cpp
  src/mermaid/theme/MermaidStyleResolve.h
  src/mermaid/theme/MermaidCssCascade.cpp
  src/mermaid/theme/MermaidCssCascade.h
  src/mermaid/text/ChromiumTextMetrics.cpp
  src/mermaid/text/ChromiumTextMetrics.h
  src/theme/CssCalc.cpp
  src/theme/CssCalc.h
  src/theme/CssComputedStyleEngine.cpp
  src/theme/CssComputedStyleEngine.h
  src/theme/CssSelectorAnalysis.cpp
  src/theme/CssSelectorAnalysis.h
  src/theme/CssSelectorUtils.h
  src/theme/CssThemeParser.cpp
  src/theme/CssThemeParser.h
  src/theme/TyporaEditorOnly.h
  src/mermaid/security/MermaidSecurityPolicy.h
  src/blocks/html/HtmlSanitizer.cpp
  src/blocks/html/HtmlSanitizer.h
  src/mermaid/scene/FlowMarkers.cpp
  src/mermaid/scene/FlowMarkers.h
  src/mermaid/scene/FlowScene.cpp
  src/mermaid/scene/FlowScene.h
  src/mermaid/scene/FlowSceneCompare.cpp
  src/mermaid/scene/FlowSceneCompare.h
  src/mermaid/scene/FlowScenePainter.cpp
  src/mermaid/scene/FlowScenePainter.h
  src/mermaid/scene/SvgPathParse.cpp
  src/mermaid/scene/SvgPathParse.h
  src/mermaid/rough/RoughOps.cpp
  src/mermaid/rough/RoughOps.h
  src/mermaid/classdiagram/ClassDiagram.cpp
  src/mermaid/classdiagram/ClassDiagram.h
  src/mermaid/classdiagram/ClassLayout.cpp
  src/mermaid/classdiagram/ClassLayout.h
  src/mermaid/classdiagram/ClassScene.cpp
  src/mermaid/classdiagram/ClassScene.h
  src/mermaid/classdiagram/ClassScenePainter.cpp
  src/mermaid/classdiagram/ClassScenePainter.h
  src/mermaid/classdiagram/ClassTokenizer.cpp
  src/mermaid/classdiagram/ClassTokenizer.h
  src/mermaid/requirement/RequirementDiagram.cpp
  src/mermaid/requirement/RequirementDiagram.h
  src/mermaid/requirement/RequirementLayout.cpp
  src/mermaid/requirement/RequirementLayout.h
  src/mermaid/requirement/RequirementScene.cpp
  src/mermaid/requirement/RequirementScene.h
  src/mermaid/requirement/RequirementScenePainter.cpp
  src/mermaid/requirement/RequirementScenePainter.h
  src/mermaid/requirement/RequirementTextStyle.cpp
  src/mermaid/requirement/RequirementTextStyle.h
  src/mermaid/pie/PieDiagram.cpp
  src/mermaid/pie/PieDiagram.h
  src/mermaid/pie/PieScene.cpp
  src/mermaid/pie/PieScene.h
  src/mermaid/pie/PieScenePainter.cpp
  src/mermaid/pie/PieScenePainter.h
  src/mermaid/quadrant/QuadrantDiagram.cpp
  src/mermaid/quadrant/QuadrantDiagram.h
  src/mermaid/quadrant/QuadrantScene.cpp
  src/mermaid/quadrant/QuadrantScene.h
  src/mermaid/quadrant/QuadrantScenePainter.cpp
  src/mermaid/quadrant/QuadrantScenePainter.h
  src/mermaid/journey/JourneyDiagram.cpp
  src/mermaid/journey/JourneyDiagram.h
  src/mermaid/journey/JourneyScene.cpp
  src/mermaid/journey/JourneyScene.h
  src/mermaid/journey/JourneyScenePainter.cpp
  src/mermaid/journey/JourneyScenePainter.h
  src/mermaid/radar/RadarDiagram.cpp
  src/mermaid/radar/RadarDiagram.h
  src/mermaid/radar/RadarScene.cpp
  src/mermaid/radar/RadarScene.h
  src/mermaid/radar/RadarScenePainter.cpp
  src/mermaid/radar/RadarScenePainter.h
  src/mermaid/xychart/XYChartDiagram.cpp
  src/mermaid/xychart/XYChartDiagram.h
  src/mermaid/xychart/XYChartScene.cpp
  src/mermaid/xychart/XYChartScene.h
  src/mermaid/xychart/XYChartScenePainter.cpp
  src/mermaid/xychart/XYChartScenePainter.h
  src/mermaid/timeline/TimelineDiagram.cpp
  src/mermaid/timeline/TimelineDiagram.h
  src/mermaid/timeline/TimelineScene.cpp
  src/mermaid/timeline/TimelineScene.h
  src/mermaid/timeline/TimelineScenePainter.cpp
  src/mermaid/timeline/TimelineScenePainter.h
  src/mermaid/packet/PacketDiagram.cpp
  src/mermaid/packet/PacketDiagram.h
  src/mermaid/packet/PacketScene.cpp
  src/mermaid/packet/PacketScene.h
  src/mermaid/packet/PacketScenePainter.cpp
  src/mermaid/packet/PacketScenePainter.h
  src/mermaid/kanban/KanbanDiagram.cpp
  src/mermaid/kanban/KanbanDiagram.h
  src/mermaid/kanban/KanbanScene.cpp
  src/mermaid/kanban/KanbanScene.h
  src/mermaid/kanban/KanbanScenePainter.cpp
  src/mermaid/kanban/KanbanScenePainter.h
  src/mermaid/mindmap/MindmapDiagram.cpp
  src/mermaid/mindmap/MindmapDiagram.h
  src/mermaid/mindmap/MindmapCoseLayout.cpp
  src/mermaid/mindmap/MindmapCoseLayout.h
  src/mermaid/mindmap/MindmapScene.cpp
  src/mermaid/mindmap/MindmapScene.h
  src/mermaid/mindmap/MindmapScenePainter.cpp
  src/mermaid/mindmap/MindmapScenePainter.h
  src/mermaid/gitgraph/GitGraphDiagram.cpp
  src/mermaid/gitgraph/GitGraphDiagram.h
  src/mermaid/gitgraph/GitGraphScene.cpp
  src/mermaid/gitgraph/GitGraphScene.h
  src/mermaid/gitgraph/GitGraphScenePainter.cpp
  src/mermaid/c4/C4Diagram.cpp
  src/mermaid/c4/C4Diagram.h
  src/mermaid/c4/C4Scene.cpp
  src/mermaid/c4/C4Scene.h
  src/mermaid/c4/C4ScenePainter.cpp
  src/mermaid/c4/C4ScenePainter.h
  src/mermaid/railroad/RailroadDiagram.cpp
  src/mermaid/railroad/RailroadDiagram.h
  src/mermaid/railroad/RailroadScene.cpp
  src/mermaid/railroad/RailroadScene.h
  src/mermaid/railroad/RailroadScenePainter.cpp
  src/mermaid/railroad/RailroadScenePainter.h
  src/mermaid/error/ErrorScene.cpp
  src/mermaid/error/ErrorScene.h
  src/mermaid/error/ErrorScenePainter.cpp
  src/mermaid/error/ErrorScenePainter.h
  src/mermaid/gantt/GanttDiagram.cpp
  src/mermaid/gantt/GanttDiagram.h
  src/mermaid/gantt/GanttScene.cpp
  src/mermaid/gantt/GanttScene.h
  src/mermaid/gantt/GanttScenePainter.cpp
  src/mermaid/gantt/GanttScenePainter.h
  src/mermaid/info/InfoDiagram.cpp
  src/mermaid/info/InfoDiagram.h
  src/mermaid/info/InfoScene.cpp
  src/mermaid/info/InfoScene.h
  src/mermaid/info/InfoScenePainter.cpp
  src/mermaid/info/InfoScenePainter.h
  src/mermaid/treeview/TreeViewDiagram.cpp
  src/mermaid/treeview/TreeViewDiagram.h
  src/mermaid/treeview/TreeViewScene.cpp
  src/mermaid/treeview/TreeViewScene.h
  src/mermaid/treeview/TreeViewScenePainter.cpp
  src/mermaid/treeview/TreeViewScenePainter.h
  src/mermaid/eventmodeling/EventModelingDiagram.cpp
  src/mermaid/eventmodeling/EventModelingDiagram.h
  src/mermaid/eventmodeling/EventModelingScene.cpp
  src/mermaid/eventmodeling/EventModelingScene.h
  src/mermaid/eventmodeling/EventModelingScenePainter.cpp
  src/mermaid/eventmodeling/EventModelingScenePainter.h
  src/mermaid/ishikawa/IshikawaDiagram.cpp
  src/mermaid/ishikawa/IshikawaDiagram.h
  src/mermaid/ishikawa/IshikawaScene.cpp
  src/mermaid/ishikawa/IshikawaScene.h
  src/mermaid/ishikawa/IshikawaScenePainter.cpp
  src/mermaid/ishikawa/IshikawaScenePainter.h
  src/mermaid/venn/VennDiagram.cpp
  src/mermaid/venn/VennDiagram.h
  src/mermaid/venn/VennLayout.cpp
  src/mermaid/venn/VennLayout.h
  src/mermaid/venn/VennScene.cpp
  src/mermaid/venn/VennScene.h
  src/mermaid/venn/VennScenePainter.cpp
  src/mermaid/venn/VennScenePainter.h
  src/mermaid/sankey/SankeyDiagram.cpp
  src/mermaid/sankey/SankeyDiagram.h
  src/mermaid/sankey/SankeyScene.cpp
  src/mermaid/sankey/SankeyScene.h
  src/mermaid/sankey/SankeyScenePainter.cpp
  src/mermaid/sankey/SankeyScenePainter.h
  src/mermaid/treemap/TreemapDiagram.cpp
  src/mermaid/treemap/TreemapDiagram.h
  src/mermaid/treemap/TreemapScene.cpp
  src/mermaid/treemap/TreemapScene.h
  src/mermaid/treemap/TreemapScenePainter.cpp
  src/mermaid/treemap/TreemapScenePainter.h
  src/mermaid/cynefin/CynefinDiagram.cpp
  src/mermaid/cynefin/CynefinDiagram.h
  src/mermaid/cynefin/CynefinScene.cpp
  src/mermaid/cynefin/CynefinScene.h
  src/mermaid/cynefin/CynefinScenePainter.cpp
  src/mermaid/cynefin/CynefinScenePainter.h
  src/mermaid/wardley/WardleyDiagram.cpp
  src/mermaid/wardley/WardleyDiagram.h
  src/mermaid/wardley/WardleyScene.cpp
  src/mermaid/wardley/WardleyScene.h
  src/mermaid/wardley/WardleyScenePainter.cpp
  src/mermaid/wardley/WardleyScenePainter.h
  src/mermaid/architecture/ArchitectureDiagram.cpp
  src/mermaid/architecture/ArchitectureDiagram.h
  src/mermaid/architecture/ArchitectureFcoseLayout.cpp
  src/mermaid/architecture/ArchitectureFcoseLayout.h
  src/mermaid/architecture/ArchitectureSpectralLayout.cpp
  src/mermaid/architecture/ArchitectureSpectralLayout.h
  src/mermaid/architecture/ArchitectureScene.cpp
  src/mermaid/architecture/ArchitectureScene.h
  src/mermaid/architecture/ArchitectureScenePainter.cpp
  src/mermaid/architecture/ArchitectureScenePainter.h
  src/mermaid/block/BlockDiagram.cpp
  src/mermaid/block/BlockDiagram.h
  src/mermaid/block/BlockScene.cpp
  src/mermaid/block/BlockScene.h
  src/mermaid/erdiagram/ErDiagram.cpp
  src/mermaid/erdiagram/ErDiagram.h
  src/mermaid/erdiagram/ErLayout.cpp
  src/mermaid/erdiagram/ErLayout.h
  src/mermaid/erdiagram/ErScene.cpp
  src/mermaid/erdiagram/ErScene.h
  src/mermaid/erdiagram/ErScenePainter.cpp
  src/mermaid/erdiagram/ErScenePainter.h
  src/mermaid/erdiagram/ErTokenizer.cpp
  src/mermaid/erdiagram/ErTokenizer.h
  src/mermaid/sequence/SequenceDiagram.cpp
  src/mermaid/sequence/SequenceDiagram.h
  src/mermaid/sequence/SequenceLayout.cpp
  src/mermaid/sequence/SequenceLayout.h
  src/mermaid/sequence/SequenceLabel.cpp
  src/mermaid/sequence/SequenceLabel.h
  src/mermaid/sequence/SequenceScene.cpp
  src/mermaid/sequence/SequenceScene.h
  src/mermaid/sequence/SequenceScenePainter.cpp
  src/mermaid/sequence/SequenceScenePainter.h
  src/mermaid/sequence/SequenceTokenizer.cpp
  src/mermaid/sequence/SequenceTokenizer.h
  src/mermaid/state/StateDiagram.cpp
  src/mermaid/state/StateDiagram.h
  src/mermaid/state/StateLayout.cpp
  src/mermaid/state/StateLayout.h
  src/mermaid/state/StateScene.cpp
  src/mermaid/state/StateScene.h
  src/mermaid/state/StateScenePainter.cpp
  src/mermaid/state/StateScenePainter.h
  src/mermaid/state/StateTokenizer.cpp
  src/mermaid/state/StateTokenizer.h
  src/mermaid/editor/MermaidRenderCache.cpp
  src/mermaid/editor/MermaidRenderCache.h
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
  src/io/TextFileFormat.h
  src/document/ImageSyntaxOps.cpp
  src/document/ImageSyntaxOps.h
  src/document/DefinitionBlock.cpp
  src/document/DefinitionBlock.h
  src/document/InlineNode.cpp
  src/document/InlineNode.h
  src/document/MarkdownNode.cpp
  src/document/MarkdownNode.h
  src/document/PieceTable.cpp
  src/document/PieceTable.h
  src/document/TextStats.cpp
  src/document/TextStats.h
  src/document/TextSelection.h
  src/document/NodeIndex.cpp
  src/document/NodeIndex.h
  src/document/MarkdownDocument.cpp
  src/document/MarkdownDocument.h
  src/document/SourcePositionIndex.cpp
  src/document/SourcePositionIndex.h
  src/document/OutlineBuilder.cpp
  src/document/OutlineBuilder.h
  src/parser/MarkdownParser.h
  src/parser/CmarkNodeAdapter.cpp
  src/parser/CmarkNodeAdapter.h
  src/parser/CmarkGfmParser.cpp
  src/parser/CmarkGfmParser.h
  src/parser/MarkdownSerializer.cpp
  src/parser/MarkdownSerializer.h
  src/app/DraftRecovery.cpp
  src/app/DraftRecovery.h
)

add_library(MuffinCore ${MUFFIN_LIB_TYPE}
  ${MUFFIN_CORE_SOURCES}
)

target_link_libraries(MuffinCore
  PUBLIC
    Qt6::Core
    harfbuzz::harfbuzz
  PRIVATE
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Svg
    yaml-cpp::yaml-cpp
    lexbor::lexbor_static
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

target_include_directories(MuffinCore SYSTEM PRIVATE
  ${MUFFIN_HARFBUZZ_INCLUDE_DIR}
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
  src/themes.qrc
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
  src/app/MainWindowContextMenu.cpp
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
  src/app/QuickOpenDialog.cpp
  src/app/QuickOpenDialog.h
  src/export/ExportFormat.h
  src/app/RenderCommandFacade.cpp
  src/app/RenderCommandFacade.h
  src/app/PreferencesPage.cpp
  src/app/PreferencesPage.h
  src/app/MarkdownSettings.cpp
  src/app/MarkdownSettings.h
  src/app/PrefsFilesPage.cpp
  src/app/PrefsFilesPage.h
  src/app/WindowsIntegration.cpp
  src/app/WindowsIntegration.h
  src/app/DwmPopupBorderFilter.cpp
  src/app/DwmPopupBorderFilter.h
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
  third_party/katex/src/fontMetricsData.js
  third_party/katex/src/svgGeometry.ts
  third_party/katex/src/symbols.ts
  src/tree_sitter_queries.qrc
  src/projection/InlineProjection.cpp
  src/projection/InlineProjection.h
  src/projection/SelectionSerializer.cpp
  src/projection/SelectionSerializer.h
  src/projection/MarkdownHtmlSerializer.cpp
  src/projection/MarkdownHtmlSerializer.h
  src/export/HtmlExporter.cpp
  src/export/HtmlExporter.h
  src/export/PandocRunner.cpp
  src/export/PandocRunner.h
  src/blocks/code/CodeFenceController.cpp
  src/blocks/code/CodeFenceController.h
  src/blocks/code/CodeFenceScrollController.cpp
  src/blocks/code/CodeFenceScrollController.h
  src/blocks/literal/LiteralBlockController.cpp
  src/blocks/literal/LiteralBlockController.h
  src/blocks/literal/LiteralBlockUtil.cpp
  src/blocks/literal/LiteralBlockUtil.h
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
  src/app/OutlineModel.cpp
  src/app/OutlineModel.h
  src/app/StatusBarWidget.cpp
  src/app/StatusBarWidget.h
  src/editor/SourceEditorWidget.cpp
  src/editor/SourceEditorWidget.h
  src/editor/SourceLineHeightIndex.cpp
  src/editor/SourceLineHeightIndex.h
  src/editor/VirtualSourceEdit.cpp
  src/editor/VirtualSourceEdit.h
  src/editor/FindBarWidget.cpp
  src/editor/FindBarWidget.h
  src/editor/DocumentSearch.cpp
  src/editor/DocumentSearch.h
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
  src/editor/InlineSplit.cpp
  src/editor/InlineSplit.h
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
  src/editor/EditorViewPaint.cpp
  src/editor/EditorViewViewport.cpp
  src/editor/EditorView.h
  src/editor/EditorViewGeometry.cpp
  src/editor/EditorViewGeometry.h
  src/editor/HtmlBlockHoverController.cpp
  src/editor/HtmlBlockHoverController.h
  src/editor/HoverAnimator.cpp
  src/editor/HoverAnimator.h
  src/editor/FocusAnimator.cpp
  src/editor/FocusAnimator.h
  src/editor/KeyframeAnimator.cpp
  src/editor/KeyframeAnimator.h
  src/editor/CodeLanguageEditor.cpp
  src/editor/CodeLanguageEditor.h
  src/editor/EmojiCompleter.cpp
  src/editor/EmojiCompleter.h
  src/editor/EmojiProvider.h
  src/editor/EmojiDictionary.cpp
  src/editor/EmojiDictionary.h
  src/editor/TableToolbar.cpp
  src/editor/TableToolbar.h
  src/editor/InputController.cpp
  src/editor/InputController.h
  src/editor/InputControllerAutoPair.cpp
  src/editor/InputControllerSmartPunct.cpp
  src/editor/InputControllerEmoji.cpp
  src/editor/InputControllerRemoval.cpp
  src/editor/InputControllerEdit.cpp
  src/io/ImageFileOps.cpp
  src/io/ImageFileOps.h
  src/io/FilePathOps.cpp
  src/io/FilePathOps.h
  src/io/MuffinMime.h
  src/image/ImageInsertionPolicy.cpp
  src/image/ImageInsertionPolicy.h
  src/image/CustomCommandUploader.cpp
  src/image/CustomCommandUploader.h
  src/editor/SelectionController.cpp
  src/editor/SelectionController.h
  src/editor/TextBlockCommandBuilder.cpp
  src/editor/TextBlockCommandBuilder.h
  src/math/MathBuilder.h
  src/math/MathDelimiter.h
  src/math/MathFontMetrics.h
  src/math/MathFontRegistry.h
  src/math/OpenTypeMathFont.h
  src/math/MathDimension.h
  src/math/MathFunctionRegistry.h
  src/math/MathLexer.h
  src/math/MathLayoutTree.h
  src/math/MathMacroExpander.h
  src/math/MathOptions.h
  src/math/MathParseNode.h
  src/math/MathParseError.h
  src/math/MathParser.h
  src/math/MathRendererTheme.cpp
  src/math/MathRenderer.h
  src/math/MathRenderNode.h
  src/math/MathSettings.h
  src/math/MathStyle.h
  src/math/MathSvgGeometry.h
  src/math/MathSymbols.h
  src/render/BlockLayout.cpp
  src/render/BlockLayout.h
  src/render/CodeHighlight.h
  src/render/BlockLayoutBuilder.cpp
  src/render/BlockLayoutBuilder.h
  src/render/DocumentLayout.cpp
  src/render/DocumentLayout.h
  src/render/LayoutPositionIndex.cpp
  src/render/LayoutPositionIndex.h
  src/render/GradientPainter.cpp
  src/render/GradientPainter.h
  src/render/Blur.cpp
  src/render/Blur.h
  src/render/Filter.cpp
  src/render/Filter.h
  src/render/DecorationPainter.cpp
  src/render/DecorationPainter.h
  src/render/KeyframeSampler.cpp
  src/render/KeyframeSampler.h
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
  src/theme/FontRendering.cpp
  src/theme/FontRendering.h
  src/theme/RenderTheme.cpp
  src/theme/RenderTheme.h
  src/theme/ThemeDefinition.cpp
  src/theme/ThemeDefinition.h
  src/theme/ThemeManager.cpp
  src/theme/ThemeManager.h
  src/theme/ChromeStyleSheet.cpp
  src/theme/ChromeStyleSheet.h
  src/theme/CssStyleDebug.cpp
  src/theme/CssStyleDebug.h
  src/theme/CssThemeMapper.cpp
  src/theme/CssThemeMapper.h
  src/theme/CssDecorationExtractor.cpp
  src/theme/CssDecorationExtractor.h
  src/theme/CssFlatDecl.h
  src/theme/CssValueParser.cpp
  src/theme/CssValueParser.h
  src/theme/NodeCssElement.cpp
  src/theme/NodeCssElement.h
  src/theme/CssContent.cpp
  src/theme/CssContent.h
)

add_library(MuffinUi ${MUFFIN_LIB_TYPE}
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
    avif_static
    # avif_static is a merged static archive (merge_static_libs) that only folds in libavif's
    # AVIF_LOCAL deps; the SYSTEM dav1d from Conan is NOT merged into the archive and is NOT
    # forwarded by the archive target, so libavif's dav1d symbols (_dav1d_open, ...) would be
    # unresolved at every consumer's link step (macOS arm64 "Undefined symbols", and Linux too).
    # Link dav1d explicitly here. MuffinUi is STATIC, so this PRIVATE dep still propagates to its
    # consumers (Muffin.exe + every render/test exe) as a link requirement.
    # See third_party/libavif/VENDORED.md.
    dav1d::dav1d
    Qt6::Svg
    lexbor::lexbor_static
    yoga::yoga
    # InlineProjection (a Ui source) calls cmark directly. Under the default
    # STATIC build it resolves those symbols from MuffinCore's archives at the
    # final-exe link; when MUFFIN_LIB_TYPE=SHARED (the CI ASan job) the Ui .so
    # must resolve them at its own link, so list the archives here too. The
    # linker deduplicates them for STATIC consumers.
    libcmark-gfm-extensions_static
    libcmark-gfm_static
    Nuspell::nuspell
    JPEG::JPEG
    PNG::PNG
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
# Determine the WiX target architecture so the same 'msi' target produces an
# x64 installer on x86_64 hosts and an arm64 installer on the windows-11-arm
# runner. Prefer an explicit override; otherwise derive from the platform so a
# native build picks the right one. CMAKE_SYSTEM_PROCESSOR can surface as
# AMD64, arm64, armv8, aarch64, ... depending on the generator/toolchain, so
# fold every ARM spelling into WiX's arm64 rather than matching exact strings.
set(MUFFIN_MSI_ARCH "" CACHE STRING "WiX target architecture for the MSI installer (x64, arm64, ...")
if(NOT MUFFIN_MSI_ARCH)
  if(DEFINED CMAKE_VS_PLATFORM_NAME AND CMAKE_VS_PLATFORM_NAME)
    string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" _msi_arch_candidate)
  elseif(DEFINED CMAKE_SYSTEM_PROCESSOR AND CMAKE_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _msi_arch_candidate)
  endif()
  if(_msi_arch_candidate MATCHES "^(amd64|x86_64|x64)$")
    set(MUFFIN_MSI_ARCH "x64")
  elseif(_msi_arch_candidate MATCHES "(aarch|arm)")
    set(MUFFIN_MSI_ARCH "arm64")
  elseif(_msi_arch_candidate STREQUAL "x86")
    set(MUFFIN_MSI_ARCH "x86")
  elseif(_msi_arch_candidate)
    set(MUFFIN_MSI_ARCH "${_msi_arch_candidate}")
  endif()
  unset(_msi_arch_candidate)
endif()
if(WIN32)
  find_program(WIX_COMMAND wix)
  if(WIX_COMMAND)
    add_custom_target(msi
      COMMAND ${CMAKE_COMMAND}
        "-DDIST_DIR=${MUFFIN_DIST_DIR}"
        "-DWIX_COMMAND=${WIX_COMMAND}"
        "-DMUFFIN_VERSION=${MUFFIN_EFFECTIVE_VERSION}"
        "-DMSI_ARCH=${MUFFIN_MSI_ARCH}"
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
if(MUFFIN_QT_PLUGIN_DIRS_ARG)
  list(APPEND MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS "QT_PLUGIN_PATH=set:${MUFFIN_QT_PLUGIN_DIRS_ARG}")
endif()
if(APPLE)
  list(APPEND MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS "QT_QPA_PLATFORM=set:offscreen")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MuffinTests.cmake)
