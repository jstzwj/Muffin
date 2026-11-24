#pragma once

// Native parser/database model for Mermaid 11.16.0 architecture diagrams.

#include <QString>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::architecture {

enum class ArchitectureErrorKind { Lexer, Parser, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(ArchitectureErrorKind kind) {
  switch (kind) {
  case ArchitectureErrorKind::Lexer: return u"lexer";
  case ArchitectureErrorKind::Parser: return u"parse";
  case ArchitectureErrorKind::Runtime: return u"runtime";
  }
  return u"parse";
}

class ArchitectureParseError final : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  ArchitectureParseError(ArchitectureErrorKind kind, int line, int column, QString token, const QString& message);

  ArchitectureErrorKind kind;
  int line;
  int column;
  QString token;
};

struct ArchitectureGroup {
  QString id;
  QString icon;
  QString title;
  QString parent;
  bool hasIcon = false;
  bool hasTitle = false;
  bool hasParent = false;
};

struct ArchitectureService {
  QString id;
  QString icon;
  QString iconText;
  QString title;
  QString parent;
  bool hasIcon = false;
  bool hasIconText = false;
  bool hasTitle = false;
  bool hasParent = false;
};

struct ArchitectureJunction {
  QString id;
  QString parent;
  bool hasParent = false;
};

struct ArchitectureEdge {
  QString lhsId;
  QString rhsId;
  QChar lhsDir;
  QChar rhsDir;
  QString title;
  bool lhsInto = false;
  bool rhsInto = false;
  bool lhsGroup = false;
  bool rhsGroup = false;
  bool hasTitle = false;
};

struct ArchitectureAlignment {
  enum class Direction { Row, Column };
  Direction direction = Direction::Row;
  QVector<QString> members;
};

struct ArchitectureData {
  QString title;
  QString accTitle;
  QString accDescr;
  bool hasTitleDirective = false;
  QVector<ArchitectureGroup> groups;
  QVector<ArchitectureService> services;
  QVector<ArchitectureJunction> junctions;
  QVector<ArchitectureEdge> edges;
  QVector<ArchitectureAlignment> alignments;
};

class ArchitectureDiagram {
public:
  static ArchitectureData parse(const QString& source);
};

} // namespace muffin::mermaid::architecture
