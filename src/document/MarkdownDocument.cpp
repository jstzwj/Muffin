#include "document/MarkdownDocument.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(documentPerf, "muffin.perf", QtWarningMsg)

// Scoped perf probe routed to the muffin.perf category (captured by MUFFIN_PERF_LOG). No-op
// (single branch) when the category is disabled, so it is safe to keep in release builds.
class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(documentPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(documentPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

}  // namespace

namespace muffin {

MarkdownDocument::MarkdownDocument(QObject* parent)
    : QObject(parent), root_(std::make_unique<MarkdownNode>(BlockType::Document)) {
  bindSourcePositionSlots();
  index_.rebuild(*root_);
}

MarkdownNode& MarkdownDocument::root() {
  return *root_;
}

const MarkdownNode& MarkdownDocument::root() const {
  return *root_;
}

NodeIndex& MarkdownDocument::index() {
  return index_;
}

const NodeIndex& MarkdownDocument::index() const {
  return index_;
}

const LineStartOffsetCache& MarkdownDocument::lineOffsets() const {
  return lineOffsets_;
}

void MarkdownDocument::setMarkdownText(QString text, std::unique_ptr<MarkdownNode> root) {
  text_ = PieceTable(std::move(text));
  lineOffsets_.bind(text_);
  replaceRoot(std::move(root));
}

void MarkdownDocument::replaceTopLevelRange(
    qsizetype first,
    qsizetype count,
    std::vector<std::unique_ptr<MarkdownNode>> replacements,
    qsizetype sourceStart,
    qsizetype sourceEnd,
    const QString& replacementText) {
  PerfTimer totalPerf("session.local.replaceRange");
  replaceOutlineRange(first, count, replacements);
  std::unique_ptr<SourcePositionToken> replacementTokens;
  for (auto& replacement : replacements) {
    auto token = sourcePositions_.makeToken(replacement->siblingKind());
    replacement->sourcePositionToken_ = token.get();
    replacementTokens = SourcePositionIndex::merge(std::move(replacementTokens), std::move(token));
  }
  // New nodes already carry absolute post-edit ranges, so their fresh tokens start at zero.
  // Existing suffix tokens retain every accumulated delta while the implicit treap splices the
  // range in O(log n), even when Enter changes the number of top-level blocks.
  sourcePositions_.replace(first, count, std::move(replacementTokens));
  {
    // The piece-table is the edit master. text_.replace is O(pieces) -- NO whole-document memmove
    // (the O(doc)-per-keystroke cost the single-QString model paid). There is no separate
    // markdownText_ to invalidate; markdownText() materializes from text_ on demand.
    PerfTimer memmovePerf("session.local.replace.memmove");
    text_.replace(sourceStart, sourceEnd, replacementText);
  }
  {
    PerfTimer lineOffsetsPerf("session.local.replace.lineOffsets");
    lineOffsets_.applyEdit(sourceStart, sourceEnd - sourceStart, replacementText.size(), text_);
  }
  {
    PerfTimer indexPerf("session.local.replace.index");
    const qsizetype boundedFirst = qBound<qsizetype>(0, first, root_->children().size());
    const qsizetype boundedCount = qBound<qsizetype>(0, count, root_->children().size() - boundedFirst);

    // Drop the soon-to-be-removed subtrees from the index lookup while they are still alive, then
    // detach (destroying them) and insert the replacements, then register the new subtrees. This
    // keeps find() correct without an O(document) index_.rebuild on every keystroke.
    std::vector<MarkdownNode*> removed;
    removed.reserve(static_cast<std::size_t>(boundedCount));
    for (qsizetype i = 0; i < boundedCount; ++i) {
      removed.push_back(root_->children().at(static_cast<std::size_t>(boundedFirst + i)).get());
    }
    index_.removeSubtreesFromLookup(removed);

    for (qsizetype i = 0; i < boundedCount; ++i) {
      root_->detachChild(boundedFirst);
    }

    std::vector<MarkdownNode*> inserted;
    inserted.reserve(replacements.size());
    qsizetype insertAt = boundedFirst;
    for (auto& replacement : replacements) {
      MarkdownNode& node = root_->insertChild(insertAt, std::move(replacement));
      inserted.push_back(&node);
      ++insertAt;
    }
    index_.addSubtreesToLookup(inserted);
  }
  refreshRootSourceRange();

  ++revision_;
  emit documentReset();
}

void MarkdownDocument::shiftTopLevelSuffix(qsizetype first, qsizetype byteDelta, int lineDelta) {
  sourcePositions_.addSuffix(first, byteDelta, lineDelta);
}

QVector<OutlineEntry> MarkdownDocument::outline() const {
  QVector<OutlineEntry> result = outlineEntries_;
  QVector<int> levelStack;
  for (qsizetype i = 0; i < result.size(); ++i) {
    OutlineEntry& entry = result[i];
    const int level = qBound(1, entry.level, 6);
    while (levelStack.size() >= level) {
      levelStack.removeLast();
    }
    entry.parentIndex = levelStack.isEmpty() ? -1 : levelStack.last();
    if (const MarkdownNode* heading = node(entry.nodeId)) {
      entry.sourceRange = heading->sourceRange();
    }
    levelStack.push_back(static_cast<int>(i));
  }
  return result;
}

void MarkdownDocument::bindSourcePositionSlots() {
  root_->sourcePositionIndex_ = &sourcePositions_;
  const auto& children = root_->children();
  QVector<quint8> siblingKinds;
  siblingKinds.reserve(static_cast<qsizetype>(children.size()));
  for (const auto& child : children) {
    siblingKinds.push_back(child->siblingKind());
  }
  const QVector<SourcePositionToken*> tokens = sourcePositions_.reset(siblingKinds);
  for (qsizetype i = 0; i < static_cast<qsizetype>(children.size()); ++i) {
    children.at(static_cast<size_t>(i))->sourcePositionToken_ = tokens.at(i);
  }
}

void MarkdownDocument::refreshRootSourceRange() {
  qsizetype parseStart = 0;
  if (!root_->children().empty() &&
      root_->children().front()->type() == BlockType::FrontMatter) {
    parseStart = qBound<qsizetype>(
        0, root_->children().front()->sourceRange().byteEnd, text_.size());
    if (parseStart < text_.size() && text_.at(parseStart) == QLatin1Char('\r')) {
      ++parseStart;
    }
    if (parseStart < text_.size() && text_.at(parseStart) == QLatin1Char('\n')) {
      ++parseStart;
    }
  }

  SourceRange range;
  range.lineStart = 1;
  range.columnStart = 1;
  if (parseStart < text_.size()) {
    const bool endsWithNewline = text_.at(text_.size() - 1) == QLatin1Char('\n');
    const int fullEndLine = lineOffsets_.lineCount() - (endsWithNewline ? 1 : 0);
    const int parseStartLine = lineOffsets_.lineForOffset(parseStart);
    const qsizetype fullByteEnd = lineOffsets_.lineEndOffset(fullEndLine);
    const qsizetype lastLineStart = lineOffsets_.lineStartOffset(fullEndLine);
    QString lastLine = text_.mid(lastLineStart, fullByteEnd - lastLineStart);
    if (lastLine.endsWith(QLatin1Char('\r'))) {
      lastLine.chop(1);
    }
    range.byteEnd = fullByteEnd - parseStart;
    range.lineEnd = fullEndLine - parseStartLine + 1;
    range.columnEnd = lastLine.toUtf8().size();
  }
  root_->setSourceRange(range);
}

void MarkdownDocument::rebuildOutlineIndex() {
  outlineEntries_.clear();
  for (const auto& topLevel : root_->children()) {
    outlineEntries_ += buildOutlineFragment(*topLevel);
  }
  ++outlineRevision_;
}

void MarkdownDocument::replaceOutlineRange(
    qsizetype first,
    qsizetype count,
    const std::vector<std::unique_ptr<MarkdownNode>>& replacements) {
  const auto& children = root_->children();
  first = qBound<qsizetype>(0, first, static_cast<qsizetype>(children.size()));
  count = qBound<qsizetype>(0, count, static_cast<qsizetype>(children.size()) - first);

  QSet<NodeId> removedTopLevelIds;
  qsizetype oldStart = first < static_cast<qsizetype>(children.size())
      ? children.at(static_cast<size_t>(first))->sourceRange().byteStart
      : text_.size();
  qsizetype oldEnd = oldStart;
  for (qsizetype i = 0; i < count; ++i) {
    const MarkdownNode& topLevel = *children.at(static_cast<size_t>(first + i));
    removedTopLevelIds.insert(topLevel.id());
    oldEnd = qMax(oldEnd, topLevel.sourceRange().byteEnd);
  }

  const auto entryStart = [this](const OutlineEntry& entry) {
    if (const MarkdownNode* heading = node(entry.nodeId)) {
      return heading->sourceRange().byteStart;
    }
    return entry.sourceRange.byteStart;
  };
  qsizetype eraseFirst = static_cast<qsizetype>(std::lower_bound(
      outlineEntries_.cbegin(), outlineEntries_.cend(), oldStart,
      [&entryStart](const OutlineEntry& entry, qsizetype offset) {
        return entryStart(entry) < offset;
      }) - outlineEntries_.cbegin());
  while (eraseFirst < outlineEntries_.size() &&
         !removedTopLevelIds.contains(outlineEntries_.at(eraseFirst).topLevelId) &&
         entryStart(outlineEntries_.at(eraseFirst)) <= oldEnd) {
    ++eraseFirst;
  }
  qsizetype eraseEnd = eraseFirst;
  while (eraseEnd < outlineEntries_.size() &&
         removedTopLevelIds.contains(outlineEntries_.at(eraseEnd).topLevelId)) {
    ++eraseEnd;
  }

  QVector<OutlineEntry> inserted;
  for (const auto& replacement : replacements) {
    inserted += buildOutlineFragment(*replacement);
  }

  bool changed = eraseEnd - eraseFirst != inserted.size();
  if (!changed) {
    for (qsizetype i = 0; i < inserted.size(); ++i) {
      const OutlineEntry& oldEntry = outlineEntries_.at(eraseFirst + i);
      const OutlineEntry& newEntry = inserted.at(i);
      if (oldEntry.nodeId != newEntry.nodeId || oldEntry.level != newEntry.level ||
          oldEntry.title != newEntry.title) {
        changed = true;
        break;
      }
    }
  }

  outlineEntries_.remove(eraseFirst, eraseEnd - eraseFirst);
  for (qsizetype i = 0; i < inserted.size(); ++i) {
    outlineEntries_.insert(eraseFirst + i, std::move(inserted[i]));
  }
  if (changed) {
    ++outlineRevision_;
  }
}

quint64 MarkdownDocument::revision() const {
  return revision_;
}

bool MarkdownDocument::isModified() const {
  return modified_;
}

void MarkdownDocument::setModified(bool modified) {
  if (modified_ == modified) {
    return;
  }
  modified_ = modified;
  emit modifiedChanged(modified_);
}

MarkdownNode* MarkdownDocument::node(NodeId id) const {
  return index_.find(id);
}

MarkdownNode* MarkdownDocument::topLevelBlockAtOffset(qsizetype offset) const {
  const auto& children = root_->children();
  if (children.empty()) { return nullptr; }
  // Children are in document order with non-decreasing byteEnd. Find the first block whose
  // byteEnd >= offset — the earliest block that could contain offset (earlier blocks end
  // before it). Binary-searching on byteEnd (rather than byteStart) preserves the previous
  // linear "first containsByte(offset)" semantics exactly, including the shared-boundary
  // tie (A.byteEnd == B.byteStart == offset resolves to the earlier block A) and the gap
  // fallback (returns the last block when no block contains offset). O(log n) instead of
  // O(blocks) — caret resolution hits this on every edit at large sizes (~250k blocks).
  auto it = std::lower_bound(children.begin(), children.end(), offset,
      [](const std::unique_ptr<MarkdownNode>& child, qsizetype off) {
        return child->sourceRange().byteEnd < off;
      });
  if (it != children.end() && (*it)->sourceRange().byteStart <= offset) {
    return it->get();
  }
  // Leading blank region: the offset precedes all real content. The VEPs synthesized there are
  // zero-width (byteStart == byteEnd), so offsets between them fall through the containment
  // check — resolve to the first child instead of the historical last-block fallback, which
  // made a source caret in the leading blanks scroll/render the document END.
  const auto firstContent = std::find_if(
      children.begin(), children.end(),
      [](const std::unique_ptr<MarkdownNode>& child) {
        return child->sourceRange().byteEnd > child->sourceRange().byteStart;
      });
  if (firstContent != children.end() && offset < (*firstContent)->sourceRange().byteStart) {
    return children.front().get();
  }
  // No block contains offset (gap, or offset past the last block's content) — preserve the
  // historical fallback to the last top-level block (load-bearing for caret resolution
  // after non-text blocks like ThematicBreak).
  return children.back().get();
}

void MarkdownDocument::replaceRoot(std::unique_ptr<MarkdownNode> root) {
  root_ = std::move(root);
  if (!root_) {
    root_ = std::make_unique<MarkdownNode>(BlockType::Document);
  }
  bindSourcePositionSlots();
  index_.rebuild(*root_);
  rebuildOutlineIndex();
  ++revision_;
  emit documentReset();
}

}  // namespace muffin
