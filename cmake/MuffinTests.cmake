# All Muffin test executables, registered via muffin_add_test (see MuffinTestFunctions.cmake).
# One line per test; grouped by subsystem in build order. Included from the top-level
# CMakeLists.txt after MUFFIN_TEST_ENVIRONMENT_MODIFICATIONS is assembled.

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MuffinTestFunctions.cmake)

# --- parser / document (pure logic; link MuffinCore, no Qt GUI lock) ---
muffin_add_test(NAME MuffinParserBasicTest      SOURCE tests/parser/ParserBasicTest.cpp      LINK MuffinCore)
muffin_add_test(NAME MuffinParserDefinitionTest SOURCE tests/parser/ParserDefinitionTest.cpp LINK MuffinCore)
muffin_add_test(NAME MuffinParserMathCodeTest   SOURCE tests/parser/ParserMathCodeTest.cpp   LINK MuffinCore)
muffin_add_test(NAME MuffinOutlineBuilderTest   SOURCE tests/document/OutlineBuilderTest.cpp LINK MuffinCore)
muffin_add_test(NAME MuffinImageSyntaxOpsTest   SOURCE tests/document/ImageSyntaxOpsTest.cpp LINK MuffinCore)
muffin_add_test(NAME MuffinMarkdownParseOptionsTest SOURCE tests/document/MarkdownParseOptionsTest.cpp LINK MuffinCore)
muffin_add_test(NAME MuffinAlertBoxParseTest       SOURCE tests/document/AlertBoxParseTest.cpp       LINK MuffinCore)
muffin_add_test(NAME MuffinHighlightParseTest      SOURCE tests/document/HighlightParseTest.cpp      LINK MuffinCore)
muffin_add_test(NAME MuffinSubscriptSuperscriptParseTest SOURCE tests/document/SubscriptSuperscriptParseTest.cpp LINK MuffinCore)

# --- render (link MuffinUi, take the render_smoke fixture, lock the GUI) ---
muffin_add_test(NAME MuffinRenderThemeTest            SOURCE tests/render/RenderThemeTest.cpp            LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK DISABLED_ON APPLE)
muffin_add_test(NAME MuffinRenderIncrementalTest      SOURCE tests/render/RenderIncrementalTest.cpp      LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK)
muffin_add_test(NAME MuffinRenderListMarkerTest       SOURCE tests/render/RenderListMarkerTest.cpp       LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK)
muffin_add_test(NAME MuffinRenderInlineProjectionTest SOURCE tests/render/RenderInlineProjectionTest.cpp LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK)
muffin_add_test(NAME MuffinRenderInlineGeometryTest   SOURCE tests/render/RenderInlineGeometryTest.cpp   LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK)
muffin_add_test(NAME MuffinRenderInlineHitTestTest    SOURCE tests/render/RenderInlineHitTest.cpp        LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK)
muffin_add_test(NAME MuffinRenderMathLayoutTest       SOURCE tests/render/RenderMathLayoutTest.cpp       LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK DISABLED_ON APPLE)
muffin_add_test(NAME MuffinRenderMathGeometryTest     SOURCE tests/render/RenderMathGeometryTest.cpp     LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK DISABLED_ON APPLE)
muffin_add_test(NAME MuffinRenderMathAuditTest        SOURCE tests/render/RenderMathAuditTest.cpp        LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK DISABLED_ON APPLE)
muffin_add_test(NAME MuffinRenderMathFunctions1Test   SOURCE tests/render/RenderMathFunctions1Test.cpp   LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK DISABLED_ON APPLE)
muffin_add_test(NAME MuffinRenderMathFunctions2Test   SOURCE tests/render/RenderMathFunctions2Test.cpp   LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK DISABLED_ON APPLE)
muffin_add_test(NAME MuffinRenderHtmlLayoutTest       SOURCE tests/render/RenderHtmlLayoutTest.cpp       LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK)
muffin_add_test(NAME MuffinRenderHtmlPaintTest        SOURCE tests/render/RenderHtmlPaintTest.cpp        LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK)
muffin_add_test(NAME MuffinRenderTreeSitterTest       SOURCE tests/render/RenderTreeSitterTest.cpp       LINK MuffinUi FIXTURE tests/fixtures/render_smoke.md RESOURCE_LOCK)

# --- editor view (link MuffinUi, lock the GUI; no fixture arg) ---
muffin_add_test(NAME MuffinEditorViewProjectionSourceTest SOURCE tests/render/EditorViewProjectionSourceTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinEditorViewProjectionMappingTest SOURCE tests/render/EditorViewProjectionMappingTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinEditorViewHitTestTest    SOURCE tests/render/EditorViewHitTestTest.cpp    LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinEditorViewTaskCheckboxTest SOURCE tests/render/EditorViewTaskCheckboxTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinEditorViewSelectionTest  SOURCE tests/render/EditorViewSelectionTest.cpp  LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinEditorViewLayoutTest     SOURCE tests/render/EditorViewLayoutTest.cpp     LINK MuffinUi RESOURCE_LOCK)

# --- editor input / controller ---
muffin_add_test(NAME MuffinEditorCoreTest              SOURCE tests/editor/EditorCoreTest.cpp              LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinImageResizeTest             SOURCE tests/editor/ImageResizeTest.cpp            LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinEditorStyleClipboardTest    SOURCE tests/editor/EditorStyleClipboardTest.cpp    LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinEditorCodeFenceSelectionTest SOURCE tests/editor/EditorCodeFenceSelectionTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputTextEditingTest        SOURCE tests/editor/InputTextEditingTest.cpp        LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputParagraphTest          SOURCE tests/editor/InputParagraphTest.cpp          LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputUndoSelectionTest      SOURCE tests/editor/InputUndoSelectionTest.cpp      LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputListTabTest            SOURCE tests/editor/InputListTabTest.cpp            LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputIndentSizeTest         SOURCE tests/editor/InputIndentSizeTest.cpp         LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputMarkdownDefaultsTest   SOURCE tests/editor/InputMarkdownDefaultsTest.cpp   LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputShiftTabDedentTest     SOURCE tests/editor/InputShiftTabDedentTest.cpp     LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputAlignIndentTest        SOURCE tests/editor/InputAlignIndentTest.cpp        LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputAutoPairTest           SOURCE tests/editor/InputAutoPairTest.cpp          LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinEmojiTriggerTest            SOURCE tests/editor/EmojiTriggerTest.cpp           LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinOrderedListMarkerCursorTest SOURCE tests/editor/OrderedListMarkerCursorTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputLazyBlockTest          SOURCE tests/editor/InputLazyBlockTest.cpp          LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinBackspaceEmptyListItemTest  SOURCE tests/editor/BackspaceEmptyListItemTest.cpp  LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinBackspaceTrailingCaretTest  SOURCE tests/editor/BackspaceTrailingCaretTest.cpp  LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinThematicBreakEditTest       SOURCE tests/editor/ThematicBreakEditTest.cpp       LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinBackspaceEmptyParagraphTest SOURCE tests/editor/BackspaceEmptyParagraphTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputDeleteRangeTest        SOURCE tests/editor/InputDeleteRangeTest.cpp        LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinSourceEditorBackendDeleteTest SOURCE tests/app/SourceEditorBackendDeleteTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputLiteralMergeTest       SOURCE tests/editor/InputLiteralMergeTest.cpp       LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinCodeFenceCommitCursorTest   SOURCE tests/editor/CodeFenceCommitCursorTest.cpp   LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputBlockEditingTest       SOURCE tests/editor/InputBlockEditingTest.cpp       LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinInputDefinitionHeadingTest  SOURCE tests/editor/InputDefinitionHeadingTest.cpp  LINK MuffinUi RESOURCE_LOCK)

# --- block commands ---
muffin_add_test(NAME MuffinParagraphHeadingTest     SOURCE tests/commands/ParagraphHeadingTest.cpp     LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinParagraphBlockInsertTest SOURCE tests/commands/ParagraphBlockInsertTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinParagraphMarkdownDefaultsTest SOURCE tests/commands/ParagraphMarkdownDefaultsTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinParagraphToggleBlockTest SOURCE tests/commands/ParagraphToggleBlockTest.cpp LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinParagraphToggleUndoTest  SOURCE tests/commands/ParagraphToggleUndoTest.cpp  LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinParagraphTaskToggleTest  SOURCE tests/commands/ParagraphTaskToggleTest.cpp  LINK MuffinUi RESOURCE_LOCK)

# --- blocks / controllers (pure-logic controller tests link MuffinUi but never spin up a GUI,
#     so they omit RESOURCE_LOCK and stay parallel; the two that do render take the lock) ---
muffin_add_test(NAME MuffinTableModelOpsTest       SOURCE tests/blocks/TableModelOpsTest.cpp       LINK MuffinUi)
muffin_add_test(NAME MuffinTableControllerTest     SOURCE tests/blocks/TableControllerTest.cpp     LINK MuffinUi)
muffin_add_test(NAME MuffinTableCellEditingTest    SOURCE tests/blocks/TableCellEditingTest.cpp    LINK MuffinUi)
muffin_add_test(NAME MuffinCodeFenceControllerTest SOURCE tests/blocks/CodeFenceControllerTest.cpp LINK MuffinUi)
muffin_add_test(NAME MuffinCodeFenceScrollControllerTest SOURCE tests/blocks/CodeFenceScrollControllerTest.cpp LINK MuffinUi)
muffin_add_test(NAME MuffinMathBlockControllerTest SOURCE tests/blocks/MathBlockControllerTest.cpp LINK MuffinUi)
muffin_add_test(NAME MuffinHtmlBlockControllerTest SOURCE tests/blocks/HtmlBlockControllerTest.cpp LINK MuffinUi)
muffin_add_test(NAME MuffinBlockTableUndoTest      SOURCE tests/blocks/BlockTableUndoTest.cpp      LINK MuffinUi RESOURCE_LOCK)
muffin_add_test(NAME MuffinBlockCodeFrontMatterTest SOURCE tests/blocks/BlockCodeFrontMatterTest.cpp LINK MuffinUi RESOURCE_LOCK)

# --- app ---
muffin_add_test(NAME MuffinTranslationResourceTest SOURCE tests/app/TranslationResourceTest.cpp LINK Qt6::Widgets EXTRA_SOURCES src/translations.qrc RESOURCE_LOCK)

# --- spell check (needs the bundled dictionaries, hence dicts.qrc as an extra source) ---
muffin_add_test(NAME MuffinSpellCheckerTest SOURCE tests/spellcheck/SpellCheckerTest.cpp LINK MuffinUi EXTRA_SOURCES ${MUFFIN_DICTS_QRC} RESOURCE_LOCK)
