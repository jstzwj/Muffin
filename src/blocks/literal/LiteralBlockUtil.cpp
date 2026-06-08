#include "blocks/literal/LiteralBlockUtil.h"

#include "blocks/html/HtmlSanitizer.h"
#include "blocks/literal/LiteralBlockController.h"
#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "parser/MarkdownSerializer.h"

namespace muffin {
namespace {

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

QString sanitizedHtmlPreview(const LiteralBlockController& ctrl) {
  const MarkdownNode* html = ctrl.currentBlock();
  return html ? HtmlSanitizer().sanitizedPreview(html->literal()) : QString();
}

bool insertFrontMatter(const EditorContext& ctx, LiteralBlockController& ctrl, FrontMatterFormat format) {
  if (!ctx.session || format == FrontMatterFormat::None) {
    return false;
  }

  const auto& blocks = ctx.session->document().root().children();
  if (!blocks.empty() && blocks.front()->type() == BlockType::FrontMatter) {
    ctrl.setEditingBlock(blocks.front()->id(), 0);
    if (ctx.selection) {
      ctx.selection->setCursorPosition(ctrl.cursorFor(blocks.front()->id(), blocks.front()->literal().size()));
    }
    if (ctx.brushQueue) {
      ctx.brushQueue->requestBlockRefresh(blocks.front()->id());
    }
    return true;
  }

  const CursorPosition beforeCursor = ctx.selection ? ctx.selection->cursorPosition() : CursorPosition();
  const bool documentWasEmpty = ctx.session->markdownText().isEmpty();
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

  if (!ctx.session->applyInsertedNode(insertedId, BlockType::FrontMatter, 0, 0, 0, insertedText, true)) {
    return false;
  }

  ctrl.setEditingBlock(insertedId, 0);
  const CursorPosition nextCursor = ctrl.cursorFor(insertedId, commandNode->literal().size());
  if (ctx.selection && nextCursor.isValid()) {
    ctx.selection->setCursorPosition(nextCursor);
  }
  if (ctx.undoStack) {
    ctx.undoStack->push(EditTransaction(
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
  if (ctx.brushQueue) {
    ctx.brushQueue->requestBlockRefresh(insertedId);
  }
  return true;
}

}  // namespace muffin
