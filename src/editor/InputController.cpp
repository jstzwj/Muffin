#include "editor/InputController.h"

#include "app/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/EditorView.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"
#include "parser/CmarkGfmParser.h"

#include <QEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>

namespace muffin {
namespace {

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
    case BlockType::Document:
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
    case BlockType::List:
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
      return plainTextForInlines(node.inlines());
    default:
      return node.literal();
  }
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
  if ((watched == view_ || (view_ && watched == view_->viewport())) && event->type() == QEvent::KeyPress) {
    return handleKeyPress(static_cast<QKeyEvent*>(event));
  }
  return QObject::eventFilter(watched, event);
}

bool InputController::insertText(QString text) {
  if (selection_ && selection_->hasCursor() && !selection_->selection().isCollapsed()) {
    return replaceSelection(std::move(text), EditTransaction::Kind::InsertText, QStringLiteral("Replace Selection"));
  }
  return text.isEmpty() ? false : editParagraph(Operation::InsertText, std::move(text));
}

bool InputController::insertParagraphBreak() {
  return editParagraph(Operation::Enter);
}

bool InputController::deleteBackward() {
  if (selection_ && selection_->hasCursor() && !selection_->selection().isCollapsed()) {
    return deleteSelection();
  }
  return editParagraph(Operation::Backspace);
}

bool InputController::deleteForward() {
  if (selection_ && selection_->hasCursor() && !selection_->selection().isCollapsed()) {
    return deleteSelection();
  }
  return editParagraph(Operation::Delete);
}

bool InputController::indentListItem() {
  ParagraphEditContext context;
  if (!paragraphContext(context) || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }

  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return false;
  }

  QString nextDocument = session_->markdownText();
  nextDocument.insert(lineStart, QStringLiteral("  "));
  applyEdit(EditTransaction::Kind::InsertText, QStringLiteral("Indent List Item"), std::move(nextDocument),
            context.sourceStart + context.cursorOffset + 2);
  return true;
}

bool InputController::outdentListItem() {
  ParagraphEditContext context;
  if (!paragraphContext(context) || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }
  return outdentListItem(context);
}

bool InputController::deleteSelection() {
  return replaceSelection(QString(), EditTransaction::Kind::DeleteText, QStringLiteral("Delete Selection"));
}

bool InputController::hasEditableSelection() const {
  qsizetype start = 0;
  qsizetype end = 0;
  return selectionSourceRange(start, end);
}

QString InputController::selectedText() const {
  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  if (selectionSourceRange(sourceStart, sourceEnd)) {
    return plainTextForMarkdownRange(sourceStart, sourceEnd);
  }
  return {};
}

QString InputController::selectedMarkdown() const {
  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  if (!selectionSourceRange(sourceStart, sourceEnd)) {
    return {};
  }
  return session_->markdownText().mid(sourceStart, sourceEnd - sourceStart);
}

bool InputController::replaceSelection(QString text, EditTransaction::Kind kind, QString label) {
  ParagraphEditContext context;
  qsizetype start = 0;
  qsizetype end = 0;
  if (selectionContext(context, start, end)) {
    QString nextParagraph = context.sourceText;
    nextParagraph.replace(start, end - start, text);
    QString nextDocument = session_->markdownText();
    nextDocument.replace(context.sourceStart, context.sourceEnd - context.sourceStart, nextParagraph);
    applyEdit(kind, label, std::move(nextDocument), context.sourceStart + start + text.size());
    return true;
  }

  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  if (!selectionSourceRange(sourceStart, sourceEnd)) {
    emit unsupportedEditRequested(QStringLiteral("Only editable text selection is supported in this M4 slice."));
    return false;
  }

  QString nextDocument = session_->markdownText();
  nextDocument.replace(sourceStart, sourceEnd - sourceStart, text);
  applyEdit(kind, label, std::move(nextDocument), sourceStart + text.size());
  return true;
}

QString InputController::plainTextForMarkdownRange(qsizetype start, qsizetype end) const {
  if (!session_ || start < 0 || end <= start) {
    return {};
  }

  const QString markdown = session_->markdownText().mid(start, end - start);
  ParseResult result = CmarkGfmParser().parseDocument(QStringView(markdown), ParseOptions());
  if (!result.root) {
    return markdown;
  }
  QString text = plainTextForNode(*result.root);
  while (text.endsWith(QLatin1Char('\n'))) {
    text.chop(1);
  }
  return text;
}

bool InputController::handleInputMethod(QInputMethodEvent* event) {
  if (!event || event->commitString().isEmpty()) {
    return false;
  }
  return insertText(event->commitString());
}

bool InputController::handleKeyPress(QKeyEvent* event) {
  if (!session_ || !selection_ || !selection_->hasCursor() || !view_ || event->modifiers().testFlag(Qt::ControlModifier) ||
      event->modifiers().testFlag(Qt::AltModifier)) {
    return false;
  }

  switch (event->key()) {
    case Qt::Key_Tab:
      return event->modifiers().testFlag(Qt::ShiftModifier) ? outdentListItem() : indentListItem();
    case Qt::Key_Backspace:
      return deleteBackward();
    case Qt::Key_Delete:
      return deleteForward();
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return insertParagraphBreak();
    default: {
      const QString text = printableText(event);
      return insertText(text);
    }
  }
}

bool InputController::editParagraph(Operation operation, QString text) {
  ParagraphEditContext context;
  if (!paragraphContext(context)) {
    emit unsupportedEditRequested(QStringLiteral("Only plain paragraph, heading, and list item text is editable in this M4 slice."));
    return false;
  }

  QString nextParagraph = context.sourceText;
  qsizetype nextOffset = qBound<qsizetype>(0, context.cursorOffset, nextParagraph.size());
  EditTransaction::Kind kind = EditTransaction::Kind::InsertText;
  QString label = QStringLiteral("Insert Text");

  switch (operation) {
    case Operation::InsertText:
      nextParagraph.insert(nextOffset, text);
      nextOffset += text.size();
      kind = EditTransaction::Kind::InsertText;
      label = QStringLiteral("Insert Text");
      break;
    case Operation::Backspace:
      if (nextOffset <= 0) {
        if (context.node && context.node->type() == BlockType::ListItem) {
          return outdentListItem(context);
        }
        return mergeWithPreviousParagraph(context);
      }
      nextParagraph.remove(nextOffset - 1, 1);
      --nextOffset;
      kind = EditTransaction::Kind::DeleteText;
      label = QStringLiteral("Backspace");
      break;
    case Operation::Delete:
      if (nextOffset >= nextParagraph.size()) {
        return mergeWithNextParagraph(context);
      }
      nextParagraph.remove(nextOffset, 1);
      kind = EditTransaction::Kind::DeleteText;
      label = QStringLiteral("Delete");
      break;
    case Operation::Enter:
      if (context.node && context.node->type() == BlockType::ListItem) {
        return context.sourceText.trimmed().isEmpty() ? exitListItem(context) : splitListItem(context);
      }
      if (nextOffset < nextParagraph.size() && nextParagraph.at(nextOffset).isSpace()) {
        nextParagraph.remove(nextOffset, 1);
      } else if (nextOffset > 0 && nextParagraph.at(nextOffset - 1).isSpace()) {
        nextParagraph.remove(nextOffset - 1, 1);
        --nextOffset;
      }
      nextParagraph.insert(nextOffset, QStringLiteral("\n\n"));
      nextOffset += 2;
      kind = EditTransaction::Kind::SplitParagraph;
      label = QStringLiteral("Split Paragraph");
      break;
  }

  QString nextDocument = session_->markdownText();
  const qsizetype nextSourceOffset = context.sourceStart + nextOffset;
  nextDocument.replace(context.sourceStart, context.sourceEnd - context.sourceStart, nextParagraph);
  applyEdit(kind, label, std::move(nextDocument), nextSourceOffset);
  return true;
}

bool InputController::mergeWithPreviousParagraph(const ParagraphEditContext& context) {
  ParagraphEditContext previous;
  if (!previousPlainParagraph(*context.node, previous)) {
    return true;
  }

  QString nextDocument = session_->markdownText();
  const qsizetype separatorStart = previous.sourceEnd;
  const qsizetype separatorLength = qMax<qsizetype>(0, context.sourceStart - previous.sourceEnd);
  const qsizetype nextSourceOffset = previous.sourceStart + previous.sourceText.size();
  nextDocument.replace(separatorStart, separatorLength, QStringLiteral(" "));
  applyEdit(EditTransaction::Kind::DeleteText, QStringLiteral("Merge Paragraphs"), std::move(nextDocument), nextSourceOffset);
  return true;
}

bool InputController::mergeWithNextParagraph(const ParagraphEditContext& context) {
  ParagraphEditContext next;
  if (!nextPlainParagraph(*context.node, next)) {
    return true;
  }

  QString nextDocument = session_->markdownText();
  const qsizetype separatorStart = context.sourceEnd;
  const qsizetype separatorLength = qMax<qsizetype>(0, next.sourceStart - context.sourceEnd);
  const qsizetype nextSourceOffset = context.sourceStart + context.sourceText.size();
  nextDocument.replace(separatorStart, separatorLength, QStringLiteral(" "));
  applyEdit(EditTransaction::Kind::DeleteText, QStringLiteral("Merge Paragraphs"), std::move(nextDocument), nextSourceOffset);
  return true;
}

bool InputController::splitListItem(const ParagraphEditContext& context) {
  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return false;
  }

  const QString markdown = session_->markdownText();
  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const QString marker = listMarkerFor(line);
  if (marker.isEmpty()) {
    return false;
  }

  QString nextDocument = markdown;
  const qsizetype splitOffset = context.sourceStart + qBound<qsizetype>(0, context.cursorOffset, context.sourceText.size());
  const qsizetype markerColumn = qMax<qsizetype>(0, contentStart - lineStart - marker.size());
  QString nextMarker = marker;
  qsizetype digitCount = 0;
  while (digitCount < marker.size() && marker.at(digitCount).isDigit()) {
    ++digitCount;
  }
  if (digitCount > 0) {
    const int nextNumber = marker.left(digitCount).toInt() + 1;
    nextMarker = QStringLiteral("%1. ").arg(nextNumber);
  }
  const QString insertion = QLatin1Char('\n') + QString(markerColumn, QLatin1Char(' ')) + nextMarker;
  nextDocument.insert(splitOffset, insertion);
  applyEdit(EditTransaction::Kind::SplitParagraph, QStringLiteral("Split List Item"), std::move(nextDocument),
            splitOffset + insertion.size());
  return true;
}

bool InputController::exitListItem(const ParagraphEditContext& context) {
  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return false;
  }

  QString nextDocument = session_->markdownText();
  const qsizetype removeStart = lineStart;
  const qsizetype removeEnd = lineEnd;
  nextDocument.remove(removeStart, removeEnd - removeStart);
  nextDocument.insert(removeStart, QLatin1Char('\n'));
  applyEdit(EditTransaction::Kind::DeleteText, QStringLiteral("Exit List Item"), std::move(nextDocument), removeStart);
  return true;
}

bool InputController::outdentListItem(const ParagraphEditContext& context) {
  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return false;
  }

  QString nextDocument = session_->markdownText();
  const qsizetype indent = qMax<qsizetype>(0, contentStart - lineStart - listMarkerFor(nextDocument.mid(lineStart, lineEnd - lineStart)).size());
  if (indent >= 2) {
    nextDocument.remove(lineStart, 2);
    applyEdit(EditTransaction::Kind::DeleteText, QStringLiteral("Outdent List Item"), std::move(nextDocument),
              qMax<qsizetype>(lineStart, context.sourceStart + context.cursorOffset - 2));
    return true;
  }

  nextDocument.remove(lineStart, contentStart - lineStart);
  applyEdit(EditTransaction::Kind::DeleteText, QStringLiteral("Exit List Item"), std::move(nextDocument), lineStart);
  return true;
}

bool InputController::listItemLineBounds(
    const ParagraphEditContext& context,
    qsizetype& lineStart,
    qsizetype& contentStart,
    qsizetype& lineEnd) const {
  if (!session_ || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }

  const QString markdown = session_->markdownText();
  lineStart = context.sourceStart;
  while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  lineEnd = context.sourceStart;
  while (lineEnd < markdown.size() && markdown.at(lineEnd) != QLatin1Char('\n')) {
    ++lineEnd;
  }

  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const QString marker = listMarkerFor(line);
  if (marker.isEmpty()) {
    return false;
  }
  contentStart = line.indexOf(marker) + marker.size() + lineStart;
  return contentStart >= lineStart && contentStart <= lineEnd;
}

QString InputController::listMarkerFor(const QString& line) const {
  qsizetype index = 0;
  while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
    ++index;
  }
  if (index + 2 <= line.size() && (line.at(index) == QLatin1Char('-') || line.at(index) == QLatin1Char('*') ||
                                   line.at(index) == QLatin1Char('+')) &&
      line.at(index + 1).isSpace()) {
    return line.mid(index, 2);
  }

  qsizetype numberStart = index;
  while (index < line.size() && line.at(index).isDigit()) {
    ++index;
  }
  if (index > numberStart && index + 2 <= line.size() && line.at(index) == QLatin1Char('.') && line.at(index + 1).isSpace()) {
    return line.mid(numberStart, index - numberStart + 2);
  }
  return {};
}

bool InputController::paragraphContext(ParagraphEditContext& context) const {
  if (!session_ || !selection_ || !selection_->hasCursor()) {
    return false;
  }

  return paragraphContextFor(selection_->cursorPosition().blockId, context);
}

bool InputController::paragraphContextFor(NodeId blockId, ParagraphEditContext& context) const {
  if (!session_) {
    return false;
  }

  MarkdownNode* node = session_->document().node(blockId);
  if (!node ||
      (node->type() != BlockType::Paragraph && node->type() != BlockType::Heading && node->type() != BlockType::ListItem)) {
    return false;
  }

  return fillEditableContext(*node, context);
}

bool InputController::fillEditableContext(MarkdownNode& displayNode, ParagraphEditContext& context) const {
  MarkdownNode* editable = &displayNode;
  if (displayNode.type() == BlockType::ListItem) {
    editable = primaryParagraph(displayNode);
    if (!editable) {
      qsizetype lineStart = -1;
      qsizetype contentStart = -1;
      qsizetype lineEnd = -1;
      ParagraphEditContext markerContext;
      markerContext.node = &displayNode;
      markerContext.sourceStart = sourceOffsetForLineColumn(
          session_->markdownText(), displayNode.sourceRange().lineStart, qMax(1, displayNode.sourceRange().columnStart));
      if (markerContext.sourceStart >= 0 && listItemLineBounds(markerContext, lineStart, contentStart, lineEnd)) {
        context.node = &displayNode;
        context.editableNode = nullptr;
        context.sourceStart = contentStart;
        context.sourceEnd = lineEnd;
        context.cursorOffset = 0;
        context.sourceText = session_->markdownText().mid(contentStart, lineEnd - contentStart);
        return context.sourceText.trimmed().isEmpty();
      }
    }
  }

  if (!editable || (editable->type() != BlockType::Paragraph && editable->type() != BlockType::Heading)) {
    return false;
  }

  const SourceRange range = editable->sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return false;
  }

  const QString markdown = session_->markdownText();
  qsizetype start = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = sourceOffsetForLineEnd(markdown, range.lineEnd);
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
  }

  context.node = &displayNode;
  context.editableNode = editable;
  context.sourceStart = start;
  context.sourceEnd = end;
  context.cursorOffset = selection_ && selection_->hasCursor() && selection_->cursorPosition().blockId == displayNode.id()
                              ? selection_->cursorPosition().text.textOffset
                              : 0;
  context.sourceText = markdown.mid(start, end - start);
  return isPlainInlineEditable(*editable, context.sourceText);
}

bool InputController::selectionContext(ParagraphEditContext& context, qsizetype& start, qsizetype& end) const {
  if (!selection_ || !selection_->hasCursor()) {
    return false;
  }

  const SelectionRange range = selection_->selection();
  if (!range.isSingleBlock() || range.isCollapsed()) {
    return false;
  }
  if (!paragraphContextFor(range.focus.blockId, context)) {
    return false;
  }

  start = qBound<qsizetype>(0, range.startOffset(), context.sourceText.size());
  end = qBound<qsizetype>(0, range.endOffset(), context.sourceText.size());
  return start < end;
}

bool InputController::selectionSourceRange(qsizetype& start, qsizetype& end) const {
  if (!selection_ || !selection_->hasCursor() || !session_) {
    return false;
  }

  const SelectionRange range = selection_->selection();
  if (range.isCollapsed()) {
    return false;
  }

  ParagraphEditContext anchor;
  ParagraphEditContext focus;
  if (!paragraphContextFor(range.anchor.blockId, anchor) || !paragraphContextFor(range.focus.blockId, focus)) {
    return false;
  }

  const qsizetype anchorOffset = anchor.sourceStart + qBound<qsizetype>(0, range.anchor.text.textOffset, anchor.sourceText.size());
  const qsizetype focusOffset = focus.sourceStart + qBound<qsizetype>(0, range.focus.text.textOffset, focus.sourceText.size());
  start = qMin(anchorOffset, focusOffset);
  end = qMax(anchorOffset, focusOffset);
  return start < end;
}

bool InputController::isPlainParagraph(const MarkdownNode& node, const QString& sourceText) const {
  return node.type() == BlockType::Paragraph && isPlainInlineEditable(node, sourceText);
}

bool InputController::isPlainInlineEditable(const MarkdownNode& node, const QString& sourceText) const {
  QString plain;
  for (const InlineNode& inlineNode : node.inlines()) {
    switch (inlineNode.type()) {
      case InlineType::Text:
        plain += inlineNode.text();
        break;
      case InlineType::SoftBreak:
        plain += QLatin1Char('\n');
        break;
      case InlineType::LineBreak:
        plain += QStringLiteral("  \n");
        break;
      default:
        return false;
    }
  }

  return plain == sourceText;
}

MarkdownNode* InputController::primaryParagraph(MarkdownNode& node) const {
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child.get();
    }
  }
  return nullptr;
}

MarkdownNode* InputController::previousPlainParagraph(const MarkdownNode& node, ParagraphEditContext& context) const {
  if (node.type() != BlockType::Paragraph) {
    return nullptr;
  }

  const MarkdownNode* previous = node.previousSibling();
  if (!previous || previous->type() != BlockType::Paragraph) {
    return nullptr;
  }
  return paragraphContextFor(previous->id(), context) ? context.node : nullptr;
}

MarkdownNode* InputController::nextPlainParagraph(const MarkdownNode& node, ParagraphEditContext& context) const {
  if (node.type() != BlockType::Paragraph) {
    return nullptr;
  }

  const MarkdownNode* next = node.nextSibling();
  if (!next || next->type() != BlockType::Paragraph) {
    return nullptr;
  }
  return paragraphContextFor(next->id(), context) ? context.node : nullptr;
}

qsizetype InputController::sourceOffsetForLineColumn(const QString& text, int line, int column) const {
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

qsizetype InputController::sourceOffsetForLineEnd(const QString& text, int line) const {
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

CursorPosition InputController::cursorFor(NodeId blockId, qsizetype offset) const {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = offset;
  return cursor;
}

CursorPosition InputController::cursorForSourceOffset(qsizetype sourceOffset) const {
  CursorPosition cursor;
  if (!session_) {
    return cursor;
  }

  MarkdownNode* node = paragraphAtSourceOffset(session_->document().root(), sourceOffset);
  if (!node) {
    return cursor;
  }

  ParagraphEditContext context;
  if (!fillEditableContext(*node, context)) {
    return cursor;
  }
  return cursorFor(node->id(), qBound<qsizetype>(0, sourceOffset - context.sourceStart, context.sourceText.size()));
}

MarkdownNode* InputController::paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset) const {
  if (node.type() == BlockType::Paragraph || node.type() == BlockType::Heading || node.type() == BlockType::ListItem) {
    const QString markdown = session_ ? session_->markdownText() : QString();
    ParagraphEditContext context;
    if (fillEditableContext(node, context) && sourceOffset >= context.sourceStart && sourceOffset <= context.sourceEnd) {
      return &node;
    }
  }

  for (const auto& child : node.children()) {
    if (MarkdownNode* found = paragraphAtSourceOffset(*child, sourceOffset)) {
      return found;
    }
  }
  return nullptr;
}

void InputController::applyEdit(
    EditTransaction::Kind kind,
    const QString& label,
    QString nextText,
    qsizetype nextSourceOffset) {
  const CursorPosition beforeCursor = selection_ && selection_->hasCursor() ? selection_->cursorPosition() : CursorPosition();
  const QString beforeText = session_->markdownText();

  session_->applyMarkdownText(nextText, true);
  CursorPosition nextCursor = cursorForSourceOffset(nextSourceOffset);
  if (selection_) {
    if (!nextCursor.isValid()) {
      nextCursor = cursorForSourceOffset(session_->markdownText().size());
    }
    selection_->setCursorPosition(nextCursor);
  }
  if (undoStack_) {
    undoStack_->push(EditTransaction(kind, label, {beforeText, beforeCursor}, {std::move(nextText), nextCursor}));
  }
  if (brushQueue_) {
    brushQueue_->requestBlockRefresh(nextCursor.blockId);
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
