#include "document/MarkdownDocument.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

#include <utility>

namespace {

Q_LOGGING_CATEGORY(documentPerf, "muffin.perf", QtWarningMsg)

// Spare capacity maintained on markdownText_ so a length-changing replace reuses it instead of
// reallocating an exact-size buffer (and copying the whole document) on every keystroke. 1 MiB of
// headroom = ~500k single-char inserts before a single amortized realloc re-establishes it.
constexpr qsizetype kTextGrowthHeadroom = 1024 * 1024;

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

const QString& MarkdownDocument::markdownText() const {
  return markdownText_;
}

const LineStartOffsetCache& MarkdownDocument::lineOffsets() const {
  return lineOffsets_;
}

void MarkdownDocument::setMarkdownText(QString text, std::unique_ptr<MarkdownNode> root) {
  markdownText_ = std::move(text);
  markdownText_.reserve(markdownText_.size() + kTextGrowthHeadroom);
  lineOffsets_.rebuild(QStringView(markdownText_));
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
  {
    PerfTimer memmovePerf("session.local.replace.memmove");
    // Keep spare capacity so a length-changing replace reuses it instead of reallocating an
    // exact-size buffer and copying the whole document on every keystroke. reserve() is a cheap
    // no-op when enough slack already exists; the headroom is re-established only after it is
    // consumed by ~1M chars of net growth. Without this, replace()'s full realloc-copy dominated
    // per-keystroke cost (~24ms@50MB) regardless of edit position. Reserve exactly the post-replace
    // size plus headroom (N - removed + inserted + H) — an earlier form added `removed` too, over-
    // allocating by 2x the removed span on every edit.
    markdownText_.reserve(markdownText_.size() - (sourceEnd - sourceStart) + replacementText.size() + kTextGrowthHeadroom);
    markdownText_.replace(sourceStart, sourceEnd - sourceStart, replacementText);
  }
  {
    PerfTimer lineOffsetsPerf("session.local.replace.lineOffsets");
    lineOffsets_.applyEdit(sourceStart, sourceEnd - sourceStart, replacementText.size(), QStringView(markdownText_));
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

  ++revision_;
  emit documentReset();
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
  for (const auto& child : root_->children()) {
    if (child->sourceRange().containsByte(offset)) {
      return child.get();
    }
  }
  if (!root_->children().empty()) {
    return root_->children().back().get();
  }
  return nullptr;
}

void MarkdownDocument::replaceRoot(std::unique_ptr<MarkdownNode> root) {
  root_ = std::move(root);
  if (!root_) {
    root_ = std::make_unique<MarkdownNode>(BlockType::Document);
  }
  index_.rebuild(*root_);
  ++revision_;
  emit documentReset();
}

}  // namespace muffin
