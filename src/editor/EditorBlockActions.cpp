#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "document/InlineBlockConversion.h"
#include <QApplication>
#include <QClipboard>
#include <QSettings>

namespace muffin {
BlockTarget EditorController::blockTarget(NodeId id) const {
  if (!session_)
    return {};
  const auto& structure = session_->document().blockStructure();
  return structure.targetFor(id);
}
bool EditorController::canPerformBlockAction(BlockTarget target, BlockAction action) const {
  if (!session_ || !session_->isEditingAllowed() || session_->isBatchPending() || (view_ && view_->editingLocked()))
    return false;
  const auto& structure = session_->document().blockStructure();
  const auto* unit = structure.resolve(target);
  if (!unit || !unit->movable || unit->contentEnd <= unit->start)
    return false;
  if (action == BlockAction::MoveUp)
    return structure.canMove(target, structure.sibling(target, -1), false);
  if (action == BlockAction::MoveDown)
    return structure.canMove(target, structure.sibling(target, 1), true);
  if (action >= BlockAction::Paragraph) {
    const auto* node = session_->document().node(target.id);
    return node && unit->first == unit->last &&
           (node->type() == BlockType::Paragraph || node->type() == BlockType::Heading);
  }
  return true;
}
bool EditorController::applyBlockPlan(BlockSourcePlan plan, const QString& label, bool preserveReading) {
  if (!plan.valid || !session_ || !session_->isEditingAllowed() || session_->isBatchPending())
    return false;
  qsizetype anchor = plan.cursor, cursor = plan.cursor;
  selectionSourceRange(anchor, cursor);
  const auto before = view_ ? view_->viewportPosition() : ViewportPosition{};
  auto after = before;
  if (preserveReading && after.sourceOffset >= 0)
    after.sourceOffset = plan.change.mapOffset(after.sourceOffset);
  else
    after = {};
  session_->applyBatchAsync(std::move(plan.text), label, anchor, cursor, plan.cursor, plan.cursor, before, after,
                            std::move(plan.change));
  return session_->isBatchPending();
}
bool EditorController::moveBlock(BlockTarget source, BlockTarget destination, bool after) {
  if (!canPerformBlockAction(source, BlockAction::Duplicate))
    return false;
  qsizetype anchor = -1, cursor = -1;
  selectionSourceRange(anchor, cursor);
  return applyBlockPlan(session_->document().blockStructure().move(
                            session_->markdownText(), source, destination, after, cursor,
                            QSettings().value(QStringLiteral("markdown/orderedList"), 0).toInt() != 1),
                        QStringLiteral("Move Block"), true);
}
bool EditorController::moveBlockUp() {
  const auto target = blockTarget(selection_.cursorPosition().blockId);
  return performBlockAction(target, BlockAction::MoveUp);
}
bool EditorController::moveBlockDown() {
  const auto target = blockTarget(selection_.cursorPosition().blockId);
  return performBlockAction(target, BlockAction::MoveDown);
}
bool EditorController::performBlockAction(BlockTarget target, BlockAction action) {
  if (!canPerformBlockAction(target, action))
    return false;
  const auto& structure = session_->document().blockStructure();
  const auto* unit = structure.resolve(target);
  const auto& text = session_->markdownText();
  const bool sequential = QSettings().value(QStringLiteral("markdown/orderedList"), 0).toInt() != 1;
  switch (action) {
  case BlockAction::Copy:
  case BlockAction::Cut: {
    const QString source = text.mid(unit->start, unit->contentEnd - unit->start);
    if (action == BlockAction::Cut &&
        !applyBlockPlan(structure.remove(text, target, sequential), QStringLiteral("Cut Block"), true))
      return false;
    QApplication::clipboard()->setText(source);
    return true;
  }
  case BlockAction::Delete:
    return applyBlockPlan(structure.remove(text, target, sequential), QStringLiteral("Delete Block"), true);
  case BlockAction::Duplicate:
    return applyBlockPlan(structure.duplicate(text, target, sequential), QStringLiteral("Duplicate Block"), true);
  case BlockAction::InsertBefore:
  case BlockAction::InsertAfter: {
    const bool after = action == BlockAction::InsertAfter;
    if (target.parent.isValid())
      return applyBlockPlan(structure.insertEmptyListItem(text, target, after, sequential),
                            QStringLiteral("Insert List Item"), false);
    const auto position = after ? unit->contentEnd : unit->start;
    const auto close = after ? structure.closingSuffix(text, target) : QString();
    const auto revision = session_->document().revision();
    inputController_.performLocalEdit(EditTransaction::Kind::SplitParagraph, QStringLiteral("Insert Paragraph"),
                                      position, 0, close + QStringLiteral("\n\n"), {},
                                      position + (after ? close.size() + 2 : 0), {}, true, true);
    return session_->document().revision() != revision;
  }
  case BlockAction::MoveUp:
    return moveBlock(target, structure.sibling(target, -1), false);
  case BlockAction::MoveDown:
    return moveBlock(target, structure.sibling(target, 1), true);
  default:
    break;
  }
  const auto* node = session_->document().node(target.id);
  if (!node)
    return false;
  InlineBlockFormat format;
  if (action >= BlockAction::Heading1 && action <= BlockAction::Heading6) {
    format.kind = InlineBlockFormat::Heading;
    format.level = int(action) - int(BlockAction::Heading1) + 1;
    format.setext = QSettings().value(QStringLiteral("markdown/headingStyle"), 0).toInt() == 1;
  } else if (action == BlockAction::BulletList || action == BlockAction::OrderedList ||
             action == BlockAction::TaskList) {
    const QChar bullets[] = {u'-', u'*', u'+'};
    format.bullet = bullets[qBound(0, QSettings().value(QStringLiteral("markdown/unorderedList"), 0).toInt(), 2)];
    format.kind = action == BlockAction::BulletList    ? InlineBlockFormat::BulletList
                  : action == BlockAction::OrderedList ? InlineBlockFormat::OrderedList
                                                       : InlineBlockFormat::TaskList;
  } else if (action == BlockAction::Quote) {
    format.kind = InlineBlockFormat::Quote;
  } else if (action == BlockAction::Code) {
    format.kind = InlineBlockFormat::Code;
  }
  auto converted = convertInlineBlock(text, *node, format, session_->parseOptions());
  if (!converted.valid || converted.text == text.mid(converted.range.start, converted.range.length()))
    return false;
  const auto cursor = converted.change.moves.isEmpty() ? converted.range.start : converted.change.moves.back().to;
  BlockSourcePlan plan{text, std::move(converted.change), cursor, true};
  plan.text.replace(converted.range.start, converted.range.end, converted.text);
  return applyBlockPlan(std::move(plan), QStringLiteral("Convert Block"), true);
}
} // namespace muffin
