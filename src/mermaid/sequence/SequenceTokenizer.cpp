#include "mermaid/sequence/SequenceTokenizer.h"

#include <QHash>

namespace muffin::mermaid::sequence {
namespace {

const QHash<QString, SequenceTokenKind>& keywords() {
  static const QHash<QString, SequenceTokenKind> values = {
      {QStringLiteral("sequencediagram"), SequenceTokenKind::Header},
      {QStringLiteral("participant"), SequenceTokenKind::Participant},
      {QStringLiteral("actor"), SequenceTokenKind::Actor},
      {QStringLiteral("create"), SequenceTokenKind::Create},
      {QStringLiteral("destroy"), SequenceTokenKind::Destroy},
      {QStringLiteral("as"), SequenceTokenKind::As},
      {QStringLiteral("box"), SequenceTokenKind::Box},
      {QStringLiteral("end"), SequenceTokenKind::End},
      {QStringLiteral("autonumber"), SequenceTokenKind::Autonumber},
      {QStringLiteral("off"), SequenceTokenKind::Off},
      {QStringLiteral("activate"), SequenceTokenKind::Activate},
      {QStringLiteral("deactivate"), SequenceTokenKind::Deactivate},
      {QStringLiteral("note"), SequenceTokenKind::Note},
      {QStringLiteral("left"), SequenceTokenKind::Left},
      {QStringLiteral("right"), SequenceTokenKind::Right},
      {QStringLiteral("of"), SequenceTokenKind::Of},
      {QStringLiteral("over"), SequenceTokenKind::Over},
      {QStringLiteral("loop"), SequenceTokenKind::Loop},
      {QStringLiteral("rect"), SequenceTokenKind::Rect},
      {QStringLiteral("opt"), SequenceTokenKind::Opt},
      {QStringLiteral("alt"), SequenceTokenKind::Alt},
      {QStringLiteral("else"), SequenceTokenKind::Else},
      {QStringLiteral("par"), SequenceTokenKind::Par},
      {QStringLiteral("par_over"), SequenceTokenKind::ParOver},
      {QStringLiteral("and"), SequenceTokenKind::And},
      {QStringLiteral("critical"), SequenceTokenKind::Critical},
      {QStringLiteral("option"), SequenceTokenKind::Option},
      {QStringLiteral("break"), SequenceTokenKind::Break},
      {QStringLiteral("title"), SequenceTokenKind::Title},
      {QStringLiteral("acctitle"), SequenceTokenKind::AccTitle},
      {QStringLiteral("accdescr"), SequenceTokenKind::AccDescr},
      {QStringLiteral("links"), SequenceTokenKind::Links},
      {QStringLiteral("link"), SequenceTokenKind::Link},
      {QStringLiteral("properties"), SequenceTokenKind::Properties},
      {QStringLiteral("details"), SequenceTokenKind::Details},
  };
  return values;
}

const QStringList& arrows() {
  static const QStringList values = {
      QStringLiteral("<<-->>"), QStringLiteral("<<->>"),
      QStringLiteral("--|\\"), QStringLiteral("--|/"),
      QStringLiteral("/|--"), QStringLiteral("\\|--"),
      QStringLiteral("//--"), QStringLiteral("\\\\--"),
      QStringLiteral("--\\\\"), QStringLiteral("--//"),
      QStringLiteral("-|\\"), QStringLiteral("-|/"),
      QStringLiteral("/|-"), QStringLiteral("\\|-"),
      QStringLiteral("//-"), QStringLiteral("\\\\-"),
      QStringLiteral("-\\\\"), QStringLiteral("-//"),
      QStringLiteral("-->>"), QStringLiteral("->>"),
      QStringLiteral("--x"), QStringLiteral("--)") ,
      QStringLiteral("-->"), QStringLiteral("-x"),
      QStringLiteral("-)"), QStringLiteral("->"),
  };
  return values;
}

}  // namespace

SequenceTokenizer::SequenceTokenizer(QString source) : source_(std::move(source)) {}

bool SequenceTokenizer::atEnd() const { return offset_ >= source_.size(); }

QChar SequenceTokenizer::peek(qsizetype ahead) const {
  const qsizetype index = offset_ + ahead;
  return index < source_.size() ? source_.at(index) : QChar{};
}

void SequenceTokenizer::advance(QStringView text) {
  for (const QChar ch : text) {
    if (ch == QLatin1Char('\n')) {
      ++line_;
      column_ = 0;
    } else {
      ++column_;
    }
  }
  offset_ += text.size();
}

SequenceToken SequenceTokenizer::take(SequenceTokenKind kind, qsizetype length) {
  SequenceToken token{kind, source_.mid(offset_, length), offset_, length, line_, column_};
  advance(QStringView(source_).mid(offset_, length));
  return token;
}

SequenceToken SequenceTokenizer::next() {
  while (!atEnd()) {
    if (peek() == QLatin1Char('\r')) {
      if (peek(1) == QLatin1Char('\n')) return take(SequenceTokenKind::Newline, 2);
      return take(SequenceTokenKind::Newline, 1);
    }
    if (peek() == QLatin1Char('\n')) return take(SequenceTokenKind::Newline, 1);
    if (peek() == QLatin1Char(';')) return take(SequenceTokenKind::Semi, 1);
    if (peek().isSpace()) {
      const qsizetype start = offset_;
      while (!atEnd() && peek().isSpace() && peek() != QLatin1Char('\n') &&
             peek() != QLatin1Char('\r'))
        advance(QStringView(source_).mid(offset_, 1));
      if (offset_ > start) continue;
    }
    if (peek() == QLatin1Char('#') ||
        (peek() == QLatin1Char('%') && peek(1) != QLatin1Char('{'))) {
      while (!atEnd() && peek() != QLatin1Char('\n') && peek() != QLatin1Char('\r'))
        advance(QStringView(source_).mid(offset_, 1));
      continue;
    }
    for (const QString& arrow : arrows()) {
      if (QStringView(source_).mid(offset_, arrow.size()) == arrow)
        return take(SequenceTokenKind::Arrow, arrow.size());
    }
    if (QStringView(source_).mid(offset_, 2) == QLatin1String("()"))
      return take(SequenceTokenKind::Central, 2);
    switch (peek().unicode()) {
      case '+': return take(SequenceTokenKind::Plus, 1);
      case '-': return take(SequenceTokenKind::Minus, 1);
      case ':': return take(SequenceTokenKind::Colon, 1);
      case ',': return take(SequenceTokenKind::Comma, 1);
      default: break;
    }
    const qsizetype start = offset_;
    while (!atEnd() && !peek().isSpace() && peek() != QLatin1Char(';') &&
           peek() != QLatin1Char(':') && peek() != QLatin1Char(',') &&
           peek() != QLatin1Char('+') && peek() != QLatin1Char('-') &&
           peek() != QLatin1Char('=')) {
      bool arrowStart = false;
      for (const QString& arrow : arrows())
        arrowStart = arrowStart || QStringView(source_).mid(offset_, arrow.size()) == arrow;
      if (arrowStart || QStringView(source_).mid(offset_, 2) == QLatin1String("()")) break;
      advance(QStringView(source_).mid(offset_, 1));
    }
    if (offset_ == start) return take(SequenceTokenKind::Invalid, 1);
    SequenceToken token{SequenceTokenKind::Word, source_.mid(start, offset_ - start),
                        start, offset_ - start, line_, column_ - static_cast<int>(offset_ - start)};
    const auto keyword = keywords().constFind(token.text.toLower());
    if (keyword != keywords().constEnd()) token.kind = keyword.value();
    return token;
  }
  return {SequenceTokenKind::Eof, {}, offset_, 0, line_, column_};
}

QVector<SequenceToken> SequenceTokenizer::tokenize() {
  QVector<SequenceToken> result;
  do {
    result.append(next());
  } while (result.last().kind != SequenceTokenKind::Eof);
  return result;
}

QString sequenceTokenName(SequenceTokenKind kind) {
  switch (kind) {
    case SequenceTokenKind::Eof: return QStringLiteral("EOF");
    case SequenceTokenKind::Newline: return QStringLiteral("NEWLINE");
    case SequenceTokenKind::Semi: return QStringLiteral("SEMI");
    case SequenceTokenKind::Header: return QStringLiteral("SD");
    case SequenceTokenKind::Arrow: return QStringLiteral("ARROW");
    case SequenceTokenKind::Word: return QStringLiteral("ACTOR");
    case SequenceTokenKind::Invalid: return QStringLiteral("INVALID");
    default: return QStringLiteral("KEYWORD");
  }
}

}  // namespace muffin::mermaid::sequence
