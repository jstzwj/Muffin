#include "app/DocumentSession.h"

#include <QFileInfo>

#include <utility>

namespace muffin {
namespace {

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

TopLevelSlice chooseTopLevelSlice(const MarkdownDocument& document, qsizetype editStart, qsizetype editEnd) {
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

  if (slice.first >= 0 && slice.count == 1) {
    const MarkdownNode& only = *blocks.at(static_cast<size_t>(slice.first));
    const SourceRange range = only.sourceRange();
    if (only.type() == BlockType::Paragraph && range.byteStart == range.byteEnd) {
      if (only.previousSibling()) {
        --slice.first;
        ++slice.count;
        slice.sourceStart = only.previousSibling()->sourceRange().byteStart;
        slice.sourceEnd = only.nextSibling() ? only.nextSibling()->sourceRange().byteStart : document.markdownText().size();
      } else if (only.nextSibling()) {
        ++slice.count;
        slice.sourceStart = 0;
        slice.sourceEnd = only.nextSibling()->sourceRange().byteEnd;
      } else {
        slice.sourceStart = 0;
        slice.sourceEnd = document.markdownText().size();
      }
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

QString DocumentSession::markdownText() const {
  return document_.markdownText();
}

qint64 DocumentSession::lastParseElapsedMs() const {
  return lastParseElapsedMs_;
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

bool DocumentSession::applyLocalMarkdownEdit(
    qsizetype sourceStart,
    qsizetype sourceEnd,
    QString replacementText,
    bool modified,
    QVector<LocalEditNodeHint> nodeHints) {
  if (sourceStart < 0 || sourceEnd < sourceStart || sourceEnd > document_.markdownText().size()) {
    return false;
  }
  if (!tryApplyTopLevelLocalEdit(sourceStart, sourceEnd, std::move(replacementText), modified, nodeHints)) {
    return false;
  }
  emit documentTextChanged(document_.markdownText());
  return true;
}

void DocumentSession::parseAndStore(QString text, bool modified) {
  ParseResult result = parser_.parseDocument(QStringView(text), parseOptions_);
  lastParseElapsedMs_ = result.elapsedMs;
  document_.setMarkdownText(std::move(text), std::move(result.root));
  document_.setModified(modified);
  emit parsed(lastParseElapsedMs_);
}

bool DocumentSession::tryApplyTopLevelLocalEdit(
    qsizetype sourceStart,
    qsizetype sourceEnd,
    QString replacementText,
    bool modified,
    const QVector<LocalEditNodeHint>& nodeHints) {
  const QString oldText = document_.markdownText();
  TopLevelSlice slice = chooseTopLevelSlice(document_, sourceStart, sourceEnd);
  if (slice.first < 0 || slice.sourceStart < 0 || slice.sourceEnd < slice.sourceStart) {
    return false;
  }

  QString nextText = oldText;
  nextText.replace(sourceStart, sourceEnd - sourceStart, replacementText);
  const qsizetype editDelta = replacementText.size() - (sourceEnd - sourceStart);
  const qsizetype nextSliceEnd = slice.sourceEnd + editDelta;
  if (nextSliceEnd < slice.sourceStart || nextSliceEnd > nextText.size()) {
    return false;
  }

  const QString sliceMarkdown = nextText.mid(slice.sourceStart, nextSliceEnd - slice.sourceStart);
  ParseResult parsedSlice = parser_.parseDocument(QStringView(sliceMarkdown), parseOptions_);
  if (!parsedSlice.root) {
    return false;
  }

  std::vector<std::unique_ptr<MarkdownNode>> replacements;
  const int sliceLineDelta = lineForOffset(nextText, slice.sourceStart) - 1;
  while (!parsedSlice.root->children().empty()) {
    auto child = parsedSlice.root->detachChild(0);
    shiftRanges(*child, slice.sourceStart, sliceLineDelta);
    replacements.push_back(std::move(child));
  }
  applyNodeHints(document_, replacements, nodeHints);

  const int editLineDelta = countNewlines(QStringView(replacementText)) - countNewlines(QStringView(oldText).mid(sourceStart, sourceEnd - sourceStart));
  const qsizetype firstFollowing = slice.first + slice.count;
  auto& existingBlocks = document_.root().children();
  for (qsizetype i = firstFollowing; i < static_cast<qsizetype>(existingBlocks.size()); ++i) {
    shiftRanges(*existingBlocks.at(static_cast<size_t>(i)), editDelta, editLineDelta);
  }

  document_.replaceTopLevelRange(slice.first, slice.count, std::move(replacements), std::move(nextText));
  document_.setModified(modified);
  lastParseElapsedMs_ = parsedSlice.elapsedMs;
  emit parsed(lastParseElapsedMs_);
  return true;
}

}  // namespace muffin
