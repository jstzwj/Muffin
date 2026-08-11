#pragma once

#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::cynefin {

enum class CynefinErrorKind { Lexer, Parser, Runtime };

class CynefinParseError : public std::runtime_error {
public:
  CynefinParseError(CynefinErrorKind kind, int line, int column,
                    QString token, const QString &message);

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
  static CynefinData parse(const QString &source);
};

} // namespace muffin::mermaid::cynefin
