#pragma once

#include <QString>

namespace muffin {

enum class TextLineEnding { Lf, Crlf, Cr };

struct TextFileFormat {
  QString encodingName = QStringLiteral("UTF-8");
  TextLineEnding lineEnding = TextLineEnding::Lf;
  bool writeBom = false;
  bool ensureTrailingNewline = false;
  bool existingFile = false;

  bool operator==(const TextFileFormat&) const = default;
};

}  // namespace muffin
