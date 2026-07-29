#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace muffin::mermaid::state {

enum class StateTokenKind {
  Eof, Newline, Semi, Header, State, Direction, AccTitle, AccDescr,
  HideEmpty, Scale, Width, Note, End, Left, Right, Of, As,
  ClassDef, Class, Style, LinkStyle, Click, Href, Default, Arrow, Concurrent,
  StyleSeparator, StartEnd, LBrace, RBrace, Colon, Comma, String,
  Identifier, Invalid,
};

struct StateToken {
  StateTokenKind kind = StateTokenKind::Invalid;
  QString text;
  qsizetype offset = 0;
  qsizetype length = 0;
  int line = 1;
  int column = 0;
};

QString stateTokenName(StateTokenKind kind);

class StateTokenizer {
public:
  explicit StateTokenizer(QString source) : source_(std::move(source)) {}
  QVector<StateToken> tokenize();

private:
  StateToken next();
  StateToken take(StateTokenKind kind, qsizetype length, bool stripQuotes = false);
  void advance(QStringView text);
  QChar peek(qsizetype ahead = 0) const;
  bool atEnd() const { return offset_ >= source_.size(); }

  QString source_;
  qsizetype offset_ = 0;
  int line_ = 1;
  int column_ = 0;
  bool lineHasContent_ = false;
};

class StateTokenCursor {
public:
  StateTokenCursor(const QVector<StateToken>& tokens, qsizetype begin = 0,
                   qsizetype end = -1);
  const StateToken& peek(qsizetype lookahead = 0) const;
  const StateToken& consume();
  const StateToken& expect(StateTokenKind kind);
  bool match(StateTokenKind kind);
  bool atEnd() const;
  void skipSeparators();
  StateTokenCursor consumeLine();
  QString raw(const QString& source) const;
  QString rawFrom(const QString& source, qsizetype position) const;
  QStringList parseList(const QString& source);
  qsizetype position() const { return position_; }
  qsizetype endPosition() const { return end_; }

private:
  const QVector<StateToken>* tokens_ = nullptr;
  qsizetype position_ = 0;
  qsizetype end_ = 0;
};

}  // namespace muffin::mermaid::state
