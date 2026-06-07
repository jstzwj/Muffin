#include "document/DefinitionBlock.h"

namespace muffin {
namespace {

bool isHorizontalSpace(QChar ch) {
  return ch == QLatin1Char(' ') || ch == QLatin1Char('\t');
}

qsizetype trimEnd(QStringView text, qsizetype start, qsizetype end) {
  while (end > start && isHorizontalSpace(text.at(end - 1))) {
    --end;
  }
  return end;
}

qsizetype skipHorizontalSpace(QStringView text, qsizetype index) {
  while (index < text.size() && isHorizontalSpace(text.at(index))) {
    ++index;
  }
  return index;
}

bool parseLabel(QStringView line, qsizetype& cursor, bool& footnote, DefinitionFieldRange& labelRange) {
  if (cursor >= line.size() || line.at(cursor) != QLatin1Char('[')) {
    return false;
  }
  const qsizetype open = cursor;
  ++cursor;
  footnote = cursor < line.size() && line.at(cursor) == QLatin1Char('^');
  if (footnote) {
    ++cursor;
  }

  const qsizetype labelStart = cursor;
  bool escaped = false;
  while (cursor < line.size()) {
    const QChar ch = line.at(cursor);
    if (escaped) {
      escaped = false;
      ++cursor;
      continue;
    }
    if (ch == QLatin1Char('\\')) {
      escaped = true;
      ++cursor;
      continue;
    }
    if (ch == QLatin1Char(']')) {
      labelRange = {labelStart, cursor};
      ++cursor;
      Q_UNUSED(open);
      return true;
    }
    ++cursor;
  }
  return false;
}

DefinitionFieldRange sourceRangeForLine(qsizetype sourceStart, QStringView line, const DefinitionBlock& definition) {
  qsizetype end = sourceStart + trimEnd(line, 0, line.size());
  auto includeField = [&end](const DefinitionFieldRange& field) {
    if (field.isValid()) {
      end = qMax(end, field.end);
    }
  };
  includeField(definition.markerRange);
  includeField(definition.labelRange);
  includeField(definition.destinationRange);
  includeField(definition.titleRange);
  includeField(definition.noteRange);
  return {sourceStart, end};
}

DefinitionFieldRange parseOptionalQuotedTitle(QStringView line, qsizetype titleStart, bool& quoted) {
  quoted = false;
  qsizetype start = skipHorizontalSpace(line, titleStart);
  if (start >= line.size()) {
    return {};
  }
  const QChar quote = line.at(start);
  if (quote != QLatin1Char('"') && quote != QLatin1Char('\'')) {
    return {};
  }

  qsizetype end = line.size();
  while (end > start + 1 && isHorizontalSpace(line.at(end - 1))) {
    --end;
  }
  if (end <= start + 1 || line.at(end - 1) != quote) {
    return {};
  }
  quoted = true;
  return {start + 1, end - 1};
}

DefinitionFieldRange parseDestination(QStringView line, qsizetype start) {
  start = skipHorizontalSpace(line, start);
  if (start >= line.size()) {
    return {start, start};
  }
  if (line.at(start) == QLatin1Char('"') || line.at(start) == QLatin1Char('\'')) {
    return {start, start};
  }
  qsizetype end = start;
  if (line.at(start) == QLatin1Char('<')) {
    end = start + 1;
    while (end < line.size() && line.at(end) != QLatin1Char('>')) {
      ++end;
    }
    return {start + 1, end};
  }
  while (end < line.size() && !isHorizontalSpace(line.at(end))) {
    ++end;
  }
  return {start, end};
}

qsizetype lineEndOffset(QStringView text, qsizetype start) {
  const qsizetype newline = text.indexOf(QLatin1Char('\n'), start);
  return newline < 0 ? text.size() : newline;
}

qsizetype nextLineStart(QStringView text, qsizetype start) {
  const qsizetype end = lineEndOffset(text, start);
  return end < text.size() ? end + 1 : text.size();
}

}  // namespace

bool parseDefinitionBlockLine(QStringView line, qsizetype sourceStart, DefinitionBlock& definition) {
  definition = {};
  if (!line.isEmpty() && line.back() == QLatin1Char('\r')) {
    line.chop(1);
  }

  qsizetype cursor = skipHorizontalSpace(line, 0);
  if (cursor > 3) {
    return false;
  }

  bool footnote = false;
  DefinitionFieldRange localLabel;
  if (!parseLabel(line, cursor, footnote, localLabel)) {
    return false;
  }
  if (cursor >= line.size() || line.at(cursor) != QLatin1Char(':')) {
    return false;
  }
  ++cursor;

  definition.kind = footnote ? DefinitionBlock::Kind::Footnote : DefinitionBlock::Kind::Link;
  definition.labelRange = {sourceStart + localLabel.start, sourceStart + localLabel.end};
  definition.label = line.mid(localLabel.start, localLabel.length()).toString();
  definition.markerRange = {sourceStart, sourceStart + cursor};

  if (footnote) {
    const qsizetype noteStart = skipHorizontalSpace(line, cursor);
    const qsizetype noteEnd = trimEnd(line, noteStart, line.size());
    definition.noteRange = {sourceStart + noteStart, sourceStart + noteEnd};
    definition.note = line.mid(noteStart, noteEnd - noteStart).toString();
    definition.sourceRange = sourceRangeForLine(sourceStart, line, definition);
    return true;
  }

  const DefinitionFieldRange destination = parseDestination(line, cursor);
  definition.destinationRange = {sourceStart + destination.start, sourceStart + destination.end};
  definition.destination = line.mid(destination.start, destination.length()).toString();

  bool titleQuoted = false;
  const DefinitionFieldRange title = parseOptionalQuotedTitle(line, destination.end, titleQuoted);
  if (title.isValid()) {
    if (definition.destinationRange.start == definition.destinationRange.end) {
      const qsizetype openingQuote = title.start - 1;
      if (openingQuote >= 2 && isHorizontalSpace(line.at(openingQuote - 1)) &&
          isHorizontalSpace(line.at(openingQuote - 2))) {
        definition.destinationRange = {sourceStart + openingQuote - 1, sourceStart + openingQuote - 1};
      }
    }
    definition.titleRange = {sourceStart + title.start, sourceStart + title.end};
    definition.title = line.mid(title.start, title.length()).toString();
    definition.titleQuoted = titleQuoted;
  } else {
    const qsizetype titleSlot = trimEnd(line, skipHorizontalSpace(line, destination.end), line.size());
    definition.titleRange = {sourceStart + titleSlot, sourceStart + titleSlot};
  }
  definition.sourceRange = sourceRangeForLine(sourceStart, line, definition);
  return true;
}

QVector<DefinitionBlock> scanDefinitionBlocks(QStringView markdown) {
  QVector<DefinitionBlock> definitions;
  qsizetype lineStart = 0;
  while (lineStart <= markdown.size()) {
    const qsizetype lineEnd = lineEndOffset(markdown, lineStart);
    DefinitionBlock definition;
    if (parseDefinitionBlockLine(markdown.mid(lineStart, lineEnd - lineStart), lineStart, definition)) {
      if (definition.kind == DefinitionBlock::Kind::Link && definition.destination.isEmpty() && lineEnd < markdown.size()) {
        const qsizetype continuationStart = nextLineStart(markdown, lineStart);
        const qsizetype continuationEnd = lineEndOffset(markdown, continuationStart);
        QStringView continuation = markdown.mid(continuationStart, continuationEnd - continuationStart);
        if (!continuation.isEmpty() && continuation.back() == QLatin1Char('\r')) {
          continuation.chop(1);
        }
        const qsizetype contentStart = skipHorizontalSpace(continuation, 0);
        if (contentStart > 0 && contentStart < continuation.size()) {
          const DefinitionFieldRange destination = parseDestination(continuation, contentStart);
          definition.destinationRange = {continuationStart + destination.start, continuationStart + destination.end};
          definition.destination = continuation.mid(destination.start, destination.length()).toString();
          bool titleQuoted = false;
          const DefinitionFieldRange title = parseOptionalQuotedTitle(continuation, destination.end, titleQuoted);
          if (title.isValid()) {
            definition.titleRange = {continuationStart + title.start, continuationStart + title.end};
            definition.title = continuation.mid(title.start, title.length()).toString();
            definition.titleQuoted = titleQuoted;
          }
          definition.sourceRange = {definition.sourceRange.start, continuationStart + trimEnd(continuation, 0, continuation.size())};
        }
      }
      definitions.push_back(std::move(definition));
    }
    if (lineEnd >= markdown.size()) {
      break;
    }
    lineStart = nextLineStart(markdown, lineStart);
  }
  return definitions;
}

}  // namespace muffin
