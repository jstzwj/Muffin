set(MUFFIN_SOURCES
    main.cpp
    app/Application.cpp
    app/MainWindow.cpp
    core/Document.cpp
    core/FileManager.cpp
    model/BlockDocumentMerger.cpp
    model/MarkdownDocument.cpp
    model/MarkdownMathSpanBuilder.cpp
    model/MarkdownNode.cpp
    model/MarkdownSerializer.cpp
    model/MarkdownSourceSpanUpdater.cpp
    model/MarkdownTransform.cpp
    parser/CmarkParser.cpp
    parser/BlockReparsePlanner.cpp
    parser/BlockReparser.cpp
    parser/MathDelimiterScanner.cpp
    parser/SourceCoordinateMapper.cpp
    renderer/DocumentRenderer.cpp
    renderer/IncrementalRenderValidationPolicy.cpp
    renderer/MarkerFragmentIndex.cpp
    renderer/MarkerProjection.cpp
    renderer/MathImageRenderer.cpp
    renderer/PartialDocumentPatchApplier.cpp
    renderer/PartialDocumentPatchPlanner.cpp
    renderer/PartialPatchApplyPolicy.cpp
    renderer/PartialRenderStateMerger.cpp
    renderer/RenderConsistency.cpp
    renderer/RenderSourceMap.cpp
    renderer/RenderUpdateQueue.cpp
    renderer/SyntaxTokenIndex.cpp
    renderer/TextDocumentPatchConsistency.cpp
    theme/Theme.cpp
    theme/ThemeStylesheet.cpp
    editor/MarkdownEditor.cpp
    editor/EditorSelectionMapper.cpp
    editor/EditTransactionApplier.cpp
    editor/EditTransactionValidator.cpp
    editor/MarkerCaretController.cpp
    editor/MarkerSelectionMapper.cpp
    editor/MarkerVisibilityController.cpp
    editor/MarkdownCommand.cpp
    editor/MarkdownEditEngine.cpp
    editor/HeadingCommandHandler.cpp
    editor/ListCommandHandler.cpp
    editor/QuoteCommandHandler.cpp
    editor/RenderInvalidation.cpp
    editor/RenderedEditPlanner.cpp
    editor/StyleCommandHandler.cpp
    editor/StructuralUndoController.cpp
)

set(MUFFIN_HEADERS
    app/Application.h
    app/MainWindow.h
    core/Document.h
    core/FileManager.h
    model/BlockDocumentMerger.h
    model/MarkdownDocument.h
    model/MarkdownMathSpanBuilder.h
    model/MarkdownNode.h
    model/MarkdownSerializer.h
    model/MarkdownSourceSpanUpdater.h
    model/MarkdownTransform.h
    parser/AstNode.h
    parser/AstTree.h
    parser/BlockReparsePlanner.h
    parser/BlockReparser.h
    parser/CmarkParser.h
    parser/MathDelimiterScanner.h
    parser/MathSpan.h
    parser/SourceSpan.h
    parser/SourceCoordinateMapper.h
    renderer/DocumentRenderer.h
    renderer/IncrementalRenderValidationPolicy.h
    renderer/MarkerFragmentIndex.h
    renderer/MarkerProjection.h
    renderer/MathImageRenderer.h
    renderer/MarkdownBlock.h
    renderer/PartialDocumentPatchApplier.h
    renderer/PartialDocumentPatchPlanner.h
    renderer/PartialPatchApplyPolicy.h
    renderer/PartialRenderResult.h
    renderer/PartialRenderStateMerger.h
    renderer/RenderConsistency.h
    renderer/RenderFragment.h
    renderer/RenderSourceMap.h
    renderer/RenderUpdateQueue.h
    renderer/SourceBlockData.h
    renderer/SyntaxTokenIndex.h
    renderer/SyntaxTokenSpan.h
    renderer/TextDocumentPatchConsistency.h
    theme/Theme.h
    theme/ThemeStylesheet.h
    editor/MarkdownEditor.h
    editor/EditorSelectionMapper.h
    editor/EditTransaction.h
    editor/EditTransactionApplier.h
    editor/EditTransactionValidator.h
    editor/MarkerCaretController.h
    editor/MarkerSelectionMapper.h
    editor/MarkerVisibilityController.h
    editor/MarkdownCommand.h
    editor/MarkdownEditEngine.h
    editor/HeadingCommandHandler.h
    editor/ListCommandHandler.h
    editor/QuoteCommandHandler.h
    editor/RenderInvalidation.h
    editor/RenderedEdit.h
    editor/RenderedEditPlanner.h
    editor/StyleCommandHandler.h
    editor/StructuralUndoController.h
    utils/SafeCString.h
)

add_executable(muffin WIN32 ${MUFFIN_SOURCES} ${MUFFIN_HEADERS})

target_include_directories(muffin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMARK_GFM_INCLUDE_DIRS}
)

target_link_libraries(muffin PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::PrintSupport
    libcmark-gfm_static
    libcmark-gfm-extensions_static
    LaTeX
)

# cmark-gfm: use static linkage (no __declspec(dllimport))
target_compile_definitions(muffin PRIVATE CMARK_GFM_STATIC_DEFINE)

# Qt resources
set(RESOURCE_FILES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
set_source_files_properties(${RESOURCE_FILES} PROPERTIES AUTORCC ON)
target_sources(muffin PRIVATE ${RESOURCE_FILES})

# Release optimizations
if(MSVC)
    target_compile_options(muffin PRIVATE /W4)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(muffin PRIVATE /O2 /GL)
        target_link_options(muffin PRIVATE /LTCG)
    endif()
else()
    target_compile_options(muffin PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(muffin PRIVATE -Os -flto)
        target_link_options(muffin PRIVATE -flto -s)
    endif()
    # macOS: generate app bundle
    if(APPLE)
        set_target_properties(muffin PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER com.muffin.editor
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        )
    endif()
endif()

# ============================================================
# Post-build: deploy runtime dependencies
# ============================================================
set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/Muffin-dist")

if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/deploy.cmake")
    add_custom_command(TARGET muffin POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE="$<TARGET_FILE:muffin>"
            -DDEPLOY_DIR="${DEPLOY_DIR}"
            -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
            -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
            -DCMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
            -P "${CMAKE_SOURCE_DIR}/scripts/deploy.cmake"
        COMMENT "Deploying runtime dependencies to ${DEPLOY_DIR}"
    )
endif()
