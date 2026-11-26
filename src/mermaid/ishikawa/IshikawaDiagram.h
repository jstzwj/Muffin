#pragma once

#include <QString>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::ishikawa {

enum class IshikawaErrorKind { Lexer, Parser };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(IshikawaErrorKind kind) {
  switch (kind) {
  case IshikawaErrorKind::Lexer:
    return u"lexer";
  case IshikawaErrorKind::Parser:
    return u"parse";
  }
  return u"parse";
}

class IshikawaParseError final : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  IshikawaParseError(IshikawaErrorKind kind, int line, int column, QString token, const QString& message);

  IshikawaErrorKind kind;
  int line = 1;
  int column = 1;
  QString token;
};

struct IshikawaNode {
  QString text;
  QVector<IshikawaNode> children;
};

struct IshikawaData {
  bool hasRoot = false;
  IshikawaNode root;
  QString title;
  QString accTitle;
  QString accDescr;
};

class IshikawaDiagram {
public:
  static IshikawaData parse(const QString& source);
};

} // namespace muffin::mermaid::ishikawa
