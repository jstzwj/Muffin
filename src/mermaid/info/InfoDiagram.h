#pragma once

#include <QString>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::info {

enum class InfoErrorKind { Lexer, Parser };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(InfoErrorKind kind) {
  switch (kind) {
  case InfoErrorKind::Lexer:
    return u"lexer";
  case InfoErrorKind::Parser:
    return u"parse";
  }
  return u"parse";
}

class InfoParseError : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  InfoParseError(const QString& message, int line, int column, InfoErrorKind kind);

  int line = 0;
  int column = 0;
  InfoErrorKind kind = InfoErrorKind::Parser;
};

struct InfoData {
  QString title;
  QString accTitle;
  QString accDescr;
};

class InfoDiagram {
public:
  static InfoData parse(const QString& source);
};

} // namespace muffin::mermaid::info
