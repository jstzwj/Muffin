#pragma once

#include <QString>
#include <QVector>

namespace muffin::mermaid::sequence {

enum class SequenceTokenKind {
  Eof,
  Newline,
  Semi,
  Header,
  Participant,
  Actor,
  Create,
  Destroy,
  As,
  Box,
  End,
  Autonumber,
  Off,
  Activate,
  Deactivate,
  Note,
  Left,
  Right,
  Of,
  Over,
  Loop,
  Rect,
  Opt,
  Alt,
  Else,
  Par,
  ParOver,
  And,
  Critical,
  Option,
  Break,
  Title,
  AccTitle,
  AccDescr,
  Links,
  Link,
  Properties,
  Details,
  Arrow,
  Plus,
  Minus,
  Central,
  Colon,
  Comma,
  Word,
  Invalid,
};

struct SequenceToken {
  SequenceTokenKind kind = SequenceTokenKind::Invalid;
  QString text;
  qsizetype offset = 0;
  qsizetype length = 0;
  int line = 1;
  int column = 0;
};

QString sequenceTokenName(SequenceTokenKind kind);

class SequenceTokenizer {
public:
  explicit SequenceTokenizer(QString source);

  SequenceToken next();
  QVector<SequenceToken> tokenize();

private:
  bool atEnd() const;
  QChar peek(qsizetype ahead = 0) const;
  SequenceToken take(SequenceTokenKind kind, qsizetype length);
  void advance(QStringView text);

  QString source_;
  qsizetype offset_ = 0;
  int line_ = 1;
  int column_ = 0;
  int braceDepth_ = 0;
  bool labelMode_ = false;
};

}  // namespace muffin::mermaid::sequence
