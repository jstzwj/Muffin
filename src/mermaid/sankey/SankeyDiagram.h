#pragma once

#include <QString>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::sankey {

enum class SankeyErrorKind { Lexer, Parser, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(SankeyErrorKind kind) {
  switch (kind) {
  case SankeyErrorKind::Lexer:
    return u"lexer";
  case SankeyErrorKind::Parser:
    return u"parse";
  case SankeyErrorKind::Runtime:
    return u"runtime";
  }
  return u"parse";
}

class SankeyParseError final : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  SankeyParseError(SankeyErrorKind kind, int line, int column, QString token, const QString& message);

  SankeyErrorKind kind = SankeyErrorKind::Parser;
  int line = 1;
  int column = 1;
  QString token;
};

struct SankeyNodeData {
  QString id;
};

struct SankeyLinkData {
  QString source;
  QString target;
  double value = 0.0;
};

struct SankeyData {
  QVector<SankeyNodeData> nodes;
  QVector<SankeyLinkData> links;
};

class SankeyDiagram {
public:
  static SankeyData parse(const QString& source);
};

} // namespace muffin::mermaid::sankey
