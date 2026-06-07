#include "blocks/frontmatter/FrontMatterController.h"

#include "app/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/MarkdownSerializer.h"

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

QString defaultLiteralFor(FrontMatterFormat format) {
  switch (format) {
    case FrontMatterFormat::Yaml:
      return QStringLiteral("title: ");
    case FrontMatterFormat::Toml:
      return QStringLiteral("title = \"\"");
    case FrontMatterFormat::Json:
      return QStringLiteral("{\n  \"title\": \"\"\n}");
    case FrontMatterFormat::None:
    default:
      return {};
  }
}

}  // namespace

FrontMatterController::FrontMatterController(QObject* parent) : QObject(parent), literal_(frontMatterSpec()) {
  literal_.setRejectedHandler([this](QString reason) { emit frontMatterCommandRejected(std::move(reason)); });
}

void FrontMatterController::setContext(const EditorContext& ctx) {
  ctx_ = ctx;
  literal_.setContext(ctx);
}

NodeId FrontMatterController::currentFrontMatterId() const {
  return literal_.currentBlockId();
}

bool FrontMatterController::isEditing() const {
  return literal_.isEditing();
}

bool FrontMatterController::enterEditMode() {
  return literal_.enterEditMode();
}

bool FrontMatterController::exitEditMode() {
  return literal_.exitEditMode();
}

bool FrontMatterController::insertText(QString text) {
  return literal_.insertText(std::move(text));
}

bool FrontMatterController::deleteBackward() {
  return literal_.deleteBackward();
}

bool FrontMatterController::deleteForward() {
  return literal_.deleteForward();
}

bool FrontMatterController::deleteSelection() {
  return literal_.deleteSelection();
}

bool FrontMatterController::setContent(QString content) {
  return literal_.setContent(std::move(content));
}

bool FrontMatterController::insertFrontMatter(FrontMatterFormat format) {
  if (!ctx_.session || format == FrontMatterFormat::None) {
    return false;
  }

  const auto& blocks = ctx_.session->document().root().children();
  if (!blocks.empty() && blocks.front()->type() == BlockType::FrontMatter) {
    literal_.setEditingBlock(blocks.front()->id(), 0);
    if (ctx_.selection) {
      ctx_.selection->setCursorPosition(literal_.cursorFor(blocks.front()->id(), blocks.front()->literal().size()));
    }
    if (ctx_.brushQueue) {
      ctx_.brushQueue->requestBlockRefresh(blocks.front()->id());
    }
    return true;
  }

  const CursorPosition beforeCursor = ctx_.selection ? ctx_.selection->cursorPosition() : CursorPosition();
  const bool documentWasEmpty = ctx_.session->markdownText().isEmpty();
  auto node = std::make_unique<MarkdownNode>(BlockType::FrontMatter);
  node->setFrontMatterFormat(format);
  node->setLiteral(defaultLiteralFor(format));
  const NodeId insertedId = node->id();
  auto commandNode = node->clone(CloneMode::PreserveIds);

  MarkdownSerializer serializer;
  QString insertedText = serializer.serializeBlock(*node);
  if (!documentWasEmpty) {
    insertedText += QStringLiteral("\n\n");
  }

  if (!ctx_.session->applyInsertedNode(insertedId, BlockType::FrontMatter, 0, 0, 0, insertedText, true)) {
    return false;
  }

  literal_.setEditingBlock(insertedId, 0);
  const CursorPosition nextCursor = literal_.cursorFor(insertedId, commandNode->literal().size());
  if (ctx_.selection && nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  }
  if (ctx_.undoStack) {
    ctx_.undoStack->push(EditTransaction(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Front Matter"),
        InsertNodeCommand{
            insertedId,
            BlockType::FrontMatter,
            0,
            TextDelta{0, QString(), insertedText},
            0,
            std::move(commandNode),
            beforeCursor,
            nextCursor,
            QVector<NodeId>{insertedId}}));
  }
  if (ctx_.brushQueue) {
    ctx_.brushQueue->requestBlockRefresh(insertedId);
  }
  return true;
}

QString FrontMatterController::tabText() const {
  return literal_.tabText();
}

}  // namespace muffin
