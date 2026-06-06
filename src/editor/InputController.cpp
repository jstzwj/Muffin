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
#include "blocks/frontmatter/FrontMatterController.h"
#include "blocks/html/HtmlBlockController.h"
#include "blocks/math/MathBlockController.h"
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

QString plainTextForInlines(const QVector<InlineNode>& inlines) {
  QString text;
  for (const InlineNode& inlineNode : inlines) {
    switch (inlineNode.type()) {
      case InlineType::Text:
      case InlineType::Code:
      case InlineType::InlineMath:
      case InlineType::HtmlInline:
        text += inlineNode.text();
        break;
      case InlineType::SoftBreak:
        text += QLatin1Char(' ');
        break;
      case InlineType::LineBreak:
        text += QLatin1Char('\n');
        break;
      case InlineType::Image:
        text += inlineNode.alt();
        break;
      default:
        text += plainTextForInlines(inlineNode.children());
        break;
    }
  }
  return text;
}

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
      return plainTextForInlines(node.inlines());
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

void InputController::setDocumentSession(DocumentSession* session) {
  session_ = session;
}

void InputController::setSelectionController(SelectionController* selection) {
  selection_ = selection;
}

void InputController::setUndoStack(UndoStack* undoStack) {
  undoStack_ = undoStack;
}

void InputController::setBrushQueue(BrushQueue* brushQueue) {
  brushQueue_ = brushQueue;
}

void InputController::setTableController(TableController* tableController) {
  tableController_ = tableController;
}

void InputController::setFrontMatterController(FrontMatterController* frontMatterController) {
  frontMatterController_ = frontMatterController;
}

void InputController::setCodeFenceController(CodeFenceController* codeFenceController) {
  codeFenceController_ = codeFenceController;
}

void InputController::setHtmlBlockController(HtmlBlockController* htmlBlockController) {
  htmlBlockController_ = htmlBlockController;
}

void InputController::setMathBlockController(MathBlockController* mathBlockController) {
  mathBlockController_ = mathBlockController;
}

void InputController::attach(EditorView* view) {
  if (view_ == view) {
    return;
  }
  if (view_) {
    view_->removeEventFilter(this);
    view_->viewport()->removeEventFilter(this);
  }

  view_ = view;
  if (view_) {
    view_->installEventFilter(this);
    view_->viewport()->installEventFilter(this);
  }
}

bool InputController::eventFilter(QObject* watched, QEvent* event) {
  if (watched == view_ || (view_ && watched == view_->viewport())) {
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
  if (session_ && session_->markdownText().isEmpty()) {
    return insertIntoEmptyDocument(std::move(text));
  }
  if (hasActiveLiteralEditor()) {
    return insertTextIntoActiveLiteral(std::move(text));
  }
  if (selection_ && selection_->hasCursor() && !selection_->selection().isCollapsed()) {
    return replaceSelection(std::move(text), EditTransaction::Kind::InsertText, QStringLiteral("Replace Selection"));
  }
  if (tableController_ && tableController_->currentCell().isValid()) {
    return tableController_->insertText(std::move(text));
  }
  if (selection_ && selection_->currentHit().zone == HitTestResult::Zone::BlockAfter) {
    return insertBlockAfterCurrentBlock(std::move(text));
  }
  return text.isEmpty() ? false : editParagraph(TextBlockCommandBuilder::Operation::InsertText, std::move(text));
}

bool InputController::insertParagraphBreak() {
  if (hasActiveLiteralEditor()) {
    return insertTextIntoActiveLiteral(QStringLiteral("\n"));
  }
  if (selection_ && selection_->currentHit().zone == HitTestResult::Zone::BlockAfter) {
    return insertBlockAfterCurrentBlock();
  }
  return editParagraph(TextBlockCommandBuilder::Operation::Enter);
}

bool InputController::insertBlockAfterCurrentBlock(QString text) {
  if (!session_ || !selection_ || !selection_->hasCursor()) {
    return false;
  }
  MarkdownNode* node = session_->document().node(selection_->cursorPosition().blockId);
  if (!node) {
    return false;
  }
  const SourceRange range = node->sourceRange();
  if (range.byteEnd < range.byteStart || range.byteEnd > session_->markdownText().size()) {
    return false;
  }

  qsizetype insertOffset = range.byteEnd;
  const QString& markdown = session_->markdownText();
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
  if (selection_ && selection_->hasCursor() && !selection_->selection().isCollapsed()) {
    return deleteSelection();
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
  if (selection_ && selection_->hasCursor() && !selection_->selection().isCollapsed()) {
    return deleteSelection();
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

  const TextBlockCommandBuilder builder(session_, &resolver);
  return applyTextCommand(builder.buildIndentListItem(context));
}

bool InputController::outdentListItem() {
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context) || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }
  const TextBlockCommandBuilder builder(session_, &resolver);
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
  const SelectionRange range = selection_->selection();
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
  if (!session_ || !selection_ || !selection_->hasCursor() || selection_->selection().isCollapsed()) {
    return false;
  }

  const SelectionRange range = selection_->selection();
  if (range.anchor.blockId != range.focus.blockId) {
    return false;
  }

  MarkdownNode* node = session_->document().node(range.anchor.blockId);
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

  const auto& blocks = session_->document().root().children();
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
    deleteEnd = session_->markdownText().size();
  } else if (nodeIndex + 1 < static_cast<int>(blocks.size())) {
    deleteEnd = blocks.at(static_cast<size_t>(nodeIndex + 1))->sourceRange().byteStart;
  } else {
    deleteStart = blocks.at(static_cast<size_t>(nodeIndex - 1))->sourceRange().byteEnd;
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, session_->markdownText().size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, session_->markdownText().size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  const CursorPosition beforeCursor = selection_->cursorPosition();
  const QString removedText = session_->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node->clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node->id();
  const BlockType removedNodeType = node->type();

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{removedNodeId, blockStart, removedNodeType});
  if (!session_->applyTextDelta(deleteStart, deleteEnd - deleteStart, QString(), true, std::move(nodeHints))) {
    return false;
  }

  CursorPosition nextCursor = cursorAfterEdit(CursorPosition(), deleteStart, true);
  if (nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  } else {
    selection_->clear();
  }

  if (undoStack_) {
    QVector<NodeId> affectedNodes{removedNodeId};
    if (nextCursor.isValid() && !affectedNodes.contains(nextCursor.blockId)) {
      affectedNodes.push_back(nextCursor.blockId);
    }
    undoStack_->push(EditTransaction(
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
  if (brushQueue_) {
    if (nextCursor.isValid()) {
      brushQueue_->requestBlockRefresh(nextCursor.blockId);
    } else {
      brushQueue_->requestFullRefresh();
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
  return (frontMatterController_ && frontMatterController_->isEditing()) || (codeFenceController_ && codeFenceController_->isEditing()) ||
         (htmlBlockController_ && htmlBlockController_->isEditing()) || (mathBlockController_ && mathBlockController_->isEditing());
}

bool InputController::insertTextIntoActiveLiteral(QString text) {
  if (frontMatterController_ && frontMatterController_->isEditing()) {
    return frontMatterController_->insertText(std::move(text));
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->insertText(std::move(text));
  }
  if (htmlBlockController_ && htmlBlockController_->isEditing()) {
    return htmlBlockController_->insertText(std::move(text));
  }
  return mathBlockController_ && mathBlockController_->isEditing() ? mathBlockController_->insertText(std::move(text)) : false;
}

bool InputController::tryRemoveEmptyLiteralBlock(EditTransaction::Kind kind, const QString& label) {
  if (!session_ || !selection_ || !selection_->hasCursor()) {
    return false;
  }

  // Only applicable to code fence and math block
  NodeId blockId;
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    blockId = codeFenceController_->currentCodeFenceId();
  } else if (mathBlockController_ && mathBlockController_->isEditing()) {
    blockId = mathBlockController_->currentMathBlockId();
  }
  if (!blockId.isValid()) {
    return false;
  }

  MarkdownNode* node = session_->document().node(blockId);
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

  const auto& blocks = session_->document().root().children();
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
    deleteEnd = session_->markdownText().size();
  } else if (nodeIndex + 1 < static_cast<int>(blocks.size())) {
    deleteEnd = blocks.at(static_cast<size_t>(nodeIndex + 1))->sourceRange().byteStart;
  } else {
    deleteStart = blocks.at(static_cast<size_t>(nodeIndex - 1))->sourceRange().byteEnd;
  }
  deleteStart = qBound<qsizetype>(0, deleteStart, session_->markdownText().size());
  deleteEnd = qBound<qsizetype>(deleteStart, deleteEnd, session_->markdownText().size());
  if (deleteStart >= deleteEnd) {
    return false;
  }

  const CursorPosition beforeCursor = selection_->cursorPosition();
  const QString removedText = session_->markdownText().mid(deleteStart, deleteEnd - deleteStart);
  std::unique_ptr<MarkdownNode> removedNode = node->clone(CloneMode::PreserveIds);
  const NodeId removedNodeId = node->id();

  // Exit edit mode before removing the block to clear stale editing state
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    codeFenceController_->exitEditMode();
  } else if (mathBlockController_ && mathBlockController_->isEditing()) {
    mathBlockController_->exitEditMode();
  }

  QVector<LocalEditNodeHint> nodeHints;
  nodeHints.push_back(LocalEditNodeHint{removedNodeId, blockStart, nodeType});
  if (!session_->applyTextDelta(deleteStart, deleteEnd - deleteStart, QString(), true, std::move(nodeHints))) {
    return false;
  }

  CursorPosition nextCursor = cursorAfterEdit(CursorPosition(), deleteStart, true);
  if (nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  } else {
    selection_->clear();
  }

  if (undoStack_) {
    QVector<NodeId> affectedNodes{removedNodeId};
    if (nextCursor.isValid() && !affectedNodes.contains(nextCursor.blockId)) {
      affectedNodes.push_back(nextCursor.blockId);
    }
    undoStack_->push(EditTransaction(
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
  if (brushQueue_) {
    if (nextCursor.isValid()) {
      brushQueue_->requestBlockRefresh(nextCursor.blockId);
    } else {
      brushQueue_->requestFullRefresh();
    }
  }
  return true;
}

bool InputController::deleteBackwardInActiveLiteral() {
  if (tryRemoveEmptyLiteralBlock(EditTransaction::Kind::DeleteText,
                                  QStringLiteral("Backspace Empty Block"))) {
    return true;
  }
  if (frontMatterController_ && frontMatterController_->isEditing()) {
    return frontMatterController_->deleteBackward();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteBackward();
  }
  if (htmlBlockController_ && htmlBlockController_->isEditing()) {
    return htmlBlockController_->deleteBackward();
  }
  return mathBlockController_ && mathBlockController_->isEditing() ? mathBlockController_->deleteBackward() : false;
}

bool InputController::deleteForwardInActiveLiteral() {
  if (tryRemoveEmptyLiteralBlock(EditTransaction::Kind::DeleteText,
                                  QStringLiteral("Delete Empty Block"))) {
    return true;
  }
  if (frontMatterController_ && frontMatterController_->isEditing()) {
    return frontMatterController_->deleteForward();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteForward();
  }
  if (htmlBlockController_ && htmlBlockController_->isEditing()) {
    return htmlBlockController_->deleteForward();
  }
  return mathBlockController_ && mathBlockController_->isEditing() ? mathBlockController_->deleteForward() : false;
}

bool InputController::deleteSelectionInActiveLiteral() {
  if (frontMatterController_ && frontMatterController_->isEditing()) {
    return frontMatterController_->deleteSelection();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->deleteSelection();
  }
  if (htmlBlockController_ && htmlBlockController_->isEditing()) {
    return htmlBlockController_->deleteSelection();
  }
  return mathBlockController_ && mathBlockController_->isEditing() ? mathBlockController_->deleteSelection() : false;
}

bool InputController::exitActiveLiteralEditor() {
  if (frontMatterController_ && frontMatterController_->isEditing()) {
    return frontMatterController_->exitEditMode();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->exitEditMode();
  }
  if (htmlBlockController_ && htmlBlockController_->isEditing()) {
    return htmlBlockController_->exitEditMode();
  }
  return mathBlockController_ && mathBlockController_->isEditing() ? mathBlockController_->exitEditMode() : false;
}

QString InputController::activeLiteralTabText() const {
  if (frontMatterController_ && frontMatterController_->isEditing()) {
    return frontMatterController_->tabText();
  }
  if (codeFenceController_ && codeFenceController_->isEditing()) {
    return codeFenceController_->tabText();
  }
  if (htmlBlockController_ && htmlBlockController_->isEditing()) {
    return htmlBlockController_->tabText();
  }
  return mathBlockController_ && mathBlockController_->isEditing() ? mathBlockController_->tabText() : QString();
}

bool InputController::handleKeyPress(QKeyEvent* event) {
  PerfTimer perf("input.keypress");
  if (event && event->key() == Qt::Key_A && event->modifiers().testFlag(Qt::ControlModifier) &&
      !event->modifiers().testFlag(Qt::AltModifier)) {
    emit selectAllRequested();
    return true;
  }

  if (!session_ || !selection_ || !view_ || event->modifiers().testFlag(Qt::ControlModifier) ||
      event->modifiers().testFlag(Qt::AltModifier)) {
    return false;
  }

  if (!selection_->hasCursor()) {
    if (!session_->markdownText().isEmpty()) {
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
  if (!session_ || !session_->markdownText().isEmpty() || text.isEmpty()) {
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
  if (!resolver.current(context) || !context.node || context.node->type() != BlockType::ListItem || !selection_ || !selection_->hasCursor()) {
    return false;
  }

  const MarkdownNode* previous = context.node->previousSibling();
  if (!previous || previous->type() != BlockType::ListItem) {
    return false;
  }

  const CursorPosition cursor = selection_->cursorPosition();
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

bool InputController::editParagraph(TextBlockCommandBuilder::Operation operation, QString text) {
  PerfTimer perf("input.editParagraph.buildCommand");
  BlockEditContextResolver resolver = contextResolver();
  BlockEditContext context;
  if (!resolver.current(context)) {
    emit unsupportedEditRequested(QStringLiteral("Only plain paragraph, heading, and list item text is editable in this M4 slice."));
    return false;
  }

  const TextBlockCommandBuilder builder(session_, &resolver);
  return applyTextCommand(builder.buildTextEdit(context, operation, std::move(text)));
}

bool InputController::applyTextCommand(const TextBlockCommandBuilder::Command& command) {
  if (!command.valid) {
    return false;
  }
  if (!command.handled) {
    return true;
  }

  if (command.hasLocalEdit()) {
    applyLocalEdit(
        command.kind,
        command.label,
        command.sourceStart,
        command.removedLength,
        command.insertedText,
        command.preferredCursor,
        command.fallbackSourceOffset,
        command.nodeHints,
        command.preferLaterEmptyAtOffset,
        command.structureEdit);
  }
  return true;
}

bool InputController::selectionSourceRange(qsizetype& start, qsizetype& end) const {
  return contextResolver().selectionSourceRange(start, end);
}

bool InputController::blockSelectionSourceRange(qsizetype& start, qsizetype& end) const {
  if (!selection_ || !selection_->hasCursor() || !session_ || selection_->selection().isCollapsed()) {
    return false;
  }

  const SelectionRange range = selection_->selection();
  MarkdownNode* anchorNode = session_->document().node(range.anchor.blockId);
  MarkdownNode* focusNode = session_->document().node(range.focus.blockId);
  if (!anchorNode || !focusNode) {
    return false;
  }

  qsizetype anchorStart = -1;
  qsizetype anchorEnd = -1;
  qsizetype focusStart = -1;
  qsizetype focusEnd = -1;
  BlockEditContextResolver resolver = contextResolver();
  if (!resolver.blockSourceRange(*anchorNode, anchorStart, anchorEnd) || !resolver.blockSourceRange(*focusNode, focusStart, focusEnd)) {
    return false;
  }

  if (anchorStart < focusStart || (anchorStart == focusStart && range.anchor.text.textOffset <= range.focus.text.textOffset)) {
    start = anchorStart + (anchorNode == focusNode ? qBound<qsizetype>(0, range.anchor.text.textOffset, anchorEnd - anchorStart) : 0);
    end = focusEnd;
    if (anchorNode == focusNode) {
      end = anchorStart + qBound<qsizetype>(0, range.focus.text.textOffset, anchorEnd - anchorStart);
    }
  } else {
    start = focusStart + (anchorNode == focusNode ? qBound<qsizetype>(0, range.focus.text.textOffset, focusEnd - focusStart) : 0);
    end = anchorEnd;
    if (anchorNode == focusNode) {
      end = focusStart + qBound<qsizetype>(0, range.anchor.text.textOffset, focusEnd - focusStart);
    }
  }
  return start < end;
}

BlockEditContextResolver InputController::contextResolver() const {
  return BlockEditContextResolver(session_, selection_);
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
  if (!session_) {
    return cursor;
  }

  MarkdownNode* node = paragraphAtSourceOffset(session_->document().root(), sourceOffset, preferLaterEmptyAtOffset);
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
  if (session_ && preferredCursor.isValid()) {
    if (preferredCursor.text.sourceOffset >= 0) {
      CursorPosition sourceCursor = cursorForSourceOffset(preferredCursor.text.sourceOffset, preferLaterEmptyAtOffset);
      if (sourceCursor.isValid()) {
        return sourceCursor;
      }
    }
    if (MarkdownNode* node = session_->document().node(preferredCursor.blockId)) {
      return cursorFor(node->id(), preferredCursor.text.textOffset);
    }
  }
  return cursorForSourceOffset(fallbackSourceOffset, preferLaterEmptyAtOffset);
}

MarkdownNode* InputController::paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset, bool preferLaterEmptyAtOffset) const {
  return contextResolver().nodeAtContentSourceOffset(node, sourceOffset, preferLaterEmptyAtOffset);
}

MarkdownNode* InputController::selectableBlockByDirection(NodeId current, int direction) const {
  if (!session_) {
    return nullptr;
  }
  const auto& blocks = session_->document().root().children();
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
      return plainTextForInlines(node.inlines()).size();
    case BlockType::ListItem:
      return plainTextForNode(node).size();
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::MathBlock:
    case BlockType::HtmlBlock:
      return node.literal().size();
    case BlockType::Table:
      return 1;
    default:
      return 0;
  }
}

bool InputController::moveCursorHorizontal(int direction, bool extendSelection) {
  if (!selection_ || !selection_->hasCursor() || !session_ || direction == 0) {
    return false;
  }

  CursorPosition current = selection_->cursorPosition();
  MarkdownNode* node = session_->document().node(current.blockId);
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
  if (!selection_ || !selection_->hasCursor() || !session_ || direction == 0) {
    return false;
  }
  MarkdownNode* target = selectableBlockByDirection(selection_->cursorPosition().blockId, direction);
  if (!target) {
    return false;
  }
  const qsizetype offset = direction > 0 ? 0 : selectableTextLength(*target);
  setCursorOrExtend(cursorForNode(*target, offset), extendSelection);
  return true;
}

void InputController::setCursorOrExtend(CursorPosition cursor, bool extendSelection) {
  if (!selection_ || !cursor.isValid()) {
    return;
  }
  if (!extendSelection) {
    selection_->setCursorPosition(cursor);
    return;
  }
  SelectionRange range = selection_->selection();
  if (!range.anchor.isValid()) {
    range.anchor = selection_->cursorPosition();
  }
  range.focus = cursor;
  selection_->setSelection(range);
}

void InputController::applyEdit(
    EditTransaction::Kind kind,
    const QString& label,
    QString nextText,
    qsizetype nextSourceOffset) {
  applyEdit(kind, label, std::move(nextText), nextSourceOffset, false);
}

void InputController::applyEdit(
    EditTransaction::Kind kind,
    const QString& label,
    QString nextText,
    qsizetype nextSourceOffset,
    bool preferLaterEmptyAtOffset) {
  applyEdit(kind, label, std::move(nextText), CursorPosition(), nextSourceOffset, {}, preferLaterEmptyAtOffset);
}

void InputController::applyLocalEdit(
    EditTransaction::Kind kind,
    const QString& label,
    qsizetype sourceStart,
    qsizetype removedLength,
    QString insertedText,
    CursorPosition preferredCursor,
    qsizetype fallbackSourceOffset,
    QVector<LocalEditNodeHint> nodeHints,
    bool preferLaterEmptyAtOffset,
    bool structureEdit) {
  PerfTimer perf("input.applyLocalEdit");
  if (!session_ || sourceStart < 0 || removedLength < 0) {
    return;
  }

  const CursorPosition beforeCursor = selection_ && selection_->hasCursor() ? selection_->cursorPosition() : CursorPosition();
  const QString& currentText = session_->markdownText();
  if (sourceStart + removedLength > currentText.size()) {
    return;
  }

  const QString removedText = currentText.mid(sourceStart, removedLength);
  const bool snapshotUndoLikely = undoStack_ && !beforeCursor.blockId.isValid();
  QString beforeText = snapshotUndoLikely ? QString(currentText) : QString();
  bool beforeTextCaptured = snapshotUndoLikely;
  const bool appliedLocally =
      session_->applyTextDelta(sourceStart, removedLength, insertedText, true, std::move(nodeHints));
  QString nextText;
  if (!appliedLocally) {
    if (!beforeTextCaptured) {
      beforeText = currentText;
      beforeTextCaptured = true;
    }
    nextText = beforeText;
    nextText.replace(sourceStart, removedLength, insertedText);
    session_->applyMarkdownText(nextText, true);
  }

  CursorPosition nextCursor = cursorAfterEdit(preferredCursor, fallbackSourceOffset, preferLaterEmptyAtOffset);
  if (selection_) {
    if (!nextCursor.isValid()) {
      nextCursor = cursorForSourceOffset(session_->markdownText().size());
    }
    selection_->setCursorPosition(nextCursor);
  }

  if (undoStack_) {
    const bool textDeltaUndoEligible = appliedLocally && !structureEdit && beforeCursor.isValid() && nextCursor.isValid();
    if (textDeltaUndoEligible) {
      QVector<NodeId> affectedNodes;
      affectedNodes.push_back(refreshNodeFor(session_, nextCursor.blockId));
      undoStack_->push(EditTransaction(
          kind,
          label,
          TextDeltaCommand{
              TextDelta{sourceStart, removedText, insertedText},
              beforeCursor,
              nextCursor,
              std::move(affectedNodes)}));
    } else {
      if (!beforeTextCaptured) {
        beforeText = session_->markdownText();
        beforeText.replace(sourceStart, insertedText.size(), removedText);
        beforeTextCaptured = true;
      }
      if (nextText.isEmpty()) {
        nextText = session_->markdownText();
      }
      undoStack_->push(EditTransaction(kind, label, {beforeText, beforeCursor}, {std::move(nextText), nextCursor}));
    }
  }
  if (brushQueue_) {
    if (!appliedLocally) {
      brushQueue_->requestFullRefresh();
    } else if (structureEdit || session_->lastLocalEditChangedTopLevelStructure()) {
      brushQueue_->requestTopLevelRangeRefresh(session_->lastLocalTopLevelRangeChange());
    } else {
      brushQueue_->requestBlockRefresh(refreshNodeFor(session_, nextCursor.blockId));
    }
  }
}

void InputController::applyEdit(
    EditTransaction::Kind kind,
    const QString& label,
    QString nextText,
    CursorPosition preferredCursor,
    qsizetype fallbackSourceOffset,
    QVector<LocalEditNodeHint> nodeHints,
    bool preferLaterEmptyAtOffset) {
  PerfTimer perf("input.applyEdit.diffFallback");
  const CursorPosition beforeCursor = selection_ && selection_->hasCursor() ? selection_->cursorPosition() : CursorPosition();
  const QString beforeText = session_->markdownText();

  qsizetype prefix = 0;
  const qsizetype minSize = qMin(beforeText.size(), nextText.size());
  while (prefix < minSize && beforeText.at(prefix) == nextText.at(prefix)) {
    ++prefix;
  }
  qsizetype beforeSuffix = beforeText.size();
  qsizetype nextSuffix = nextText.size();
  while (beforeSuffix > prefix && nextSuffix > prefix && beforeText.at(beforeSuffix - 1) == nextText.at(nextSuffix - 1)) {
    --beforeSuffix;
    --nextSuffix;
  }

  const bool appliedLocally =
      session_->applyTextDelta(prefix, beforeSuffix - prefix, nextText.mid(prefix, nextSuffix - prefix), true, std::move(nodeHints));
  if (!appliedLocally) {
    session_->applyMarkdownText(nextText, true);
  }
  CursorPosition nextCursor = cursorAfterEdit(preferredCursor, fallbackSourceOffset, preferLaterEmptyAtOffset);
  if (selection_) {
    if (!nextCursor.isValid()) {
      nextCursor = cursorForSourceOffset(session_->markdownText().size());
    }
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    const QString removedText = beforeText.mid(prefix, beforeSuffix - prefix);
    const QString insertedText = nextText.mid(prefix, nextSuffix - prefix);
    const bool textDeltaUndoEligible = appliedLocally && beforeCursor.isValid() && nextCursor.isValid();
    if (textDeltaUndoEligible) {
      QVector<NodeId> affectedNodes;
      affectedNodes.push_back(refreshNodeFor(session_, nextCursor.blockId));
      undoStack_->push(EditTransaction(
          kind,
          label,
          TextDeltaCommand{
              TextDelta{prefix, removedText, insertedText},
              beforeCursor,
              nextCursor,
              std::move(affectedNodes)}));
    } else {
      undoStack_->push(EditTransaction(kind, label, {beforeText, beforeCursor}, {std::move(nextText), nextCursor}));
    }
  }
  if (brushQueue_) {
    if (!appliedLocally) {
      brushQueue_->requestFullRefresh();
    } else if (session_->lastLocalEditChangedTopLevelStructure()) {
      brushQueue_->requestTopLevelRangeRefresh(session_->lastLocalTopLevelRangeChange());
    } else {
      brushQueue_->requestBlockRefresh(refreshNodeFor(session_, nextCursor.blockId));
    }
  }
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
