#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::venn {

enum class VennErrorKind { Lexer, Parser, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(VennErrorKind kind) {
  switch (kind) {
  case VennErrorKind::Lexer: return u"lexer";
  case VennErrorKind::Parser: return u"parse";
  case VennErrorKind::Runtime: return u"runtime";
  }
  return u"parse";
}

class VennParseError final : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  VennParseError(VennErrorKind kind, int line, int column, QString token, const QString& message);

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

} // namespace muffin::mermaid::venn
