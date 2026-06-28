#include "document/MarkdownDocument.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

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
  // Rebuild the line-offset cache from the full text BEFORE moving it into the piece-table (the
  // table owns the buffer afterwards, and it is not a contiguous QStringView). text_ becomes the
  // sole source of truth; markdownText() materializes from it on demand.
  lineOffsets_.rebuild(QStringView(text));
  text_ = PieceTable(std::move(text));
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
