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

struct ParsePhasePerformance {
  QString name;
  qint64 elapsedNs = 0;
  qint64 workingSetBytesAfter = 0;

  bool operator==(const ParsePhasePerformance&) const = default;
};

struct ParsePerformanceMetrics {
  qint64 totalElapsedNs = 0;
  qint64 workingSetBytesBefore = 0;
  qint64 workingSetBytesAfter = 0;
  QVector<ParsePhasePerformance> phases;

  bool isEmpty() const { return phases.isEmpty(); }
  bool operator==(const ParsePerformanceMetrics&) const = default;
};

struct ParseOptions {
  bool enableTable = true;
  bool enableStrikethrough = true;
  bool enableAutolink = true;
  bool enableTaskList = true;
  bool enableMath = true;
  bool relaxedInlineMath = true;
  bool enableFrontMatter = true;
  bool enableAlertBox = true;
  bool enableHighlight = false;
  bool enableSubscript = false;
  bool enableSuperscript = false;
  bool preserveSourceRange = true;
  // Remap Unicode em-dash/ellipsis back to ASCII before cmark parses (byte-length-preserving), so
  // syntax like a `---` thematic break survives when Smart Dashes has written an em-dash to source.
  bool enableUnicodeRemap = false;

  // Used by DocumentSession::setParseOptions to skip a full re-parse when nothing changed
  // (e.g. the PreferencesDialog closed without altering any parse-gating setting).
  bool operator==(const ParseOptions&) const = default;
};

struct ParseResult {
  std::unique_ptr<MarkdownNode> root;
  QVector<ParseDiagnostic> diagnostics;
  qint64 elapsedMs = 0;
  ParsePerformanceMetrics performanceMetrics;
};

class MarkdownParser {
public:
  virtual ~MarkdownParser() = default;

  virtual ParseResult parseDocument(QStringView markdown, const ParseOptions& options) = 0;
  virtual ParseResult parseBlock(QStringView markdown, BlockType context, const ParseOptions& options) = 0;
};

}  // namespace muffin
