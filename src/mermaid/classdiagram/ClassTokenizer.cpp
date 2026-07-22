#include "mermaid/classdiagram/ClassTokenizer.h"

#include <algorithm>

namespace muffin::mermaid::classdiagram {
namespace {

bool isWordCharacter(QChar character) {
  return character.isLetterOrNumber() || character == QLatin1Char('_') ||
         character == QLatin1Char('-') || character == QLatin1Char('.') ||
         character.category() == QChar::Mark_NonSpacing;
}

}  // namespace

void ClassTokenizer::advance(QStringView text) {
  for (QChar character : text) {
    if (character == QLatin1Char('\n')) {
      ++line_;
      column_ = 0;
    } else {
      ++column_;
    }
  }
  offset_ += text.size();
}

ClassToken ClassTokenizer::take(ClassTokenKind kind, qsizetype length,
                                bool stripDelimiters) {
  const qsizetype start = offset_;
  const int line = line_;
  const int column = column_;
  const QStringView raw = QStringView(source_).mid(start, length);
  advance(raw);
  QString text = raw.toString();
  if (stripDelimiters && text.size() >= 2) text = text.mid(1, text.size() - 2);
  return {kind, text, start, length, line, column};
}

ClassToken ClassTokenizer::next() {
  while (offset_ < source_.size()) {
    const QChar current = source_[offset_];
    if (current == QLatin1Char('\r')) {
      if (offset_ + 1 < source_.size() && source_[offset_ + 1] == QLatin1Char('\n'))
        return take(ClassTokenKind::Newline, 2);
      return take(ClassTokenKind::Newline, 1);
    }
    if (current == QLatin1Char('\n')) return take(ClassTokenKind::Newline, 1);
    if (current.isSpace()) {
      const qsizetype start = offset_;
      while (offset_ < source_.size() && source_[offset_].isSpace() &&
             source_[offset_] != QLatin1Char('\r') &&
             source_[offset_] != QLatin1Char('\n'))
        advance(QStringView(source_).mid(offset_, 1));
      Q_UNUSED(start);
      continue;
    }
    if (QStringView(source_).mid(offset_, 2) == QLatin1String("%%")) {
      while (offset_ < source_.size() && source_[offset_] != QLatin1Char('\r') &&
             source_[offset_] != QLatin1Char('\n'))
        advance(QStringView(source_).mid(offset_, 1));
      continue;
    }
    if (current == QLatin1Char('"') || current == QLatin1Char('`') ||
        current == QLatin1Char('~')) {
      const QChar delimiter = current;
      qsizetype end = offset_ + 1;
      while (end < source_.size() && source_[end] != delimiter) {
        if (source_[end] == QLatin1Char('\\') && end + 1 < source_.size()) ++end;
        ++end;
      }
      if (end >= source_.size()) return take(ClassTokenKind::Invalid, source_.size() - offset_);
      const ClassTokenKind kind = delimiter == QLatin1Char('"') ? ClassTokenKind::String
          : delimiter == QLatin1Char('`') ? ClassTokenKind::Backtick
                                           : ClassTokenKind::Generic;
      return take(kind, end - offset_ + 1, true);
    }
    const QStringView rest = QStringView(source_).mid(offset_);
    if (rest.startsWith(QLatin1String("$$"))) {
      const qsizetype closing = source_.indexOf(QStringLiteral("$$"), offset_ + 2);
      if (closing >= 0)
        return take(ClassTokenKind::Word, closing + 2 - offset_);
    }
    if (rest.startsWith(QLatin1String("classDiagram-v2")))
      return take(ClassTokenKind::Header, 15);
    if (rest.startsWith(QLatin1String("classDiagram")))
      return take(ClassTokenKind::Header, 12);
    if (rest.startsWith(QLatin1String(":::"))) return take(ClassTokenKind::StyleSeparator, 3);
    if (rest.startsWith(QLatin1String("<<"))) return take(ClassTokenKind::AnnotationStart, 2);
    if (rest.startsWith(QLatin1String(">>"))) return take(ClassTokenKind::AnnotationEnd, 2);

    const QStringList relations = {
        QStringLiteral("<|--"), QStringLiteral("--|>"), QStringLiteral("<|.."),
        QStringLiteral("..|>"), QStringLiteral("()--"), QStringLiteral("--()"),
        QStringLiteral("*--"), QStringLiteral("--*"), QStringLiteral("o--"),
        QStringLiteral("--o"), QStringLiteral("<--"), QStringLiteral("-->"),
        QStringLiteral("<.."), QStringLiteral("..>"), QStringLiteral("--"),
        QStringLiteral("..")};
    for (const QString& relation : relations)
      if (rest.startsWith(relation)) return take(ClassTokenKind::Relation, relation.size());

    switch (current.unicode()) {
      case '{': return take(ClassTokenKind::LBrace, 1);
      case '}': return take(ClassTokenKind::RBrace, 1);
      case '[': return take(ClassTokenKind::LBracket, 1);
      case ']': return take(ClassTokenKind::RBracket, 1);
      case ':': return take(ClassTokenKind::Colon, 1);
      case ',': return take(ClassTokenKind::Comma, 1);
      default: break;
    }
    if (isWordCharacter(current) || QStringView(u"+#$*()<>|/\\%=!?&'").contains(current)) {
      const qsizetype start = offset_;
      while (offset_ < source_.size()) {
        const QChar character = source_[offset_];
        if (character.isSpace() || QStringView(u"{}[],:\"`~<>").contains(character)) break;
        bool relationStart = false;
        const QStringView candidate = QStringView(source_).mid(offset_);
        for (const QString& relation : relations)
          if (candidate.startsWith(relation)) { relationStart = true; break; }
        if (relationStart && offset_ > start) break;
        if (relationStart) break;
        advance(QStringView(source_).mid(offset_, 1));
      }
      if (offset_ > start) {
        QString text = source_.mid(start, offset_ - start);
        return {ClassTokenKind::Word, text, start, offset_ - start, line_,
                column_ - static_cast<int>(offset_ - start)};
      }
    }
    return take(ClassTokenKind::Invalid, 1);
  }
  return {ClassTokenKind::Eof, {}, offset_, 0, line_, column_};
}

QVector<ClassToken> ClassTokenizer::tokenize() {
  QVector<ClassToken> result;
  do {
    result.append(next());
  } while (result.back().kind != ClassTokenKind::Eof);
  return result;
}

ClassTokenCursor::ClassTokenCursor(const QVector<ClassToken>& tokens,
                                   qsizetype begin, qsizetype end)
    : tokens_(&tokens), position_(begin),
      end_(end < 0 ? tokens.size() : std::min(end, tokens.size())) {}

const ClassToken& ClassTokenCursor::peek(qsizetype lookahead) const {
  const qsizetype index = position_ + lookahead;
  if (!tokens_ || index < 0 || index >= end_)
    throw std::out_of_range("class token cursor reached the end");
  return (*tokens_)[index];
}

const ClassToken& ClassTokenCursor::consume() {
  const ClassToken& token = peek();
  ++position_;
  return token;
}

bool ClassTokenCursor::match(ClassTokenKind kind) {
  if (atEnd() || peek().kind != kind) return false;
  consume();
  return true;
}

bool ClassTokenCursor::matchWord(QStringView word) {
  if (atEnd() || peek().kind != ClassTokenKind::Word || peek().text != word)
    return false;
  consume();
  return true;
}

const ClassToken& ClassTokenCursor::expect(ClassTokenKind kind) {
  if (atEnd() || peek().kind != kind)
    throw std::invalid_argument("unexpected class diagram token");
  return consume();
}

ClassTokenCursor ClassTokenCursor::consumeLine() {
  const qsizetype begin = position_;
  while (!atEnd() && peek().kind != ClassTokenKind::Newline &&
         peek().kind != ClassTokenKind::Eof)
    ++position_;
  const qsizetype end = position_;
  if (!atEnd() && peek().kind == ClassTokenKind::Newline) ++position_;
  return ClassTokenCursor(*tokens_, begin, end);
}

QString ClassTokenCursor::raw(const QString& source) const {
  return rawFrom(source, position_);
}

QString ClassTokenCursor::rawFrom(const QString& source, qsizetype position) const {
  if (!tokens_ || position < 0 || position >= end_) return {};
  const ClassToken& first = (*tokens_)[position];
  const ClassToken& last = (*tokens_)[end_ - 1];
  return source.mid(first.offset, last.offset + last.length - first.offset).trimmed();
}

QStringList ClassTokenCursor::parseList(const QString& source) {
  QStringList values;
  qsizetype itemStart = position_;
  for (; !atEnd(); ++position_) {
    const ClassToken& token = peek();
    if (token.kind == ClassTokenKind::Comma) {
      const qsizetype comma = position_;
      ClassTokenCursor item(*tokens_, itemStart, comma);
      if (const QString value = item.raw(source); !value.isEmpty()) values.append(value);
      itemStart = comma + 1;
    }
  }
  ClassTokenCursor item(*tokens_, itemStart, end_);
  if (const QString value = item.raw(source); !value.isEmpty()) values.append(value);
  return values;
}

QString classTokenName(ClassTokenKind kind) {
  switch (kind) {
    case ClassTokenKind::Eof: return QStringLiteral("EOF");
    case ClassTokenKind::Newline: return QStringLiteral("NEWLINE");
    case ClassTokenKind::Header: return QStringLiteral("CLASS_DIAGRAM");
    case ClassTokenKind::Word: return QStringLiteral("ALPHA");
    case ClassTokenKind::String: return QStringLiteral("STR");
    case ClassTokenKind::Backtick: return QStringLiteral("BQUOTE_STR");
    case ClassTokenKind::Generic: return QStringLiteral("GENERICTYPE");
    case ClassTokenKind::Relation: return QStringLiteral("RELATION");
    case ClassTokenKind::Invalid: return QStringLiteral("INVALID");
    default: return QStringLiteral("PUNCTUATION");
  }
}

}  // namespace muffin::mermaid::classdiagram
