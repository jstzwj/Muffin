#pragma once

#include <QString>

#include <stdexcept>

namespace muffin::mermaid::info {

enum class InfoErrorKind { Lexer, Parser };

class InfoParseError : public std::runtime_error {
public:
  InfoParseError(const QString& message, int line, int column,
                 InfoErrorKind kind);

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

}  // namespace muffin::mermaid::info
