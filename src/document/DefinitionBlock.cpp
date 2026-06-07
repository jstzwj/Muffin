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

DefinitionFieldRange parseOptionalTitle(
    QStringView line,
    qsizetype titleStart,
    DefinitionBlock::TitleDelimiter& delimiter) {
  delimiter = DefinitionBlock::TitleDelimiter::None;
  qsizetype start = skipHorizontalSpace(line, titleStart);
  if (start >= line.size()) {
    return {};
  }

  const QChar opener = line.at(start);
  QChar closer;
  if (opener == QLatin1Char('"') || opener == QLatin1Char('\'')) {
    closer = opener;
    delimiter = opener == QLatin1Char('"')
                    ? DefinitionBlock::TitleDelimiter::DoubleQuote
                    : DefinitionBlock::TitleDelimiter::SingleQuote;
  } else if (opener == QLatin1Char('(')) {
    closer = QLatin1Char(')');
    delimiter = DefinitionBlock::TitleDelimiter::Parentheses;
  } else {
    return {};
  }

  bool escaped = false;
  qsizetype end = start + 1;
  while (end < line.size()) {
    const QChar ch = line.at(end);
    if (escaped) {
      escaped = false;
      ++end;
      continue;
    }
    if (ch == QLatin1Char('\\')) {
      escaped = true;
      ++end;
      continue;
    }
    if (ch == closer) {
      const qsizetype afterTitle = skipHorizontalSpace(line, end + 1);
      if (afterTitle != line.size()) {
        delimiter = DefinitionBlock::TitleDelimiter::None;
        return {};
      }
      return {start + 1, end};
    }
    ++end;
  }
  delimiter = DefinitionBlock::TitleDelimiter::None;
  return {};
}

DefinitionBlock::DestinationDelimiter destinationDelimiterFor(QStringView line, qsizetype start) {
  start = skipHorizontalSpace(line, start);
  return start < line.size() && line.at(start) == QLatin1Char('<')
             ? DefinitionBlock::DestinationDelimiter::Angle
             : DefinitionBlock::DestinationDelimiter::Bare;
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
    bool escaped = false;
    while (end < line.size()) {
      const QChar ch = line.at(end);
      if (ch == QLatin1Char('\n')) {
        break;
      }
      if (escaped) {
        escaped = false;
        ++end;
        continue;
      }
      if (ch == QLatin1Char('\\')) {
        escaped = true;
        ++end;
        continue;
      }
      if (ch == QLatin1Char('>')) {
        return {start + 1, end};
      }
      if (ch == QLatin1Char('<')) {
        return {};
      }
      ++end;
    }
    return {};
  }
  bool escaped = false;
  int parenDepth = 0;
  while (end < line.size() && !isHorizontalSpace(line.at(end))) {
    const QChar ch = line.at(end);
    if (escaped) {
      escaped = false;
      ++end;
      continue;
    }
    if (ch == QLatin1Char('\\')) {
      escaped = true;
      ++end;
      continue;
    }
    if (ch == QLatin1Char('(')) {
      ++parenDepth;
    } else if (ch == QLatin1Char(')')) {
      if (parenDepth == 0) {
        return {};
      }
      --parenDepth;
    }
    ++end;
  }
  if (parenDepth != 0) {
    return {};
  }
  return {start, end};
}

qsizetype destinationTokenEnd(QStringView line, const DefinitionFieldRange& destination) {
  return destination.isValid() && destination.end < line.size() && line.at(destination.end) == QLatin1Char('>')
             ? destination.end + 1
             : destination.end;
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

DefinitionParseResult parseDefinitionBlockLineFields(QStringView line, qsizetype sourceStart) {
  DefinitionParseResult result;
  DefinitionBlock& definition = result.definition;
  if (!line.isEmpty() && line.back() == QLatin1Char('\r')) {
    line.chop(1);
  }

  qsizetype cursor = skipHorizontalSpace(line, 0);
  if (cursor > 3) {
    return result;
  }

  bool footnote = false;
  DefinitionFieldRange localLabel;
  if (!parseLabel(line, cursor, footnote, localLabel)) {
    return result;
  }
  if (cursor >= line.size() || line.at(cursor) != QLatin1Char(':')) {
    return result;
  }
  ++cursor;
  const bool hasTemplatePaddingAfterMarker = cursor < line.size() && isHorizontalSpace(line.at(cursor));

  definition.kind = footnote ? DefinitionBlock::Kind::Footnote : DefinitionBlock::Kind::Link;
  definition.labelRange = {sourceStart + localLabel.start, sourceStart + localLabel.end};
  definition.label = line.mid(localLabel.start, localLabel.length()).toString();
  definition.markerRange = {sourceStart, sourceStart + cursor};
  definition.sourceText = line.mid(0, trimEnd(line, 0, line.size())).toString();

  if (footnote) {
    const qsizetype noteStart = skipHorizontalSpace(line, cursor);
    const qsizetype noteEnd = trimEnd(line, noteStart, line.size());
    definition.noteRange = {sourceStart + noteStart, sourceStart + noteEnd};
    definition.note = line.mid(noteStart, noteEnd - noteStart).toString();
    definition.virtualTemplate = definition.label.isEmpty() && definition.note.isEmpty();
    definition.sourceRange = sourceRangeForLine(sourceStart, line, definition);
    result.classification = definition.virtualTemplate
                                ? DefinitionParseClassification::VirtualTemplate
                                : DefinitionParseClassification::ValidMarkdownDefinition;
    return result;
  }

  const DefinitionFieldRange destination = parseDestination(line, cursor);
  if (!destination.isValid()) {
    return result;
  }
  definition.destinationDelimiter = destinationDelimiterFor(line, cursor);
  definition.destinationRange = {sourceStart + destination.start, sourceStart + destination.end};
  definition.destination = line.mid(destination.start, destination.length()).toString();

  const qsizetype destinationEnd = destinationTokenEnd(line, destination);
  DefinitionBlock::TitleDelimiter titleDelimiter = DefinitionBlock::TitleDelimiter::None;
  const DefinitionFieldRange title = parseOptionalTitle(line, destinationEnd, titleDelimiter);
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
    definition.titleDelimiter = titleDelimiter;
    definition.titleQuoted = titleDelimiter == DefinitionBlock::TitleDelimiter::DoubleQuote ||
                             titleDelimiter == DefinitionBlock::TitleDelimiter::SingleQuote;
  } else {
    const qsizetype afterDestination = skipHorizontalSpace(line, destinationEnd);
    if (afterDestination < line.size()) {
      return result;
    }
    const qsizetype titleSlot = trimEnd(line, afterDestination, line.size());
    definition.titleRange = {sourceStart + titleSlot, sourceStart + titleSlot};
  }
  definition.virtualTemplate = definition.destination.isEmpty() &&
                               definition.title.isEmpty() &&
                               (definition.label.isEmpty() || hasTemplatePaddingAfterMarker);
  definition.sourceRange = sourceRangeForLine(sourceStart, line, definition);
  if (definition.virtualTemplate) {
    result.classification = DefinitionParseClassification::VirtualTemplate;
  } else if (!definition.destination.isEmpty()) {
    result.classification = DefinitionParseClassification::ValidMarkdownDefinition;
  }
  return result;
}

bool isIndentedContinuationLine(QStringView line) {
  if (!line.isEmpty() && line.back() == QLatin1Char('\r')) {
    line.chop(1);
  }
  if (line.isEmpty()) {
    return false;
  }
  int columns = 0;
  for (qsizetype i = 0; i < line.size(); ++i) {
    const QChar ch = line.at(i);
    if (ch == QLatin1Char(' ')) {
      ++columns;
    } else if (ch == QLatin1Char('\t')) {
      columns += 4 - (columns % 4);
    } else {
      break;
    }
  }
  return columns >= 4;
}

QVector<DefinitionParseResult> scanDefinitionBlocks(QStringView markdown) {
  QVector<DefinitionParseResult> definitions;
  qsizetype lineStart = 0;
  while (lineStart <= markdown.size()) {
    const qsizetype lineEnd = lineEndOffset(markdown, lineStart);
    DefinitionParseResult result = parseDefinitionBlockLineFields(markdown.mid(lineStart, lineEnd - lineStart), lineStart);
    if (result.definition.isValid()) {
      DefinitionBlock& definition = result.definition;
      if (definition.kind == DefinitionBlock::Kind::Footnote) {
        qsizetype continuationStart = nextLineStart(markdown, lineStart);
        while (continuationStart < markdown.size()) {
          const qsizetype continuationEnd = lineEndOffset(markdown, continuationStart);
          QStringView continuation = markdown.mid(continuationStart, continuationEnd - continuationStart);
          if (!isIndentedContinuationLine(continuation)) {
            break;
          }
          definition.sourceRange = {definition.sourceRange.start,
                                    continuationStart + trimEnd(continuation, 0, continuation.size())};
          continuationStart = nextLineStart(markdown, continuationStart);
        }
        definition.sourceText = markdown.mid(definition.sourceRange.start, definition.sourceRange.length()).toString();
      } else if (definition.kind == DefinitionBlock::Kind::Link && definition.destination.isEmpty() && lineEnd < markdown.size()) {
        const qsizetype continuationStart = nextLineStart(markdown, lineStart);
        const qsizetype continuationEnd = lineEndOffset(markdown, continuationStart);
        QStringView continuation = markdown.mid(continuationStart, continuationEnd - continuationStart);
        if (!continuation.isEmpty() && continuation.back() == QLatin1Char('\r')) {
          continuation.chop(1);
        }
        const qsizetype contentStart = skipHorizontalSpace(continuation, 0);
        if (contentStart > 0 && contentStart < continuation.size()) {
          const DefinitionFieldRange destination = parseDestination(continuation, contentStart);
          if (!destination.isValid()) {
            result.classification = definition.virtualTemplate ? DefinitionParseClassification::VirtualTemplate
                                                               : DefinitionParseClassification::Invalid;
          } else {
            definition.destinationDelimiter = destinationDelimiterFor(continuation, contentStart);
            definition.destinationRange = {continuationStart + destination.start, continuationStart + destination.end};
            definition.destination = continuation.mid(destination.start, destination.length()).toString();
            const qsizetype destinationEnd = destinationTokenEnd(continuation, destination);
            DefinitionBlock::TitleDelimiter titleDelimiter = DefinitionBlock::TitleDelimiter::None;
            const DefinitionFieldRange title = parseOptionalTitle(continuation, destinationEnd, titleDelimiter);
            bool continuationValid = false;
            if (title.isValid()) {
              definition.titleRange = {continuationStart + title.start, continuationStart + title.end};
              definition.title = continuation.mid(title.start, title.length()).toString();
              definition.titleDelimiter = titleDelimiter;
              definition.titleQuoted = titleDelimiter == DefinitionBlock::TitleDelimiter::DoubleQuote ||
                                       titleDelimiter == DefinitionBlock::TitleDelimiter::SingleQuote;
              continuationValid = true;
            } else if (skipHorizontalSpace(continuation, destinationEnd) < continuation.size()) {
              result.classification = definition.virtualTemplate ? DefinitionParseClassification::VirtualTemplate
                                                                 : DefinitionParseClassification::Invalid;
            } else {
              continuationValid = true;
            }
            if (continuationValid) {
              definition.sourceRange = {definition.sourceRange.start, continuationStart + trimEnd(continuation, 0, continuation.size())};
              definition.sourceText = markdown.mid(definition.sourceRange.start, definition.sourceRange.length()).toString();
              definition.virtualTemplate = definition.label.isEmpty() || definition.destination.isEmpty();
              result.classification = definition.virtualTemplate ? DefinitionParseClassification::VirtualTemplate
                                                                 : DefinitionParseClassification::ValidMarkdownDefinition;
            }
          }
        }
      }
      if (result.isRecognized()) {
        definitions.push_back(std::move(result));
      }
    }
    if (lineEnd >= markdown.size()) {
      break;
    }
    lineStart = nextLineStart(markdown, lineStart);
  }
  return definitions;
}

DefinitionParseResult parseDefinitionBlockLine(QStringView line, qsizetype sourceStart) {
  DefinitionParseResult result = parseDefinitionBlockLineFields(line, sourceStart);
  return result.isRecognized() ? result : DefinitionParseResult{};
}

}  // namespace muffin
