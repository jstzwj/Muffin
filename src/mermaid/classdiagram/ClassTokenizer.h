#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::classdiagram {

enum class ClassTokenKind {
  Eof,
  Newline,
  Header,
  Word,
  String,
  Backtick,
  Generic,
  Relation,
  AnnotationStart,
  AnnotationEnd,
  StyleSeparator,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Colon,
  Comma,
  Invalid,
};

struct ClassToken {
  ClassTokenKind kind = ClassTokenKind::Invalid;
  QString text;
  qsizetype offset = 0;
  qsizetype length = 0;
  int line = 1;
  int column = 0;
};

class ClassTokenizer {
public:
  explicit ClassTokenizer(QString source) : source_(std::move(source)) {}
  QVector<ClassToken> tokenize();

private:
  ClassToken next();
  ClassToken take(ClassTokenKind kind, qsizetype length, bool stripDelimiters = false);
  void advance(QStringView text);

  QString source_;
  qsizetype offset_ = 0;
  int line_ = 1;
  int column_ = 0;
};

class ClassTokenCursor {
public:
  ClassTokenCursor(const QVector<ClassToken>& tokens, qsizetype begin = 0,
                   qsizetype end = -1);

  bool atEnd() const { return position_ >= end_; }
  qsizetype position() const { return position_; }
  qsizetype endPosition() const { return end_; }
  const ClassToken& peek(qsizetype lookahead = 0) const;
  const ClassToken& consume();
  bool match(ClassTokenKind kind);
  bool matchWord(QStringView word);
  const ClassToken& expect(ClassTokenKind kind);
  ClassTokenCursor consumeLine();

  QString raw(const QString& source) const;
  QString rawFrom(const QString& source, qsizetype position) const;
  QStringList parseList(const QString& source);

private:
  const QVector<ClassToken>* tokens_ = nullptr;
  qsizetype position_ = 0;
  qsizetype end_ = 0;
};

QString classTokenName(ClassTokenKind kind);

}  // namespace muffin::mermaid::classdiagram
