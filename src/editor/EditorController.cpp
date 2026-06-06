#include "editor/EditorController.h"

#include "blocks/table/TableModelOps.h"
#include "document/InlineProjection.h"
#include "document/MarkdownNode.h"
#include "editor/BlockEditContext.h"
#include "editor/EditorView.h"

#include <utility>

namespace muffin {
namespace {

qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column) {
  if (line <= 0 || column <= 0) {
    return -1;
  }

  int currentLine = 1;
  qsizetype offset = 0;
  while (currentLine < line && offset < text.size()) {
    if (text.at(offset) == QLatin1Char('\n')) {
      ++currentLine;
    }
    ++offset;
  }

  if (currentLine != line) {
    return -1;
  }
  return qMin(offset + column - 1, text.size());
}

qsizetype sourceOffsetForLineEnd(const QString& text, int line) {
  if (line <= 0) {
    return -1;
  }

  int currentLine = 1;
  qsizetype offset = 0;
  while (offset < text.size()) {
    if (currentLine == line && text.at(offset) == QLatin1Char('\n')) {
      return offset;
    }
    if (text.at(offset) == QLatin1Char('\n')) {
      ++currentLine;
    }
    ++offset;
  }
  return currentLine == line ? text.size() : -1;
}

MarkdownNode* primaryParagraph(MarkdownNode& node) {
  if (node.type() == BlockType::ListItem) {
    for (const auto& child : node.children()) {
      if (child->type() == BlockType::Paragraph) {
        return child.get();
      }
    }
  }
  return &node;
}

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

qsizetype selectableTextLength(const MarkdownNode& node) {
  switch (node.type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::TableCell:
      return plainTextForInlines(node.inlines()).size();
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

CursorPosition cursorForNodeText(const MarkdownNode& node, qsizetype offset, NodeId blockId = {}) {
  CursorPosition cursor;
  cursor.blockId = blockId.isValid() ? blockId : node.id();
  cursor.text.nodeId = node.id();
  cursor.text.textOffset = qBound<qsizetype>(0, offset, selectableTextLength(node));
  return cursor;
}

MarkdownNode* firstSelectableBlock(MarkdownNode& root) {
  for (const auto& child : root.children()) {
    if (child->type() != BlockType::Unknown) {
      return child.get();
    }
  }
  return nullptr;
}

MarkdownNode* lastSelectableBlock(MarkdownNode& root) {
  for (auto it = root.children().rbegin(); it != root.children().rend(); ++it) {
    if ((*it)->type() != BlockType::Unknown) {
      return it->get();
    }
  }
  return nullptr;
}

qsizetype paragraphContentStartIncludingCommonMarkIndent(const QString& markdown, qsizetype astStart) {
  qsizetype lineStart = astStart;
  while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  qsizetype start = astStart;
  while (start > lineStart && astStart - start < 3 && markdown.at(start - 1) == QLatin1Char(' ')) {
    --start;
  }
  return start == lineStart ? start : astStart;
}

bool fillSourceOffsetForTextHit(const DocumentSession& session, HitTestResult& hit) {
  if (hit.zone != HitTestResult::Zone::Text && hit.zone != HitTestResult::Zone::Marker &&
      hit.zone != HitTestResult::Zone::TableCell) {
    return false;
  }

  MarkdownNode* node = session.document().node(hit.zone == HitTestResult::Zone::TableCell ? hit.textNodeId : hit.blockId);
  if (!node) {
    return false;
  }

  MarkdownNode* editable = primaryParagraph(*node);
  if (!editable || (editable->type() != BlockType::Paragraph && editable->type() != BlockType::Heading &&
                    editable->type() != BlockType::TableCell)) {
    return false;
  }

  const SourceRange range = editable->sourceRange();
  const QString markdown = session.markdownText();
  qsizetype start = editable->type() == BlockType::TableCell && range.byteEnd >= range.byteStart
                     ? range.byteStart
                     : sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = editable->type() == BlockType::TableCell && range.byteEnd >= range.byteStart
                         ? range.byteEnd
                         : sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (start < 0 || end < start) {
    return false;
  }

  if (editable->type() == BlockType::Heading) {
    while (start < end && markdown.at(start) == QLatin1Char('#')) {
      ++start;
    }
    if (start < end && markdown.at(start).isSpace()) {
      ++start;
    }
  } else if (editable->type() == BlockType::Paragraph) {
    start = paragraphContentStartIncludingCommonMarkIndent(markdown, start);
  }

  const QString contentText = markdown.mid(start, end - start);
  qsizetype localSourceOffset = -1;
  CursorPosition cursor = hit.cursorPosition();
  cursor.text.textOffset = hit.textOffset;
  cursor.text.sourceOffset = hit.sourceOffset;
  InlineProjectionState projectionState = InlineProjectionState::forCursor(cursor, hit.blockId, start);
  InlineProjection projection(editable->inlines(), contentText, projectionState);
  if (hit.sourceOffset >= 0) {
    localSourceOffset = qBound<qsizetype>(0, hit.sourceOffset - start, contentText.size());
  } else if (!projection.sourceOffsetForVisibleOffset(hit.textOffset, localSourceOffset)) {
    localSourceOffset = qBound<qsizetype>(0, hit.textOffset, contentText.size());
  }
  hit.sourceOffset = start + localSourceOffset;
  return true;
}

MarkdownNode* tableByIdOrIndex(DocumentSession& session, NodeId tableId, int tableIndex) {
  if (tableId.isValid()) {
    if (MarkdownNode* table = session.document().node(tableId)) {
      if (table->type() == BlockType::Table) {
        return table;
      }
    }
  }
  if (tableIndex < 0) {
    return nullptr;
  }

  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == BlockType::Table) {
      if (index == tableIndex) {
        return &node;
      }
      ++index;
    }
    for (const auto& child : node.children()) {
      if (MarkdownNode* found = self(self, *child)) {
        return found;
      }
    }
    return nullptr;
  };
  return visit(visit, session.document().root());
}

CursorPosition tableCursorForLocation(DocumentSession& session, const TableCommand& command, int row, int column, const CursorPosition& fallback) {
  CursorPosition cursor;
  MarkdownNode* table = tableByIdOrIndex(session, command.tableId, command.tableIndex);
  if (!table) {
    return cursor;
  }

  MarkdownNode* cell = TableModelOps::cellAt(*table, qMax(0, row), qMax(0, column));
  if (!cell && fallback.text.nodeId.isValid()) {
    if (MarkdownNode* fallbackCell = session.document().node(fallback.text.nodeId)) {
      if (fallbackCell->type() == BlockType::TableCell) {
        cell = fallbackCell;
      }
    }
  }
  if (!cell) {
    return cursor;
  }

  cursor.blockId = table->id();
  cursor.text.nodeId = cell->id();
  cursor.text.textOffset = qMax<qsizetype>(0, fallback.text.textOffset);
  cursor.text.sourceOffset = fallback.text.sourceOffset;
  return cursor;
}

CursorPosition tableCellTextCursor(DocumentSession& session, const CursorPosition& storedCursor) {
  CursorPosition cursor;
  if (!storedCursor.isValid() || !storedCursor.text.nodeId.isValid()) {
    return cursor;
  }

  MarkdownNode* cell = session.document().node(storedCursor.text.nodeId);
  if (!cell || cell->type() != BlockType::TableCell) {
    return cursor;
  }

  MarkdownNode* table = cell;
  while (table && table->type() != BlockType::Table) {
    table = table->parent();
  }
  if (!table) {
    return cursor;
  }

  cursor = storedCursor;
  cursor.blockId = table->id();
  cursor.text.nodeId = cell->id();
  return cursor;
}

MarkdownNode* nodeByTopLevelIndex(DocumentSession& session, int nodeIndex, BlockType nodeType) {
  const auto& children = session.document().root().children();
  if (nodeIndex < 0 || nodeIndex >= static_cast<int>(children.size())) {
    return nullptr;
  }
  MarkdownNode* node = children.at(static_cast<size_t>(nodeIndex)).get();
  return node && node->type() == nodeType ? node : nullptr;
}

MarkdownNode* nodeBySourceOffset(MarkdownNode& node, BlockType nodeType, qsizetype sourceOffset) {
  const SourceRange range = node.sourceRange();
  if (node.type() == nodeType && range.byteStart <= sourceOffset && range.byteEnd >= sourceOffset) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = nodeBySourceOffset(*child, nodeType, sourceOffset)) {
      return found;
    }
  }
  return nullptr;
}

CursorPosition insertedNodeCursor(DocumentSession& session, const InsertNodeCommand& command, const CursorPosition& storedCursor) {
  CursorPosition cursor;
  if (command.nodeType == BlockType::FrontMatter) {
    MarkdownNode* node = session.document().node(command.nodeId);
    if (!node || node->type() != BlockType::FrontMatter) {
      node = nodeBySourceOffset(session.document().root(), BlockType::FrontMatter, command.nodeSourceStart);
    }
    if (!node) {
      node = nodeByTopLevelIndex(session, command.nodeIndex, BlockType::FrontMatter);
    }
    if (!node) {
      return cursor;
    }
    cursor = storedCursor;
    cursor.blockId = node->id();
    cursor.text.nodeId = node->id();
    cursor.text.textOffset = qBound<qsizetype>(0, storedCursor.text.textOffset, node->literal().size());
    return cursor;
  }
  if (command.nodeType != BlockType::Table) {
    return cursor;
  }

  MarkdownNode* table = session.document().node(command.nodeId);
  if (!table || table->type() != BlockType::Table) {
    table = nodeBySourceOffset(session.document().root(), BlockType::Table, command.nodeSourceStart);
  }
  if (!table) {
    table = nodeByTopLevelIndex(session, command.nodeIndex, BlockType::Table);
  }
  if (!table) {
    return cursor;
  }

  MarkdownNode* cell = storedCursor.text.nodeId.isValid() ? session.document().node(storedCursor.text.nodeId) : nullptr;
  if (!cell || cell->type() != BlockType::TableCell) {
    cell = TableModelOps::cellAt(*table, 0, 0);
  }
  if (!cell) {
    return cursor;
  }

  cursor = storedCursor;
  cursor.blockId = table->id();
  cursor.text.nodeId = cell->id();
  cursor.text.textOffset = qMax<qsizetype>(0, storedCursor.text.textOffset);
  return cursor;
}

CursorPosition replacedNodeCursor(DocumentSession& session, const ReplaceNodeCommand& command, const CursorPosition& storedCursor) {
  CursorPosition cursor;
  if (!storedCursor.isValid()) {
    return cursor;
  }

  MarkdownNode* node = command.nodeId.isValid() ? session.document().node(command.nodeId) : nullptr;
  if (!node || node->type() != command.nodeType) {
    node = nodeByTopLevelIndex(session, command.nodeIndex, command.nodeType);
  }
  if (!node) {
    return cursor;
  }

  cursor = storedCursor;
  cursor.blockId = node->id();
  cursor.text.nodeId = node->id();
  return cursor;
}

MarkdownNode* nodeByIdOrIndex(DocumentSession& session, NodeId nodeId, BlockType nodeType, int nodeIndex) {
  MarkdownNode* node = nodeId.isValid() ? session.document().node(nodeId) : nullptr;
  if (!node || node->type() != nodeType) {
    node = nodeByTopLevelIndex(session, nodeIndex, nodeType);
  }
  return node;
}

template <typename T>
T attributeValue(const NodeAttributeValue& value) {
  return std::get<T>(value);
}

void applyNodeAttribute(MarkdownNode& node, NodeAttribute attribute, const NodeAttributeValue& value) {
  switch (attribute) {
    case NodeAttribute::HeadingLevel:
      node.setHeadingLevel(attributeValue<int>(value));
      break;
    case NodeAttribute::ListKind:
      node.setListKind(attributeValue<ListKind>(value));
      break;
    case NodeAttribute::ListStart:
      node.setListStart(attributeValue<int>(value));
      break;
    case NodeAttribute::ListTight:
      node.setListTight(attributeValue<bool>(value));
      break;
    case NodeAttribute::TaskChecked:
      node.setTaskChecked(attributeValue<bool>(value));
      break;
    case NodeAttribute::CodeLanguage:
      node.setCodeLanguage(attributeValue<QString>(value));
      break;
    case NodeAttribute::TableAlignments:
      node.setTableAlignments(attributeValue<QVector<TableAlignment>>(value));
      break;
    case NodeAttribute::TableRowIsHeader:
      node.setTableRowIsHeader(attributeValue<bool>(value));
      break;
    case NodeAttribute::Unknown:
      break;
  }
}

bool applyTextReplacement(DocumentSession& session, qsizetype replaceStart, qsizetype replaceLength, const QString& replacement, const QVector<NodeId>& affectedNodes) {
  QVector<LocalEditNodeHint> nodeHints;
  for (NodeId nodeId : affectedNodes) {
    nodeHints.push_back(LocalEditNodeHint{nodeId, replaceStart, BlockType::Unknown});
  }
  if (session.applyTextDelta(replaceStart, replaceLength, replacement, true, std::move(nodeHints))) {
    return true;
  }

  QString text = session.markdownText();
  if (replaceStart < 0 || replaceLength < 0 || replaceStart + replaceLength > text.size()) {
    return false;
  }
  text.replace(replaceStart, replaceLength, replacement);
  session.applyMarkdownText(std::move(text), true);
  return true;
}

NodeId topLevelBlockIdFor(DocumentSession& session, NodeId nodeId) {
  if (!nodeId.isValid()) {
    return {};
  }
  const MarkdownNode* node = session.document().node(nodeId);
  if (!node) {
    return {};
  }
  while (node->parent() && node->parent()->type() != BlockType::Document) {
    node = node->parent();
  }
  return node && node->parent() && node->parent()->type() == BlockType::Document ? node->id() : NodeId();
}

void addRefreshNode(DocumentSession& session, QVector<NodeId>& refreshNodes, NodeId nodeId) {
  NodeId topLevelId = topLevelBlockIdFor(session, nodeId);
  if (!topLevelId.isValid()) {
    topLevelId = nodeId;
  }
  if (topLevelId.isValid() && !refreshNodes.contains(topLevelId)) {
    refreshNodes.push_back(topLevelId);
  }
}

QVector<NodeId> refreshNodesFor(DocumentSession& session, const QVector<NodeId>& affectedNodes, CursorPosition cursor = {}) {
  QVector<NodeId> refreshNodes;
  for (NodeId nodeId : affectedNodes) {
    addRefreshNode(session, refreshNodes, nodeId);
  }
  if (cursor.isValid()) {
    addRefreshNode(session, refreshNodes, cursor.blockId);
    addRefreshNode(session, refreshNodes, cursor.text.nodeId);
  }
  return refreshNodes;
}

void requestRefreshForNodes(BrushQueue& brushQueue, DocumentSession& session, const QVector<NodeId>& affectedNodes, CursorPosition cursor = {}) {
  QVector<NodeId> refreshNodes = refreshNodesFor(session, affectedNodes, cursor);
  if (refreshNodes.isEmpty()) {
    brushQueue.requestFullRefresh();
    return;
  }
  brushQueue.requestBlocksRefresh(std::move(refreshNodes));
}

}  // namespace

EditorController::EditorController(QObject* parent) : QObject(parent) {}

void EditorController::attach(DocumentSession* session, EditorView* view) {
  if (session_ == session && view_ == view) {
    return;
  }

  detach();
  session_ = session;
  view_ = view;

  inputController_.setDocumentSession(session_);
  inputController_.setSelectionController(&selection_);
  inputController_.setUndoStack(&undoStack_);
  inputController_.setBrushQueue(&brushQueue_);
  inputController_.setTableController(&tableController_);
  inputController_.setFrontMatterController(&frontMatterController_);
  inputController_.setCodeFenceController(&codeFenceController_);
  inputController_.setHtmlBlockController(&htmlBlockController_);
  inputController_.setMathBlockController(&mathBlockController_);
  inputController_.attach(view_);
  stylizeController_.setDocumentSession(session_);
  stylizeController_.setSelectionController(&selection_);
  stylizeController_.setUndoStack(&undoStack_);
  stylizeController_.setBrushQueue(&brushQueue_);
  paragraphController_.setDocumentSession(session_);
  paragraphController_.setSelectionController(&selection_);
  paragraphController_.setUndoStack(&undoStack_);
  paragraphController_.setBrushQueue(&brushQueue_);
  frontMatterController_.setDocumentSession(session_);
  frontMatterController_.setSelectionController(&selection_);
  frontMatterController_.setUndoStack(&undoStack_);
  frontMatterController_.setBrushQueue(&brushQueue_);
  codeFenceController_.setDocumentSession(session_);
  codeFenceController_.setSelectionController(&selection_);
  codeFenceController_.setUndoStack(&undoStack_);
  codeFenceController_.setBrushQueue(&brushQueue_);
  htmlBlockController_.setDocumentSession(session_);
  htmlBlockController_.setSelectionController(&selection_);
  htmlBlockController_.setUndoStack(&undoStack_);
  htmlBlockController_.setBrushQueue(&brushQueue_);
  mathBlockController_.setDocumentSession(session_);
  mathBlockController_.setSelectionController(&selection_);
  mathBlockController_.setUndoStack(&undoStack_);
  mathBlockController_.setBrushQueue(&brushQueue_);
  tableController_.setDocumentSession(session_);
  tableController_.setSelectionController(&selection_);
  tableController_.setUndoStack(&undoStack_);
  tableController_.setBrushQueue(&brushQueue_);
  clipboardController_.setDocumentSession(session_);
  clipboardController_.setSelectionController(&selection_);
  clipboardController_.setInputController(&inputController_);

  if (view_) {
    connect(view_, &EditorView::blockClicked, this, &EditorController::activateHit);
    connect(view_, &EditorView::selectionChanged, &selection_, &SelectionController::setSelection);
    connect(view_, &EditorView::textCommitted, &inputController_, &InputController::insertText);
  }
  connect(&inputController_, &InputController::selectAllRequested, this, &EditorController::selectAll);
  connect(&selection_, &SelectionController::selectionChanged, this, [this](SelectionRange selection, HitTestResult hit) {
    if (view_) {
      if (selection.focus.isValid() && !selection.isCollapsed()) {
        view_->setSelectionRange(selection);
      } else if (selection.focus.isValid()) {
        view_->setCursorPosition(selection.focus);
      } else {
        view_->clearCursor();
      }
    }
    emit cursorChanged(hit);
    emit stateChanged();
  });
  connect(&undoStack_, &UndoStack::stateChanged, this, &EditorController::stateChanged);
  connect(&brushQueue_, &BrushQueue::refreshRequested, this, [this](const BrushQueue::RefreshRequest& request) {
    if (!session_ || !view_) {
      return;
    }
    if (request.fullLayoutDirty) {
      view_->setDocument(session_->document());
      return;
    }
    if (request.topLevelRangeDirty.isValid()) {
      if (request.topLevelRangeDirty.documentRevision != session_->document().revision() || !view_->refreshTopLevelRange(request.topLevelRangeDirty, session_->document())) {
        view_->setDocument(session_->document());
      }
      return;
    }
    if (request.layoutDirtyBlocks.size() == 1) {
      if (!view_->refreshBlock(request.layoutDirtyBlocks.first(), session_->document())) {
        view_->setDocument(session_->document());
      }
      return;
    }
    if (!request.layoutDirtyBlocks.isEmpty() && !view_->refreshBlocks(request.layoutDirtyBlocks, session_->document())) {
      view_->setDocument(session_->document());
    }
  });
}

void EditorController::detach() {
  if (view_) {
    view_->disconnect(this);
  }
  selection_.disconnect(this);
  undoStack_.disconnect(this);
  brushQueue_.disconnect(this);
  inputController_.attach(nullptr);
  session_ = nullptr;
  view_ = nullptr;
}

SelectionController& EditorController::selection() {
  return selection_;
}

const SelectionController& EditorController::selection() const {
  return selection_;
}

UndoStack& EditorController::undoStack() {
  return undoStack_;
}

const UndoStack& EditorController::undoStack() const {
  return undoStack_;
}

InputController& EditorController::inputController() {
  return inputController_;
}

StylizeController& EditorController::stylizeController() {
  return stylizeController_;
}

FrontMatterController& EditorController::frontMatterController() {
  return frontMatterController_;
}

CodeFenceController& EditorController::codeFenceController() {
  return codeFenceController_;
}

HtmlBlockController& EditorController::htmlBlockController() {
  return htmlBlockController_;
}

MathBlockController& EditorController::mathBlockController() {
  return mathBlockController_;
}

TableController& EditorController::tableController() {
  return tableController_;
}

ClipboardController& EditorController::clipboardController() {
  return clipboardController_;
}

BrushQueue& EditorController::brushQueue() {
  return brushQueue_;
}

bool EditorController::canUndo() const {
  return undoStack_.canUndo();
}

bool EditorController::canRedo() const {
  return undoStack_.canRedo();
}

void EditorController::undo() {
  if (!canUndo()) {
    return;
  }
  applyTransaction(undoStack_.takeUndo(), true);
}

void EditorController::redo() {
  if (!canRedo()) {
    return;
  }
  applyTransaction(undoStack_.takeRedo(), false);
}

bool EditorController::toggleBold() {
  return stylizeController_.toggleBold();
}

bool EditorController::toggleItalic() {
  return stylizeController_.toggleItalic();
}

bool EditorController::toggleCode() {
  return stylizeController_.toggleCode();
}

bool EditorController::insertLink() {
  return stylizeController_.insertLink();
}

bool EditorController::insertTableRowBefore() {
  return tableController_.insertRowBefore();
}

bool EditorController::insertTableRowAfter() {
  return tableController_.insertRowAfter();
}

bool EditorController::deleteTableRow() {
  return tableController_.deleteCurrentRow();
}

bool EditorController::moveTableRowUp() {
  return tableController_.moveCurrentRowUp();
}

bool EditorController::moveTableRowDown() {
  return tableController_.moveCurrentRowDown();
}

bool EditorController::insertTableColumnBefore() {
  return tableController_.insertColumnBefore();
}

bool EditorController::insertTableColumnAfter() {
  return tableController_.insertColumnAfter();
}

bool EditorController::deleteTableColumn() {
  return tableController_.deleteCurrentColumn();
}

bool EditorController::moveTableColumnLeft() {
  return tableController_.moveCurrentColumnLeft();
}

bool EditorController::moveTableColumnRight() {
  return tableController_.moveCurrentColumnRight();
}

bool EditorController::setTableColumnAlignment(TableAlignment alignment) {
  return tableController_.setCurrentColumnAlignment(alignment);
}

bool EditorController::resizeTable(int rows, int columns) {
  return tableController_.resizeCurrentTable(rows, columns);
}

bool EditorController::deleteTable() {
  return tableController_.deleteCurrentTable();
}

bool EditorController::insertTable() {
  return tableController_.insertTable();
}

bool EditorController::insertFrontMatter(FrontMatterFormat format) {
  return frontMatterController_.insertFrontMatter(format);
}

bool EditorController::enterFrontMatterEditMode() {
  return frontMatterController_.enterEditMode();
}

bool EditorController::exitFrontMatterEditMode() {
  return frontMatterController_.exitEditMode();
}

bool EditorController::enterCodeFenceEditMode() {
  return codeFenceController_.enterEditMode();
}

bool EditorController::exitCodeFenceEditMode() {
  return codeFenceController_.exitEditMode();
}

bool EditorController::setCodeFenceLanguage(QString language) {
  return codeFenceController_.setLanguage(std::move(language));
}

bool EditorController::setCodeFenceLanguage(NodeId codeId, QString language) {
  return codeFenceController_.setLanguageFor(codeId, std::move(language));
}

bool EditorController::enterHtmlBlockEditMode() {
  return htmlBlockController_.enterEditMode();
}

bool EditorController::exitHtmlBlockEditMode() {
  return htmlBlockController_.exitEditMode();
}

bool EditorController::setHtmlBlockSource(QString html) {
  return htmlBlockController_.setHtml(std::move(html));
}

bool EditorController::enterMathBlockEditMode() {
  return mathBlockController_.enterEditMode();
}

bool EditorController::exitMathBlockEditMode() {
  return mathBlockController_.exitEditMode();
}

bool EditorController::setMathBlockTex(QString tex) {
  return mathBlockController_.setTex(std::move(tex));
}

bool EditorController::copy() {
  return clipboardController_.copy();
}

bool EditorController::cut() {
  return clipboardController_.cut();
}

bool EditorController::paste() {
  return clipboardController_.paste();
}

bool EditorController::copyAsPlainText() {
  return clipboardController_.copyAsPlainText();
}

bool EditorController::copyAsMarkdown() {
  return clipboardController_.copyAsMarkdown();
}

bool EditorController::copyAsHtml() {
  return clipboardController_.copyAsHtml();
}

bool EditorController::pasteAsPlainText() {
  return clipboardController_.pasteAsPlainText();
}

ParagraphController& EditorController::paragraphController() {
  return paragraphController_;
}

bool EditorController::setHeadingLevel(int level) {
  return paragraphController_.setHeadingLevel(level);
}

bool EditorController::promoteHeading() {
  return paragraphController_.promoteHeading();
}

bool EditorController::demoteHeading() {
  return paragraphController_.demoteHeading();
}

bool EditorController::insertFormulaBlock() {
  return paragraphController_.insertFormulaBlock();
}

bool EditorController::insertCodeBlock() {
  return paragraphController_.insertCodeBlock();
}

bool EditorController::insertLinkReference() {
  return paragraphController_.insertLinkReference();
}

bool EditorController::toggleQuote() {
  return paragraphController_.toggleQuote();
}

bool EditorController::convertToOrderedList() {
  return paragraphController_.convertToOrderedList();
}

bool EditorController::convertToUnorderedList() {
  return paragraphController_.convertToUnorderedList();
}

bool EditorController::convertToTaskList() {
  return paragraphController_.convertToTaskList();
}

bool EditorController::insertParagraphBefore() {
  return paragraphController_.insertParagraphBefore();
}

bool EditorController::insertParagraphAfter() {
  return paragraphController_.insertParagraphAfter();
}

bool EditorController::selectAll() {
  if (!session_ || !selection_.hasCursor()) {
    return false;
  }

  const CursorPosition cursor = selection_.cursorPosition();
  MarkdownNode* focusNode = cursor.text.nodeId.isValid() ? session_->document().node(cursor.text.nodeId) : nullptr;
  MarkdownNode* blockNode = session_->document().node(cursor.blockId);
  MarkdownNode* target = focusNode ? focusNode : blockNode;
  if (!target) {
    return false;
  }

  if (target->type() == BlockType::TableCell) {
    SelectionRange range;
    range.anchor = cursorForNodeText(*target, 0, cursor.blockId);
    range.focus = cursorForNodeText(*target, selectableTextLength(*target), cursor.blockId);
    selection_.setSelection(range);
    return true;
  }

  if (blockNode && (blockNode->type() == BlockType::FrontMatter || blockNode->type() == BlockType::CodeFence ||
                    blockNode->type() == BlockType::MathBlock || blockNode->type() == BlockType::HtmlBlock)) {
    SelectionRange range;
    range.anchor = cursorForNodeText(*blockNode, 0);
    range.focus = cursorForNodeText(*blockNode, selectableTextLength(*blockNode));
    selection_.setSelection(range);
    return true;
  }

  MarkdownNode* first = firstSelectableBlock(session_->document().root());
  MarkdownNode* last = lastSelectableBlock(session_->document().root());
  if (!first || !last) {
    return false;
  }

  SelectionRange range;
  range.anchor = cursorForNodeText(*first, 0);
  range.focus = cursorForNodeText(*last, selectableTextLength(*last));
  selection_.setSelection(range);
  return true;
}

void EditorController::clearHistoryAndSelection() {
  undoStack_.clear();
  selection_.clear();
  exitAllLiteralEditModes();
}

bool EditorController::selectCurrentBlock() {
  if (!session_ || !selection_.hasCursor()) {
    return false;
  }

  const CursorPosition cursor = selection_.cursorPosition();
  MarkdownNode* blockNode = session_->document().node(cursor.blockId);
  if (!blockNode) {
    return false;
  }

  // For list items, select the primary paragraph content
  MarkdownNode* target = blockNode;
  if (blockNode->type() == BlockType::ListItem) {
    for (const auto& child : blockNode->children()) {
      if (child->type() == BlockType::Paragraph || child->type() == BlockType::Heading) {
        target = child.get();
        break;
      }
    }
  }

  SelectionRange range;
  range.anchor = cursorForNodeText(*target, 0, cursor.blockId);
  range.focus = cursorForNodeText(*target, selectableTextLength(*target), cursor.blockId);
  selection_.setSelection(range);
  return true;
}

bool EditorController::selectCurrentFormatSpan() {
  if (!session_ || !selection_.hasCursor()) {
    return false;
  }

  BlockEditContextResolver resolver(const_cast<DocumentSession*>(session_), const_cast<SelectionController*>(&selection_));
  BlockEditContext context;
  if (!resolver.current(context)) {
    return false;
  }

  // For literal blocks, select the whole block
  if (context.blockType == BlockType::FrontMatter || context.blockType == BlockType::CodeFence ||
      context.blockType == BlockType::MathBlock || context.blockType == BlockType::HtmlBlock) {
    MarkdownNode* blockNode = session_->document().node(selection_.cursorPosition().blockId);
    if (!blockNode) {
      return false;
    }
    SelectionRange range;
    range.anchor = cursorForNodeText(*blockNode, 0);
    range.focus = cursorForNodeText(*blockNode, selectableTextLength(*blockNode));
    selection_.setSelection(range);
    return true;
  }

  // For paragraph/heading/table cell: find the InlineProjection span at cursor
  if (!context.inlineProjection.isValid() || !context.editableNode) {
    // Fallback: select word at cursor
    return selectWordAtCursor(context);
  }

  const qsizetype offset = context.cursorTextOffset;
  const auto& spans = context.inlineProjection.spans();

  // Walk spans to find the innermost content span containing the cursor
  const InlineProjectionSpan* bestSpan = nullptr;
  for (const auto& span : spans) {
    if (span.kind == InlineSpanKind::OpenMarker || span.kind == InlineSpanKind::CloseMarker ||
        span.kind == InlineSpanKind::HiddenSyntax) {
      continue;
    }
    // Check if cursor is within this span's visible range
    if (offset >= span.visibleStart && offset <= span.visibleEnd) {
      // Prefer innermost (non-Text) spans, or the last Text span
      if (span.kind != InlineSpanKind::Text || !bestSpan) {
        bestSpan = &span;
      }
    }
  }

  if (bestSpan && bestSpan->visibleStart < bestSpan->visibleEnd) {
    SelectionRange range;
    range.anchor.blockId = context.blockId;
    range.anchor.text.nodeId = context.editableNode->id();
    range.anchor.text.textOffset = bestSpan->visibleStart;
    range.focus.blockId = context.blockId;
    range.focus.text.nodeId = context.editableNode->id();
    range.focus.text.textOffset = bestSpan->visibleEnd;
    selection_.setSelection(range);
    return true;
  }

  return selectWordAtCursor(context);
}

bool EditorController::selectWordAtCursor(const BlockEditContext& context) {
  const QString& visible = context.visibleText;
  const qsizetype offset = qBound<qsizetype>(0, context.cursorTextOffset, visible.size());

  qsizetype wordStart = offset;
  qsizetype wordEnd = offset;
  while (wordStart > 0 && visible.at(wordStart - 1).isLetterOrNumber()) {
    --wordStart;
  }
  while (wordEnd < visible.size() && visible.at(wordEnd).isLetterOrNumber()) {
    ++wordEnd;
  }
  if (wordStart >= wordEnd) {
    return false;
  }

  SelectionRange range;
  range.anchor.blockId = context.blockId;
  range.anchor.text.nodeId = context.editableNode ? context.editableNode->id() : context.node->id();
  range.anchor.text.textOffset = wordStart;
  range.focus.blockId = context.blockId;
  range.focus.text.nodeId = context.editableNode ? context.editableNode->id() : context.node->id();
  range.focus.text.textOffset = wordEnd;
  selection_.setSelection(range);
  return true;
}

bool EditorController::moveBlockUp() {
  if (!session_ || !selection_.hasCursor()) {
    return false;
  }

  const CursorPosition cursor = selection_.cursorPosition();
  MarkdownNode* current = session_->document().node(cursor.blockId);
  if (!current) {
    return false;
  }

  // Walk up to top-level child of Document
  while (current->parent() && current->parent()->type() != BlockType::Document) {
    current = current->parent();
  }
  if (!current || !current->parent() || current->parent()->type() != BlockType::Document) {
    return false;
  }

  MarkdownNode* prev = current->previousSibling();
  if (!prev) {
    return false;
  }

  return swapTopLevelBlocks(*prev, *current);
}

bool EditorController::moveBlockDown() {
  if (!session_ || !selection_.hasCursor()) {
    return false;
  }

  const CursorPosition cursor = selection_.cursorPosition();
  MarkdownNode* current = session_->document().node(cursor.blockId);
  if (!current) {
    return false;
  }

  // Walk up to top-level child of Document
  while (current->parent() && current->parent()->type() != BlockType::Document) {
    current = current->parent();
  }
  if (!current || !current->parent() || current->parent()->type() != BlockType::Document) {
    return false;
  }

  MarkdownNode* next = current->nextSibling();
  if (!next) {
    return false;
  }

  return swapTopLevelBlocks(*current, *next);
}

bool EditorController::swapTopLevelBlocks(MarkdownNode& upper, MarkdownNode& lower) {
  const QString& markdown = session_->markdownText();
  const SourceRange upperRange = upper.sourceRange();
  const SourceRange lowerRange = lower.sourceRange();

  qsizetype upperStart = upperRange.byteStart;
  qsizetype lowerEnd = lowerRange.byteEnd;

  // Extend lowerEnd to include trailing newline for clean swap
  while (lowerEnd < markdown.size() && markdown.at(lowerEnd) == QLatin1Char('\n')) {
    ++lowerEnd;
  }

  const QString upperText = markdown.mid(upperStart, lowerRange.byteStart - upperStart);
  const QString lowerText = markdown.mid(lowerRange.byteStart, lowerEnd - lowerRange.byteStart);

  // Compute new cursor source offset: same relative position within the moved block
  const qsizetype cursorSrcOff = selection_.cursorPosition().text.sourceOffset;
  qsizetype newCursorOffset = cursorSrcOff;

  // The block moves by the difference in text lengths
  // Current block text (upperText) gets replaced by lowerText, then upperText
  // If cursor was in the upper block, it shifts by (lowerText.size() - 0) = lowerText.size()
  // If cursor was in the lower block, it shifts by -(upperText.size())
  const qsizetype upperBlockEnd = lowerRange.byteStart;
  if (cursorSrcOff >= upperStart && cursorSrcOff < upperBlockEnd) {
    // Cursor was in upper block, it shifts down by lowerText.size()
    newCursorOffset = cursorSrcOff + lowerText.size();
  } else if (cursorSrcOff >= lowerRange.byteStart && cursorSrcOff <= lowerEnd) {
    // Cursor was in lower block, it shifts up by upperText.size()
    newCursorOffset = cursorSrcOff - upperText.size();
  }

  const QString combined = lowerText + upperText;
  inputController_.performLocalEdit(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Move Block"),
      upperStart,
      lowerEnd - upperStart,
      combined,
      CursorPosition{},
      newCursorOffset,
      {},
      false,
      true);
  return true;
}

void EditorController::exitAllLiteralEditModes() {
  frontMatterController_.exitEditMode();
  codeFenceController_.exitEditMode();
  htmlBlockController_.exitEditMode();
  mathBlockController_.exitEditMode();
}

bool EditorController::enterLiteralEditMode(HitTestResult::Zone zone) {
  switch (zone) {
    case HitTestResult::Zone::FrontMatter:
      return frontMatterController_.enterEditMode();
    case HitTestResult::Zone::Code:
      return codeFenceController_.enterEditMode();
    case HitTestResult::Zone::Html:
      return htmlBlockController_.enterEditMode();
    case HitTestResult::Zone::Math:
      return mathBlockController_.enterEditMode();
    default:
      return false;
  }
}

void EditorController::activateHit(HitTestResult hit) {
  if (!hit.isValid()) {
    selection_.clear();
    exitAllLiteralEditModes();
    return;
  }
  if (session_) {
    fillSourceOffsetForTextHit(*session_, hit);
  }

  exitAllLiteralEditModes();
  selection_.setHitResult(hit);
  if (enterLiteralEditMode(hit.zone)) {
    selection_.setHitResult(hit);
  }
}

void EditorController::applySnapshot(const DocumentSnapshot& snapshot) {
  if (!session_) {
    return;
  }

  session_->applyMarkdownText(snapshot.markdownText, true);
  const CursorPosition cursor = remapSnapshotCursor(snapshot.cursor);
  if (cursor.isValid()) {
    selection_.setCursorPosition(cursor);
  } else {
    selection_.clear();
  }
  brushQueue_.requestFullRefresh();
}

void EditorController::applyTransaction(const EditTransaction& transaction, bool undo) {
  if (!session_ || !transaction.isValid()) {
    return;
  }

  if (transaction.isSnapshot()) {
    applySnapshot(undo ? transaction.before() : transaction.after());
    return;
  }

  if (transaction.isTableCommand()) {
    const TableCommand& command = transaction.tableCommand();
    const MarkdownNode* table = undo ? command.beforeTable.get() : command.afterTable.get();
    if (!table) {
      return;
    }
    const bool applied = session_->applyTableSnapshot(command.tableId, command.tableIndex, *table, true);
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = tableCursorForLocation(
        *session_, command, undo ? command.beforeRow : command.afterRow, undo ? command.beforeColumn : command.afterColumn, storedCursor);
    if (!cursor.isValid()) {
      cursor = remapSnapshotCursor(storedCursor);
    }
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (applied) {
      requestRefreshForNodes(brushQueue_, *session_, QVector<NodeId>{command.tableId}, cursor);
    }
    return;
  }

  if (transaction.isInsertNodeCommand()) {
    const InsertNodeCommand& command = transaction.insertNodeCommand();
    const QString replacement = undo ? command.delta.removedText : command.delta.insertedText;
    const qsizetype replaceStart = command.delta.start;
    const qsizetype replaceEnd = command.delta.start + (undo ? command.delta.insertedText.size() : command.delta.removedText.size());
    const bool appliedLocally =
        session_->applyInsertedNode(command.nodeId, command.nodeType, replaceStart, command.nodeSourceStart, replaceEnd - replaceStart, replacement, true);
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = undo ? CursorPosition() : insertedNodeCursor(*session_, command, storedCursor);
    if (!cursor.isValid()) {
      cursor = tableCellTextCursor(*session_, storedCursor);
    }
    if (!cursor.isValid()) {
      cursor = remapSnapshotCursor(storedCursor);
    }
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (appliedLocally) {
      QVector<NodeId> affectedNodes = command.affectedNodes;
      if (!undo && command.nodeId.isValid() && !affectedNodes.contains(command.nodeId)) {
        affectedNodes.push_back(command.nodeId);
      }
      requestRefreshForNodes(brushQueue_, *session_, affectedNodes, cursor);
    } else {
      brushQueue_.requestFullRefresh();
    }
    return;
  }

  if (transaction.isReplaceNodeCommand()) {
    const ReplaceNodeCommand& command = transaction.replaceNodeCommand();
    const MarkdownNode* node = undo ? command.beforeNode.get() : command.afterNode.get();
    if (!node) {
      return;
    }
    const bool appliedLocally = session_->applyNodeSnapshot(command.nodeId, command.nodeType, command.nodeIndex, *node, true);
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = replacedNodeCursor(*session_, command, storedCursor);
    if (!cursor.isValid()) {
      cursor = remapSnapshotCursor(storedCursor);
    }
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (appliedLocally) {
      requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    } else {
      brushQueue_.requestFullRefresh();
    }
    return;
  }

  if (transaction.isRemoveNodeCommand()) {
    const RemoveNodeCommand& command = transaction.removeNodeCommand();
    const QString replacement = undo ? command.delta.removedText : QString();
    const qsizetype replaceLength = undo ? 0 : command.delta.removedText.size();
    const bool applied = applyTextReplacement(*session_, command.delta.start, replaceLength, replacement, command.affectedNodes);
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = remapSnapshotCursor(storedCursor);
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (applied) {
      requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    }
    return;
  }

  if (transaction.isSetNodeAttrCommand()) {
    const SetNodeAttrCommand& command = transaction.setNodeAttrCommand();
    MarkdownNode* currentNode = nodeByIdOrIndex(*session_, command.nodeId, command.nodeType, command.nodeIndex);
    if (!currentNode) {
      return;
    }
    auto nextNode = currentNode->clone(CloneMode::PreserveIds);
    applyNodeAttribute(*nextNode, command.attribute, undo ? command.beforeValue : command.afterValue);
    const bool appliedLocally = session_->applyNodeSnapshot(command.nodeId, command.nodeType, command.nodeIndex, *nextNode, true);
    const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
    CursorPosition cursor = remapSnapshotCursor(storedCursor);
    if (cursor.isValid()) {
      selection_.setCursorPosition(cursor);
    } else {
      selection_.clear();
    }
    if (appliedLocally) {
      requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
    } else {
      brushQueue_.requestFullRefresh();
    }
    return;
  }

  if (!transaction.isTextDeltaCommand()) {
    return;
  }

  const TextDeltaCommand& command = transaction.textDeltaCommand();
  const TextDelta& delta = command.delta;
  const QString replacement = undo ? delta.removedText : delta.insertedText;
  const qsizetype replaceStart = delta.start;
  const qsizetype replaceEnd = delta.start + (undo ? delta.insertedText.size() : delta.removedText.size());
  QVector<LocalEditNodeHint> nodeHints;
  for (NodeId nodeId : command.affectedNodes) {
    nodeHints.push_back(LocalEditNodeHint{nodeId, replaceStart, BlockType::Unknown});
  }
  const bool appliedLocally = session_->applyTextDelta(replaceStart, replaceEnd - replaceStart, replacement, true, std::move(nodeHints));
  const CursorPosition storedCursor = undo ? command.beforeCursor : command.afterCursor;
  CursorPosition cursor = tableCellTextCursor(*session_, storedCursor);
  if (!cursor.isValid()) {
    cursor = remapSnapshotCursor(storedCursor);
  }
  if (cursor.isValid()) {
    selection_.setCursorPosition(cursor);
  } else {
    selection_.clear();
  }

  if (!appliedLocally) {
    QString text = session_->markdownText();
    if (replaceStart < 0 || replaceEnd < replaceStart || replaceEnd > text.size()) {
      return;
    }
    text.replace(replaceStart, replaceEnd - replaceStart, replacement);
    session_->applyMarkdownText(std::move(text), true);
    brushQueue_.requestFullRefresh();
  } else {
    requestRefreshForNodes(brushQueue_, *session_, command.affectedNodes, cursor);
  }
}

CursorPosition EditorController::remapSnapshotCursor(const CursorPosition& snapshotCursor) const {
  CursorPosition cursor;
  if (!session_ || !snapshotCursor.isValid()) {
    return cursor;
  }

  BlockEditContextResolver resolver(const_cast<DocumentSession*>(session_), const_cast<SelectionController*>(&selection_));
  if (snapshotCursor.text.sourceOffset >= 0) {
    if (MarkdownNode* node = resolver.nodeAtContentSourceOffset(session_->document().root(), snapshotCursor.text.sourceOffset)) {
      BlockEditContext context;
      if (resolver.fill(*node, context)) {
        const qsizetype localSourceOffset =
            qBound<qsizetype>(0, snapshotCursor.text.sourceOffset - context.contentRange.byteStart, context.contentText.size());
        qsizetype visibleOffset = -1;
        if (!context.inlineProjection.visibleOffsetForSourceOffset(localSourceOffset, visibleOffset)) {
          visibleOffset = qBound<qsizetype>(0, localSourceOffset, context.visibleText.size());
        }
        cursor.blockId = node->id();
        cursor.text.nodeId = context.editableNode ? context.editableNode->id() : node->id();
        cursor.text.textOffset = visibleOffset;
        cursor.text.sourceOffset = snapshotCursor.text.sourceOffset;
        cursor.text.inMeta = snapshotCursor.text.inMeta;
        return cursor;
      }
    }
  }

  if (MarkdownNode* node = session_->document().node(snapshotCursor.blockId)) {
    BlockEditContext context;
    if (resolver.fill(*node, context)) {
      const qsizetype visibleOffset = qBound<qsizetype>(0, snapshotCursor.text.textOffset, context.visibleText.size());
      qsizetype localSourceOffset = -1;
      context.inlineProjection.sourceOffsetForVisibleOffset(visibleOffset, localSourceOffset);
      cursor.blockId = node->id();
      cursor.text.nodeId = context.editableNode ? context.editableNode->id() : node->id();
      cursor.text.textOffset = visibleOffset;
      cursor.text.sourceOffset = localSourceOffset >= 0 ? context.contentRange.byteStart + localSourceOffset : snapshotCursor.text.sourceOffset;
      cursor.text.inMeta = snapshotCursor.text.inMeta;
      return cursor;
    }
    cursor.blockId = node->id();
    cursor.text.nodeId = node->id();
    cursor.text.textOffset = snapshotCursor.text.textOffset;
    cursor.text.sourceOffset = snapshotCursor.text.sourceOffset;
    cursor.text.inMeta = snapshotCursor.text.inMeta;
    return cursor;
  }

  const QString markdown = session_->markdownText();
  const qsizetype sourceOffset = qBound<qsizetype>(0, snapshotCursor.text.sourceOffset >= 0 ? snapshotCursor.text.sourceOffset : markdown.size(), markdown.size());
  if (MarkdownNode* node = resolver.nodeAtContentSourceOffset(session_->document().root(), sourceOffset)) {
    BlockEditContext context;
    if (resolver.fill(*node, context)) {
      const qsizetype localSourceOffset = qBound<qsizetype>(0, sourceOffset - context.contentRange.byteStart, context.contentText.size());
      qsizetype visibleOffset = -1;
      context.inlineProjection.visibleOffsetForSourceOffset(localSourceOffset, visibleOffset);
      cursor.blockId = node->id();
      cursor.text.nodeId = context.editableNode ? context.editableNode->id() : node->id();
      cursor.text.textOffset = qBound<qsizetype>(0, visibleOffset, context.visibleText.size());
      cursor.text.sourceOffset = context.contentRange.byteStart + localSourceOffset;
      cursor.text.inMeta = snapshotCursor.text.inMeta;
      return cursor;
    }
  }
  return cursor;
}

}  // namespace muffin
