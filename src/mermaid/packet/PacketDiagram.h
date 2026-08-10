#pragma once

// Native parser/database model for Mermaid 11.16.0 packet diagrams.

#include <QJsonValue>
#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::packet {

enum class PacketErrorKind { Lexer, Parser, Runtime };

struct PacketBlock {
  qreal start = 0.0;
  qreal end = 0.0;
  qreal bits = 0.0;
  QString label;
};

using PacketWord = QVector<PacketBlock>;

struct PacketData {
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<PacketWord> words;
};

struct PacketParseError : std::runtime_error {
  int line = 0;
  int column = 0;
  PacketErrorKind kind = PacketErrorKind::Parser;

  PacketParseError(const QString& message, int line = 0, int column = 1,
                   PacketErrorKind kind = PacketErrorKind::Parser);
};

class PacketDiagram {
public:
  // bitsPerRow is deliberately a raw JSON scalar. Mermaid uses it both in the
  // parser-side row splitter and in JavaScript arithmetic; strings and booleans
  // therefore have observable behavior that an eager toInt() would erase.
  // Undefined/null/array/object match lodash merge over the numeric default 32.
  static PacketData parse(
      const QString& source,
      const QJsonValue& bitsPerRow = QJsonValue(32.0));
};

}  // namespace muffin::mermaid::packet
