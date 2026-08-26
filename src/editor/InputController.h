#pragma once

#include "document/DocumentSession.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/EditorContext.h"
#include "editor/TextBlockCommandBuilder.h"

#include <QObject>
#include <QVector>

#include <optional>

class QKeyEvent;
class QInputMethodEvent;

namespace muffin {

class BlockLayout;
class CodeFenceController;
class EditorView;
class EmojiCompleter;
class EmojiProvider;
class LiteralBlockController;
class MarkdownNode;
class SelectionController;
class TableController;

class InputController final : public QObject {
  Q_OBJECT

public:
  explicit InputController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);
  void setTableController(TableController* tableController);
  void setCodeFenceController(CodeFenceController* codeFenceController);
  // editor/emojiAutocomplete: supplies the shortcode->glyph table. nullptr (the default) leaves
  // the feature inert even when the preference is on; production wires a BundledEmojiProvider,
  // tests inject a fake.
  void setEmojiProvider(const EmojiProvider* provider);
  // Exposed for tests so they can assert popup visibility after typing a ":shortcode" trigger.
  EmojiCompleter* emojiCompleter() const { return emojiCompleter_; }

  bool insertText(QString text);
  bool insertParagraphBreak();
  bool insertBlockAfterCurrentBlock(QString text = {});
  bool deleteBackward();
  bool deleteForward();
  bool indentListItem();
  bool outdentListItem();
  bool deleteSelection();
  bool hasEditableSelection() const;
  bool handleInputMethod(QInputMethodEvent* event);

  // Select the next occurrence of the current selection (Ctrl+J). When the
  // selection is collapsed the caller is expected to expand to the current word
  // first; returns false when there is nothing to search for.
  bool selectNextOccurrence();
  bool selectSourceRange(qsizetype start, qsizetype end);

  void performLocalEdit(
      EditTransaction::Kind kind,
      const QString& label,
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      CursorPosition preferredCursor,
      qsizetype fallbackSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {},
      bool preferLaterEmptyAtOffset = false,
      bool structureEdit = false);

  bool eventFilter(QObject* watched, QEvent* event) override;

  // Exit the active literal editor when the caret is no longer on its block. Called after
  // command-driven cursor moves (e.g. insert-paragraph-before/after) so the caret and the next
  // keystroke follow the new position instead of being trapped in the literal block.
  void reconcileLiteralEditorForCursor();

signals:
  void unsupportedEditRequested(QString reason);
  void selectAllRequested();

private:
  bool handleKeyPress(QKeyEvent* event);
  bool hasActiveLiteralEditor() const;
  void syncLiteralEditMode(NodeId newBlockId);
  bool insertTextIntoActiveLiteral(QString text);
  // Shift+Tab inside a code fence: dedents the selected lines (or the caret's line when there is
  // no selection) by one indent unit. Returns true if it handled the keystroke; false to fall back
  // to inserting a tab.
  bool tryDedentActiveCodeFence();
  bool tryInsertOptionalDefinitionTitle(QString text);
  // Auto-pair / wrap-selection / skip-over for a single typed character, gated by the
  // editor/matchBrackets and editor/matchMarkdown preferences. Returns true when it handled the
  // keystroke (caller returns true); false to let the character insert normally.
  bool tryAutoPairOrWrap(QChar ch);
  // markdown/* smart punctuation. trySmartDashes collapses "--"/"---" into en/em dashes (applies its
  // own edit since it must delete preceding characters); returns true when handled. The quote
  // conversion is a pure text transform, applied to `text` before auto-pairing so the smart quote
  // chars bypass the pair table. Both honor markdown/convertOnInput and only run in prose (code/math
  // literal editors return before this point) with a collapsed selection.
  bool trySmartDashes(QChar ch);
  bool trySmartEllipsis(QChar ch);
  QString maybeConvertSmartPunctuation(QString text);
  EmojiCompleter* ensureEmojiCompleter();
  void maybeUpdateEmojiPopup();
  void insertEmoji(const QString& glyph);
  void hideEmojiPopup();
  bool deleteBackwardInActiveLiteral();
  bool deleteForwardInActiveLiteral();
  bool deleteSelectionInActiveLiteral();
  bool exitActiveLiteralEditor();
  QString activeLiteralTabText() const;
  bool insertIntoEmptyDocument(QString text);
  bool shouldIndentListItemFromKeyboard() const;
  bool editParagraph(TextBlockCommandBuilder::Operation operation, QString text = {});
  bool applyTextCommand(const TextBlockCommandBuilder::Command& command);
  bool replaceSelection(QString text, EditTransaction::Kind kind, QString label);
  bool tryRemoveExactWholeBlockSelection(EditTransaction::Kind kind, const QString& label);
  bool tryRemoveEmptyLiteralBlock(EditTransaction::Kind kind, const QString& label);
  bool tryRemoveEmptyDefinitionBlock(EditTransaction::Kind kind, const QString& label);
  bool tryRemoveThematicBreak(bool forward);
  // Shared top-level block-removal helpers used by the tryRemove* handlers above.
  int topLevelBlockIndex(const MarkdownNode& node) const;
  // Standard removal range: extend [blockStart, blockEnd] to swallow the inter-block separator
  // (the next block's leading bytes, or the previous block's trailing bytes) so the deletion
  // leaves no blank line. Returns false if the resulting range is empty.
  bool computeStandardRemovalRange(qsizetype blockStart, qsizetype blockEnd, int nodeIndex,
                                   qsizetype& deleteStart, qsizetype& deleteEnd) const;
  // Apply a top-level block deletion: capture undo state, optionally exit a literal editor
  // first, apply the text delta, place the post-edit caret, push the undo transaction, and
  // flush the brush queue synchronously. `exitLiteralEditorFirst` is set by the literal handler.
  bool removeTopLevelBlock(MarkdownNode& node, int nodeIndex, qsizetype blockStart,
                           qsizetype deleteStart, qsizetype deleteEnd, EditTransaction::Kind kind,
                           const QString& label, bool exitLiteralEditorFirst);
  bool collapseTrailingCaretToEndOfLastBlock();
  // True for a top-level block that hosts NO inline-editable text anywhere AND is not a literal
  // block (code/math/html/front matter edit through their own controllers; lists/tables/quotes have
  // editable descendants). Currently only a thematic break, but expressed structurally so a future
  // editor-less non-text leaf is covered without touching every call site. The shared predicate for
  // the Enter/typing routing that has nothing to split or type into.
  bool isNonTextLeafBlock(MarkdownNode& node) const;
  // True when the caret rests on such a non-text leaf — in its afterBlock virtual area or landed on
  // it directly via arrow nav — so Enter/typing must insert a fresh paragraph after the block.
  bool caretRestsOnNonTextBlock() const;
  bool selectionSourceRange(qsizetype& start, qsizetype& end) const;
  bool blockSelectionSourceRange(qsizetype& start, qsizetype& end) const;
  // Node whose content text word operations should scan: the caret's block, or the table cell
  // node when the caret sits inside one (word ops stay within the cell).
  MarkdownNode* wordEditNode(const CursorPosition& current) const;
  BlockEditContextResolver contextResolver() const;
  CursorPosition cursorFor(NodeId blockId, qsizetype offset) const;
  CursorPosition cursorForNode(MarkdownNode& node, qsizetype offset) const;
  CursorPosition cursorForSourceOffset(qsizetype sourceOffset, bool preferLaterEmptyAtOffset = false) const;
  // Resolve an absolute source offset that is known to land inside the editable-text `node`
  // (paragraph / heading / list item / cell / definition). O(block) — it fills the node's edit
  // context and maps source→visible locally, instead of cursorForSourceOffset's document-wide
  // tree walk to LOCATE the node. Use this whenever the block is already in hand (vertical
  // navigation, which fires per key press).
  CursorPosition cursorForSourceInNode(MarkdownNode& node, qsizetype sourceOffset) const;
  CursorPosition cursorAfterEdit(CursorPosition preferredCursor, qsizetype fallbackSourceOffset, bool preferLaterEmptyAtOffset = false) const;
  // A caret that sits block-after a non-text top-level block (thematic break, etc.) — the only kind
  // of caret an offset on such a block can resolve to, since they host no inline-editable text.
  CursorPosition cursorForBlockAfter(const MarkdownNode& host, qsizetype sourceOffset) const;
  MarkdownNode* paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset, bool preferLaterEmptyAtOffset = false) const;
  MarkdownNode* selectableBlockByDirection(NodeId current, int direction) const;
  // The neighbour block in DOCUMENT ORDER, climbing out of containers (lists / block quotes) so a
  // nested caret can reach the editable block that visually follows/precedes it. Unlike
  // selectableBlockByDirection (top-level only), this crosses list items and quoted paragraphs.
  MarkdownNode* neighborBlockInDocumentDirection(const MarkdownNode& node, int direction) const;
  // The LAST editable block in document order within a subtree, descending into a list item's nested
  // children (unlike the resolver's lastEditableDescendant, which short-circuits on a ListItem).
  MarkdownNode* deepestLastEditableInSubtree(MarkdownNode& node) const;
  qsizetype selectableTextLength(const MarkdownNode& node) const;
  // The selectable text itself (mirrors selectableTextLength's per-type source), used for
  // grapheme-aware caret stepping.
  QString selectableTextOf(const MarkdownNode& node) const;
  bool moveCursorHorizontal(int direction, bool extendSelection);
  // Ctrl+Left/Right: caret by programmer word (shared muffin::words semantics), crossing to the
  // neighbouring block when the caret is already at the word-less edge of this one.
  bool moveCursorWord(int direction, bool extendSelection);
  // Ctrl+Backspace / Ctrl+Delete: delete to the previous/next word boundary within the current
  // block. Returns false when there is nothing word-wise to eat in that direction (block edge,
  // non-editable caret) so the caller can fall back to character semantics.
  bool deleteWordBackward();
  bool deleteWordForward();
  bool deleteWordInDirection(int direction);
  bool moveCursorVertical(int direction, bool extendSelection);
  // Ctrl+Up/Down: caret to the start of the previous/next block in document order (Word-like
  // paragraph navigation). neighbourBlockInDocumentDirection climbs out of lists and quotes.
  bool moveBlockVertical(int direction, bool extendSelection);
  // PageUp/PageDown: caret one viewport page (with a one-line overlap), scrolling by the same
  // delta so the caret keeps its screen Y. Caret moves never auto-scroll, so both halves are
  // owned here.
  bool moveCursorPage(int direction, bool extendSelection);
  bool moveTableCellHorizontal(int direction, bool extendSelection);
  bool moveTableCellVertical(int direction, bool extendSelection, qreal documentX);
  bool moveLiteralVertical(const BlockLayout& block, MarkdownNode& node, int direction, bool extendSelection);
  enum class JumpTarget { LineStart, LineEnd, DocumentStart, DocumentEnd };
  bool moveJump(JumpTarget target, bool extendSelection);
  HitTestResult richHitForCursor(CursorPosition cursor) const;
  HitTestResult visualEdgeHitForBlock(NodeId blockId, int direction, qreal documentX) const;
  void setCursorOrExtend(CursorPosition cursor, bool extendSelection);
  void setHitOrExtend(HitTestResult hit, bool extendSelection);
  void clearVerticalNavigationX();
  void applyEdit(EditTransaction::Kind kind, const QString& label, QString nextText, qsizetype nextSourceOffset);
  void applyEdit(EditTransaction::Kind kind, const QString& label, QString nextText, qsizetype nextSourceOffset, bool preferLaterEmptyAtOffset);
  void applyLocalEdit(
      EditTransaction::Kind kind,
      const QString& label,
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      CursorPosition preferredCursor,
      qsizetype fallbackSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {},
      bool preferLaterEmptyAtOffset = false,
      bool structureEdit = false);
  void applyEdit(
      EditTransaction::Kind kind,
      const QString& label,
      QString nextText,
      CursorPosition preferredCursor,
      qsizetype fallbackSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {},
      bool preferLaterEmptyAtOffset = false);
  QString printableText(QKeyEvent* event) const;

  LiteralBlockController* activeLiteralEditor() const;

  EditorContext ctx_;
  CodeFenceController* codeFenceController_ = nullptr;
  TableController* tableController_ = nullptr;
  EmojiCompleter* emojiCompleter_ = nullptr;
  const EmojiProvider* emojiProvider_ = nullptr;
  bool hasVerticalNavigationX_ = false;
  qreal verticalNavigationX_ = 0.0;
  CursorPosition verticalNavigationCursor_;
  qsizetype emojiColonStart_ = -1;  // source offset of the leading ':' of the active shortcode
};

}  // namespace muffin
