#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::venn {

enum class VennErrorKind { Lexer, Parser, Runtime };

class VennParseError final : public std::runtime_error {
 public:
  VennParseError(VennErrorKind kind, int line, int column, QString token,
                 const QString& message);

  VennErrorKind kind = VennErrorKind::Parser;
  int line = 1;
  int column = 1;
  QString token;
};

struct VennSubset {
  QStringList sets;
  double size = 0.0;
  QString label;
  bool hasLabel = false;
};

struct VennTextNode {
  QStringList sets;
  QString id;
  QString label;
  bool hasLabel = false;
};

struct VennStyleEntry {
  QStringList targets;
  QVector<QPair<QString, QString>> declarations;
};

struct VennData {
  QString title;
  bool hasTitleDirective = false;
  QString accTitle;
  QString accDescr;
  QVector<VennSubset> subsets;
  QVector<VennTextNode> textNodes;
  QVector<VennStyleEntry> styles;
};

class VennDiagram {
 public:
  static VennData parse(const QString& source);
};

}  // namespace muffin::mermaid::venn
