#include "editor/InputController.h"

#include "app/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/InlineProjection.h"
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
  if (ctx_.session && ctx_.session->markdownText().isEmpty()) {
    return insertIntoEmptyDocument(std::move(text));
  }
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
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor()) {
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

  const QString insertedText = text.isEmpty() ? QStringLiteral("\n\n") : QStringLiteral("\n%1").arg(text);
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
  if (hasActiveLiteralEditor()) {
    return deleteBackwardInActiveLiteral();
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
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
  if (hasActiveLiteralEditor()) {
    return deleteForwardInActiveLiteral();
  }
  if (ctx_.selection && ctx_.selection->hasCursor() && !ctx_.selection->selection().isCollapsed()) {
    return deleteSelection();
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
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor() || ctx_.selection->selection().isCollapsed()) {
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
  if (!ctx_.session || !ctx_.selection) {
    return;
  }
  MarkdownNode* node = ctx_.session->document().node(newBlockId);
  if (!node) {
    return;
  }

  const BlockType type = node->type();
  const bool isLiteral = type == BlockType::CodeFence || type == BlockType::MathBlock || type == BlockType::HtmlBlock || type == BlockType::FrontMatter;

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
  if (text.isEmpty() || !ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor() || !ctx_.selection->selection().isCollapsed()) {
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
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor()) {
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
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor()) {
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

  if (!ctx_.session || !ctx_.selection || !ctx_.view || event->modifiers().testFlag(Qt::ControlModifier) ||
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
  if (!ctx_.session || !ctx_.session->markdownText().isEmpty() || text.isEmpty()) {
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
  if (!ctx_.session) {
    return cursor;
  }

  MarkdownNode* node = paragraphAtSourceOffset(ctx_.session->document().root(), sourceOffset, preferLaterEmptyAtOffset);
  if (!node) {
    return cursor;
  }

  BlockEditContextResolver resolver = contextResolver();
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
  if (ctx_.session && preferredCursor.isValid()) {
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
  if (!ctx_.session) {
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
  if (!ctx_.selection || !ctx_.selection->hasCursor() || !ctx_.session || direction == 0) {
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
  if (!ctx_.selection || !ctx_.selection->hasCursor() || !ctx_.session || direction == 0) {
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
