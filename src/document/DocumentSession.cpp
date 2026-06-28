#include "document/DocumentSession.h"

#include "document/InlineNode.h"
#include "document/PendingBlockMarker.h"
#include "document/SourceRangeUtil.h"
#include "parser/MarkdownSerializer.h"
#include "diagnostics/ProcessMemory.h"

#include <QFileInfo>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(sessionPerf, "muffin.perf", QtWarningMsg)
Q_LOGGING_CATEGORY(sessionLog, "muffin.session", QtWarningMsg)

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(sessionPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(sessionPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0
                                     << " ms ws=" << (muffin::diag::workingSetBytes() >> 20) << "MB";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

struct TopLevelSlice {
  qsizetype first = -1;
  qsizetype count = 0;
  qsizetype sourceStart = -1;
  qsizetype sourceEnd = -1;
  bool startsAtEditStart = false;  // the preceding block(s) were skipped (trailing-newline touch);
                                  // a leading VEP in the replacements is a separator artifact
};

void warnLocalEditRejected(
    const char* reason,
    qsizetype sourceStart,
    qsizetype sourceEnd,
    qsizetype replacementLength,
    qsizetype documentSize) {
  qCWarning(sessionLog).nospace()
      << "Rejected local document edit: " << reason
      << " sourceStart=" << sourceStart
      << " sourceEnd=" << sourceEnd
      << " replacementLength=" << replacementLength
      << " documentSize=" << documentSize;
  // Mirror to the perf category (captured by MUFFIN_PERF_LOG) so a localized-edit rejection —
  // the trigger for the catastrophic full-reparse fallback on huge docs (Ctrl+Z ~20s) — is visible
  // in the perf trace, not just the (uncaptured) session log.
  qCDebug(sessionPerf).nospace()
      << "localEdit.rejected reason=" << reason
      << " start=" << sourceStart << " end=" << sourceEnd
      << " replLen=" << replacementLength << " docSize=" << documentSize;
}

void warnLocalEditSliceRejected(
    const char* reason,
    qsizetype sourceStart,
    qsizetype sourceEnd,
    qsizetype replacementLength,
    qsizetype documentSize,
    const TopLevelSlice& slice) {
  qCWarning(sessionLog).nospace()
      << "Rejected local document edit: " << reason
      << " sourceStart=" << sourceStart
      << " sourceEnd=" << sourceEnd
      << " replacementLength=" << replacementLength
      << " documentSize=" << documentSize
      << " sliceFirst=" << slice.first
      << " sliceCount=" << slice.count
      << " sliceSourceStart=" << slice.sourceStart
      << " sliceSourceEnd=" << slice.sourceEnd;
  qCDebug(sessionPerf).nospace()
      << "localEdit.rejected reason=" << reason
      << " start=" << sourceStart << " end=" << sourceEnd
      << " replLen=" << replacementLength << " docSize=" << documentSize
      << " sliceFirst=" << slice.first << " sliceCount=" << slice.count
      << " sliceSrc=[" << slice.sourceStart << "," << slice.sourceEnd << "]";
}

muffin::SourceRange usableRange(const muffin::MarkdownNode& node) {
  return node.sourceRange();
}

// True when editStart sits inside the block's trailing-newline region: every byte from editStart
// to the block's byteEnd is a line terminator. Such a block's CONTENT is entirely before editStart,
// so a deletion beginning there leaves the block unchanged (cmark re-assigns the trailing newline
// from the new following context) — chooseTopLevelSlice skips it to keep its original node/range.
bool editStartInTrailingNewlines(const muffin::PieceTable& text, const muffin::MarkdownNode& block, qsizetype editStart) {
  const muffin::SourceRange r = block.sourceRange();
  if (r.byteStart < 0 || editStart < r.byteStart || editStart >= r.byteEnd) {
    return false;
  }
  for (qsizetype i = editStart; i < r.byteEnd; ++i) {
    const QChar c = text.at(i);
    if (c != QLatin1Char('\n') && c != QLatin1Char('\r')) {
      return false;
    }
  }
  return true;
}

bool isVirtualEmptyParagraph(const muffin::MarkdownNode& node) {
  const muffin::SourceRange range = node.sourceRange();
  return node.type() == muffin::BlockType::Paragraph && range.byteStart >= 0 && range.byteEnd == range.byteStart;
}

// A slice is parsed in isolation, so insertVirtualEmptyParagraphs can synthesize a virtual empty
// paragraph at the leading edge purely from the slice's own leading blank lines — without knowing
// the neighboring block on the other side of the slice boundary. For a pure insertion (count == 0)
// that lands after an existing block, a leading VEP followed by real content is a context artifact:
// it represents the single blank line that separates the new content from the preceding block,
// which the isolated slice mis-counts as a double-blank gap. Promoted to a permanent block it
// renders stale neighbor source (e.g. a phantom "---" after a thematic break). Such separator VEPs
// are dropped. A leading VEP that is itself the intended content (the slice yields only VEPs, e.g.
// the new empty paragraph created by insertParagraphBefore/After) is kept.
void stripSeparatorVirtualEmptyParagraphs(std::vector<std::unique_ptr<muffin::MarkdownNode>>& replacements) {
  const bool hasRealContent =
      std::any_of(replacements.begin(), replacements.end(),
                  [](const std::unique_ptr<muffin::MarkdownNode>& node) { return !isVirtualEmptyParagraph(*node); });
  if (!hasRealContent) {
    return;
  }
  while (!replacements.empty() && isVirtualEmptyParagraph(*replacements.front())) {
    replacements.erase(replacements.begin());
  }
}

bool isOnlyNewlines(QStringView text) {
  for (QChar ch : text) {
    if (ch != QLatin1Char('\n')) {
      return false;
    }
  }
  return true;
}

bool isBlankLineStructuralEdit(QStringView removedText, QStringView insertedText) {
  return isOnlyNewlines(removedText) && isOnlyNewlines(insertedText) &&
         (removedText.contains(QLatin1Char('\n')) || insertedText.contains(QLatin1Char('\n')));
}

int countNewlines(QStringView text) {
  int count = 0;
  for (QChar ch : text) {
    if (ch == QLatin1Char('\n')) {
      ++count;
    }
  }
  return count;
}

void demotePendingMarkersInSubtree(const QString& markdown, muffin::MarkdownNode& node) {
  muffin::demotePendingMarkerToParagraph(markdown, node);
  for (const auto& child : node.children()) {
    if (child) {
      demotePendingMarkersInSubtree(markdown, *child);
    }
  }
}

bool demotePendingMarkerAtOffset(const QString& markdown, muffin::MarkdownNode& node, qsizetype offset) {
  const muffin::SourceRange range = node.sourceRange();
  if (node.type() != muffin::BlockType::Document && (range.byteStart > offset || range.byteEnd < offset)) {
    return false;
  }
  for (const auto& child : node.children()) {
    if (child && demotePendingMarkerAtOffset(markdown, *child, offset)) {
      return true;
    }
  }
  const muffin::BlockType beforeType = node.type();
  muffin::demotePendingMarkerToParagraph(markdown, node);
  return node.type() != beforeType;
}

// Demote every pending marker whose source range contains one of `offsets` back to a paragraph.
// This lets the edit-driven full-reparse fallback keep still-incomplete openers (lone `###`, `-`,
// ``` ``` ```, `$$`/`\[`) as paragraphs even when the local-edit path was rejected. The search is
// recursive so pending markers behave the same inside block quotes and list items as they do at
// document root.
void demotePendingMarkersAtOffsets(const QString& markdown, muffin::MarkdownNode& root, const QVector<qsizetype>& offsets) {
  for (const qsizetype offset : offsets) {
    if (offset < 0) {
      continue;
    }
    demotePendingMarkerAtOffset(markdown, root, offset);
  }
}

TopLevelSlice chooseTopLevelSlice(const muffin::MarkdownDocument& document, qsizetype editStart, qsizetype editEnd, bool blankLineStructuralEdit, bool isPureDeletion) {
  TopLevelSlice slice;
  const auto& blocks = document.root().children();
  if (blocks.empty()) {
    slice.first = 0;
    slice.count = 0;
    slice.sourceStart = 0;
    slice.sourceEnd = document.pieceText().size();
    return slice;
  }

  // Top-level block ranges are absolute and non-decreasing in document order, so binary-search to
  // the overlap window instead of scanning every block. lo = first block whose byteEnd >= editStart,
  // hiExcl = first block whose byteStart > editEnd; every block in [lo, hiExcl) overlaps the edit
  // (the original `editStart == byteStart/byteEnd` boundary cases are subsumed given editStart <=
  // editEnd). Then accumulate the editable ones exactly as before. Replaces an O(blocks) scan that
  // cost ~16ms @ 50MB on every keystroke with O(log n + overlap-window).
  qsizetype lo = 0;
  {
    qsizetype a = 0;
    qsizetype b = static_cast<qsizetype>(blocks.size());
    while (a < b) {
      const qsizetype mid = a + (b - a) / 2;
      if (usableRange(*blocks.at(static_cast<size_t>(mid))).byteEnd < editStart) {
        a = mid + 1;
      } else {
        b = mid;
      }
    }
    lo = a;
  }
  qsizetype hiExcl = static_cast<qsizetype>(blocks.size());
  {
    qsizetype a = lo;
    qsizetype b = static_cast<qsizetype>(blocks.size());
    while (a < b) {
      const qsizetype mid = a + (b - a) / 2;
      if (usableRange(*blocks.at(static_cast<size_t>(mid))).byteStart <= editEnd) {
        a = mid + 1;
      } else {
        b = mid;
      }
    }
    hiExcl = a;
  }
  // A pure deletion that begins inside a block's trailing-newline region (caret at the end of a
  // block's content — the classic "delete at end of a paragraph/list eats the following divider")
  // leaves that block's CONTENT unchanged: only trailing whitespace is removed, and cmark
  // re-assigns the trailing newline from the new following context. Skip such blocks so they keep
  // their original node + source range; re-parsing them in slice isolation would give cmark a
  // context-dependent range (off by the trailing newline) that mismatches a full re-parse. The
  // deletion must extend past the block (editEnd >= byteEnd) so its trailing newlines are fully
  // consumed and cmark re-derives them — a deletion that stops mid-whitespace still owns the rest.
  bool sliceStartsAtEditStart = false;
  // Only when a surviving block follows the window (hiExcl < blocks.size()): cmark then re-assigns
  // the skipped block's trailing newline from that following content, so keeping its original node
  // + range stays correct. If the skipped block would become the last block (nothing follows), its
  // range would shrink and the original would go stale — so don't skip in that case (re-parse it).
  if (isPureDeletion && hiExcl < static_cast<qsizetype>(blocks.size())) {
    const muffin::PieceTable& src = document.pieceText();
    while (lo < hiExcl) {
      const muffin::MarkdownNode& blk = *blocks.at(static_cast<size_t>(lo));
      const muffin::SourceRange r = usableRange(blk);
      if (editEnd < r.byteEnd || !editStartInTrailingNewlines(src, blk, editStart)) {
        break;
      }
      ++lo;
      sliceStartsAtEditStart = true;
    }
  }
  // The slice spans the FULL overlap window [lo, hiExcl) — including NON-editable top-level
  // blocks (a thematic break). A slice is just a contiguous source region re-parsed in
  // isolation; editability is a rendering concern the parser ignores. Spanning the whole window
  // guarantees slice.sourceStart <= editStart and slice.sourceEnd >= editEnd, so the slice text
  // (built in tryApplyTopLevelLocalEdit as pre-edit + replacement + post-edit) is exactly the
  // post-edit text of that region — re-parsing it reproduces the correct blocks and
  // replaceTopLevelRange swaps them in. A `---` being deleted simply isn't among the re-parsed
  // blocks, so it is removed from the live tree instead of forcing a whole-document re-parse.
  // (Earlier this SKIPPED non-editable blocks, which left slice.sourceStart past editStart and a
  // stale divider node + duplicates; the refuse-and-full-reparse was the workaround. Including
  // them is the fundamental fix — guarded by testDeleteRuleFromNestedListItemKeepsLiveTreeConsistent.)
  for (qsizetype i = lo; i < hiExcl; ++i) {
    const muffin::MarkdownNode& block = *blocks.at(static_cast<size_t>(i));
    const muffin::SourceRange range = usableRange(block);
    if (range.byteStart < 0 || range.byteEnd < range.byteStart) {
      continue;  // skip only invalid ranges — NOT non-editable blocks (see comment above)
    }
    if (slice.first < 0) {
      slice.first = i;
      slice.sourceStart = range.byteStart;
    }
    slice.count = i - slice.first + 1;
    slice.sourceEnd = range.byteEnd;
  }

  if (slice.first >= 0) {
    bool onlyVirtualEmptyParagraphs = slice.count > 0;
    for (qsizetype i = slice.first; i < slice.first + slice.count; ++i) {
      if (!isVirtualEmptyParagraph(*blocks.at(static_cast<size_t>(i)))) {
        onlyVirtualEmptyParagraphs = false;
        break;
      }
    }

    if (onlyVirtualEmptyParagraphs) {
      qsizetype selectedFirst = slice.first;
      qsizetype selectedEnd = slice.first + slice.count;
      if (blankLineStructuralEdit) {
        while (selectedFirst > 0 && isVirtualEmptyParagraph(*blocks.at(static_cast<size_t>(selectedFirst - 1)))) {
          --selectedFirst;
        }
        while (selectedEnd < static_cast<qsizetype>(blocks.size()) && isVirtualEmptyParagraph(*blocks.at(static_cast<size_t>(selectedEnd)))) {
          ++selectedEnd;
        }
      }

      // Only expand into neighboring blocks for blank-line structural edits.
      // Character insertions (like typing "*" in a virtual empty paragraph)
      // don't need surrounding context — the pending-marker demotion logic
      // handles single-line markers correctly.  Expanding would pull in a
      // List block, causing cmark to absorb the "*" into the list structure,
      // which then can't be demoted because the multi-line source doesn't
      // match the single-line pending-marker regex.
      qsizetype expandedFirst = selectedFirst;
      qsizetype expandedEnd = selectedEnd;
      if (blankLineStructuralEdit) {
        if (expandedFirst > 0) {
          --expandedFirst;
        }
        if (expandedEnd < static_cast<qsizetype>(blocks.size())) {
          ++expandedEnd;
        }
      }

      slice.first = expandedFirst;
      slice.count = expandedEnd - expandedFirst;
      slice.sourceStart = expandedFirst > 0 ? blocks.at(static_cast<size_t>(expandedFirst))->sourceRange().byteStart : 0;
      slice.sourceEnd = expandedEnd < static_cast<qsizetype>(blocks.size())
                            ? blocks.at(static_cast<size_t>(expandedEnd - 1))->sourceRange().byteEnd
                            : document.pieceText().size();
    }
  }

  if (slice.first >= 0) {
    // Snap the slice start to the line boundary (shared helper) so block-leading indentation —
    // notably indented-code blocks, whose node range begins at the content — is re-parsed intact.
    slice.sourceStart = muffin::lineStartOffset(document.pieceText(), slice.sourceStart);
    if (sliceStartsAtEditStart) {
      // Override the snap: the edit began in a skipped block's trailing whitespace, not at a
      // re-parseable line boundary. The slice must still cover editStart, so start exactly there.
      slice.sourceStart = editStart;
    }
    slice.startsAtEditStart = sliceStartsAtEditStart;
    return slice;
  }

  for (qsizetype i = 0; i < static_cast<qsizetype>(blocks.size()); ++i) {
    const muffin::SourceRange range = usableRange(*blocks.at(static_cast<size_t>(i)));
    if (editStart < range.byteStart) {
      slice.first = i;
      slice.count = 0;
      slice.sourceStart = editStart;
      slice.sourceEnd = editStart;
      return slice;
    }
  }

  slice.first = blocks.size();
  slice.count = 0;
  slice.sourceStart = document.pieceText().size();
  slice.sourceEnd = document.pieceText().size();
  return slice;
}

// The old recursive shiftRanges is gone: under the block-relative offset model, a suffix
// top-level block only needs its OWN sourceRange shifted (descendants are relative and invariant).
// See tryApplyTopLevelLocalEdit's suffix loop. Slice nodes are made block-relative via
// MarkdownNode::relativizeDescendants + an own-range absolutize (no recursive shift).

void inheritIdsByStructure(const muffin::MarkdownNode& oldNode, muffin::MarkdownNode& newNode) {
  if (oldNode.type() != newNode.type()) {
    return;
  }
  newNode.setId(oldNode.id());
  const qsizetype childCount = qMin<qsizetype>(oldNode.children().size(), newNode.children().size());
  for (qsizetype i = 0; i < childCount; ++i) {
    inheritIdsByStructure(*oldNode.children().at(static_cast<size_t>(i)), *newNode.children().at(static_cast<size_t>(i)));
  }
}

muffin::MarkdownNode* nodeAtSourceOffset(muffin::MarkdownNode& node, const muffin::LocalEditNodeHint& hint) {
  if (hint.targetSourceOffset < 0) {
    return nullptr;
  }
  const muffin::SourceRange range = node.sourceRange();
  if ((hint.type == muffin::BlockType::Unknown || node.type() == hint.type) &&
      range.byteStart <= hint.targetSourceOffset && range.byteEnd >= hint.targetSourceOffset) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (muffin::MarkdownNode* found = nodeAtSourceOffset(*child, hint)) {
      return found;
    }
  }
  return nullptr;
}

muffin::SourceRange stableRangeAfterEdit(muffin::SourceRange range, qsizetype editStart, qsizetype editEnd, qsizetype editDelta) {
  if (range.byteEnd <= editStart) {
    return range;
  }
  if (range.byteStart >= editEnd) {
    range.byteStart += editDelta;
    range.byteEnd += editDelta;
    return range;
  }
  range.byteStart = -1;
  range.byteEnd = -1;
  return range;
}

bool sameStableRange(const muffin::SourceRange& oldRange, const muffin::SourceRange& newRange) {
  return oldRange.byteStart >= 0 && oldRange.byteEnd >= oldRange.byteStart && oldRange.byteStart == newRange.byteStart &&
         oldRange.byteEnd == newRange.byteEnd;
}

void inheritIdsForUnchangedTopLevelBlocks(
    const muffin::MarkdownDocument& document,
    const TopLevelSlice& slice,
    const std::vector<std::unique_ptr<muffin::MarkdownNode>>& replacements,
    qsizetype editStart,
    qsizetype editEnd,
    qsizetype editDelta) {
  const auto& oldBlocks = document.root().children();
  for (qsizetype oldIndex = slice.first; oldIndex < slice.first + slice.count && oldIndex < static_cast<qsizetype>(oldBlocks.size()); ++oldIndex) {
    const muffin::MarkdownNode& oldNode = *oldBlocks.at(static_cast<size_t>(oldIndex));
    const muffin::SourceRange expectedRange = stableRangeAfterEdit(oldNode.sourceRange(), editStart, editEnd, editDelta);
    for (const auto& replacement : replacements) {
      if (replacement->type() == oldNode.type() && sameStableRange(expectedRange, replacement->sourceRange())) {
        inheritIdsByStructure(oldNode, *replacement);
        break;
      }
    }
  }
}

void applyNodeHints(
    const muffin::MarkdownDocument& document,
    std::vector<std::unique_ptr<muffin::MarkdownNode>>& replacements,
    const QVector<muffin::LocalEditNodeHint>& nodeHints) {
  for (const muffin::LocalEditNodeHint& hint : nodeHints) {
    if (!hint.nodeId.isValid()) {
      continue;
    }
    muffin::MarkdownNode* oldNode = document.node(hint.nodeId);
    if (!oldNode) {
      continue;
    }
    for (auto& replacement : replacements) {
      muffin::MarkdownNode* candidate = nodeAtSourceOffset(*replacement, hint);
      if (candidate && candidate->type() == oldNode->type()) {
        inheritIdsByStructure(*oldNode, *candidate);
        break;
      }
    }
  }
}

muffin::MarkdownNode* tableByIdOrIndex(muffin::MarkdownDocument& document, muffin::NodeId tableId, int tableIndex) {
  if (tableId.isValid()) {
    if (muffin::MarkdownNode* table = document.node(tableId)) {
      if (table->type() == muffin::BlockType::Table) {
        return table;
      }
    }
  }

  if (tableIndex < 0) {
    return nullptr;
  }

  int index = 0;
  const auto visit = [&](const auto& self, muffin::MarkdownNode& node) -> muffin::MarkdownNode* {
    if (node.type() == muffin::BlockType::Table) {
      if (index == tableIndex) {
        return &node;
      }
      ++index;
    }
    for (const auto& child : node.children()) {
      if (muffin::MarkdownNode* found = self(self, *child)) {
        return found;
      }
    }
    return nullptr;
  };
  return visit(visit, document.root());
}

bool topLevelStructureChanged(
    const std::vector<std::unique_ptr<muffin::MarkdownNode>>& oldBlocks,
    qsizetype first,
    qsizetype count,
    const std::vector<std::unique_ptr<muffin::MarkdownNode>>& replacements) {
  if (count != static_cast<qsizetype>(replacements.size())) {
    return true;
  }
  for (qsizetype i = 0; i < count; ++i) {
    const muffin::MarkdownNode& oldNode = *oldBlocks.at(static_cast<size_t>(first + i));
    const muffin::MarkdownNode& newNode = *replacements.at(static_cast<size_t>(i));
    if (oldNode.id() != newNode.id() || oldNode.type() != newNode.type()) {
      return true;
    }
  }
  return false;
}

muffin::MarkdownNode* nodeByTypeIndex(muffin::MarkdownDocument& document, muffin::BlockType type, int targetIndex) {
  if (type == muffin::BlockType::Unknown || targetIndex < 0) {
    return nullptr;
  }

  int index = 0;
  const auto visit = [&](const auto& self, muffin::MarkdownNode& node) -> muffin::MarkdownNode* {
    if (node.type() == type) {
      if (index == targetIndex) {
        return &node;
      }
      ++index;
    }
    for (const auto& child : node.children()) {
      if (muffin::MarkdownNode* found = self(self, *child)) {
        return found;
      }
    }
    return nullptr;
  };
  return visit(visit, document.root());
}

muffin::MarkdownNode* nodeByIdOrTypeIndex(muffin::MarkdownDocument& document, muffin::NodeId nodeId, muffin::BlockType type, int nodeIndex) {
  if (nodeId.isValid()) {
    if (muffin::MarkdownNode* node = document.node(nodeId)) {
      if (node->type() == type) {
        return node;
      }
    }
  }
  return nodeByTypeIndex(document, type, nodeIndex);
}

}  // namespace

muffin::DocumentSession::DocumentSession(QObject* parent) : QObject(parent) {
  connect(&document_, &MarkdownDocument::modifiedChanged, this, &DocumentSession::modifiedChanged);
  newDocument();
  parseWatcher_ = new QFutureWatcher<std::shared_ptr<ParseResult>>(this);
  connect(parseWatcher_, &QFutureWatcher<std::shared_ptr<ParseResult>>::finished, this, &DocumentSession::finishAsyncParse);
}

muffin::MarkdownDocument& muffin::DocumentSession::document() {
  return document_;
}

const muffin::MarkdownDocument& muffin::DocumentSession::document() const {
  return document_;
}

QString muffin::DocumentSession::filePath() const {
  return filePath_;
}

QString muffin::DocumentSession::displayName() const {
  if (filePath_.isEmpty()) {
    return tr("Untitled");
  }
  return QFileInfo(filePath_).fileName();
}

qint64 muffin::DocumentSession::lastParseElapsedMs() const {
  return lastParseElapsedMs_;
}

bool muffin::DocumentSession::lastParseWasLocalEdit() const {
  return lastParseWasLocalEdit_;
}

bool muffin::DocumentSession::lastLocalEditChangedTopLevelStructure() const {
  return lastLocalEditChangedTopLevelStructure_;
}

muffin::TopLevelRangeChange muffin::DocumentSession::lastLocalTopLevelRangeChange() const {
  return lastLocalTopLevelRangeChange_;
}

void muffin::DocumentSession::newDocument() {
  filePath_.clear();
  emit filePathChanged(filePath_);
  parseAndStore(QString(), false);
  emit documentTextChanged(QString());
}

void muffin::DocumentSession::setFilePath(QString path) {
  if (filePath_ == path) {
    return;
  }
  filePath_ = std::move(path);
  emit filePathChanged(filePath_);
}

void muffin::DocumentSession::setMarkdownText(QString text, bool modified) {
  parseAndStore(std::move(text), modified);
  emit documentTextChanged(document_.markdownText().toString());
}

void muffin::DocumentSession::updateFromEditor(QString text) {
  parseAndStore(std::move(text), true);
}

void muffin::DocumentSession::applyMarkdownText(QString text, bool modified, QVector<qsizetype> demoteAtOffsets) {
  parseAndStore(std::move(text), modified, std::move(demoteAtOffsets));
  emit documentTextChanged(document_.markdownText().toString());
}

bool muffin::DocumentSession::isAsyncParseInProgress() const {
  return parseWatcher_ != nullptr && parseWatcher_->isRunning();
}

bool muffin::DocumentSession::applyTextDelta(
    qsizetype sourceStart,
    qsizetype removedLength,
    QString insertedText,
    bool modified,
    QVector<LocalEditNodeHint> nodeHints) {
  // While an async open parse is in flight, document_ still holds the STALE pre-open text. A local
  // edit now would land on the wrong content, and bumping parseGeneration_ to "win" would discard
  // the worker's result for the file the user actually opened — silent data loss. Reject the edit
  // and leave the worker to finish instead. InputController drops keystrokes upstream so this branch
  // is normally unreachable; it guards undo / table-snapshot / render-facade paths that bypass it.
  // Do NOT bump parseGeneration_ here — that self-supersede is exactly what loses the open.
  if (isAsyncParseInProgress()) {
    warnLocalEditRejected(
        "async open parse in flight",
        sourceStart,
        sourceStart >= 0 && removedLength >= 0 ? sourceStart + removedLength : qsizetype(-1),
        insertedText.size(),
        document_.pieceText().size());
    return false;
  }
  ++parseGeneration_;  // record this edit as a state change (no in-flight worker remains to supersede)
  lastLocalEditChangedTopLevelStructure_ = false;
  lastLocalTopLevelRangeChange_ = {};
  if (sourceStart < 0 || removedLength < 0 || sourceStart + removedLength > document_.pieceText().size()) {
    warnLocalEditRejected(
        "invalid text delta range",
        sourceStart,
        sourceStart >= 0 && removedLength >= 0 ? sourceStart + removedLength : qsizetype(-1),
        insertedText.size(),
        document_.pieceText().size());
    return false;
  }
  if (!tryApplyTopLevelLocalEdit(sourceStart, sourceStart + removedLength, insertedText, modified, nodeHints)) {
    qCDebug(sessionPerf).nospace() << "localEdit.rejected start=" << sourceStart << " removedLen=" << removedLength
        << " insertedLen=" << insertedText.size() << " -> caller full-reparses";
    lastLocalEditChangedTopLevelStructure_ = false;
    lastLocalTopLevelRangeChange_ = {};
    return false;
  }
  emit documentLocallyEdited(sourceStart, removedLength, insertedText);
  qCDebug(sessionPerf).nospace() << "localEdit.applied start=" << sourceStart
      << " removedLen=" << removedLength << " insertedLen=" << insertedText.size();
  return true;
}

bool muffin::DocumentSession::applyTableSnapshot(NodeId tableId, int tableIndex, const MarkdownNode& tableSnapshot, bool modified) {
  if (tableSnapshot.type() != BlockType::Table) {
    return false;
  }

  MarkdownNode* currentTable = tableByIdOrIndex(document_, tableId, tableIndex);
  if (!currentTable) {
    return false;
  }

  const SourceRange range = currentTable->sourceRange();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > document_.pieceText().size()) {
    return false;
  }

  MarkdownSerializer serializer;
  const QString replacementText = serializer.serializeBlock(tableSnapshot);
  QVector<LocalEditNodeHint> nodeHints{LocalEditNodeHint{tableId.isValid() ? tableId : currentTable->id(), range.byteStart, BlockType::Table}};
  return applyTextDelta(range.byteStart, range.byteEnd - range.byteStart, replacementText, modified, std::move(nodeHints));
}

bool muffin::DocumentSession::applyNodeSnapshot(NodeId nodeId, BlockType nodeType, int nodeIndex, const MarkdownNode& nodeSnapshot, bool modified) {
  if (nodeType == BlockType::Unknown || nodeSnapshot.type() != nodeType) {
    return false;
  }

  MarkdownNode* currentNode = nodeByIdOrTypeIndex(document_, nodeId, nodeType, nodeIndex);
  if (!currentNode) {
    return false;
  }

  const SourceRange range = fullBlockSourceRange(*currentNode, document_.markdownText());
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > document_.pieceText().size()) {
    return false;
  }

  MarkdownSerializer serializer;
  const QString replacementText = serializer.serializeBlock(nodeSnapshot);
  QVector<LocalEditNodeHint> nodeHints{LocalEditNodeHint{nodeId.isValid() ? nodeId : currentNode->id(), range.byteStart, nodeType}};
  return applyTextDelta(range.byteStart, range.byteEnd - range.byteStart, replacementText, modified, std::move(nodeHints));
}

bool muffin::DocumentSession::applyInsertedNode(
    NodeId nodeId,
    BlockType nodeType,
    qsizetype sourceStart,
    qsizetype targetSourceOffset,
    qsizetype removedLength,
    QString insertedText,
    bool modified) {
  if (!nodeId.isValid() || nodeType == BlockType::Unknown) {
    return false;
  }

  QVector<LocalEditNodeHint> nodeHints;
  if (!insertedText.isEmpty()) {
    nodeHints.push_back(LocalEditNodeHint{nodeId, targetSourceOffset, nodeType});
  }
  return applyTextDelta(sourceStart, removedLength, std::move(insertedText), modified, std::move(nodeHints));
}

void muffin::DocumentSession::parseAndStore(QString text, bool modified, QVector<qsizetype> demoteAtOffsets, bool async) {
  ++parseGeneration_;  // any new parse supersedes an in-flight async worker
  if (async) {
    // Run only the (Qt-free, stateless) parser on a worker thread; finishAsyncParse does the
    // QObject-mutating setMarkdownText + relativize + emit parsed on the GUI thread. pendingText_ is
    // captured (shared copy) before the worker takes ownership of `text`.
    pendingText_ = text;
    pendingModified_ = modified;
    pendingDemoteAtOffsets_ = std::move(demoteAtOffsets);
    const ParseOptions options = parseOptions_;
    launchGeneration_ = parseGeneration_;
    emit parseBusy(true);
    parseWatcher_->setFuture(QtConcurrent::run(
        [text = std::move(text), options, this]() -> std::shared_ptr<ParseResult> {
          return std::make_shared<ParseResult>(parser_.parseDocument(QStringView(text), options));
        }));
    return;
  }
  PerfTimer perf("session.fullParse");
  ParseResult result;
  {
    PerfTimer parsePerf("session.parse");
    result = parser_.parseDocument(QStringView(text), parseOptions_);
  }
  lastParseElapsedMs_ = result.elapsedMs;
  lastParseWasLocalEdit_ = false;
  lastLocalEditChangedTopLevelStructure_ = false;
  lastLocalTopLevelRangeChange_ = {};
  {
    PerfTimer buildPerf("session.buildDocument");
    document_.setMarkdownText(std::move(text), std::move(result.root));
  }
  document_.setModified(modified);
  if (!demoteAtOffsets.isEmpty()) {
    demotePendingMarkersAtOffsets(document_.markdownText().toString(), document_.root(), demoteAtOffsets);
  }
  // Block-relative offsets: convert each top-level block's subtree to relative now that every
  // parse/demote pass has written absolute offsets. Runs once per full parse (not per keystroke).
  {
    PerfTimer relativizePerf("session.relativize");
    for (const auto& child : document_.root().children()) {
      if (child) {
        // textDoc left null: InlineNode text-sharing is implemented (bindSharedText) but currently
        // DISABLED — it caused a table-undo correctness regression (block-relative offset ×
        // clone/snapshot interaction). Re-enable after that's resolved. Infrastructure is inert.
        child->relativizeDescendants();
      }
    }
  }
  emit parsed(lastParseElapsedMs_);
}

void muffin::DocumentSession::finishAsyncParse() {
  // A newer parse/edit may have landed while this worker ran; its result would clobber the live
  // document, so discard it. (The worker still ran to completion — cmark isn't cancellable.)
  if (launchGeneration_ != parseGeneration_) {
    emit parseBusy(false);
    return;
  }
  PerfTimer perf("session.fullParse");
  std::shared_ptr<ParseResult> resultPtr = parseWatcher_->result();
  ParseResult& result = *resultPtr;
  lastParseElapsedMs_ = result.elapsedMs;
  lastParseWasLocalEdit_ = false;
  lastLocalEditChangedTopLevelStructure_ = false;
  lastLocalTopLevelRangeChange_ = {};
  {
    PerfTimer buildPerf("session.buildDocument");
    document_.setMarkdownText(pendingText_, std::move(result.root));
  }
  document_.setModified(pendingModified_);
  if (!pendingDemoteAtOffsets_.isEmpty()) {
    demotePendingMarkersAtOffsets(document_.markdownText().toString(), document_.root(), pendingDemoteAtOffsets_);
    pendingDemoteAtOffsets_.clear();
  }
  {
    PerfTimer relativizePerf("session.relativize");
    for (const auto& child : document_.root().children()) {
      if (child) {
        child->relativizeDescendants();
      }
    }
  }
  emit parsed(lastParseElapsedMs_);
  emit documentTextChanged(pendingText_);
  emit parseBusy(false);
}

void muffin::DocumentSession::openDocumentAsync(QString text) {
  // Async open: document_ isn't updated until finishAsyncParse, so documentTextChanged is emitted
  // there (not here) to avoid broadcasting stale text. filePath is set by the caller (FileController).
  parseAndStore(std::move(text), false, {}, /*async=*/true);
}

void muffin::DocumentSession::setParseOptions(ParseOptions options) {
  if (options == parseOptions_) {
    return;
  }
  parseOptions_ = options;
  // Re-parse in place, preserving the current text and modified flag. parseAndStore sets
  // lastParseWasLocalEdit_=false, so the rendered view rebuilds via the `parsed` signal.
  parseAndStore(document_.markdownText().toString(), document_.isModified());
}

bool muffin::DocumentSession::tryApplyTopLevelLocalEdit(
    qsizetype sourceStart,
    qsizetype sourceEnd,
    const QString& replacementText,
    bool modified,
    const QVector<LocalEditNodeHint>& nodeHints) {
  PerfTimer perf("session.localParse");
  const PieceTable& oldText = document_.pieceText();  // Phase 2a: read the piece-table, not the QString cache
  const QString removedText = oldText.mid(sourceStart, sourceEnd - sourceStart);
  TopLevelSlice slice = chooseTopLevelSlice(document_, sourceStart, sourceEnd, isBlankLineStructuralEdit(removedText, replacementText), replacementText.isEmpty());
  qCDebug(sessionPerf).nospace() << "slice.chosen first=" << slice.first << " count=" << slice.count
      << " src=[" << slice.sourceStart << "," << slice.sourceEnd << "]";
  if (slice.first < 0 || slice.sourceStart < 0 || slice.sourceEnd < slice.sourceStart) {
    warnLocalEditSliceRejected(
        "could not choose a valid top-level slice",
        sourceStart,
        sourceEnd,
        replacementText.size(),
        oldText.size(),
        slice);
    return false;
  }

  const qsizetype editDelta = replacementText.size() - (sourceEnd - sourceStart);
  const qsizetype nextSliceEnd = slice.sourceEnd + editDelta;
  const qsizetype nextTextSize = oldText.size() + editDelta;
  if (nextSliceEnd < slice.sourceStart || nextSliceEnd > nextTextSize) {
    warnLocalEditSliceRejected(
        "edited slice would exceed updated document bounds",
        sourceStart,
        sourceEnd,
        replacementText.size(),
        oldText.size(),
        slice);
    return false;
  }

  QString sliceMarkdown;
  ParseResult parsedSlice;
  {
    PerfTimer sliceParsePerf("session.local.sliceParse");
    sliceMarkdown = oldText.mid(slice.sourceStart, sourceStart - slice.sourceStart);
    sliceMarkdown += replacementText;
    sliceMarkdown += oldText.mid(sourceEnd, slice.sourceEnd - sourceEnd);
    ParseOptions sliceOptions = parseOptions_;
    sliceOptions.enableFrontMatter = slice.sourceStart == 0;
    parsedSlice = parser_.parseDocument(QStringView(sliceMarkdown), sliceOptions);
  }
  if (!parsedSlice.root) {
    warnLocalEditSliceRejected(
        "local slice parse produced no root node",
        sourceStart,
        sourceEnd,
        replacementText.size(),
        oldText.size(),
        slice);
    return false;
  }

  // Lazy markers: keep a still-incomplete list bullet / fence / math opener as a paragraph
  // instead of letting cmark snap it into a block while the user is mid-keystroke. Demote while
  // the freshly-parsed nodes are still SLICE-RELATIVE (they index into sliceMarkdown, which is
  // exactly the post-edit text of the slice region). The previous code shifted them to absolute
  // offsets first and then built a FULL-document postEditText copy to demote against — an O(doc)
  // cost on every keystroke. Demoting slice-relative nodes against sliceMarkdown is equivalent
  // (demotePendingMarkerToParagraph is frame-consistent: it only reads node.range against the
  // passed text) and avoids the copy.
  {
    PerfTimer demoteWalkPerf("session.local.demoteWalk");
    for (const auto& child : parsedSlice.root->children()) {
      if (child) {
        demotePendingMarkersInSubtree(sliceMarkdown, *child);
      }
    }
  }

  std::vector<std::unique_ptr<MarkdownNode>> replacements;
  int sliceLineDelta;
  {
    PerfTimer lineProbe("session.local.lineForOffset");
    // oldText == document_.markdownText() here (replaceTopLevelRange has not run yet), and
    // document_.lineOffsets() is maintained incrementally against that exact text via applyEdit on
    // every edit — so the cache answers in O(log n) instead of an O(offset) scan of oldText that
    // dominated per-keystroke cost near the end of a large document (~64ms@50MB).
    sliceLineDelta = document_.lineOffsets().lineForOffset(slice.sourceStart) - 1;
  }
  while (!parsedSlice.root->children().empty()) {
    auto child = parsedSlice.root->detachChild(0);
    // Block-relative offsets: relativize this block's descendants while its own range is still
    // SLICE-RELATIVE (the base is the block's slice-relative byteStart), THEN absolutize the
    // block's own range. Descendants stay relative-to-block; sourceRange() resolves them to
    // absolute via the block's now-absolute byteStart.
    child->relativizeDescendants();
    SourceRange own = child->sourceRange();  // detached top-level block: stored, slice-relative
    if (own.byteStart >= 0 && own.byteEnd >= own.byteStart) {
      own.byteStart += slice.sourceStart;
      own.byteEnd += slice.sourceStart;
    }
    if (own.lineStart > 0) {
      own.lineStart += sliceLineDelta;
    }
    if (own.lineEnd > 0) {
      own.lineEnd += sliceLineDelta;
    }
    child->setSourceRange(own);
    replacements.push_back(std::move(child));
  }
  // For a pure insertion after an existing block, drop the context-dependent leading VEP the
  // isolated slice parse synthesized from its own leading blank line (see
  // stripLeadingVirtualEmptyParagraphsForInsertion). Slices that replace real blocks (count > 0)
  // or start at the document root (slice.first == 0) keep their VEPs — those are anchored to real
  // content or document start, not to a neighbor the slice can't see.
  if (slice.count == 0 && slice.first > 0) {
    stripSeparatorVirtualEmptyParagraphs(replacements);
  }
  // When the slice started at editStart because the preceding block was skipped (its trailing
  // newlines were touched by the deletion), any leading VEP in the replacements is a separator
  // artifact synthesized from residual whitespace after the removed block — not intended content.
  // The skipped block is the real preceding content, so drop the leading VEP(s).
  if (slice.startsAtEditStart) {
    while (!replacements.empty() && isVirtualEmptyParagraph(*replacements.front())) {
      replacements.erase(replacements.begin());
    }
  }
  inheritIdsForUnchangedTopLevelBlocks(document_, slice, replacements, sourceStart, sourceEnd, editDelta);
  applyNodeHints(document_, replacements, nodeHints);
  const qsizetype replacementCount = static_cast<qsizetype>(replacements.size());
  lastLocalEditChangedTopLevelStructure_ = topLevelStructureChanged(document_.root().children(), slice.first, slice.count, replacements);

  {
    PerfTimer shiftSuffixPerf("session.local.shiftSuffix");
    const int editLineDelta = countNewlines(QStringView(replacementText)) - countNewlines(oldText.mid(sourceStart, sourceEnd - sourceStart));
    const qsizetype firstFollowing = slice.first + slice.count;
    auto& existingBlocks = document_.root().children();
    // Block-relative offsets: only each suffix top-level block's OWN sourceRange shifts
    // (descendants are relative to it and stay invariant). O(num top-level blocks), no recursion —
    // replaces the old O(all suffix nodes+inlines) shiftRanges sweep. shiftOwnSourceRange mutates
    // metadata_ in place (no SourceRange struct copy-out/copy-in per block).
    for (qsizetype i = firstFollowing; i < static_cast<qsizetype>(existingBlocks.size()); ++i) {
      existingBlocks.at(static_cast<size_t>(i))->shiftOwnSourceRange(editDelta, editLineDelta);
    }
    qCDebug(sessionPerf).nospace() << "session.local.shiftSuffixCount n="
        << (static_cast<qsizetype>(existingBlocks.size()) - firstFollowing);
  }

  document_.replaceTopLevelRange(slice.first, slice.count, std::move(replacements), sourceStart, sourceEnd, replacementText);
  lastLocalTopLevelRangeChange_ = {slice.first, slice.count, replacementCount, document_.revision()};
  document_.setModified(modified);
  lastParseElapsedMs_ = parsedSlice.elapsedMs;
  lastParseWasLocalEdit_ = true;
  emit parsed(lastParseElapsedMs_);
  return true;
}
