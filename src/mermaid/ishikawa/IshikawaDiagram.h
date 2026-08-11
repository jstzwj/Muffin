#pragma once

#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::ishikawa {

enum class IshikawaErrorKind { Lexer, Parser };

class IshikawaParseError final : public std::runtime_error {
 public:
  IshikawaParseError(IshikawaErrorKind kind, int line, int column,
                     QString token, const QString& message);

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

}  // namespace muffin::mermaid::ishikawa
