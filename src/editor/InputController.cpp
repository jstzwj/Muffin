#include "editor/InputController.h"

#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "projection/InlineProjection.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"
#include "editor/SelectionController.h"
#include "editor/TextBlockCommandBuilder.h"
#include "edit/UndoStack.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/literal/LiteralBlockController.h"
#include "blocks/table/TableController.h"

#include <QEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QInputMethodEvent>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(inputPerf, "muffin.perf", QtWarningMsg)

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(inputPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(inputPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

QString plainTextForNode(const MarkdownNode& node) {
  QString text;
  switch (node.type()) {
    case BlockType::List:
    case BlockType::ListItem:
      for (const auto& child : node.children()) {
        const QString childText = plainTextForNode(*child);
        if (childText.isEmpty()) {
          continue;
        }
        if (!text.isEmpty()) {
          text += QLatin1Char('\n');
        }
        text += childText;
      }
      return text;
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::TableCell:
      return InlineProjection::plainTextForInlines(node.inlines());
    default:
      return node.literal();
  }
}

bool isDeadKey(int key) {
  return (key >= Qt::Key_Dead_Grave && key <= Qt::Key_Dead_Currency) ||
         (key >= Qt::Key_Dead_a && key <= Qt::Key_Dead_Greek) ||
         (key >= Qt::Key_Dead_Lowline && key <= Qt::Key_Dead_Longsolidusoverlay);
}

NodeId refreshNodeFor(DocumentSession* session, NodeId nodeId) {
  if (!session || !nodeId.isValid()) {
    return nodeId;
  }
  MarkdownNode* node = session->document().node(nodeId);
  if (!node) {
    return nodeId;
  }
  while (node->parent() && node->parent()->type() != BlockType::Document) {
    node = node->parent();
  }
  return node->id();
}

// Blocks that edit through a dedicated literal controller rather than inline text.
bool isLiteralBlockType(BlockType type) {
  return type == BlockType::CodeFence || type == BlockType::MathBlock ||
         type == BlockType::HtmlBlock || type == BlockType::FrontMatter;
}

}  // namespace

InputController::InputController(QObject* parent) : QObject(parent) {}

void InputController::setContext(const EditorContext& ctx) {
  EditorView* const oldView = ctx_.view;
  ctx_ = ctx;
  if (oldView != ctx_.view) {
    if (oldView) {
      oldView->removeEventFilter(this);
      oldView->viewport()->removeEventFilter(this);
    }
    if (ctx_.view) {
      ctx_.view->installEventFilter(this);
      ctx_.view->viewport()->installEventFilter(this);
    }
  }
}

void InputController::setTableController(TableController* tableController) {
  tableController_ = tableController;
}

void InputController::setCodeFenceController(CodeFenceController* codeFenceController) {
  codeFenceController_ = codeFenceController;
}

bool InputController::eventFilter(QObject* watched, QEvent* event) {
  if (watched == ctx_.view || (ctx_.view && watched == ctx_.view->viewport())) {
    if (event->type() == QEvent::ShortcutOverride) {
      auto* keyEvent = static_cast<QKeyEvent*>(event);
      if (keyEvent->key() == Qt::Key_A && keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
        keyEvent->accept();
        return true;
      }
      if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
        keyEvent->accept();
      }
      return false;
    }
    if (event->type() == QEvent::KeyPress) {
      return handleKeyPress(static_cast<QKeyEvent*>(event));
    }
  }
  return QObject::eventFilter(watched, event);
}

bool InputController::insertText(QString text) {
  if (ctx_.hasSession() && ctx_.session->markdownText().isEmpty()) {
    return insertIntoEmptyDocument(std::move(text));
  }
  reconcileLiteralEditorForCursor();
  if (hasActiveLiteralEditor()) {
    return insertTextIntoActiveLiteral(std::move(text));
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return replaceSelection(std::move(text), EditTransaction::Kind::InsertText, QStringLiteral("Replace Selection"));
  }
  if (tableController_ && tableController_->currentCell().isValid()) {
    return tableController_->insertText(std::move(text));
  }
  if (ctx_.selection && ctx_.selection->currentHit().zone == HitTestResult::Zone::BlockAfter) {
    return insertBlockAfterCurrentBlock(std::move(text));
  }
  if (tryInsertOptionalDefinitionTitle(text)) {
    return true;
  }
  return text.isEmpty() ? false : editParagraph(TextBlockCommandBuilder::Operation::InsertText, std::move(text));
}

bool InputController::insertParagraphBreak() {
  if (hasActiveLiteralEditor()) {
    return insertTextIntoActiveLiteral(QStringLiteral("\n"));
  }
  if (ctx_.selection && ctx_.selection->currentHit().zone == HitTestResult::Zone::BlockAfter) {
    return insertBlockAfterCurrentBlock();
  }
  return editParagraph(TextBlockCommandBuilder::Operation::Enter);
}

bool InputController::insertBlockAfterCurrentBlock(QString text) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }
  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node) {
    return false;
  }
  const SourceRange range = node->sourceRange();
  if (range.byteEnd < range.byteStart || range.byteEnd > ctx_.session->markdownText().size()) {
    return false;
  }

  qsizetype insertOffset = range.byteEnd;
  const QString& markdown = ctx_.session->markdownText();
  if (insertOffset < markdown.size() && markdown.at(insertOffset) == QLatin1Char('\r')) {
    ++insertOffset;
  }
  if (insertOffset < markdown.size() && markdown.at(insertOffset) == QLatin1Char('\n')) {
    ++insertOffset;
  }

  // Literal blocks (front matter / code fence / math / html) already end on a
  // fence boundary, so a single newline starts a fresh block. Content blocks
  // (paragraph, heading, list, ...) need a blank line (\n\n) to form a separate
  // paragraph instead of a soft line break within the current block.
  const bool isLiteralBlock = node->type() == BlockType::FrontMatter || node->type() == BlockType::CodeFence ||
                              node->type() == BlockType::MathBlock || node->type() == BlockType::HtmlBlock;
  const QString insertedText = text.isEmpty() ? QStringLiteral("\n\n")
      : isLiteralBlock                          ? QStringLiteral("\n%1").arg(text)
                                               : QStringLiteral("\n\n%1").arg(text);
  applyLocalEdit(
      EditTransaction::Kind::SplitParagraph,
      text.isEmpty() ? QStringLiteral("Insert Paragraph After") : QStringLiteral("Insert Text After Block"),
      insertOffset,
      0,
      insertedText,
      CursorPosition(),
      insertOffset + insertedText.size(),
      QVector<LocalEditNodeHint>{LocalEditNodeHint{node->id(), range.byteStart, node->type()}},
      true,
      true);
  return true;
}

bool InputController::deleteBackward() {
  reconcileLiteralEditorForCursor();
  if (hasActiveLiteralEditor()) {
    return deleteBackwardInActiveLiteral();
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
  }
  // The caret can sit on a thematic break itself — either landed directly on the rule (arrow-key
  // navigation does not skip non-editable blocks) or in its virtual trailing area (afterBlock).
  // The rule carries no editable text, so editParagraph rejects it and backspace would be a silent
  // no-op (or, for the trailing caret, merely collapse the caret). Eat the divider instead.
  if (tryRemoveThematicBreak(/*forward=*/false)) {
    return true;
  }
  // The virtual trailing paragraph carries no text. A backspace from it pulls the caret up to
  // the end of the last block's content; the next backspace deletes from there.
  // Without this, backspace on the trailing area is a silent no-op — and for a document whose
  // last block is a list it even routes to "unsupported edit", since the list node is not an
  // editable text block.
  if (ctx_.selection && ctx_.selection->hasCursor() && ctx_.selection->cursorPosition().afterBlock) {
    return collapseTrailingCaretToEndOfLastBlock();
  }
  if (tryRemoveEmptyDefinitionBlock(EditTransaction::Kind::DeleteText, QStringLiteral("Backspace Empty Definition"))) {
    return true;
  }
  if (tableController_ && tableController_->currentCell().isValid()) {
    return tableController_->deleteBackward();
  }
  return editParagraph(TextBlockCommandBuilder::Operation::Backspace);
}

bool InputController::deleteForward() {
  reconcileLiteralEditorForCursor();
  if (hasActiveLiteralEditor()) {
    return deleteForwardInActiveLiteral();
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
  }
  // Symmetric to the backspace case: Delete from a caret resting on a thematic break (directly or
  // in its trailing area) removes the divider.
  if (tryRemoveThematicBreak(/*forward=*/true)) {
    return true;
  }
  if (tryRemoveEmptyDefinitionBlock(EditTransaction::Kind::DeleteText, QStringLiteral("Delete Empty Definition"))) {
    return true;
  }
  if (tableController_ && tableController_->currentCell().isValid()) {
    return tableController_->deleteForward();
  }
  return editParagraph(TextBlockCommandBuilder::Operation::Delete);
}

bool InputController::indentListItem() {
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }

  const TextBlockCommandBuilder builder(ctx_.session, &resolver);
  return applyTextCommand(builder.buildIndentListItem(context));
}

bool InputController::outdentListItem() {
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }
  const TextBlockCommandBuilder builder(ctx_.session, &resolver);
  return applyTextCommand(builder.buildOutdentListItem(context));
}

bool InputController::deleteSelection() {
  if (hasActiveLiteralEditor()) {
    return deleteSelectionInActiveLiteral();
  }
  return replaceSelection(QString(), EditTransaction::Kind::DeleteText, QStringLiteral("Delete Selection"));
}

bool InputController::hasEditableSelection() const {
  qsizetype start = 0;
  qsizetype end = 0;
  return selectionSourceRange(start, end) || blockSelectionSourceRange(start, end);
}

bool InputController::replaceSelection(QString text, EditTransaction::Kind kind, QString label) {
  if (text.isEmpty() && kind == EditTransaction::Kind::DeleteText && tryRemoveExactWholeBlockSelection(kind, label)) {
    return true;
  }

  const qsizetype insertedLength = text.size();
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  qsizetype start = 0;
  qsizetype end = 0;
  if (resolver.selectionContext(context, start, end)) {
    const qsizetype sourceStart = context.contentRange.byteStart + start;
    QVector<LocalEditNodeHint> nodeHints;
    if (context.node) {
      nodeHints.push_back(LocalEditNodeHint{
          context.node->id(),
          context.blockRange.byteStart >= 0 ? context.blockRange.byteStart : context.contentRange.byteStart,
          context.node->type()});
    }
    applyLocalEdit(
        kind,
        label,
        sourceStart,
        end - start,
        std::move(text),
        CursorPosition(),
        sourceStart + insertedLength,
        std::move(nodeHints));
    return true;
  }

  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  if (!selectionSourceRange(sourceStart, sourceEnd)) {
    emit unsupportedEditRequested(QStringLiteral("Only editable text selection is supported in this M4 slice."));
    return false;
  }

  QVector<LocalEditNodeHint> nodeHints;
  const SelectionRange range = ctx_.selection->selection();
  if (range.anchor.blockId.isValid()) {
    nodeHints.push_back(LocalEditNodeHint{range.anchor.blockId, sourceStart, BlockType::Unknown});
  }
  if (range.focus.blockId.isValid() && range.focus.blockId != range.anchor.blockId) {
    nodeHints.push_back(LocalEditNodeHint{range.focus.blockId, sourceEnd, BlockType::Unknown});
  }
  applyLocalEdit(
      kind,
      label,
      sourceStart,
      sourceEnd - sourceStart,
      std::move(text),
      CursorPosition(),
      sourceStart + insertedLength,
      std::move(nodeHints));
  return true;
}

bool InputController::tryRemoveExactWholeBlockSelection(EditTransaction::Kind kind, const QString& label) {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionRange range = ctx_.selection->selection();
  if (range.anchor.blockId != range.focus.blockId) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(range.anchor.blockId);
  if (!node || !node->parent() || node->parent()->type() != BlockType::Document) {
    return false;
  }

  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*node, blockStart, blockEnd)) {
    return false;
  }

  qsizetype selectionStart = -1;
  qsizetype selectionEnd = -1;
  if (!blockSelectionSourceRange(selectionStart, selectionEnd) || selectionStart != blockStart || selectionEnd != blockEnd) {
    return false;
  }

  const auto& blocks = ctx_.session->document().root().children();
  int nodeIndex = -1;
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    if (blocks.at(static_cast<size_t>(i)).get() == node) {
      nodeIndex = i;
      break;
    }
  }
  if (nodeIndex < 0) {
    return false;
  }

  qsizetype deleteStart = blockStart;
  qsizetype deleteEnd = blockEnd;
  if (blocks.size() == 1) {
    deleteStart = 0;
    deleteEnd = ctx_.session->markdownText().size();
  } else if (nodeIndex + 1 < static_cast<int>(blocks.size())) {
    deleteEnd = blocks.at(static_cast<size_t>(nodeIndex + 1))->sourceRange().byteStart;
  } else {
    deleteStart = blocks.at(static_cast<size_t>(nodeIndex - 1))->sourceRange().byteEnd;
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, ctx_.session->markdownText().size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, ctx_.session->markdownText().size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  const CursorPosition beforeCursor = ctx_.selection->cursorPosition();
  const QString removedText = ctx_.session->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node->clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node->id();
  const BlockType removedNodeType = node->type();

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{removedNodeId, blockStart, removedNodeType});
  if (!ctx_.session->applyTextDelta(deleteStart, deleteEnd - deleteStart, QString(), true, std::move(nodeHints))) {
    return false;
  }

  CursorPosition nextCursor = cursorAfterEdit(CursorPosition(), deleteStart, true);
  if (nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  } else {
    ctx_.selection->clear();
  }

  if (ctx_.undoStack) {
    QVector<NodeId> affectedNodes{removedNodeId};
    if (nextCursor.isValid() && !affectedNodes.contains(nextCursor.blockId)) {
      affectedNodes.push_back(nextCursor.blockId);
    }
    ctx_.undoStack->push(EditTransaction(
        kind,
        label,
        RemoveNodeCommand{
            removedNodeId,
            removedNodeType,
            nodeIndex,
            TextDelta{deleteStart, removedText, QString()},
            blockStart,
            std::move(removedNode),
            beforeCursor,
            nextCursor,
            std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    if (nextCursor.isValid()) {
      ctx_.brushQueue->requestBlockRefresh(nextCursor.blockId);
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
  }
  return true;
}

bool InputController::handleInputMethod(QInputMethodEvent* event) {
  if (!event || event->commitString().isEmpty()) {
    return false;
  }
  return insertText(event->commitString());
}

bool InputController::hasActiveLiteralEditor() const {
  for (auto it = ctx_.literalEditors.constBegin(); it != ctx_.literalEditors.constEnd(); ++it) {
    if (it.value() && it.value()->isEditing()) {
      return true;
    }
  }
  return codeFenceController_ && codeFenceController_->isEditing();
}

LiteralBlockController* InputController::activeLiteralEditor() const {
  for (auto it = ctx_.literalEditors.constBegin(); it != ctx_.literalEditors.constEnd(); ++it) {
    if (it.value() && it.value()->isEditing()) {
      return it.value();
    }
  }
  return nullptr;
}

void InputController::syncLiteralEditMode(NodeId newBlockId) {
  if (!ctx_.hasSession() || !ctx_.hasSelection()) {
    return;
  }
  MarkdownNode* node = ctx_.session->document().node(newBlockId);
  if (!node) {
    return;
  }

  const BlockType type = node->type();
  const bool isLiteral = isLiteralBlockType(type);

  const auto exitAllLiteralEditors = [this]() {
    for (auto it = ctx_.literalEditors.constBegin(); it != ctx_.literalEditors.constEnd(); ++it) {
      if (it.value()) {
        it.value()->exitEditMode();
      }
    }
    if (codeFenceController_) {
      codeFenceController_->exitEditMode();
    }
  };

  if (!isLiteral) {
    if (hasActiveLiteralEditor()) {
      exitAllLiteralEditors();
    }
    return;
  }

  bool alreadyEditingTarget = false;
  if (type == BlockType::CodeFence) {
    alreadyEditingTarget = codeFenceController_ && codeFenceController_->isEditing() && codeFenceController_->currentCodeFenceId() == newBlockId;
  } else if (LiteralBlockController* ctrl = ctx_.literalEditors.value(static_cast<int>(type))) {
    alreadyEditingTarget = ctrl->isEditing() && ctrl->currentBlockId() == newBlockId;
  }
  if (alreadyEditingTarget) {
    return;
  }

  const CursorPosition savedCursor = ctx_.selection->cursorPosition();
  exitAllLiteralEditors();

  bool entered = false;
  if (type == BlockType::CodeFence) {
    entered = codeFenceController_ && codeFenceController_->enterEditMode();
  } else if (LiteralBlockController* ctrl = ctx_.literalEditors.value(static_cast<int>(type))) {
    entered = ctrl->enterEditMode();
  }
  if (entered) {
    ctx_.selection->setCursorPosition(savedCursor);
  }
}

void InputController::reconcileLiteralEditorForCursor() {
  // Drop a literal editor that no longer matches the caret. Structure edits and undo can move the
  // caret off the block being edited (e.g. committing "```" then undoing restores the paragraph)
  // without going through syncLiteralEditMode, leaving a stale editor active. Without this, the
  // next keystroke would route into a now-absent block and silently no-op. Entering a new editor
  // is intentionally NOT done here — that is the caller's responsibility (syncLiteralEditMode).
  if (!hasActiveLiteralEditor() || !ctx_.hasSession() || !ctx_.hasCursor()) {
    return;
  }
  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node || !isLiteralBlockType(node->type())) {
    exitActiveLiteralEditor();
  }
}

bool InputController::insertTextIntoActiveLiteral(QString text) {
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->insertText(std::move(text));
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->insertText(std::move(text));
  }
  return false;
}

bool InputController::tryInsertOptionalDefinitionTitle(QString text) {
  if (text.isEmpty() || !ctx_.hasSession() || !ctx_.hasCursor() || !ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const CursorPosition cursor = ctx_.selection->cursorPosition();
  if (ctx_.selection->currentHit().definitionField != HitTestResult::DefinitionField::Title) {
    return false;
  }
  MarkdownNode* node = ctx_.session->document().node(cursor.blockId);
  if (!node || node->type() != BlockType::LinkDefinition) {
    return false;
  }

  const DefinitionBlock definition = node->definition();
  if (!definition.title.isEmpty() || !definition.titleRange.isValid() ||
      cursor.text.sourceOffset != definition.titleRange.start) {
    return false;
  }

  const QString inserted = definition.titleQuoted ? text : QStringLiteral("  \"%1\"").arg(text);
  const qsizetype titleSourceStart = definition.titleQuoted ? definition.titleRange.start : definition.titleRange.start + 3;
  CursorPosition preferredCursor;
  preferredCursor.blockId = node->id();
  preferredCursor.text.nodeId = node->id();
  preferredCursor.text.sourceOffset = titleSourceStart + text.size();
  preferredCursor.text.textOffset = preferredCursor.text.sourceOffset - definition.markerRange.start;
  applyLocalEdit(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Link Definition Title"),
      definition.titleRange.start,
      0,
      inserted,
      preferredCursor,
      preferredCursor.text.sourceOffset,
      QVector<LocalEditNodeHint>{LocalEditNodeHint{node->id(), node->sourceRange().byteStart, node->type()}});
  return true;
}

bool InputController::tryRemoveEmptyLiteralBlock(EditTransaction::Kind kind, const QString& label) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  // Only applicable to code fence and math block
  NodeId blockId;
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    blockId = codeFenceController_->currentCodeFenceId();
  } else if (LiteralBlockController* math = ctx_.literalEditors.value(static_cast<int>(BlockType::MathBlock))) {
    if (math->isEditing()) {
      blockId = math->currentBlockId();
    }
  }
  if (!blockId.isValid()) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node || !node->literal().isEmpty()) {
    return false;
  }
  if (!node->parent() || node->parent()->type() != BlockType::Document) {
    return false;
  }
  const BlockType nodeType = node->type();
  if (nodeType != BlockType::CodeFence && nodeType != BlockType::MathBlock) {
    return false;
  }

  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*node, blockStart, blockEnd)) {
    return false;
  }

  const auto& blocks = ctx_.session->document().root().children();
  int nodeIndex = -1;
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    if (blocks.at(static_cast<size_t>(i)).get() == node) {
      nodeIndex = i;
      break;
    }
  }
  if (nodeIndex < 0) {
    return false;
  }

  qsizetype deleteStart = blockStart;
  qsizetype deleteEnd = blockEnd;
  if (blocks.size() == 1) {
    deleteStart = 0;
    deleteEnd = ctx_.session->markdownText().size();
  } else if (nodeIndex + 1 < static_cast<int>(blocks.size())) {
    deleteEnd = blocks.at(static_cast<size_t>(nodeIndex + 1))->sourceRange().byteStart;
  } else {
    deleteStart = blocks.at(static_cast<size_t>(nodeIndex - 1))->sourceRange().byteEnd;
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, ctx_.session->markdownText().size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, ctx_.session->markdownText().size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  const CursorPosition beforeCursor = ctx_.selection->cursorPosition();
  const QString removedText = ctx_.session->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node->clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node->id();

  // Exit edit mode before removing the block to clear stale editing state
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    codeFenceController_->exitEditMode();
  } else if (LiteralBlockController* math = ctx_.literalEditors.value(static_cast<int>(BlockType::MathBlock))) {
    if (math->isEditing()) {
      math->exitEditMode();
    }
  }

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{removedNodeId, blockStart, nodeType});
  if (!ctx_.session->applyTextDelta(deleteStart, deleteEnd - deleteStart, QString(), true, std::move(nodeHints))) {
    return false;
  }

  CursorPosition nextCursor = cursorAfterEdit(CursorPosition(), deleteStart, true);
  if (nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  } else {
    ctx_.selection->clear();
  }

  if (ctx_.undoStack) {
    QVector<NodeId> affectedNodes{removedNodeId};
    if (nextCursor.isValid() && !affectedNodes.contains(nextCursor.blockId)) {
      affectedNodes.push_back(nextCursor.blockId);
    }
    ctx_.undoStack->push(EditTransaction(
        kind,
        label,
        RemoveNodeCommand{
            removedNodeId,
            nodeType,
            nodeIndex,
            TextDelta{deleteStart, removedText, QString()},
            blockStart,
            std::move(removedNode),
            beforeCursor,
            nextCursor,
            std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    if (nextCursor.isValid()) {
      ctx_.brushQueue->requestBlockRefresh(nextCursor.blockId);
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
  }
  return true;
}

bool InputController::tryRemoveEmptyDefinitionBlock(EditTransaction::Kind kind, const QString& label) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node || (node->type() != BlockType::LinkDefinition && node->type() != BlockType::FootnoteDefinition)) {
    return false;
  }
  const DefinitionBlock definition = node->definition();
  const bool empty = node->type() == BlockType::LinkDefinition
                         ? definition.label.isEmpty() && definition.destination.isEmpty() && definition.title.isEmpty()
                         : definition.label.isEmpty() && definition.note.isEmpty();
  if (!empty || !node->parent() || node->parent()->type() != BlockType::Document) {
    return false;
  }

  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*node, blockStart, blockEnd)) {
    return false;
  }

  const auto& blocks = ctx_.session->document().root().children();
  int nodeIndex = -1;
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    if (blocks.at(static_cast<size_t>(i)).get() == node) {
      nodeIndex = i;
      break;
    }
  }
  if (nodeIndex < 0) {
    return false;
  }

  qsizetype deleteStart = blockStart;
  qsizetype deleteEnd = blockEnd;
  if (blocks.size() == 1) {
    deleteStart = 0;
    deleteEnd = ctx_.session->markdownText().size();
  } else {
    for (int i = nodeIndex + 1; i < static_cast<int>(blocks.size()); ++i) {
      const qsizetype nextStart = blocks.at(static_cast<size_t>(i))->sourceRange().byteStart;
      if (nextStart > blockStart) {
        deleteEnd = nextStart;
        break;
      }
    }
  }
  if (nodeIndex > 0) {
    qsizetype previousEnd = 0;
    for (int i = nodeIndex - 1; i >= 0; --i) {
      const qsizetype candidateEnd = blocks.at(static_cast<size_t>(i))->sourceRange().byteEnd;
      if (candidateEnd <= blockStart) {
        previousEnd = candidateEnd;
        break;
      }
    }
    deleteStart = blockStart;
    while (deleteStart > previousEnd &&
           (ctx_.session->markdownText().at(deleteStart - 1) == QLatin1Char('\n') ||
            ctx_.session->markdownText().at(deleteStart - 1) == QLatin1Char('\r'))) {
      --deleteStart;
    }
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, ctx_.session->markdownText().size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, ctx_.session->markdownText().size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  const CursorPosition beforeCursor = ctx_.selection->cursorPosition();
  const QString removedText = ctx_.session->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node->clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node->id();
  const BlockType removedNodeType = node->type();

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{removedNodeId, blockStart, removedNodeType});
  if (!ctx_.session->applyTextDelta(deleteStart, deleteEnd - deleteStart, QString(), true, std::move(nodeHints))) {
    return false;
  }

  CursorPosition nextCursor = cursorAfterEdit(CursorPosition(), deleteStart, true);
  if (nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  } else {
    ctx_.selection->clear();
  }

  if (ctx_.undoStack) {
    QVector<NodeId> affectedNodes{removedNodeId};
    if (nextCursor.isValid() && !affectedNodes.contains(nextCursor.blockId)) {
      affectedNodes.push_back(nextCursor.blockId);
    }
    ctx_.undoStack->push(EditTransaction(
        kind,
        label,
        RemoveNodeCommand{
            removedNodeId,
            removedNodeType,
            nodeIndex,
            TextDelta{deleteStart, removedText, QString()},
            blockStart,
            std::move(removedNode),
            beforeCursor,
            nextCursor,
            std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    if (nextCursor.isValid()) {
      ctx_.brushQueue->requestBlockRefresh(nextCursor.blockId);
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
  }
  return true;
}

bool InputController::tryRemoveThematicBreak(bool forward) {
  // Removes a top-level thematic break the caret currently rests on. The caret reaches a rule in
  // two ways: arrow-key navigation lands directly on it (selectableBlockByDirection does not skip
  // non-editable blocks), and a click in the virtual trailing area below a rule that ends the
  // document puts the caret there with afterBlock set. Either way editParagraph rejects the block
  // (a rule has no editable text), so without this the keystroke is a silent no-op.
  //
  // The removal goes through applyLocalEdit, which falls back to a full reparse when the
  // incremental slice picker cannot anchor the edit — and it cannot here, because
  // chooseTopLevelSlice deliberately skips non-editable top-level blocks (a thematic break has no
  // content to slice). The caret lands on the neighbour the gesture points at: backspace → the
  // previous block's content end, delete → the next block's start.
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
  if (!node || node->type() != BlockType::ThematicBreak) {
    return false;
  }
  if (!node->parent() || node->parent()->type() != BlockType::Document) {
    return false;
  }

  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*node, blockStart, blockEnd)) {
    return false;
  }

  const auto& blocks = ctx_.session->document().root().children();
  int nodeIndex = -1;
  for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
    if (blocks.at(static_cast<size_t>(i)).get() == node) {
      nodeIndex = i;
      break;
    }
  }
  if (nodeIndex < 0) {
    return false;
  }

  // Span the rule plus exactly one adjacent separator so the surviving blocks stay distinct.
  qsizetype deleteStart = blockStart;
  qsizetype deleteEnd = blockEnd;
  if (blocks.size() == 1) {
    deleteStart = 0;
    deleteEnd = ctx_.session->markdownText().size();
  } else if (nodeIndex + 1 < static_cast<int>(blocks.size())) {
    deleteEnd = blocks.at(static_cast<size_t>(nodeIndex + 1))->sourceRange().byteStart;
  } else {
    deleteStart = blocks.at(static_cast<size_t>(nodeIndex - 1))->sourceRange().byteEnd;
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, ctx_.session->markdownText().size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, ctx_.session->markdownText().size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  // Neighbouring editable blocks, for caret placement. applyLocalEdit falls back to a full
  // reparse here (the incremental slice picker cannot anchor a non-editable block's removal), which
  // hands every node a fresh id — so the preferred caret must be expressed as a SOURCE offset that
  // still resolves on the post-edit document, not as a node id. The preceding block's content end
  // sits before the edit point and is stable; the following block, after removing
  // [deleteStart, deleteEnd), now begins exactly at deleteStart.
  MarkdownNode* beforeEditable =
      nodeIndex > 0 ? resolver.lastEditableDescendant(*blocks.at(static_cast<size_t>(nodeIndex - 1))) : nullptr;
  MarkdownNode* afterEditable = nodeIndex + 1 < static_cast<int>(blocks.size())
                                    ? resolver.firstEditableDescendant(*blocks.at(static_cast<size_t>(nodeIndex + 1)))
                                    : nullptr;
  BlockEditContext beforeCtx;
  const bool haveBefore = beforeEditable && resolver.fill(*beforeEditable, beforeCtx);
  const qsizetype beforeEnd = haveBefore ? beforeCtx.contentRange.byteEnd : qsizetype(-1);
  CursorPosition preferred;
  auto makePreferred = [&](MarkdownNode* target, qsizetype sourceOffset) {
    preferred.blockId = target->id();
    preferred.text.sourceOffset = sourceOffset;
  };
  if (forward) {
    if (afterEditable) {
      makePreferred(afterEditable, deleteStart);
    } else if (haveBefore) {
      makePreferred(beforeEditable, beforeEnd);
    }
  } else {
    if (haveBefore) {
      makePreferred(beforeEditable, beforeEnd);
    } else if (afterEditable) {
      makePreferred(afterEditable, deleteStart);
    }
  }

  applyLocalEdit(
      EditTransaction::Kind::DeleteText,
      forward ? QStringLiteral("Delete Thematic Break") : QStringLiteral("Remove Thematic Break"),
      deleteStart,
      deleteEnd - deleteStart,
      QString(),
      preferred,
      deleteStart,
      {LocalEditNodeHint{node->id(), blockStart, BlockType::ThematicBreak}},
      false,
      true);
  return true;
}

bool InputController::collapseTrailingCaretToEndOfLastBlock() {
  if (!ctx_.hasSession() || !ctx_.selection) {
    return false;
  }
  // Resolving the end-of-document source offset lands the caret on the deepest editable
  // block — a list item's content end, a paragraph's end, or a literal block's content end —
  // and clears the afterBlock flag so the caret paints inside real content.
  CursorPosition target = cursorForSourceOffset(ctx_.session->markdownText().size());
  if (!target.isValid()) {
    // No editable content reaches the document end — e.g. a table whose last cell ends before
    // the trailing newline, or "alpha\n\n---" where the last block is a non-editable break.
    // Retreat to the last editable block's content end instead.
    BlockEditContextResolver resolver = contextResolver();
    if (MarkdownNode* last = resolver.lastEditableDescendant(ctx_.session->document().root())) {
      BlockEditContext context;
      if (resolver.fill(*last, context)) {
        target = cursorForSourceOffset(context.contentRange.byteEnd);
      }
    }
  }
  if (!target.isValid()) {
    return false;  // no editable content anywhere (e.g. a lone thematic break)
  }
  ctx_.selection->setCursorPosition(target);
  return true;
}

bool InputController::deleteBackwardInActiveLiteral() {
  if (tryRemoveEmptyLiteralBlock(EditTransaction::Kind::DeleteText,
                                  QStringLiteral("Backspace Empty Block"))) {
    return true;
  }
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->deleteBackward();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteBackward();
  }
  return false;
}

bool InputController::deleteForwardInActiveLiteral() {
  if (tryRemoveEmptyLiteralBlock(EditTransaction::Kind::DeleteText,
                                  QStringLiteral("Delete Empty Block"))) {
    return true;
  }
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->deleteForward();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteForward();
  }
  return false;
}

bool InputController::deleteSelectionInActiveLiteral() {
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->deleteSelection();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteSelection();
  }
  return false;
}

bool InputController::exitActiveLiteralEditor() {
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->exitEditMode();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->exitEditMode();
  }
  return false;
}

QString InputController::activeLiteralTabText() const {
  if (LiteralBlockController* active = activeLiteralEditor()) {
    return active->tabText();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->tabText();
  }
  return QString();
}

bool InputController::handleKeyPress(QKeyEvent* event) {
  PerfTimer perf("input.keypress");
  if (event && event->key() == Qt::Key_A && event->modifiers().testFlag(Qt::ControlModifier) &&
      !event->modifiers().testFlag(Qt::AltModifier)) {
    emit selectAllRequested();
    return true;
  }

  if (!ctx_.hasSession() || !ctx_.hasSelection() || !ctx_.view || event->modifiers().testFlag(Qt::ControlModifier) ||
      event->modifiers().testFlag(Qt::AltModifier)) {
    return false;
  }

  if (!ctx_.selection->hasCursor()) {
    if (!ctx_.session->markdownText().isEmpty()) {
      return false;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      return insertIntoEmptyDocument(QStringLiteral("\n"));
    }
    return insertIntoEmptyDocument(printableText(event));
  }

  // Phantom trailing line in an indented code block. cmark cannot persist a trailing empty line,
  // so Enter at the end of an indented block is held only in the node's literal as a trailing '\n'
  // (created by LiteralBlockController::insertText). While that phantom is present this block owns
  // the per-key semantics; everything else falls through to normal dispatch:
  //   - printable / Tab: commit — the text lands on the phantom line (becomes real code)
  //   - Backspace:        undo the phantom without touching the source
  //   - Enter:            a second Enter on the empty line leaves the block to a new paragraph
  //   - modifier-only:    leave the phantom intact
  //   - any other key:    discard the phantom first, then proceed normally
  if (codeFenceController_ && codeFenceController_->hasPendingTrailingNewline()) {
    const int key = event->key();
    const auto isModifierKey = [](int k) {
      return k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Alt || k == Qt::Key_Meta ||
             k == Qt::Key_AltGr || k == Qt::Key_CapsLock || k == Qt::Key_NumLock || k == Qt::Key_ScrollLock;
    };
    switch (key) {
      case Qt::Key_Backspace:
        codeFenceController_->clearPendingTrailingNewline();
        return true;
      case Qt::Key_Return:
      case Qt::Key_Enter:
        // Indented code cannot hold a second trailing empty line, and Enter-on-empty is the
        // standard "leave the block" gesture. exitEditMode() clears the phantom as its choke point,
        // so the cursor stays usable for the paragraph insert below.
        codeFenceController_->exitEditMode();
        return insertBlockAfterCurrentBlock();
      case Qt::Key_Tab:
      case Qt::Key_Backtab:
        break;  // commit: the Tab text lands on the phantom line via the switch below
      default:
        if (!printableText(event).isEmpty()) {
          break;  // commit: the character lands on the phantom line via insertText below
        }
        if (isModifierKey(key)) {
          break;  // modifier-only press (e.g. Shift): leave the phantom intact
        }
        codeFenceController_->clearPendingTrailingNewline();
        break;
    }
  }

  switch (event->key()) {
    case Qt::Key_Tab:
      if (hasActiveLiteralEditor()) {
        return insertTextIntoActiveLiteral(activeLiteralTabText());
      }
      if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        return outdentListItem();
      }
      if (shouldIndentListItemFromKeyboard()) {
        return indentListItem();
      }
      return insertText(QStringLiteral("\u200b"));
    case Qt::Key_Backtab:
      if (hasActiveLiteralEditor()) {
        return insertTextIntoActiveLiteral(activeLiteralTabText());
      }
      return outdentListItem();
    case Qt::Key_Backspace:
      return deleteBackward();
    case Qt::Key_Delete:
      return deleteForward();
    case Qt::Key_Left:
      return moveCursorHorizontal(-1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Right:
      return moveCursorHorizontal(1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Up:
      return moveCursorVertical(-1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Down:
      return moveCursorVertical(1, event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Home:
      return moveJump(event->modifiers().testFlag(Qt::ControlModifier) ? JumpTarget::DocumentStart : JumpTarget::BlockStart,
                      event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_End:
      return moveJump(event->modifiers().testFlag(Qt::ControlModifier) ? JumpTarget::DocumentEnd : JumpTarget::BlockEnd,
                      event->modifiers().testFlag(Qt::ShiftModifier));
    case Qt::Key_Escape:
      return exitActiveLiteralEditor();
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return insertParagraphBreak();
    default: {
      const QString text = printableText(event);
      return insertText(text);
    }
  }
}

bool InputController::insertIntoEmptyDocument(QString text) {
  if (!ctx_.hasSession() || !ctx_.session->markdownText().isEmpty() || text.isEmpty()) {
    return false;
  }
  const qsizetype insertedLength = text.size();
  applyLocalEdit(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Text"),
      0,
      0,
      std::move(text),
      CursorPosition(),
      insertedLength,
      {},
      false,
      true);
  return true;
}

bool InputController::shouldIndentListItemFromKeyboard() const {
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node || context.node->type() != BlockType::ListItem || !ctx_.selection || !ctx_.selection->hasCursor()) {
    return false;
  }

  const MarkdownNode* previous = context.node->previousSibling();
  if (!previous || previous->type() != BlockType::ListItem) {
    return false;
  }

  const CursorPosition cursor = ctx_.selection->cursorPosition();
  if (cursor.text.inMeta) {
    return true;
  }

  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!resolver.listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return false;
  }

  const qsizetype sourceOffset = cursor.text.sourceOffset >= 0 ? cursor.text.sourceOffset : context.cursorSourceOffset;
  if (sourceOffset >= 0) {
    return sourceOffset <= contentStart + 1;
  }
  return context.cursorTextOffset <= 1;
}

CursorPosition InputController::cursorFor(NodeId blockId, qsizetype offset) const {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = offset;
  return cursor;
}

CursorPosition InputController::cursorForNode(MarkdownNode& node, qsizetype offset) const {
  CursorPosition cursor;
  cursor.blockId = node.id();
  cursor.text.nodeId = node.id();
  cursor.text.textOffset = qBound<qsizetype>(0, offset, selectableTextLength(node));
  if (node.type() == BlockType::Table) {
    for (const auto& row : node.children()) {
      for (const auto& cell : row->children()) {
        cursor.text.nodeId = cell->id();
        return cursor;
      }
    }
  }
  return cursor;
}

CursorPosition InputController::cursorForSourceOffset(qsizetype sourceOffset, bool preferLaterEmptyAtOffset) const {
  CursorPosition cursor;
  if (!ctx_.hasSession()) {
    return cursor;
  }

  BlockEditContextResolver resolver = contextResolver();

  // A source offset that lands inside a literal block (code/math/HTML/front matter) resolves to
  // that block. nodeAtContentSourceOffset only matches inline-editable text, so without this a
  // freshly committed "```"/"$$" block would leave the post-edit caret unresolvable — and, since
  // the fallback (end-of-document) is also unresolvable, the selection cursor would be cleared,
  // making the caret vanish entirely.
  qsizetype literalContentStart = -1;
  if (MarkdownNode* literal =
          resolver.literalBlockAtSourceOffset(ctx_.session->document().root(), sourceOffset, literalContentStart)) {
    cursor.blockId = literal->id();
    cursor.text.nodeId = literal->id();
    cursor.text.textOffset = qBound<qsizetype>(0, sourceOffset - literalContentStart, literal->literal().size());
    cursor.text.sourceOffset = sourceOffset;
    return cursor;
  }

  MarkdownNode* node = paragraphAtSourceOffset(ctx_.session->document().root(), sourceOffset, preferLaterEmptyAtOffset);
  if (!node) {
    return cursor;
  }

  BlockEditContext context;
  if (!resolver.fill(*node, context)) {
    return cursor;
  }
  const qsizetype localSourceOffset = qBound<qsizetype>(0, sourceOffset - context.contentRange.byteStart, context.contentText.size());
  qsizetype visibleOffset = -1;
  if (context.inlineProjection.visibleOffsetForSourceOffset(localSourceOffset, visibleOffset)) {
    CursorPosition cursorForVisible = cursorFor(node->id(), visibleOffset);
    cursorForVisible.text.sourceOffset = sourceOffset;
    return cursorForVisible;
  }
  CursorPosition fallbackCursor = cursorFor(node->id(), qBound<qsizetype>(0, localSourceOffset, context.visibleText.size()));
  fallbackCursor.text.sourceOffset = sourceOffset;
  return fallbackCursor;
}

CursorPosition InputController::cursorAfterEdit(CursorPosition preferredCursor, qsizetype fallbackSourceOffset, bool preferLaterEmptyAtOffset) const {
  if (ctx_.hasSession() && preferredCursor.isValid()) {
    if (preferredCursor.text.sourceOffset >= 0) {
      CursorPosition sourceCursor = cursorForSourceOffset(preferredCursor.text.sourceOffset, preferLaterEmptyAtOffset);
      if (sourceCursor.isValid()) {
        return sourceCursor;
      }
    }
    if (MarkdownNode* node = ctx_.session->document().node(preferredCursor.blockId)) {
      return cursorFor(node->id(), preferredCursor.text.textOffset);
    }
  }
  return cursorForSourceOffset(fallbackSourceOffset, preferLaterEmptyAtOffset);
}

MarkdownNode* InputController::paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset, bool preferLaterEmptyAtOffset) const {
  return contextResolver().nodeAtContentSourceOffset(node, sourceOffset, preferLaterEmptyAtOffset);
}

MarkdownNode* InputController::selectableBlockByDirection(NodeId current, int direction) const {
  if (!ctx_.hasSession()) {
    return nullptr;
  }
  const auto& blocks = ctx_.session->document().root().children();
  for (qsizetype i = 0; i < blocks.size(); ++i) {
    if (blocks.at(i)->id() != current) {
      continue;
    }
    const qsizetype next = i + direction;
    if (next >= 0 && next < blocks.size()) {
      return blocks.at(next).get();
    }
    return nullptr;
  }
  return nullptr;
}

qsizetype InputController::selectableTextLength(const MarkdownNode& node) const {
  switch (node.type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
      return InlineProjection::plainTextForInlines(node.inlines()).size();
    case BlockType::ListItem:
      return plainTextForNode(node).size();
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::MathBlock:
    case BlockType::HtmlBlock:
      return node.literal().size();
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition: {
      const DefinitionBlock definition = node.definition();
      if (!definition.markerRange.isValid()) {
        return 0;
      }
      const qsizetype end = definition.sourceRange.isValid()
                                 ? definition.sourceRange.end
                                 : qMax(definition.markerRange.end,
                                        qMax(definition.destinationRange.end,
                                             qMax(definition.titleRange.end, definition.noteRange.end)));
      return qMax<qsizetype>(0, end - definition.markerRange.start);
    }
    case BlockType::Table:
      return 1;
    default:
      return 0;
  }
}

bool InputController::moveCursorHorizontal(int direction, bool extendSelection) {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
  }

  CursorPosition current = ctx_.selection->cursorPosition();
  MarkdownNode* node = ctx_.session->document().node(current.blockId);
  if (!node) {
    return false;
  }

  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (resolver.fill(*node, context)) {
    const qsizetype currentSourceOffset =
        current.text.sourceOffset >= context.contentRange.byteStart && current.text.sourceOffset <= context.contentRange.byteEnd
            ? current.text.sourceOffset
            : context.contentRange.byteStart + qBound<qsizetype>(0, current.text.textOffset, context.contentText.size());
    qsizetype nextSourceOffset = currentSourceOffset + direction;
    if (nextSourceOffset < context.contentRange.byteStart) {
      if (MarkdownNode* previous = selectableBlockByDirection(current.blockId, -1)) {
        setCursorOrExtend(cursorForNode(*previous, selectableTextLength(*previous)), extendSelection);
        return true;
      }
      nextSourceOffset = context.contentRange.byteStart;
    } else if (nextSourceOffset > context.contentRange.byteEnd) {
      if (MarkdownNode* next = selectableBlockByDirection(current.blockId, 1)) {
        setCursorOrExtend(cursorForNode(*next, 0), extendSelection);
        return true;
      }
      nextSourceOffset = context.contentRange.byteEnd;
    }
    setCursorOrExtend(cursorForSourceOffset(nextSourceOffset), extendSelection);
    return true;
  }

  const qsizetype length = selectableTextLength(*node);
  qsizetype nextOffset = current.text.textOffset + direction;
  if (nextOffset < 0) {
    if (MarkdownNode* previous = selectableBlockByDirection(current.blockId, -1)) {
      setCursorOrExtend(cursorForNode(*previous, selectableTextLength(*previous)), extendSelection);
      return true;
    }
    nextOffset = 0;
  } else if (nextOffset > length) {
    if (MarkdownNode* next = selectableBlockByDirection(current.blockId, 1)) {
      setCursorOrExtend(cursorForNode(*next, 0), extendSelection);
      return true;
    }
    nextOffset = length;
  }

  setCursorOrExtend(cursorForNode(*node, nextOffset), extendSelection);
  return true;
}

bool InputController::moveCursorVertical(int direction, bool extendSelection) {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || direction == 0) {
    return false;
  }
  MarkdownNode* target = selectableBlockByDirection(ctx_.selection->cursorPosition().blockId, direction);
  if (!target) {
    return false;
  }
  const qsizetype offset = direction > 0 ? 0 : selectableTextLength(*target);
  setCursorOrExtend(cursorForNode(*target, offset), extendSelection);
  return true;
}

bool InputController::moveJump(JumpTarget target, bool extendSelection) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }
  switch (target) {
    case JumpTarget::BlockStart:
    case JumpTarget::BlockEnd: {
      MarkdownNode* node = ctx_.session->document().node(ctx_.selection->cursorPosition().blockId);
      if (!node) {
        return false;
      }
      const qsizetype offset = target == JumpTarget::BlockStart ? 0 : selectableTextLength(*node);
      setCursorOrExtend(cursorForNode(*node, offset), extendSelection);
      return true;
    }
    case JumpTarget::DocumentStart:
    case JumpTarget::DocumentEnd: {
      MarkdownNode& root = ctx_.session->document().root();
      MarkdownNode* edge = nullptr;
      if (target == JumpTarget::DocumentStart) {
        for (const auto& child : root.children()) {
          if (child->type() != BlockType::Unknown) {
            edge = child.get();
            break;
          }
        }
      } else {
        for (auto it = root.children().rbegin(); it != root.children().rend(); ++it) {
          if ((*it)->type() != BlockType::Unknown) {
            edge = it->get();
            break;
          }
        }
      }
      if (!edge) {
        return false;
      }
      const qsizetype offset = target == JumpTarget::DocumentStart ? 0 : selectableTextLength(*edge);
      setCursorOrExtend(cursorForNode(*edge, offset), extendSelection);
      return true;
    }
  }
  return false;
}

bool InputController::selectNextOccurrence() {
  if (!ctx_.hasSession()) {
    return false;
  }
  qsizetype start = -1;
  qsizetype end = -1;
  if (!selectionSourceRange(start, end) || end <= start) {
    return false;  // nothing selected — caller expands to the current word first
  }
  const QString& markdown = ctx_.session->markdownText();
  const QString needle = markdown.mid(start, end - start);
  if (needle.isEmpty()) {
    return false;
  }
  qsizetype idx = markdown.indexOf(needle, end);
  if (idx < 0) {
    idx = markdown.indexOf(needle, 0);  // wrap around
  }
  if (idx < 0) {
    return false;
  }
  CursorPosition anchor = cursorForSourceOffset(idx);
  CursorPosition focus = cursorForSourceOffset(idx + needle.size());
  if (!anchor.isValid() || !focus.isValid()) {
    return false;
  }
  SelectionRange range;
  range.anchor = anchor;
  range.focus = focus;
  ctx_.selection->setSelection(range);
  return true;
}

void InputController::setCursorOrExtend(CursorPosition cursor, bool extendSelection) {
  if (!ctx_.selection || !cursor.isValid()) {
    return;
  }
  if (!extendSelection) {
    ctx_.selection->setCursorPosition(cursor);
    syncLiteralEditMode(cursor.blockId);
    return;
  }
  SelectionRange range = ctx_.selection->selection();
  if (!range.anchor.isValid()) {
    range.anchor = ctx_.selection->cursorPosition();
  }
  range.focus = cursor;
  ctx_.selection->setSelection(range);
}

QString InputController::printableText(QKeyEvent* event) const {
  if (isDeadKey(event->key())) {
    return {};
  }
  const QString text = event->text();
  if (text.isEmpty()) {
    return {};
  }
  const QChar ch = text.at(0);
  if (ch.isNull() || ch.category() == QChar::Other_Control) {
    return {};
  }
  return text;
}

}  // namespace muffin
