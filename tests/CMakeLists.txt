set(TEST_SOURCES
    core/test_document.cpp
    model/test_block_document_merger.cpp
    model/test_markdown_math_span_builder.cpp
    model/test_markdown_transform.cpp
    parser/test_cmark_parser.cpp
    parser/test_block_reparse_planner.cpp
    parser/test_block_reparser.cpp
    parser/test_math_delimiter_scanner.cpp
    parser/test_source_coordinate_mapper.cpp
    renderer/test_render_source_map.cpp
    renderer/test_render_update_queue.cpp
    renderer/test_document_renderer.cpp
    renderer/test_incremental_render_validation_policy.cpp
    renderer/test_math_rendering.cpp
    renderer/test_partial_document_patch_applier.cpp
    renderer/test_partial_document_patch_planner.cpp
    renderer/test_partial_patch_apply_policy.cpp
    renderer/test_partial_render_state_merger.cpp
    renderer/test_text_document_patch_consistency.cpp
    renderer/test_syntax_token_index.cpp
    renderer/test_marker_fragment_index.cpp
    renderer/test_marker_projection.cpp
    editor/test_markdown_command.cpp
    editor/test_style_command_handler.cpp
    editor/test_heading_command_handler.cpp
    editor/test_list_command_handler.cpp
    editor/test_quote_command_handler.cpp
    editor/test_editor_selection_mapper.cpp
    editor/test_marker_caret_controller.cpp
    editor/test_marker_selection_mapper.cpp
    editor/test_marker_visibility_controller.cpp
    editor/test_edit_transaction_applier.cpp
    editor/test_edit_transaction_validator.cpp
    editor/test_markdown_edit_engine.cpp
    editor/test_render_invalidation.cpp
    editor/test_rendered_edit_planner.cpp
    editor/test_structural_undo_controller.cpp
    editor/test_formatting_undo_integration.cpp
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
    if(TEST_FILE MATCHES "model/test_markdown_transform.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownMathSpanBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSourceSpanUpdater.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownTransform.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "model/test_block_document_merger.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/BlockDocumentMerger.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownMathSpanBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparsePlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "model/test_markdown_math_span_builder.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownMathSpanBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSourceSpanUpdater.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownTransform.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "parser/test_cmark_parser.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "parser/test_block_reparse_planner.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownMathSpanBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparsePlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "parser/test_block_reparser.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownMathSpanBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparsePlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "parser/test_math_delimiter_scanner.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "parser/test_source_coordinate_mapper.cpp")
        target_sources(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "renderer/test_render_source_map.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp)
    endif()
    if(TEST_FILE MATCHES "renderer/test_render_update_queue.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderUpdateQueue.cpp)
    endif()
    if(TEST_FILE MATCHES "renderer/test_document_renderer.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/IncrementalRenderValidationPolicy.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderConsistency.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "renderer/test_incremental_render_validation_policy.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/renderer/IncrementalRenderValidationPolicy.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Gui)
    endif()
    if(TEST_FILE MATCHES "renderer/test_partial_document_patch_planner.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialDocumentPatchPlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "renderer/test_partial_document_patch_applier.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/IncrementalRenderValidationPolicy.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialDocumentPatchApplier.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialDocumentPatchPlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/TextDocumentPatchConsistency.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "renderer/test_partial_patch_apply_policy.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialPatchApplyPolicy.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/TextDocumentPatchConsistency.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Gui)
    endif()
    if(TEST_FILE MATCHES "renderer/test_partial_render_state_merger.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialDocumentPatchPlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialRenderStateMerger.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "renderer/test_text_document_patch_consistency.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/renderer/TextDocumentPatchConsistency.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Gui)
    endif()
    if(TEST_FILE MATCHES "renderer/test_syntax_token_index.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/SyntaxTokenIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "renderer/test_marker_fragment_index.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MarkerFragmentIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/SyntaxTokenIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "renderer/test_marker_projection.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MarkerFragmentIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MarkerProjection.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/SyntaxTokenIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "core/test_document.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/EditTransactionApplier.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/EditTransactionValidator.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/EditorSelectionMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkerVisibilityController.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/RenderInvalidation.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/StructuralUndoController.cpp
            ${CMAKE_SOURCE_DIR}/src/model/BlockDocumentMerger.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownMathSpanBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSourceSpanUpdater.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownTransform.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparsePlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/IncrementalRenderValidationPolicy.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MarkerFragmentIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialDocumentPatchApplier.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialDocumentPatchPlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialPatchApplyPolicy.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialRenderStateMerger.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderConsistency.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderUpdateQueue.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/SyntaxTokenIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/TextDocumentPatchConsistency.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "renderer/test_math_rendering.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "editor/test_markdown_command.cpp")
        target_sources(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets)
    endif()
    if(TEST_FILE MATCHES "editor/test_style_command_handler.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/StyleCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets)
    endif()
    if(TEST_FILE MATCHES "editor/test_heading_command_handler.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/HeadingCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets)
    endif()
    if(TEST_FILE MATCHES "editor/test_list_command_handler.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/ListCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets)
    endif()
    if(TEST_FILE MATCHES "editor/test_quote_command_handler.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/QuoteCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets)
    endif()
    if(TEST_FILE MATCHES "editor/test_editor_selection_mapper.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/EditorSelectionMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets)
    endif()
    if(TEST_FILE MATCHES "editor/test_marker_visibility_controller.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/EditorSelectionMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkerVisibilityController.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/SyntaxTokenIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "editor/test_marker_caret_controller.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/MarkerCaretController.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui)
    endif()
    if(TEST_FILE MATCHES "editor/test_marker_selection_mapper.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/MarkerSelectionMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MarkerProjection.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp)
    endif()
    if(TEST_FILE MATCHES "editor/test_edit_transaction_validator.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/EditTransactionValidator.cpp)
    endif()
    if(TEST_FILE MATCHES "editor/test_edit_transaction_applier.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/EditTransactionApplier.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownTransform.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "editor/test_markdown_edit_engine.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/EditorSelectionMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/HeadingCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/ListCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/QuoteCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownEditEngine.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/EditTransactionValidator.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/RenderedEditPlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/StyleCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownTransform.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "editor/test_render_invalidation.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/RenderInvalidation.cpp)
    endif()
    if(TEST_FILE MATCHES "editor/test_rendered_edit_planner.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/RenderedEditPlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
    endif()
    if(TEST_FILE MATCHES "editor/test_structural_undo_controller.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/editor/EditTransactionApplier.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/StructuralUndoController.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownTransform.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp)
    endif()
    if(TEST_FILE MATCHES "editor/test_formatting_undo_integration.cpp")
        target_sources(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/core/Document.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/EditTransactionApplier.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/EditTransactionValidator.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/EditorSelectionMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/HeadingCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/ListCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownCommand.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkdownEditEngine.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/MarkerVisibilityController.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/QuoteCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/RenderInvalidation.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/RenderedEditPlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/StyleCommandHandler.cpp
            ${CMAKE_SOURCE_DIR}/src/editor/StructuralUndoController.cpp
            ${CMAKE_SOURCE_DIR}/src/model/BlockDocumentMerger.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownDocument.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownMathSpanBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownNode.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSerializer.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownSourceSpanUpdater.cpp
            ${CMAKE_SOURCE_DIR}/src/model/MarkdownTransform.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparsePlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/BlockReparser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/CmarkParser.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/MathDelimiterScanner.cpp
            ${CMAKE_SOURCE_DIR}/src/parser/SourceCoordinateMapper.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/DocumentRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/IncrementalRenderValidationPolicy.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MarkerFragmentIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/MathImageRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialDocumentPatchApplier.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialDocumentPatchPlanner.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialPatchApplyPolicy.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/PartialRenderStateMerger.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderConsistency.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderSourceMap.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/RenderUpdateQueue.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/SyntaxTokenIndex.cpp
            ${CMAKE_SOURCE_DIR}/src/renderer/TextDocumentPatchConsistency.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp
            ${CMAKE_SOURCE_DIR}/src/theme/ThemeStylesheet.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE Qt6::Widgets Qt6::Gui LaTeX)
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
