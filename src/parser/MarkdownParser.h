#pragma once

#include "document/MarkdownNode.h"

#include <QStringView>
#include <QVector>

#include <memory>

namespace muffin {

struct ParseDiagnostic {
  QString message;
  SourceRange range;
};

struct ParseOptions {
  bool enableTable = true;
  bool enableStrikethrough = true;
  bool enableAutolink = true;
  bool enableTaskList = true;
  bool enableMath = true;
  bool preserveSourceRange = true;
};

struct ParseResult {
  std::unique_ptr<MarkdownNode> root;
  QVector<ParseDiagnostic> diagnostics;
  qint64 elapsedMs = 0;
};

class MarkdownParser {
public:
  virtual ~MarkdownParser() = default;

  virtual ParseResult parseDocument(QStringView markdown, const ParseOptions& options) = 0;
  virtual ParseResult parseBlock(QStringView markdown, BlockType context, const ParseOptions& options) = 0;
};

}  // namespace muffin
