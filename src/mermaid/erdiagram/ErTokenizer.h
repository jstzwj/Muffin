#pragma once

#include "mermaid/erdiagram/ErDiagram.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace muffin::mermaid::er {

// Token model mirrors ClassTokenizer.h. Token design decisions (documented once
// here; ErDiagram.cpp's parser depends on them):
//
// * ErHeader         -- the literal "erDiagram" keyword (one token, text is the
//                       matched header word).
// * Identifier       -- [A-Za-z][A-Za-z0-9_-]*  Entity ids / attribute type and
//                       name / direction codes / accTitle-style keywords. May
//                       contain internal hyphens so `LINE-ITEM` is one token.
// * QuotedText       -- a double-quoted run ("..."). Used for roles, entity
//                       aliases, attribute comments, accTitle/accDescription
//                       values. Delimiters are stripped; text is the payload.
// * Cardinality      -- a dedicated token whose text is one of the eight
//                       crow's-foot glyphs:  ||  }o  o{  }|  |{  |o  o|
//                       (chosen over an Identifier-ish token so the parser maps
//                       glyph -> ErCardinality by exact text lookup).
// * Hyphen / Dot     -- maximal-munch runs of '-' / '.' that are NOT part of an
//                       Identifier. In a relationship the connector is exactly
//                       "--" (one Hyphen token => identifying) or ".." (one Dot
//                       token => non-identifying). The parser validates the run
//                       length is 2.
// * KeyPK/KeyFK/KeyUK-- the bare words PK / FK / UK in an attribute line.
// * Comment          -- a `%%` line comment; text is the comment body.
enum class ErTokenKind {
  Eof,
  Newline,
  Space,
  ErHeader,
  Identifier,
  QuotedText,
  Cardinality,
  Hyphen,
  Dot,
  OpenBrace,
  CloseBrace,
  Colon,
  Comma,
  KeyPK,
  KeyFK,
  KeyUK,
  Comment,
  Invalid,
  Unknown,
};

struct ErToken {
  ErTokenKind kind = ErTokenKind::Invalid;
  QString text;
  int line = 1;
  int column = 0;
  qsizetype offset = 0;
  ErErrorCode diagnosticCode = ErErrorCode::UnexpectedToken;
};

class ErTokenizer {
public:
  explicit ErTokenizer(QString source) : source_(std::move(source)) {}
  ErToken next();
  QVector<ErToken> tokenize();

private:
  ErToken make(ErTokenKind kind, qsizetype length, bool stripDelimiters = false);
  ErToken makeInvalid(qsizetype length);
  void advance(QStringView text);
  QChar peek(qsizetype ahead = 0) const;

  QString source_;
  qsizetype offset_ = 0;
  int line_ = 1;
  int column_ = 0;
};

// Line-oriented cursor over a token vector, mirroring ClassTokenCursor so the
// parser can consume statements one logical line at a time. The raw(...) family
// reconstructs source text from token offsets (tokens only carry their text, so
// inter-token whitespace is recovered from the original source).
class ErTokenCursor {
public:
  ErTokenCursor(const QVector<ErToken>& tokens, qsizetype begin = 0,
                qsizetype end = -1);

  bool atEnd() const { return position_ >= end_; }
  qsizetype position() const { return position_; }
  qsizetype endPosition() const { return end_; }
  const ErToken& peek(qsizetype lookahead = 0) const;
  const ErToken& consume();
  bool match(ErTokenKind kind);
  bool matchWord(QStringView word);
  const ErToken& expect(ErTokenKind kind);
  ErTokenCursor consumeLine();

  QString raw(const QString& source) const;
  QString rawFrom(const QString& source, qsizetype position) const;

private:
  const QVector<ErToken>* tokens_ = nullptr;
  qsizetype position_ = 0;
  qsizetype end_ = 0;
};

QString erTokenName(ErTokenKind kind);

}  // namespace muffin::mermaid::er
