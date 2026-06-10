#include "editor/EditorController.h"

#include "html/HtmlBox.h"
#include "blocks/literal/LiteralBlockUtil.h"
#include "projection/InlineProjection.h"
#include "document/MarkdownNode.h"
#include "document/SourceRangeUtil.h"
#include "editor/BlockEditContext.h"
#include "editor/EditorView.h"

namespace muffin {
namespace {

LiteralBlockSpec frontMatterSpec() {
  return LiteralBlockSpec{
      BlockType::FrontMatter,
      HitTestResult::Zone::FrontMatter,
      QStringLiteral("No front matter is active."),
      QStringLiteral("Edit Front Matter"),
      QStringLiteral("Backspace Front Matter"),
      QStringLiteral("Delete Front Matter Text"),
      QStringLiteral("Delete Front Matter Selection"),
      QStringLiteral("Set Front Matter Content"),
      QStringLiteral("  ")};
}

LiteralBlockSpec htmlSpec() {
  return LiteralBlockSpec{
      BlockType::HtmlBlock,
      HitTestResult::Zone::Html,
      QStringLiteral("No HTML block is active."),
      QStringLiteral("Edit HTML Block"),
      QStringLiteral("Backspace HTML Block"),
      QStringLiteral("Delete HTML Block Text"),
      QStringLiteral("Delete HTML Block Selection"),
      QStringLiteral("Set HTML Block"),
      QStringLiteral("  ")};
}

LiteralBlockSpec mathSpec() {
  return LiteralBlockSpec{
      BlockType::MathBlock,
      HitTestResult::Zone::Math,
      QStringLiteral("No math block is active."),
      QStringLiteral("Edit Math Block"),
      QStringLiteral("Backspace Math Block"),
      QStringLiteral("Delete Math Block Text"),
      QStringLiteral("Delete Math Block Selection"),
      QStringLiteral("Set Math Block TeX"),
      QStringLiteral("  ")};
}

MarkdownNode* primaryParagraphOrSelf(MarkdownNode& node) {
  if (node.type() == BlockType::ListItem) {
    for (const auto& child : node.children()) {
      if (child->type() == BlockType::Paragraph) {
        return child.get();
      }
    }
  }
  return &node;
}

qsizetype selectableTextLength(const MarkdownNode& node) {
  switch (node.type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::TableCell:
      return InlineProjection::plainTextForInlines(node.inlines()).size();
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

CursorPosition cursorForNodeText(const MarkdownNode& node, qsizetype offset, NodeId blockId = {}) {
  CursorPosition cursor;
  cursor.blockId = blockId.isValid() ? blockId : node.id();
  cursor.text.nodeId = node.id();
  cursor.text.textOffset = qBound<qsizetype>(0, offset, selectableTextLength(node));
  if (node.type() == BlockType::LinkDefinition || node.type() == BlockType::FootnoteDefinition) {
    const DefinitionBlock definition = node.definition();
    if (definition.markerRange.isValid()) {
      cursor.text.sourceOffset = definition.markerRange.start + cursor.text.textOffset;
    }
  }
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

  if ((node->type() == BlockType::LinkDefinition || node->type() == BlockType::FootnoteDefinition) && hit.sourceOffset >= 0) {
    const DefinitionBlock definition = node->definition();
    const qsizetype blockStart = definition.markerRange.isValid() ? definition.markerRange.start : node->sourceRange().byteStart;
    hit.textNodeId = node->id();
    hit.textOffset = qMax<qsizetype>(0, hit.sourceOffset - blockStart);
    return true;
  }

  MarkdownNode* editable = primaryParagraphOrSelf(*node);
  if (!editable || (editable->type() != BlockType::Paragraph && editable->type() != BlockType::Heading &&
                    editable->type() != BlockType::TableCell)) {
    return false;
  }

  const SourceRange range = editable->sourceRange();
  const QString markdown = session.markdownText();
  qsizetype start = range.byteEnd > range.byteStart
                     ? range.byteStart
                     : sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = range.byteEnd > range.byteStart
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
  InlineProjection projection(editable->inlines(), contentText, projectionState, start);
  if (hit.sourceOffset >= 0) {
    localSourceOffset = qBound<qsizetype>(0, hit.sourceOffset - start, contentText.size());
  } else if (!projection.sourceOffsetForVisibleOffset(hit.textOffset, localSourceOffset)) {
    localSourceOffset = qBound<qsizetype>(0, hit.textOffset, contentText.size());
  }
  hit.sourceOffset = start + localSourceOffset;
  return true;
}

}  // namespace

EditorController::EditorController(QObject* parent)
    : QObject(parent),
      frontMatterLiteral_(frontMatterSpec()),
      htmlLiteral_(htmlSpec()),
      mathLiteral_(mathSpec()) {
  frontMatterLiteral_.setRejectedHandler([this](QString reason) { emit frontMatterCommandRejected(std::move(reason)); });
  htmlLiteral_.setRejectedHandler([this](QString reason) { emit htmlCommandRejected(std::move(reason)); });
  mathLiteral_.setRejectedHandler([this](QString reason) { emit mathCommandRejected(std::move(reason)); });
}

void EditorController::attach(DocumentSession* session, EditorView* view) {
  if (session_ == session && view_ == view) {
    return;
  }

  detach();
  session_ = session;
  view_ = view;

  const EditorContext ctx{session_, &selection_, &undoStack_, &brushQueue_, view_,
      {{static_cast<int>(BlockType::FrontMatter), &frontMatterLiteral_},
       {static_cast<int>(BlockType::HtmlBlock),   &htmlLiteral_},
       {static_cast<int>(BlockType::MathBlock),   &mathLiteral_}}};

  inputController_.setContext(ctx);
  inputController_.setCodeFenceController(&codeFenceController_);
  inputController_.setTableController(&tableController_);
  stylizeController_.setContext(ctx);
  paragraphController_.setContext(ctx);
  frontMatterLiteral_.setContext(ctx);
  codeFenceController_.setContext(ctx);
  htmlLiteral_.setContext(ctx);
  mathLiteral_.setContext(ctx);
  tableController_.setContext(ctx);
  clipboardController_.setContext(ctx);
  clipboardController_.setInputController(&inputController_);

  if (view_) {
    connect(view_, &EditorView::blockClicked, this, &EditorController::activateHit);
    connect(view_, &EditorView::selectionChanged, &selection_, &SelectionController::setSelection);
    connect(view_, &EditorView::textCommitted, &inputController_, &InputController::insertText);
    connect(view_, &EditorView::htmlEditToggleRequested, this, [this](NodeId blockId) {
      if (!blockId.isValid()) {
        return;
      }
      if (htmlLiteral_.isEditing() && htmlLiteral_.currentBlockId() == blockId) {
        htmlLiteral_.exitEditMode();
        if (view_) {
          view_->setEditingHtmlBlock({});
        }
        emit stateChanged();
        return;
      }

      exitAllLiteralEditModes();
      HitTestResult hit;
      hit.zone = HitTestResult::Zone::Html;
      hit.blockId = blockId;
      hit.textNodeId = blockId;
      selection_.setHitResult(hit);
      if (htmlLiteral_.enterEditMode() && view_) {
        view_->setEditingHtmlBlock(htmlLiteral_.currentBlockId());
      }
      emit stateChanged();
    });
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
      view_->setDocument(session_->document(), session_->filePath());
      return;
    }
    if (request.topLevelRangeDirty.isValid()) {
      if (request.topLevelRangeDirty.documentRevision != session_->document().revision() || !view_->refreshTopLevelRange(request.topLevelRangeDirty, session_->document())) {
        view_->setDocument(session_->document(), session_->filePath());
        return;
      }
      // The range refresh handled structural changes.  Still refresh any
      // blocks that were marked dirty before the range arrived — they may
      // fall outside the structural-change range and would otherwise be
      // silently dropped.
      if (!request.layoutDirtyBlocks.isEmpty()) {
        if (request.layoutDirtyBlocks.size() == 1) {
          if (!view_->refreshBlock(request.layoutDirtyBlocks.first(), session_->document())) {
            view_->setDocument(session_->document(), session_->filePath());
          }
        } else if (!view_->refreshBlocks(request.layoutDirtyBlocks, session_->document())) {
          view_->setDocument(session_->document(), session_->filePath());
        }
      }
      return;
    }
    if (request.layoutDirtyBlocks.size() == 1) {
      if (!view_->refreshBlock(request.layoutDirtyBlocks.first(), session_->document())) {
        view_->setDocument(session_->document(), session_->filePath());
      }
      return;
    }
    if (!request.layoutDirtyBlocks.isEmpty() && !view_->refreshBlocks(request.layoutDirtyBlocks, session_->document())) {
      view_->setDocument(session_->document(), session_->filePath());
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
  inputController_.setContext(EditorContext{});
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

LiteralBlockController& EditorController::frontMatterLiteral() {
  return frontMatterLiteral_;
}

CodeFenceController& EditorController::codeFenceController() {
  return codeFenceController_;
}

LiteralBlockController& EditorController::htmlLiteral() {
  return htmlLiteral_;
}

LiteralBlockController& EditorController::mathLiteral() {
  return mathLiteral_;
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

ParagraphController& EditorController::paragraphController() {
  return paragraphController_;
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

CursorFormatState EditorController::queryCursorFormatState() const {
  CursorFormatState state;
  if (!session_ || !selection_.hasCursor()) {
    return state;
  }

  BlockEditContextResolver resolver(
      const_cast<DocumentSession*>(session_),
      const_cast<SelectionController*>(&selection_));
  BlockEditContext context;
  if (!resolver.current(context) || !context.inlineProjection.isValid()) {
    return state;
  }

  const qsizetype offset = context.cursorTextOffset;
  const auto& spans = context.inlineProjection.spans();

  for (const auto& span : spans) {
    if (span.kind == InlineSpanKind::OpenMarker || span.kind == InlineSpanKind::CloseMarker ||
        span.kind == InlineSpanKind::HiddenSyntax) {
      continue;
    }
    if (offset >= span.visibleStart && offset <= span.visibleEnd) {
      if (span.bold) state.bold = true;
      if (span.italic) state.italic = true;
      if (span.strike) state.strikethrough = true;
      if (span.type == InlineType::Code && span.kind == InlineSpanKind::Text) state.code = true;
      if (span.type == InlineType::InlineMath && span.kind == InlineSpanKind::Text) state.inlineMath = true;
    }
  }

  // Check HTML inline format data for underline
  for (const auto& hd : context.inlineProjection.htmlFormatData()) {
    for (const auto& fs : hd.formatSpans) {
      const bool hasUnderline = (static_cast<int>(fs.decoration) & static_cast<int>(html::HtmlTextDecoration::Underline)) != 0;
      if (!hasUnderline) {
        continue;
      }
      const qsizetype spanDisplayStart = hd.displayStart + fs.start;
      const qsizetype spanDisplayEnd = spanDisplayStart + fs.length;
      for (const auto& projSpan : spans) {
        if (projSpan.kind == InlineSpanKind::HtmlContent &&
            projSpan.displayStart >= spanDisplayStart &&
            projSpan.displayEnd <= spanDisplayEnd &&
            offset >= projSpan.visibleStart && offset <= projSpan.visibleEnd) {
          state.underline = true;
        }
      }
    }
  }

  return state;
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
  frontMatterLiteral_.exitEditMode();
  codeFenceController_.exitEditMode();
  htmlLiteral_.exitEditMode();
  mathLiteral_.exitEditMode();
  if (view_) {
    view_->setEditingHtmlBlock({});
  }
}

bool EditorController::enterLiteralEditMode(HitTestResult::Zone zone) {
  switch (zone) {
    case HitTestResult::Zone::Code:
      return codeFenceController_.enterEditMode();
    default: {
      LiteralBlockController* ctrl = literalForZone(zone);
      return ctrl ? ctrl->enterEditMode() : false;
    }
  }
}

LiteralBlockController* EditorController::literalForZone(HitTestResult::Zone zone) {
  switch (zone) {
    case HitTestResult::Zone::FrontMatter:
      return &frontMatterLiteral_;
    case HitTestResult::Zone::Html:
      return &htmlLiteral_;
    case HitTestResult::Zone::Math:
      return &mathLiteral_;
    default:
      return nullptr;
  }
}

bool EditorController::insertFrontMatter(FrontMatterFormat format) {
  const EditorContext ctx{session_, &selection_, &undoStack_, &brushQueue_};
  return muffin::insertFrontMatter(ctx, frontMatterLiteral_, format);
}

bool EditorController::enterHtmlEditMode() {
  if (!htmlLiteral_.enterEditMode()) {
    return false;
  }
  if (view_) {
    view_->setEditingHtmlBlock(htmlLiteral_.currentBlockId());
  }
  emit stateChanged();
  return true;
}

bool EditorController::exitHtmlEditMode() {
  const bool changed = htmlLiteral_.isEditing();
  if (!htmlLiteral_.exitEditMode()) {
    return false;
  }
  if (view_) {
    view_->setEditingHtmlBlock({});
  }
  if (changed) {
    emit stateChanged();
  }
  return true;
}

QString EditorController::sanitizedHtmlPreview() const {
  return muffin::sanitizedHtmlPreview(htmlLiteral_);
}

bool EditorController::isOnImage() const {
  return !imageSrcAtCursor().isEmpty();
}

QString EditorController::imageSrcAtCursor() const {
  if (!session_ || !selection_.hasCursor()) {
    return {};
  }
  BlockEditContextResolver resolver(const_cast<DocumentSession*>(session_), const_cast<SelectionController*>(&selection_));
  BlockEditContext context;
  if (!resolver.current(context) || !context.inlineProjection.isValid() || !context.editableNode) {
    return {};
  }
  const qsizetype offset = context.cursorTextOffset;
  for (const auto& span : context.inlineProjection.spans()) {
    if (span.type != InlineType::Image || span.kind != InlineSpanKind::Atom) {
      continue;
    }
    if (offset >= span.visibleStart && offset <= span.visibleEnd) {
      // Match this span to an InlineNode by source range
      for (const auto& inlineNode : context.editableNode->inlines()) {
        if (inlineNode.type() == InlineType::Image &&
            inlineNode.sourceRanges().source.start <= span.sourceStart &&
            inlineNode.sourceRanges().source.end >= span.sourceEnd) {
          return inlineNode.href();
        }
      }
    }
  }
  return {};
}

bool EditorController::imageSourceRangeAtCursor(qsizetype& outStart, qsizetype& outEnd) const {
  if (!session_ || !selection_.hasCursor()) {
    return false;
  }
  BlockEditContextResolver resolver(const_cast<DocumentSession*>(session_), const_cast<SelectionController*>(&selection_));
  BlockEditContext context;
  if (!resolver.current(context) || !context.inlineProjection.isValid() || !context.editableNode) {
    return false;
  }
  const qsizetype offset = context.cursorTextOffset;
  for (const auto& span : context.inlineProjection.spans()) {
    if (span.type != InlineType::Image || span.kind != InlineSpanKind::Atom) {
      continue;
    }
    if (offset >= span.visibleStart && offset <= span.visibleEnd) {
      for (const auto& inlineNode : context.editableNode->inlines()) {
        if (inlineNode.type() == InlineType::Image &&
            inlineNode.sourceRanges().source.start <= span.sourceStart &&
            inlineNode.sourceRanges().source.end >= span.sourceEnd) {
          outStart = inlineNode.sourceStart();
          outEnd = inlineNode.sourceEnd();
          return true;
        }
      }
    }
  }
  return false;
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

  // Preserve HTML edit mode when clicking within the editing block — the click
  // is for cursor repositioning in the source text, not for exiting edit mode.
  const bool preserveHtmlEdit = hit.zone == HitTestResult::Zone::Html &&
                                htmlLiteral_.isEditing() &&
                                htmlLiteral_.currentBlockId() == hit.blockId;
  if (!preserveHtmlEdit) {
    exitAllLiteralEditModes();
  }
  selection_.setHitResult(hit);
  if (hit.zone != HitTestResult::Zone::Html && enterLiteralEditMode(hit.zone)) {
    selection_.setHitResult(hit);
  }
}

}  // namespace muffin
