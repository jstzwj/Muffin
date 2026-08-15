#pragma once

#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::sankey {

enum class SankeyErrorKind { Lexer, Parser, Runtime };

class SankeyParseError final : public std::runtime_error {
public:
  SankeyParseError(SankeyErrorKind kind, int line, int column, QString token,
                   const QString &message);

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
  static SankeyData parse(const QString &source);
};

} // namespace muffin::mermaid::sankey
