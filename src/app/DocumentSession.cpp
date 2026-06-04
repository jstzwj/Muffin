#include "app/DocumentSession.h"

#include "parser/MarkdownSerializer.h"

#include <QFileInfo>
#include <QElapsedTimer>
#include <QLoggingCategory>

#include <utility>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(sessionPerf, "muffin.perf", QtWarningMsg)

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(sessionPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(sessionPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
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
};

bool isEditableTopLevelType(BlockType type) {
  switch (type) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::List:
    case BlockType::BlockQuote:
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
    case BlockType::MathBlock:
    case BlockType::Table:
      return true;
    default:
      return false;
  }
}

SourceRange usableRange(const MarkdownNode& node) {
  return node.sourceRange();
}

bool overlapsEdit(const SourceRange& range, qsizetype editStart, qsizetype editEnd) {
  return range.byteStart <= editEnd && range.byteEnd >= editStart;
}

bool isVirtualEmptyParagraph(const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  return node.type() == BlockType::Paragraph && range.byteStart >= 0 && range.byteEnd == range.byteStart;
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

int lineForOffset(const QString& text, qsizetype offset) {
  int line = 1;
  const qsizetype bounded = qBound<qsizetype>(0, offset, text.size());
  for (qsizetype i = 0; i < bounded; ++i) {
    if (text.at(i) == QLatin1Char('\n')) {
      ++line;
    }
  }
  return line;
}

TopLevelSlice chooseTopLevelSlice(const MarkdownDocument& document, qsizetype editStart, qsizetype editEnd, bool blankLineStructuralEdit) {
  TopLevelSlice slice;
  const auto& blocks = document.root().children();
  if (blocks.empty()) {
    slice.first = 0;
    slice.count = 0;
    slice.sourceStart = 0;
    slice.sourceEnd = document.markdownText().size();
    return slice;
  }

  for (qsizetype i = 0; i < static_cast<qsizetype>(blocks.size()); ++i) {
    const MarkdownNode& block = *blocks.at(static_cast<size_t>(i));
    const SourceRange range = usableRange(block);
    if (range.byteStart < 0 || range.byteEnd < range.byteStart || !isEditableTopLevelType(block.type())) {
      continue;
    }
    if (overlapsEdit(range, editStart, editEnd) || editStart == range.byteStart || editStart == range.byteEnd) {
      if (slice.first < 0) {
        slice.first = i;
        slice.sourceStart = range.byteStart;
      }
      slice.count = i - slice.first + 1;
      slice.sourceEnd = range.byteEnd;
    }
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

      qsizetype expandedFirst = selectedFirst;
      qsizetype expandedEnd = selectedEnd;
      if (expandedFirst > 0) {
        --expandedFirst;
      }
      if (expandedEnd < static_cast<qsizetype>(blocks.size())) {
        ++expandedEnd;
      }

      slice.first = expandedFirst;
      slice.count = expandedEnd - expandedFirst;
      slice.sourceStart = expandedFirst > 0 ? blocks.at(static_cast<size_t>(expandedFirst))->sourceRange().byteStart : 0;
      slice.sourceEnd = expandedEnd < static_cast<qsizetype>(blocks.size())
                            ? blocks.at(static_cast<size_t>(expandedEnd - 1))->sourceRange().byteEnd
                            : document.markdownText().size();
    }
  }

  if (slice.first >= 0) {
    return slice;
  }

  for (qsizetype i = 0; i < static_cast<qsizetype>(blocks.size()); ++i) {
    const SourceRange range = usableRange(*blocks.at(static_cast<size_t>(i)));
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
  slice.sourceStart = document.markdownText().size();
  slice.sourceEnd = document.markdownText().size();
  return slice;
}

void shiftRanges(MarkdownNode& node, qsizetype delta, int lineDelta) {
  SourceRange range = node.sourceRange();
  if (range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
    range.byteStart += delta;
    range.byteEnd += delta;
    if (range.lineStart > 0) {
      range.lineStart += lineDelta;
    }
    if (range.lineEnd > 0) {
      range.lineEnd += lineDelta;
    }
    node.setSourceRange(range);
  }
  for (const auto& child : node.children()) {
    shiftRanges(*child, delta, lineDelta);
  }
}

void inheritIdsByStructure(const MarkdownNode& oldNode, MarkdownNode& newNode) {
  if (oldNode.type() != newNode.type()) {
    return;
  }
  newNode.setId(oldNode.id());
  const qsizetype childCount = qMin<qsizetype>(oldNode.children().size(), newNode.children().size());
  for (qsizetype i = 0; i < childCount; ++i) {
    inheritIdsByStructure(*oldNode.children().at(static_cast<size_t>(i)), *newNode.children().at(static_cast<size_t>(i)));
  }
}

MarkdownNode* nodeAtSourceOffset(MarkdownNode& node, const LocalEditNodeHint& hint) {
  if (hint.targetSourceOffset < 0) {
    return nullptr;
  }
  const SourceRange range = node.sourceRange();
  if ((hint.type == BlockType::Unknown || node.type() == hint.type) &&
      range.byteStart <= hint.targetSourceOffset && range.byteEnd >= hint.targetSourceOffset) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = nodeAtSourceOffset(*child, hint)) {
      return found;
    }
  }
  return nullptr;
}

SourceRange stableRangeAfterEdit(SourceRange range, qsizetype editStart, qsizetype editEnd, qsizetype editDelta) {
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

bool sameStableRange(const SourceRange& oldRange, const SourceRange& newRange) {
  return oldRange.byteStart >= 0 && oldRange.byteEnd >= oldRange.byteStart && oldRange.byteStart == newRange.byteStart &&
         oldRange.byteEnd == newRange.byteEnd;
}

void inheritIdsForUnchangedTopLevelBlocks(
    const MarkdownDocument& document,
    const TopLevelSlice& slice,
    const std::vector<std::unique_ptr<MarkdownNode>>& replacements,
    qsizetype editStart,
    qsizetype editEnd,
    qsizetype editDelta) {
  const auto& oldBlocks = document.root().children();
  for (qsizetype oldIndex = slice.first; oldIndex < slice.first + slice.count && oldIndex < static_cast<qsizetype>(oldBlocks.size()); ++oldIndex) {
    const MarkdownNode& oldNode = *oldBlocks.at(static_cast<size_t>(oldIndex));
    const SourceRange expectedRange = stableRangeAfterEdit(oldNode.sourceRange(), editStart, editEnd, editDelta);
    for (const auto& replacement : replacements) {
      if (replacement->type() == oldNode.type() && sameStableRange(expectedRange, replacement->sourceRange())) {
        inheritIdsByStructure(oldNode, *replacement);
        break;
      }
    }
  }
}

void applyNodeHints(
    const MarkdownDocument& document,
    std::vector<std::unique_ptr<MarkdownNode>>& replacements,
    const QVector<LocalEditNodeHint>& nodeHints) {
  for (const LocalEditNodeHint& hint : nodeHints) {
    if (!hint.nodeId.isValid()) {
      continue;
    }
    MarkdownNode* oldNode = document.node(hint.nodeId);
    if (!oldNode) {
      continue;
    }
    for (auto& replacement : replacements) {
      MarkdownNode* candidate = nodeAtSourceOffset(*replacement, hint);
      if (candidate && candidate->type() == oldNode->type()) {
        inheritIdsByStructure(*oldNode, *candidate);
        break;
      }
    }
  }
}

MarkdownNode* tableByIdOrIndex(MarkdownDocument& document, NodeId tableId, int tableIndex) {
  if (tableId.isValid()) {
    if (MarkdownNode* table = document.node(tableId)) {
      if (table->type() == BlockType::Table) {
        return table;
      }
    }
  }

  if (tableIndex < 0) {
    return nullptr;
  }

  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == BlockType::Table) {
      if (index == tableIndex) {
        return &node;
      }
      ++index;
    }
    for (const auto& child : node.children()) {
      if (MarkdownNode* found = self(self, *child)) {
        return found;
      }
    }
    return nullptr;
  };
  return visit(visit, document.root());
}

bool topLevelStructureChanged(
    const std::vector<std::unique_ptr<MarkdownNode>>& oldBlocks,
    qsizetype first,
    qsizetype count,
    const std::vector<std::unique_ptr<MarkdownNode>>& replacements) {
  if (count != static_cast<qsizetype>(replacements.size())) {
    return true;
  }
  for (qsizetype i = 0; i < count; ++i) {
    const MarkdownNode& oldNode = *oldBlocks.at(static_cast<size_t>(first + i));
    const MarkdownNode& newNode = *replacements.at(static_cast<size_t>(i));
    if (oldNode.id() != newNode.id() || oldNode.type() != newNode.type()) {
      return true;
    }
  }
  return false;
}

MarkdownNode* nodeByTypeIndex(MarkdownDocument& document, BlockType type, int targetIndex) {
  if (type == BlockType::Unknown || targetIndex < 0) {
    return nullptr;
  }

  int index = 0;
  const auto visit = [&](const auto& self, MarkdownNode& node) -> MarkdownNode* {
    if (node.type() == type) {
      if (index == targetIndex) {
        return &node;
      }
      ++index;
    }
    for (const auto& child : node.children()) {
      if (MarkdownNode* found = self(self, *child)) {
        return found;
      }
    }
    return nullptr;
  };
  return visit(visit, document.root());
}

MarkdownNode* nodeByIdOrTypeIndex(MarkdownDocument& document, NodeId nodeId, BlockType type, int nodeIndex) {
  if (nodeId.isValid()) {
    if (MarkdownNode* node = document.node(nodeId)) {
      if (node->type() == type) {
        return node;
      }
    }
  }
  return nodeByTypeIndex(document, type, nodeIndex);
}

}  // namespace

DocumentSession::DocumentSession(QObject* parent) : QObject(parent) {
  connect(&document_, &MarkdownDocument::modifiedChanged, this, &DocumentSession::modifiedChanged);
  newDocument();
}

MarkdownDocument& DocumentSession::document() {
  return document_;
}

const MarkdownDocument& DocumentSession::document() const {
  return document_;
}

QString DocumentSession::filePath() const {
  return filePath_;
}

QString DocumentSession::displayName() const {
  if (filePath_.isEmpty()) {
    return tr("Untitled");
  }
  return QFileInfo(filePath_).fileName();
}

const QString& DocumentSession::markdownText() const {
  return document_.markdownText();
}

qint64 DocumentSession::lastParseElapsedMs() const {
  return lastParseElapsedMs_;
}

bool DocumentSession::lastParseWasLocalEdit() const {
  return lastParseWasLocalEdit_;
}

bool DocumentSession::lastLocalEditChangedTopLevelStructure() const {
  return lastLocalEditChangedTopLevelStructure_;
}

void DocumentSession::newDocument() {
  filePath_.clear();
  emit filePathChanged(filePath_);
  parseAndStore(QString(), false);
  emit documentTextChanged(QString());
}

void DocumentSession::setFilePath(QString path) {
  if (filePath_ == path) {
    return;
  }
  filePath_ = std::move(path);
  emit filePathChanged(filePath_);
}

void DocumentSession::setMarkdownText(QString text, bool modified) {
  parseAndStore(std::move(text), modified);
  emit documentTextChanged(document_.markdownText());
}

void DocumentSession::updateFromEditor(QString text) {
  parseAndStore(std::move(text), true);
}

void DocumentSession::applyMarkdownText(QString text, bool modified) {
  parseAndStore(std::move(text), modified);
  emit documentTextChanged(document_.markdownText());
}

bool DocumentSession::applyTextDelta(
    qsizetype sourceStart,
    qsizetype removedLength,
    QString insertedText,
    bool modified,
    QVector<LocalEditNodeHint> nodeHints) {
  lastLocalEditChangedTopLevelStructure_ = false;
  if (sourceStart < 0 || removedLength < 0 || sourceStart + removedLength > document_.markdownText().size()) {
    return false;
  }
  if (!tryApplyTopLevelLocalEdit(sourceStart, sourceStart + removedLength, insertedText, modified, nodeHints)) {
    lastLocalEditChangedTopLevelStructure_ = false;
    return false;
  }
  emit documentLocallyEdited(sourceStart, removedLength, insertedText);
  return true;
}

bool DocumentSession::applyTableSnapshot(NodeId tableId, int tableIndex, const MarkdownNode& tableSnapshot, bool modified) {
  if (tableSnapshot.type() != BlockType::Table) {
    return false;
  }

  MarkdownNode* currentTable = tableByIdOrIndex(document_, tableId, tableIndex);
  if (!currentTable) {
    return false;
  }

  const SourceRange range = currentTable->sourceRange();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > document_.markdownText().size()) {
    return false;
  }

  MarkdownSerializer serializer;
  const QString replacementText = serializer.serializeBlock(tableSnapshot);
  QVector<LocalEditNodeHint> nodeHints{LocalEditNodeHint{tableId.isValid() ? tableId : currentTable->id(), range.byteStart, BlockType::Table}};
  return applyTextDelta(range.byteStart, range.byteEnd - range.byteStart, replacementText, modified, std::move(nodeHints));
}

bool DocumentSession::applyNodeSnapshot(NodeId nodeId, BlockType nodeType, int nodeIndex, const MarkdownNode& nodeSnapshot, bool modified) {
  if (nodeType == BlockType::Unknown || nodeSnapshot.type() != nodeType) {
    return false;
  }

  MarkdownNode* currentNode = nodeByIdOrTypeIndex(document_, nodeId, nodeType, nodeIndex);
  if (!currentNode) {
    return false;
  }

  const SourceRange range = currentNode->sourceRange();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > document_.markdownText().size()) {
    return false;
  }

  MarkdownSerializer serializer;
  const QString replacementText = serializer.serializeBlock(nodeSnapshot);
  QVector<LocalEditNodeHint> nodeHints{LocalEditNodeHint{nodeId.isValid() ? nodeId : currentNode->id(), range.byteStart, nodeType}};
  return applyTextDelta(range.byteStart, range.byteEnd - range.byteStart, replacementText, modified, std::move(nodeHints));
}

bool DocumentSession::applyInsertedNode(
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

void DocumentSession::parseAndStore(QString text, bool modified) {
  PerfTimer perf("session.fullParse");
  ParseResult result = parser_.parseDocument(QStringView(text), parseOptions_);
  lastParseElapsedMs_ = result.elapsedMs;
  lastParseWasLocalEdit_ = false;
  lastLocalEditChangedTopLevelStructure_ = false;
  document_.setMarkdownText(std::move(text), std::move(result.root));
  document_.setModified(modified);
  emit parsed(lastParseElapsedMs_);
}

bool DocumentSession::tryApplyTopLevelLocalEdit(
    qsizetype sourceStart,
    qsizetype sourceEnd,
    const QString& replacementText,
    bool modified,
    const QVector<LocalEditNodeHint>& nodeHints) {
  PerfTimer perf("session.localParse");
  const QString& oldText = document_.markdownText();
  const QStringView removedText = QStringView(oldText).mid(sourceStart, sourceEnd - sourceStart);
  TopLevelSlice slice = chooseTopLevelSlice(document_, sourceStart, sourceEnd, isBlankLineStructuralEdit(removedText, replacementText));
  if (slice.first < 0 || slice.sourceStart < 0 || slice.sourceEnd < slice.sourceStart) {
    return false;
  }

  const qsizetype editDelta = replacementText.size() - (sourceEnd - sourceStart);
  const qsizetype nextSliceEnd = slice.sourceEnd + editDelta;
  const qsizetype nextTextSize = oldText.size() + editDelta;
  if (nextSliceEnd < slice.sourceStart || nextSliceEnd > nextTextSize) {
    return false;
  }

  QString sliceMarkdown = oldText.mid(slice.sourceStart, sourceStart - slice.sourceStart);
  sliceMarkdown += replacementText;
  sliceMarkdown += oldText.mid(sourceEnd, slice.sourceEnd - sourceEnd);
  ParseResult parsedSlice = parser_.parseDocument(QStringView(sliceMarkdown), parseOptions_);
  if (!parsedSlice.root) {
    return false;
  }

  std::vector<std::unique_ptr<MarkdownNode>> replacements;
  const int sliceLineDelta = lineForOffset(oldText, slice.sourceStart) - 1;
  while (!parsedSlice.root->children().empty()) {
    auto child = parsedSlice.root->detachChild(0);
    shiftRanges(*child, slice.sourceStart, sliceLineDelta);
    replacements.push_back(std::move(child));
  }
  inheritIdsForUnchangedTopLevelBlocks(document_, slice, replacements, sourceStart, sourceEnd, editDelta);
  applyNodeHints(document_, replacements, nodeHints);
  lastLocalEditChangedTopLevelStructure_ = topLevelStructureChanged(document_.root().children(), slice.first, slice.count, replacements);

  const int editLineDelta = countNewlines(QStringView(replacementText)) - countNewlines(QStringView(oldText).mid(sourceStart, sourceEnd - sourceStart));
  const qsizetype firstFollowing = slice.first + slice.count;
  auto& existingBlocks = document_.root().children();
  for (qsizetype i = firstFollowing; i < static_cast<qsizetype>(existingBlocks.size()); ++i) {
    shiftRanges(*existingBlocks.at(static_cast<size_t>(i)), editDelta, editLineDelta);
  }

  document_.replaceTopLevelRange(slice.first, slice.count, std::move(replacements), sourceStart, sourceEnd, replacementText);
  document_.setModified(modified);
  lastParseElapsedMs_ = parsedSlice.elapsedMs;
  lastParseWasLocalEdit_ = true;
  emit parsed(lastParseElapsedMs_);
  return true;
}

}  // namespace muffin
