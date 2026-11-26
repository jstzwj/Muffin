#pragma once

#include <QString>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::cynefin {

enum class CynefinErrorKind { Lexer, Parser, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(CynefinErrorKind kind) {
  switch (kind) {
  case CynefinErrorKind::Lexer:
    return u"lexer";
  case CynefinErrorKind::Parser:
    return u"parse";
  case CynefinErrorKind::Runtime:
    return u"runtime";
  }
  return u"parse";
}

class CynefinParseError : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  CynefinParseError(CynefinErrorKind kind, int line, int column, QString token, const QString& message);

  CynefinErrorKind kind;
  int line;
  int column;
  QString token;
};

struct CynefinItem {
  QString label;
};

struct CynefinDomain {
  QString name;
  QVector<CynefinItem> items;
};

struct CynefinTransition {
  QString from;
  QString to;
  QString label;
  bool hasLabel = false;
};

struct CynefinData {
  QString title;
  QString accTitle;
  QString accDescr;
  bool hasTitleDirective = false;
  QVector<CynefinDomain> domains;
  QVector<CynefinTransition> transitions;
};

class CynefinDiagram {
public:
  static CynefinData parse(const QString& source);
};

} // namespace muffin::mermaid::cynefin
