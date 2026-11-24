#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

#include <stdexcept>

namespace muffin::mermaid {

// A diagram's own account of why its source could not be rendered. Every diagram's parse error
// derives from DiagramError and gives this, so the renderer handles all of them in one place and
// each diagram family keeps its own quirks to itself.
//
// Positions address the code the diagram's parser saw, with front matter, directives and comments
// already removed. Lines and columns are counted from 1, and are 0 where the parser reported none.
struct DiagramErrorInfo {
  // What the parser said about where the failure is.
  enum class Place {
    None,                    // nothing
    Offset,                  // `offset` and `length`
    LineColumn,              // `line` and `column`
    LineStart,               // `line` only
    LineColumnElseLineStart, // `line` and `column`, where the column may lie past the end of the line
  };

  QString stage = QStringLiteral("parse");
  QString code; // e.g. "packet-parse-error"
  QString message;
  Place place = Place::LineColumn;
  qsizetype offset = -1;
  // How much source the failure covers. For a place that is not an offset, the length given to
  // that position.
  qsizetype length = 1;
  int line = 0;
  int column = 0;
  QString production;
  QString actual;
  QStringList expected;
};

class DiagramError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
  ~DiagramError() override = default;
  // The failure as the diagram reports it.
  virtual DiagramErrorInfo info() const = 0;
};

// The name of a failure kind inside a diagnostic code: "<diagram>-<name>-error", such as
// "gantt-lexer-error". Each family names its own kinds with an overload of kindName.
template <typename Kind>
QString kindErrorCode(QStringView diagram, Kind kind) {
  return QString(diagram) + QLatin1Char('-') + QString(kindName(kind)) + QStringLiteral("-error");
}

// The report of an error that carries a failure kind and the line and column it was found at.
// Families whose parser names the offending token have it as what was found.
template <typename Error>
DiagramErrorInfo kindErrorInfo(const Error& error, QStringView diagram, qsizetype length = 1) {
  DiagramErrorInfo info;
  info.code = kindErrorCode(diagram, error.kind);
  info.message = QString::fromUtf8(error.what());
  info.line = error.line;
  info.column = error.column;
  info.length = length;
  if constexpr (requires { error.token; }) {
    info.actual = error.token;
  }
  return info;
}

} // namespace muffin::mermaid
