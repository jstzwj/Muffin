#include "document/BlockStructure.h"
#include "document/BlockSourceBoundary.h"
#include "document/HtmlContainerStructure.h"
#include "document/MarkdownDocument.h"
#include "document/SourceRangeUtil.h"
#include "document/ListItemSource.h"
#include <algorithm>

namespace muffin {
namespace {
QString separatorBefore(const PieceTable& text, qsizetype position, qsizetype floor = 0) {
  int lines = 0;
  for (auto i = position; i > floor && lines < 2 && text.at(i - 1) == u'\n'; --i)
    ++lines;
  return position > 0 ? QString(qMax(0, 2 - lines), u'\n') : QString();
}
QString separated(QString content) {
  int lines = 0;
  for (auto i = content.size(); i > 0 && lines < 2 && content.at(i - 1) == u'\n'; --i)
    ++lines;
  content += QString(qMax(0, 2 - lines), u'\n');
  return content;
}
}
BlockStructure::BlockStructure(const MarkdownDocument& document) : document_(document), revision_(document.revision()) {
  const auto& nodes = document.root().children();
  const auto pairs = htmlContainerPairs(document.root());
  const auto& text = document.markdownText();
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto pair = pairs.constFind(i);
    const auto last = pair == pairs.cend() ? i : *pair;
    const auto start = blockLineSpan(*nodes[i], text).byteStart;
    const auto end = blockLineSpan(*nodes[last], text).byteEnd;
    if (start >= 0 && end >= start)
      units_.append({nodes[i]->id(), start, end, end, int(i), int(last), nodes[i]->type() != BlockType::FrontMatter});
    i = last;
  }
  for (qsizetype i = 0; i < units_.size(); ++i)
    units_[i].end = i + 1 < units_.size() ? units_[i + 1].start : text.size();
}
int BlockStructure::indexFor(const MarkdownDocument& document, NodeId id) const {
  if (revision_ != document.revision())
    return -1;
  const auto* node = document.node(id);
  while (node && node->parent() && node->parent()->type() != BlockType::Document)
    node = node->parent();
  if (!node || !node->parent())
    return -1;
  const int ordinal = node->siblingIndex();
  const auto it = std::lower_bound(units_.cbegin(), units_.cend(), ordinal,
                                   [](const auto& unit, int value) { return unit.last < value; });
  return it != units_.cend() && it->first <= ordinal ? int(it - units_.cbegin()) : -1;
}
const BlockSourceUnit* BlockStructure::resolve(BlockTarget target) const {
  if (target.revision != revision_ || document_.revision() != revision_)
    return nullptr;
  const auto* items = siblings(target);
  return items && target.index >= 0 && target.index < items->size() && items->at(target.index).id == target.id
             ? &items->at(target.index)
             : nullptr;
}
BlockTarget BlockStructure::targetAt(int index) const {
  return index >= 0 && index < units_.size() ? BlockTarget{units_[index].id, revision_, index} : BlockTarget{};
}
const BlockStructure::ListScope* BlockStructure::listScope(NodeId id) const {
  if (document_.revision() != revision_)
    return nullptr;
  if (const auto found = lists_.constFind(id); found != lists_.cend())
    return found->valid ? &*found : nullptr;
  const auto* list = document_.node(id);
  if (!list || list->type() != BlockType::List)
    return nullptr;
  ListScope result;
  const auto& text = document_.markdownText();
  for (const auto& item : list->children()) {
    const auto source = listItemSource(text, *item);
    bool ownsLine = source.isValid();
    for (auto offset = source.firstLine.start; ownsLine && offset < source.markerStart(); ++offset)
      ownsLine = text.at(offset).isSpace() || text.at(offset) == u'>';
    // A marker nested on its parent's physical line ("3. 4. text") cannot
    // move as complete lines without stealing that parent's marker.
    if (!ownsLine) {
      result.valid = false;
      break;
    }
    const auto index = int(result.units.size());
    result.indices.insert(item->id(), index);
    auto end = source.item.end;
    if (end < text.size() && text.at(end) == u'\n')
      ++end;
    result.units.append({item->id(), source.firstLine.start, source.item.end, end, index, index, true});
  }
  for (qsizetype i = 1; i < result.units.size(); ++i)
    result.units[i - 1].end = result.units[i].start;
  const auto saved = lists_.insert(id, std::move(result));
  return saved->valid ? &*saved : nullptr;
}
const QVector<BlockSourceUnit>* BlockStructure::siblings(BlockTarget target) const {
  if (!target.parent.isValid())
    return &units_;
  const auto* scope = listScope(target.parent);
  return scope ? &scope->units : nullptr;
}
BlockTarget BlockStructure::targetFor(NodeId id) const {
  if (document_.revision() != revision_)
    return {};
  for (auto* node = document_.node(id); node && node->parent(); node = node->parent()) {
    if (node->type() == BlockType::ListItem && node->parent()->type() == BlockType::List) {
      if (const auto* scope = listScope(node->parent()->id())) {
        const auto found = scope->indices.constFind(node->id());
        if (found != scope->indices.cend())
          return {node->id(), revision_, *found, node->parent()->id()};
      }
    }
  }
  return targetAt(indexFor(document_, id));
}
BlockTarget BlockStructure::targetInScope(NodeId id, BlockTarget target) const {
  if (!resolve(target))
    return {};
  if (!target.parent.isValid())
    return targetAt(indexFor(document_, id));
  const auto* scope = listScope(target.parent);
  for (auto* node = document_.node(id); scope && node && node->parent(); node = node->parent())
    if (node->parent()->id() == target.parent) {
      const auto found = scope->indices.constFind(node->id());
      return found != scope->indices.cend() ? BlockTarget{node->id(), revision_, *found, target.parent} : BlockTarget{};
    }
  return {};
}
BlockTarget BlockStructure::sibling(BlockTarget target, int direction) const {
  if (!resolve(target))
    return {};
  const auto* items = siblings(target);
  const auto index = target.index + direction;
  return index >= 0 && index < items->size() ? BlockTarget{items->at(index).id, revision_, index, target.parent}
                                             : BlockTarget{};
}
bool BlockStructure::canMove(BlockTarget source, BlockTarget destination, bool after) const {
  if (source.parent != destination.parent)
    return false;
  const auto* from = resolve(source);
  const auto* to = resolve(destination);
  if (!from || !to || source.parent != destination.parent || !from->movable || (!to->movable && !after))
    return false;
  const auto insertion = after ? to->end : to->start;
  return insertion < from->start || insertion > from->end;
}
QString BlockStructure::closingSuffix(const PieceTable& text, BlockTarget target) const {
  const auto* unit = resolve(target);
  if (!unit || target.parent.isValid())
    return {};
  return blockClosingSuffix(text, *document_.root().children()[unit->last]);
}
BlockSourcePlan BlockStructure::move(const PieceTable& text, BlockTarget source, BlockTarget destination, bool after,
                                     qsizetype cursorOffset, bool sequential) const {
  if (!canMove(source, destination, after))
    return {};
  if (source.parent.isValid())
    return listPlan(text, source, destination, after, ListOperation::Move, sequential, cursorOffset);
  const auto* from = resolve(source);
  const auto* to = resolve(destination);
  if (!from || !to || !from->movable || (!to->movable && !after) || from == to)
    return {};
  const auto insertion = after ? to->end : to->start;
  if (insertion >= from->start && insertion <= from->end)
    return {};
  BlockSourcePlan plan;
  plan.text = text;
  const auto length = from->end - from->start;
  QString payload = text.mid(from->start, length);
  payload.insert(from->contentEnd - from->start, closingSuffix(text, source));
  payload = separated(std::move(payload));
  plan.text.replace(from->start, from->end, {});
  const auto position = insertion > from->start ? insertion - length : insertion;
  const auto close = after ? closingSuffix(text, destination) : QString();
  const auto contentEnd = to->contentEnd - (insertion > from->start ? length : 0);
  const auto prefix = separatorBefore(plan.text, position, close.isEmpty() ? 0 : contentEnd);
  payload.prepend(prefix);
  plan.text.replace(position, position, payload);
  if (!close.isEmpty())
    plan.text.replace(contentEnd, contentEnd, close);
  if (insertion < from->start)
    plan.change.edits = {{insertion, 0, payload.size()}, {from->start, length, 0}};
  else
    plan.change.edits = {{from->start, length, 0}, {insertion, 0, payload.size()}};
  if (!close.isEmpty()) {
    const auto at = std::lower_bound(plan.change.edits.begin(), plan.change.edits.end(), to->contentEnd,
                                     [](const auto& edit, qsizetype offset) { return edit.start < offset; });
    plan.change.edits.insert(at, {to->contentEnd, 0, close.size()});
  }
  const auto movedStart = position + close.size() + prefix.size();
  plan.change.moves.append({from->start, from->contentEnd - from->start, movedStart});
  plan.cursor = cursorOffset >= from->start && cursorOffset <= from->contentEnd
                    ? movedStart + cursorOffset - from->start
                    : movedStart;
  plan.valid = true;
  return plan;
}
BlockSourcePlan BlockStructure::remove(const PieceTable& text, BlockTarget target, bool sequential) const {
  if (target.parent.isValid())
    return listPlan(text, target, target, false, ListOperation::Remove, sequential);
  const auto* unit = resolve(target);
  if (!unit || !unit->movable || unit->end <= unit->start)
    return {};
  BlockSourcePlan plan{text, {{{unit->start, unit->end - unit->start, 0}}}, unit->start, true};
  plan.text.replace(unit->start, unit->end, {});
  return plan;
}
BlockSourcePlan BlockStructure::insert(const PieceTable& text, BlockTarget target, bool after, QString content) const {
  const auto* unit = resolve(target);
  if (!unit || (!unit->movable && !after))
    return {};
  const auto position = after ? unit->end : unit->start;
  const auto close = after ? closingSuffix(text, target) : QString();
  const auto prefix = separatorBefore(text, position, close.isEmpty() ? 0 : unit->contentEnd);
  const QString payload = prefix + separated(std::move(content));
  BlockSourcePlan plan{text, {}, position + close.size() + prefix.size(), true};
  if (!close.isEmpty())
    plan.change.edits.append({unit->contentEnd, 0, close.size()});
  plan.change.edits.append({position, 0, payload.size()});
  plan.text.replace(position, position, payload);
  if (!close.isEmpty())
    plan.text.replace(unit->contentEnd, unit->contentEnd, close);
  return plan;
}
BlockSourcePlan BlockStructure::duplicate(const PieceTable& text, BlockTarget target, bool sequential) const {
  if (target.parent.isValid())
    return listPlan(text, target, target, true, ListOperation::Duplicate, sequential);
  const auto* unit = resolve(target);
  return unit && unit->movable
             ? insert(text, target, true,
                      text.mid(unit->start, unit->contentEnd - unit->start) + closingSuffix(text, target))
             : BlockSourcePlan{};
}
BlockSourcePlan BlockStructure::insertEmptyListItem(const PieceTable& text, BlockTarget target, bool after,
                                                    bool sequential) const {
  return target.parent.isValid() ? listPlan(text, target, target, after, ListOperation::Insert, sequential)
                                 : BlockSourcePlan{};
}
BlockSourcePlan BlockStructure::listPlan(const PieceTable& text, BlockTarget source, BlockTarget destination,
                                         bool after, ListOperation operation, bool sequential, qsizetype cursor) const {
  const auto* from = resolve(source);
  const auto* to = resolve(destination);
  if (!from || !to || !source.parent.isValid() || source.parent != destination.parent ||
      (operation == ListOperation::Move && !canMove(source, destination, after)))
    return {};
  const auto* items = siblings(source);
  const auto* list = document_.node(source.parent);
  QVector<int> order;
  for (int i = 0; i < items->size(); ++i)
    order.append(i);
  if (operation == ListOperation::Move || operation == ListOperation::Remove)
    order.removeAt(source.index);
  int insertedRank = -1;
  if (operation != ListOperation::Remove) {
    insertedRank = destination.index + int(after);
    if (operation == ListOperation::Move && source.index < insertedRank)
      --insertedRank;
    order.insert(insertedRank, operation == ListOperation::Move ? source.index : -1);
  }
  QVector<int> ranks(items->size(), -1);
  for (int i = 0; i < order.size(); ++i)
    if (order[i] >= 0)
      ranks[order[i]] = i;
  const int startNumber = list->listStart();
  const auto numberFor = [&](int rank) { return sequential ? startNumber + rank : 1; };
  struct Edit {
    InlineRange range;
    QString text;
    bool insertion = false;
  };
  QVector<Edit> edits;
  for (int i = 0; i < items->size(); ++i) {
    if (ranks[i] < 0 || (operation == ListOperation::Move && i == source.index))
      continue;
    const auto info = listItemSource(text, *document_.node(items->at(i).id));
    for (const auto& edit : listItemNumberEdits(text, info, numberFor(ranks[i])))
      edits.append({edit.range, edit.text});
  }
  if (operation == ListOperation::Move || operation == ListOperation::Remove)
    edits.append(Edit{{from->start, from->end}, QString(), false});
  QVector<ListSourceEdit> payloadEdits;
  QString payload, prefix;
  const auto original = listItemSource(text, *document_.node(source.id));
  if (operation != ListOperation::Remove) {
    const auto position = after ? to->end : to->start;
    if (position > 0 && text.at(position - 1) != u'\n')
      prefix = u'\n';
    payload = text.mid(from->start, from->end - from->start);
    payloadEdits = listItemNumberEdits(text, original, numberFor(insertedRank));
    if (operation == ListOperation::Insert) {
      payload = text.mid(from->start, original.textStart() - from->start);
      if (original.marker.task)
        payload[original.firstLine.start + original.marker.taskMarkerStart + 1 - from->start] = u' ';
      // A new empty item has no continuation lines to rebase.
      if (!payloadEdits.isEmpty())
        payloadEdits.resize(1);
    }
    for (auto edit = payloadEdits.crbegin(); edit != payloadEdits.crend(); ++edit)
      payload.replace(edit->range.start - from->start, edit->range.length(), edit->text);
    if (operation == ListOperation::Insert)
      payload += text.mid(from->contentEnd, from->end - from->contentEnd);
    if (!payload.endsWith(u'\n'))
      payload += u'\n';
    edits.append({{position, position}, prefix + payload, true});
  }
  std::stable_sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) {
    return a.range.start < b.range.start || (a.range.start == b.range.start && a.insertion && !b.insertion);
  });
  BlockSourcePlan plan;
  plan.text = text;
  qsizetype shift = 0, insertedStart = 0;
  for (const auto& edit : edits) {
    if (edit.insertion)
      insertedStart = edit.range.start + shift + prefix.size();
    plan.change.edits.append({edit.range.start, edit.range.length(), edit.text.size()});
    shift += edit.text.size() - edit.range.length();
  }
  for (auto edit = edits.crbegin(); edit != edits.crend(); ++edit)
    plan.text.replace(edit->range.start, edit->range.end, edit->text);
  plan.cursor =
      operation == ListOperation::Remove ? qMin(plan.text.size(), plan.change.mapOffset(from->start)) : insertedStart;
  if (operation == ListOperation::Move) {
    SourceTextChange local;
    QVector<SourceTextChange::Move> relocations;
    qsizetype old = from->start, delta = 0;
    for (const auto& edit : payloadEdits) {
      if (edit.range.start > old)
        relocations.append({old, edit.range.start - old, insertedStart + old - from->start + delta});
      if (const auto length = qMin(edit.range.length(), edit.text.size()); length > 0)
        relocations.append({edit.range.start, length, insertedStart + edit.range.start - from->start + delta});
      local.edits.append({edit.range.start, edit.range.length(), edit.text.size()});
      delta += edit.text.size() - edit.range.length();
      old = edit.range.end;
    }
    relocations.append({old, from->contentEnd - old, insertedStart + old - from->start + delta});
    for (auto relocation = relocations.crbegin(); relocation != relocations.crend(); ++relocation)
      plan.change.moves.append(*relocation);
    if (cursor >= from->start && cursor <= from->contentEnd)
      plan.cursor = insertedStart + local.mapOffset(cursor) - from->start;
    else
      plan.cursor = insertedStart + local.mapOffset(original.textStart()) - from->start;
  } else if (operation == ListOperation::Insert) {
    plan.cursor = insertedStart + payload.indexOf(u'\n');
  }
  plan.valid = true;
  return plan;
}
} // namespace muffin
