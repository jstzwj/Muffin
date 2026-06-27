#include "parser/CmarkGfmParser.h"

#include "document/DefinitionBlock.h"
#include "document/InlineNode.h"
#include "document/LineStartOffsetCache.h"
#include "document/PendingBlockMarker.h"
#include "parser/CmarkNodeAdapter.h"
#include "parser/MarkdownSerializer.h"
#include "diagnostics/ProcessMemory.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QStringList>
#include <QVector>

extern "C" {
#include "cmark-gfm-core-extensions.h"
}

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(parsePerf, "muffin.perf", QtWarningMsg)

// RAII per-phase timer for parseDocument. Emits "label <ms>" on scope exit via the
// muffin.perf category, which MUFFIN_PERF_LOG captures. No-op (single branch) when the
// category's debug level is disabled.
class ParsePerfTimer {
public:
  explicit ParsePerfTimer(const char* label) : label_(label), enabled_(parsePerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }
  ~ParsePerfTimer() {
    if (enabled_) {
      qCDebug(parsePerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0
                                   << " ms ws=" << (muffin::diag::workingSetBytes() >> 20) << "MB";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

void annotateSourceOffsets(const LineStartOffsetCache& lineOffsets, QStringView markdown, MarkdownNode& node) {
  SourceRange range = node.sourceRange();
  if (range.lineStart > 0) {
    const qsizetype start = lineOffsets.offsetForLineColumn(range.lineStart, qMax(1, range.columnStart));
    const qsizetype end = lineOffsets.lineEndOffset(range.lineEnd);
    if (start >= 0 && end >= start) {
      range.byteStart = start;
      range.byteEnd = end;
      node.setSourceRange(range);
    }
  }

  // cmark-gfm reports a math block's source range up to the last CONTENT line only, excluding the
  // closing delimiter line (`$$` / `\]`). Every other consumer — block deletion, slice selection,
  // serialization-range replacement — needs the range to span the whole block including the closer,
  // just like a fenced code block. Extend it here, at the single place block byte ranges are
  // resolved, so the range is correct everywhere instead of being patched ad hoc downstream.
  if (node.type() == BlockType::MathBlock && range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
    for (const QString& closer : {QStringLiteral("\n$$"), QStringLiteral("\n\\]")}) {
      if (range.byteEnd + closer.size() <= markdown.size() &&
          markdown.mid(range.byteEnd, closer.size()) == closer) {
        range.byteEnd += closer.size();
        range.lineEnd = lineOffsets.lineForOffset(range.byteEnd);
        node.setSourceRange(range);
        break;
      }
    }
  }

  for (const auto& child : node.children()) {
    annotateSourceOffsets(lineOffsets, markdown, *child);
  }
}

bool isListMarkerAt(const QString& line, int index, int& markerEnd) {
  if (index >= line.size()) {
    return false;
  }
  if ((line.at(index) == QLatin1Char('-') || line.at(index) == QLatin1Char('*') || line.at(index) == QLatin1Char('+')) &&
      index + 1 < line.size() && line.at(index + 1).isSpace()) {
    markerEnd = index + 2;
    return true;
  }

  int digitEnd = index;
  while (digitEnd < line.size() && line.at(digitEnd).isDigit()) {
    ++digitEnd;
  }
  if (digitEnd > index && digitEnd + 1 < line.size() &&
      (line.at(digitEnd) == QLatin1Char('.') || line.at(digitEnd) == QLatin1Char(')')) &&
      line.at(digitEnd + 1).isSpace()) {
    markerEnd = digitEnd + 2;
    return true;
  }
  return false;
}

int containerPrefixEnd(const QString& line) {
  int index = 0;
  bool consumedContainer = false;
  while (true) {
    const int beforeIndent = index;
    int indent = 0;
    while (index < line.size() && indent < 3 && line.at(index) == QLatin1Char(' ')) {
      ++index;
      ++indent;
    }

    if (index < line.size() && line.at(index) == QLatin1Char('>')) {
      ++index;
      if (index < line.size() && line.at(index) == QLatin1Char(' ')) {
        ++index;
      }
      consumedContainer = true;
      continue;
    }

    int markerEnd = -1;
    if (isListMarkerAt(line, index, markerEnd)) {
      index = markerEnd;
      consumedContainer = true;
      continue;
    }

    return consumedContainer ? beforeIndent : 0;
  }
}

int contentStartWithinContainer(const QString& line) {
  const int prefixEnd = containerPrefixEnd(line);
  int index = prefixEnd;
  int indent = 0;
  while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
    ++index;
    ++indent;
  }
  return indent <= 3 ? index : -1;
}

bool hasOnlyTrailingSpaces(const QString& line, int start) {
  for (int i = start; i < line.size(); ++i) {
    if (!line.at(i).isSpace()) {
      return false;
    }
  }
  return true;
}

bool isLegacyMathDelimiterLine(const QString& line, int contentStart, const QString& delimiter) {
  if (contentStart < 0 || contentStart + delimiter.size() > line.size()) {
    return false;
  }
  return line.mid(contentStart, delimiter.size()) == delimiter &&
         hasOnlyTrailingSpaces(line, contentStart + delimiter.size());
}

QString bodyForLegacyMathBlock(QStringView markdown, const SourceRange& range) {
  if (range.byteStart < 0 || range.byteEnd > markdown.size() || range.byteEnd <= range.byteStart) {
    return {};
  }

  const QString text = markdown.toString();
  qsizetype openerLineStart = range.byteStart;
  while (openerLineStart > 0 && text.at(openerLineStart - 1) != QLatin1Char('\n')) {
    --openerLineStart;
  }
  qsizetype openerLineEnd = range.byteStart;
  while (openerLineEnd < text.size() && text.at(openerLineEnd) != QLatin1Char('\n')) {
    ++openerLineEnd;
  }
  if (openerLineEnd >= text.size()) {
    return {};
  }

  const QString openerLine = text.mid(openerLineStart, openerLineEnd - openerLineStart);
  const bool stripContainerPrefixes = contentStartWithinContainer(openerLine) == range.byteStart - openerLineStart;

  QStringList bodyLines;
  qsizetype lineStart = openerLineEnd + 1;
  while (lineStart <= range.byteEnd && lineStart < text.size()) {
    qsizetype lineEnd = lineStart;
    while (lineEnd < text.size() && lineEnd < range.byteEnd && text.at(lineEnd) != QLatin1Char('\n')) {
      ++lineEnd;
    }

    const QString line = text.mid(lineStart, lineEnd - lineStart);
    const int contentStart = contentStartWithinContainer(line);
    if (isLegacyMathDelimiterLine(line, contentStart, QStringLiteral("\\]"))) {
      break;
    }

    bodyLines.append(stripContainerPrefixes && contentStart > 0 ? line.mid(contentStart) : line);

    if (lineEnd >= text.size() || lineEnd >= range.byteEnd) {
      break;
    }
    lineStart = lineEnd + 1;
  }
  return bodyLines.join(QLatin1Char('\n')).trimmed();
}

bool fenceInfoAt(const QString& line, int contentStart, QChar& fenceChar, int& fenceLength) {
  if (contentStart < 0 || contentStart >= line.size()) {
    return false;
  }
  const QChar c = line.at(contentStart);
  if (c != QLatin1Char('`') && c != QLatin1Char('~')) {
    return false;
  }
  int run = 0;
  while (contentStart + run < line.size() && line.at(contentStart + run) == c) {
    ++run;
  }
  if (run < 3) {
    return false;
  }
  fenceChar = c;
  fenceLength = run;
  return true;
}

bool isDollarMathDelimiterLine(const QString& line, int contentStart) {
  if (contentStart < 0 || contentStart + 2 > line.size()) {
    return false;
  }
  if (line.at(contentStart) != QLatin1Char('$') || line.at(contentStart + 1) != QLatin1Char('$')) {
    return false;
  }
  return hasOnlyTrailingSpaces(line, contentStart + 2);
}

enum class MathScanState {
  None,
  Dollar,
  Bracket
};

// Display-math shorthand: a `\[` line opens and the next matching `\]` line closes a
// LaTeX display-math block. cmark-gfm only parses `$$`, so rewrite the paired delimiters to `$$`.
// The delimiter swaps are byte-for-byte the same length on the same column, so every offset cmark
// reports still maps onto the original text. Lines inside fenced code blocks are left untouched.
// Dollar delimiter lines inside bracket math are temporarily escaped, then the MathBlock literal is
// restored from the original source when delimiters are annotated.
QString legacyMathDelimitersToDollar(QStringView markdown) {
  QStringList lines = markdown.toString().split(QLatin1Char('\n'));
  bool inFence = false;
  MathScanState mathState = MathScanState::None;
  QChar fenceChar;
  int fenceLength = 0;
  for (QString& line : lines) {
    const int contentStart = contentStartWithinContainer(line);

    if (inFence) {
      if (contentStart >= 0 && line.size() > contentStart && line.at(contentStart) == fenceChar) {
        int run = 0;
        while (contentStart + run < line.size() && line.at(contentStart + run) == fenceChar) {
          ++run;
        }
        if (run >= fenceLength && hasOnlyTrailingSpaces(line, contentStart + run)) {
          inFence = false;
          fenceChar = QChar();
          fenceLength = 0;
        }
      }
      continue;
    }

    if (mathState == MathScanState::Dollar) {
      if (isDollarMathDelimiterLine(line, contentStart)) {
        mathState = MathScanState::None;
      }
      continue;
    }

    if (mathState == MathScanState::Bracket) {
      if (isLegacyMathDelimiterLine(line, contentStart, QStringLiteral("\\]"))) {
        line.replace(contentStart, 2, QStringLiteral("$$"));
        mathState = MathScanState::None;
      } else if (isDollarMathDelimiterLine(line, contentStart)) {
        line.replace(contentStart, 2, QStringLiteral("\\$"));
      }
      continue;
    }

    QChar openingFenceChar;
    int openingFenceLength = 0;
    if (fenceInfoAt(line, contentStart, openingFenceChar, openingFenceLength)) {
      inFence = true;
      fenceChar = openingFenceChar;
      fenceLength = openingFenceLength;
      continue;
    }

    if (isDollarMathDelimiterLine(line, contentStart)) {
      mathState = MathScanState::Dollar;
      continue;
    }

    if (isLegacyMathDelimiterLine(line, contentStart, QStringLiteral("\\["))) {
      line.replace(contentStart, 2, QStringLiteral("$$"));
      mathState = MathScanState::Bracket;
    }
  }
  return lines.join(QLatin1Char('\n'));
}

// Remap the Unicode em-dash and ellipsis that Smart Dashes may have written into the source back to
// their ASCII forms so Markdown syntax (`---` thematic breaks, `...`) still parses. Only the
// byte-length-preserving replacements are applied, so every cmark-reported offset still maps onto
// the original text (same invariant as legacyMathDelimitersToDollar). Lines inside fenced code
// blocks are left untouched so a code literal's em-dash isn't turned into "---".
QString remapUnicodePunctuation(QStringView markdown) {
  static const QString kEmDash = QString::fromUtf8("\xe2\x80\x94");    // —
  static const QString kEllipsis = QString::fromUtf8("\xe2\x80\xa6");  // …
  QStringList lines = markdown.toString().split(QLatin1Char('\n'));
  bool inFence = false;
  QChar fenceChar;
  int fenceLength = 0;
  for (QString& line : lines) {
    const int contentStart = contentStartWithinContainer(line);
    if (inFence) {
      if (contentStart >= 0 && line.size() > contentStart && line.at(contentStart) == fenceChar) {
        int run = 0;
        while (contentStart + run < line.size() && line.at(contentStart + run) == fenceChar) {
          ++run;
        }
        if (run >= fenceLength && hasOnlyTrailingSpaces(line, contentStart + run)) {
          inFence = false;
          fenceChar = QChar();
          fenceLength = 0;
        }
      }
      continue;
    }
    QChar openingFenceChar;
    int openingFenceLength = 0;
    if (fenceInfoAt(line, contentStart, openingFenceChar, openingFenceLength)) {
      inFence = true;
      fenceChar = openingFenceChar;
      fenceLength = openingFenceLength;
      continue;
    }
    line.replace(kEmDash, QStringLiteral("---"));
    line.replace(kEllipsis, QStringLiteral("..."));
  }
  return lines.join(QLatin1Char('\n'));
}

// After source offsets are known, mark each MathBlock whose original opener was `\[` so the
// serializer can re-emit `\[ ... \]` instead of `$$ ... $$`.
void annotateMathDelimiters(QStringView markdown, MarkdownNode& root) {
  const auto visit = [](auto&& self, QStringView md, MarkdownNode& node) -> void {
    if (node.type() == BlockType::MathBlock) {
      const SourceRange range = node.sourceRange();
      if (range.byteStart >= 0 && range.byteStart + 1 < md.size() &&
          md.at(range.byteStart) == QLatin1Char('\\') && md.at(range.byteStart + 1) == QLatin1Char('[')) {
        node.setMathDelimiter(MathDelimiter::Bracket);
        node.setLiteral(bodyForLegacyMathBlock(md, range));
      }
    }
    for (const auto& child : node.children()) {
      self(self, md, *child);
    }
  };
  visit(visit, markdown, root);
}

// Collects the leading plain text of a paragraph's inlines (enough to test a GitHub alert marker),
// stopping at the first line break so the marker must sit on the blockquote's first line.
QString leadingInlineText(const QVector<InlineNode>& inlines) {
  QString text;
  const auto visit = [&](auto&& self, const QVector<InlineNode>& nodes) -> void {
    for (const InlineNode& node : nodes) {
      if (text.size() >= 32) {
        return;
      }
      switch (node.type()) {
        case InlineType::Text:
        case InlineType::Code:
        case InlineType::InlineMath:
        case InlineType::HtmlInline:
          text += node.text();
          break;
        case InlineType::SoftBreak:
        case InlineType::LineBreak:
          return;
        default:
          break;
      }
      self(self, node.children());
    }
  };
  visit(visit, inlines);
  return text;
}

// Returns the alert kind for a blockquote whose first line is `[!NOTE]`/`[!TIP]`/... (case
// insensitive, matching GitHub's five kinds), else None.
AlertKind alertKindFromFirstLine(const QString& firstLine) {
  const QString s = firstLine.trimmed();
  if (s.size() < 5 || s.at(0) != QLatin1Char('[') || s.at(1) != QLatin1Char('!')) {
    return AlertKind::None;
  }
  const int close = s.indexOf(QLatin1Char(']'), 2);
  if (close < 0) {
    return AlertKind::None;
  }
  const QString key = s.mid(2, close - 2).toUpper();
  if (key == QLatin1String("NOTE")) {
    return AlertKind::Note;
  }
  if (key == QLatin1String("TIP")) {
    return AlertKind::Tip;
  }
  if (key == QLatin1String("IMPORTANT")) {
    return AlertKind::Important;
  }
  if (key == QLatin1String("WARNING")) {
    return AlertKind::Warning;
  }
  if (key == QLatin1String("CAUTION")) {
    return AlertKind::Caution;
  }
  return AlertKind::None;
}

// Tags each blockquote whose first line is a GitHub alert marker with its kind, so the renderer can
// draw a themed card instead of a plain quote bar. Mirrors annotateMathDelimiters's tree walk.
void annotateAlertKinds(MarkdownNode& root) {
  const auto visit = [](auto&& self, MarkdownNode& node) -> void {
    if (node.type() == BlockType::BlockQuote) {
      MarkdownNode* paragraph = node.firstChildByType(BlockType::Paragraph);
      if (paragraph) {
        const AlertKind kind = alertKindFromFirstLine(leadingInlineText(paragraph->inlines()));
        if (kind != AlertKind::None) {
          node.setAlertKind(kind);
        }
      }
    }
    for (const auto& child : node.children()) {
      self(self, *child);
    }
  };
  visit(visit, root);
}

// === Generic delimited-inline splitter (highlight == / subscript ~ / superscript ^) ===
// cmark-gfm has no extension for these markers, so `==x==` / `~x~` / `^x^` arrive as literal Text.
// A single parameterized pass tokenizes each paragraph/heading/table-cell inline list into marker
// runs + content, then pairs runs with an opener stack (emphasis-style flank rules) into the typed
// inline named by the spec. Faithful to Pandoc: only a run of the spec's exact length is a marker
// (so `===`, a lone `=`, and a backslash-escaped marker all stay literal). Source ranges stay
// absolute (the projection subtracts the block's source base later). Driven by a DelimitedInlineSpec
// so one engine yields Highlight (`==`, run 2), Subscript (`~`, run 1) and Superscript (`^`, run 1).
struct DelimitedInlineSpec {
  QChar marker;
  int runLength = 0;
  InlineType type = InlineType::Highlight;
  QString markerString;
};

DelimitedInlineSpec specForHighlight() { return {QLatin1Char('='), 2, InlineType::Highlight, QStringLiteral("==")}; }
DelimitedInlineSpec specForSubscript() { return {QLatin1Char('~'), 1, InlineType::Subscript, QStringLiteral("~")}; }
DelimitedInlineSpec specForSuperscript() { return {QLatin1Char('^'), 1, InlineType::Superscript, QStringLiteral("^")}; }
DelimitedInlineSpec specForStrikethrough() { return {QLatin1Char('~'), 2, InlineType::Strikethrough, QStringLiteral("~~")}; }

struct DelimToken {
  enum class Kind { Inline, MarkerRun, Built };
  Kind kind = Kind::Inline;
  InlineNode node;  // Inline: a marker-free text segment or an opaque inline; Built: the assembled node
  qsizetype srcStart = -1;
  qsizetype srcEnd = -1;
  int runLength = 0;
  bool escaped = false;
};

QChar firstSignificantChar(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Text:
    case InlineType::Code:
    case InlineType::InlineMath:
    case InlineType::HtmlInline:
      return node.text().isEmpty() ? QChar() : node.text().at(0);
    case InlineType::SoftBreak:
    case InlineType::LineBreak:
      return QChar::Space;
    case InlineType::Emphasis:
    case InlineType::Strong:
    case InlineType::Strikethrough:
    case InlineType::Highlight:
    case InlineType::Subscript:
    case InlineType::Superscript:
    case InlineType::Link:
      for (const InlineNode& child : node.children()) {
        const QChar ch = firstSignificantChar(child);
        if (!ch.isNull()) {
          return ch;
        }
      }
      return QChar();
    default:  // Image, TaskMarker — treated as non-space content
      return QLatin1Char('x');
  }
}

QChar lastSignificantChar(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Text:
    case InlineType::Code:
    case InlineType::InlineMath:
    case InlineType::HtmlInline:
      return node.text().isEmpty() ? QChar() : node.text().at(node.text().size() - 1);
    case InlineType::SoftBreak:
    case InlineType::LineBreak:
      return QChar::Space;
    case InlineType::Emphasis:
    case InlineType::Strong:
    case InlineType::Strikethrough:
    case InlineType::Highlight:
    case InlineType::Subscript:
    case InlineType::Superscript:
    case InlineType::Link: {
      const QVector<InlineNode>& kids = node.children();
      for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        const QChar ch = lastSignificantChar(*it);
        if (!ch.isNull()) {
          return ch;
        }
      }
      return QChar();
    }
    default:
      return QLatin1Char('x');
  }
}

InlineNode makeDelimitedInline(const DelimitedInlineSpec& spec, QVector<InlineNode> children) {
  switch (spec.type) {
    case InlineType::Strikethrough:
      return InlineNode::strikethrough(spec.markerString, std::move(children));
    case InlineType::Subscript:
      return InlineNode::subscript(spec.markerString, std::move(children));
    case InlineType::Superscript:
      return InlineNode::superscript(spec.markerString, std::move(children));
    case InlineType::Highlight:
    default:
      return InlineNode::highlight(spec.markerString, std::move(children));
  }
}

// Only Text nodes whose decoded text maps 1:1 to their source span can be safely split — escapes
// and HTML entities decode to fewer source chars and would misalign segment offsets. For those,
// keep the node opaque (its marker stays literal).
QVector<DelimToken> buildDelimTokens(const QVector<InlineNode>& inlines, QStringView markdown, const DelimitedInlineSpec& spec) {
  QVector<DelimToken> tokens;
  for (const InlineNode& node : inlines) {
    if (node.type() == InlineType::Text) {
      const QString text = node.text();
      const InlineRange range = node.sourceRange();
      const qsizetype srcLen = range.end - range.start;
      if (range.start < 0 || srcLen != text.size()) {
        tokens.append({DelimToken::Kind::Inline, node, range.start, range.end, 0, false});
        continue;
      }
      qsizetype i = 0;
      while (i < text.size()) {
        if (text.at(i) == spec.marker) {
          qsizetype j = i;
          while (j < text.size() && text.at(j) == spec.marker) {
            ++j;
          }
          DelimToken t{DelimToken::Kind::MarkerRun, InlineNode(InlineType::Text), range.start + i, range.start + j,
                       int(j - i), false};
          if (t.srcStart > 0 && markdown.at(t.srcStart - 1) == QLatin1Char('\\')) {
            t.escaped = true;
          }
          tokens.append(t);
          i = j;
        } else {
          qsizetype j = i;
          while (j < text.size() && text.at(j) != spec.marker) {
            ++j;
          }
          InlineNode seg = InlineNode::text(text.mid(i, j - i));
          InlineSourceRanges segRanges;
          segRanges.source = InlineRange{range.start + i, range.start + j};
          segRanges.content = segRanges.source;
          seg.setSourceRanges(segRanges);
          tokens.append({DelimToken::Kind::Inline, seg, range.start + i, range.start + j, 0, false});
          i = j;
        }
      }
    } else {
      const InlineRange range = node.sourceRange();
      tokens.append({DelimToken::Kind::Inline, node, range.start, range.end, 0, false});
    }
  }
  return tokens;
}

InlineNode tokenToInline(const DelimToken& t, const DelimitedInlineSpec& spec) {
  if (t.kind == DelimToken::Kind::MarkerRun) {
    InlineNode text = InlineNode::text(QString(t.runLength, spec.marker));
    InlineSourceRanges ranges;
    ranges.source = InlineRange{t.srcStart, t.srcEnd};
    ranges.content = ranges.source;
    text.setSourceRanges(ranges);
    return text;
  }
  return t.node;
}

void matchDelimTokens(QVector<DelimToken>& tokens, const DelimitedInlineSpec& spec) {
  QVector<int> openers;  // indices of marker runs that can open
  for (int i = 0; i < tokens.size(); ++i) {
    const DelimToken& t = tokens[i];
    if (t.kind != DelimToken::Kind::MarkerRun || t.runLength != spec.runLength || t.escaped) {
      continue;
    }
    const bool canOpen = i + 1 < tokens.size() &&
        !firstSignificantChar(tokens[i + 1].node).isNull() &&
        !firstSignificantChar(tokens[i + 1].node).isSpace();
    const bool canClose = i > 0 &&
        !lastSignificantChar(tokens[i - 1].node).isNull() &&
        !lastSignificantChar(tokens[i - 1].node).isSpace();
    if (canClose && !openers.isEmpty()) {
      const int opener = openers.last();
      if (opener + 1 < i) {  // at least one content token between them
        QVector<InlineNode> children;
        for (int k = opener + 1; k < i; ++k) {
          children.append(tokenToInline(tokens[k], spec));
        }
        InlineNode built = makeDelimitedInline(spec, std::move(children));
        InlineSourceRanges ranges;
        ranges.openMarker = InlineRange{tokens[opener].srcStart, tokens[opener].srcEnd};
        ranges.closeMarker = InlineRange{t.srcStart, t.srcEnd};
        ranges.source = InlineRange{ranges.openMarker.start, ranges.closeMarker.end};
        ranges.content = InlineRange{ranges.openMarker.end, ranges.closeMarker.start};
        built.setSourceRanges(ranges);
        tokens[opener] = DelimToken{DelimToken::Kind::Built, built, ranges.source.start, ranges.source.end, 0, false};
        tokens.remove(opener + 1, i - opener);
        openers.removeLast();
        i = opener;  // rescan from the new built token (its content may pair further out)
        continue;
      }
      openers.removeLast();
    }
    if (canOpen) {
      openers.append(i);
    }
  }
}

void splitDelimInBlock(QVector<InlineNode>& inlines, QStringView markdown, const DelimitedInlineSpec& spec) {
  QVector<DelimToken> tokens = buildDelimTokens(inlines, markdown, spec);
  matchDelimTokens(tokens, spec);
  QVector<InlineNode> rebuilt;
  rebuilt.reserve(tokens.size());
  bool changed = false;
  for (const DelimToken& t : tokens) {
    if (t.kind == DelimToken::Kind::Built) {
      changed = true;
    }
    rebuilt.append(tokenToInline(t, spec));
  }
  if (changed) {
    inlines = std::move(rebuilt);
  }
}

// Walks the tree and splits spec-delimited runs into typed inlines within every block that owns
// inlines (paragraph, heading, table cell). Gated on the matching ParseOptions flag at the call site.
void splitDelimInlines(MarkdownNode& root, QStringView markdown, const DelimitedInlineSpec& spec) {
  // The marker char can only ever produce a split where it occurs; if the whole document has none,
  // the walk has nothing to do. One fast scan beats a full tree walk over every inline-bearing
  // block (~1s @100MB saved for the common "no ~ / ^ / ==" document).
  if (markdown.indexOf(spec.marker) < 0) {
    return;
  }
  const auto visit = [&](auto&& self, MarkdownNode& node) -> void {
    if (!node.inlines().isEmpty()) {
      splitDelimInBlock(node.inlines(), markdown, spec);
    }
    for (const auto& child : node.children()) {
      self(self, *child);
    }
  };
  visit(visit, root);
}

void annotateDefinitionBlocks(
    MarkdownNode& root,
    const QVector<DefinitionParseResult>& definitions,
    const LineStartOffsetCache& lineOffsets) {
  if (definitions.isEmpty()) {
    return;
  }
  // Index every footnote/link definition node by its source start offset once, so each definition
  // resolves in O(1) instead of re-walking the whole tree per definition (was O(N x nodes)).
  QHash<qsizetype, MarkdownNode*> defNodeByStart;
  const auto indexTree = [&](const auto& self, MarkdownNode& node) -> void {
    if ((node.type() == BlockType::FootnoteDefinition || node.type() == BlockType::LinkDefinition) &&
        node.sourceRange().byteStart >= 0) {
      defNodeByStart.insert(node.sourceRange().byteStart, &node);
    }
    for (const auto& child : node.children()) {
      self(self, *child);
    }
  };
  indexTree(indexTree, root);

  for (const DefinitionParseResult& parsedDefinition : definitions) {
    const DefinitionBlock& definition = parsedDefinition.definition;
    const auto found = defNodeByStart.constFind(definition.markerRange.start);
    if (found == defNodeByStart.constEnd()) {
      continue;
    }
    MarkdownNode& node = *found.value();
    const SourceRange range = node.sourceRange();
    const bool matchesRange = range.byteStart == definition.markerRange.start &&
                              range.byteEnd >= definition.markerRange.end;
    const bool matchesType =
        (definition.kind == DefinitionBlock::Kind::Footnote && node.type() == BlockType::FootnoteDefinition) ||
        (definition.kind == DefinitionBlock::Kind::Link && node.type() == BlockType::LinkDefinition);
    if (!matchesRange || !matchesType) {
      continue;
    }
    DefinitionBlock annotated = definition;
    if (node.type() == BlockType::FootnoteDefinition) {
      annotated.sourceRange = {range.byteStart, range.byteEnd};
    }
    node.setDefinition(annotated);
    if (definition.sourceRange.isValid() && node.type() != BlockType::FootnoteDefinition) {
      SourceRange preciseRange = node.sourceRange();
      preciseRange.byteStart = definition.sourceRange.start;
      preciseRange.byteEnd = definition.sourceRange.end;
      preciseRange.lineStart = lineOffsets.lineForOffset(preciseRange.byteStart);
      preciseRange.lineEnd = lineOffsets.lineForOffset(preciseRange.byteEnd);
      const qsizetype lineStart = lineOffsets.offsetForLineColumn(preciseRange.lineStart, 1);
      preciseRange.columnStart = lineStart >= 0 ? static_cast<int>(preciseRange.byteStart - lineStart + 1) : 1;
      preciseRange.columnEnd = lineStart >= 0 ? static_cast<int>(preciseRange.byteEnd - lineStart + 1) : preciseRange.columnStart;
      node.setSourceRange(preciseRange);
    }
  }
}

bool shouldInsertSyntheticDefinition(const DefinitionParseResult& parsedDefinition) {
  return parsedDefinition.classification == DefinitionParseClassification::ValidMarkdownDefinition ||
         parsedDefinition.classification == DefinitionParseClassification::VirtualTemplate;
}

std::unique_ptr<MarkdownNode> createDefinitionNode(const DefinitionBlock& definition, const LineStartOffsetCache& lineOffsets) {
  const BlockType type =
      definition.kind == DefinitionBlock::Kind::Footnote ? BlockType::FootnoteDefinition : BlockType::LinkDefinition;
  auto node = std::make_unique<MarkdownNode>(type);
  node->setDefinition(definition);
  node->setLiteral(definition.kind == DefinitionBlock::Kind::Footnote ? definition.note : definition.destination);

  SourceRange range;
  range.byteStart = definition.sourceRange.isValid() ? definition.sourceRange.start : definition.markerRange.start;
  range.byteEnd = definition.sourceRange.isValid()
                      ? definition.sourceRange.end
                      : qMax(definition.markerRange.end,
                             qMax(definition.destinationRange.end, qMax(definition.titleRange.end, definition.noteRange.end)));
  range.lineStart = lineOffsets.lineForOffset(range.byteStart);
  range.lineEnd = lineOffsets.lineForOffset(range.byteEnd);
  const qsizetype lineStart = lineOffsets.offsetForLineColumn(range.lineStart, 1);
  range.columnStart = lineStart >= 0 ? static_cast<int>(range.byteStart - lineStart + 1) : 1;
  range.columnEnd = lineStart >= 0 ? static_cast<int>(range.byteEnd - lineStart + 1) : range.columnStart;
  node->setSourceRange(range);
  return node;
}

bool isVirtualEmptyParagraph(const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  return node.type() == BlockType::Paragraph && range.byteStart >= 0 && range.byteEnd == range.byteStart;
}

bool isDefinitionSourceParagraph(const MarkdownNode& node, const DefinitionBlock& definition) {
  if (node.type() != BlockType::Paragraph || !definition.sourceRange.isValid()) {
    return false;
  }
  const SourceRange range = node.sourceRange();
  return range.byteStart == definition.sourceRange.start && range.byteEnd == definition.sourceRange.end;
}

void insertMissingDefinitions(MarkdownNode& root, const QVector<DefinitionParseResult>& definitions, const LineStartOffsetCache& lineOffsets) {
  if (root.type() != BlockType::Document || definitions.isEmpty()) {
    return;
  }
  // definitions are scanned top-to-bottom, so markerRange.start is non-decreasing. Index existing
  // top-level definition blocks once so the existence check is O(1) (was O(M) per definition), and
  // a monotonically advancing cursor makes the insert-position search O(N + M) overall instead of
  // re-scanning from the first child for every definition.
  QHash<qsizetype, BlockType> existingDefTypeByStart;
  for (const auto& child : root.children()) {
    const BlockType type = child->type();
    if ((type == BlockType::FootnoteDefinition || type == BlockType::LinkDefinition) &&
        child->sourceRange().byteStart >= 0) {
      existingDefTypeByStart.insert(child->sourceRange().byteStart, type);
    }
  }

  qsizetype cursor = 0;
  for (const DefinitionParseResult& parsedDefinition : definitions) {
    if (!shouldInsertSyntheticDefinition(parsedDefinition)) {
      continue;
    }
    const DefinitionBlock& definition = parsedDefinition.definition;
    const BlockType expectedType =
        definition.kind == DefinitionBlock::Kind::Footnote ? BlockType::FootnoteDefinition : BlockType::LinkDefinition;
    const auto existing = existingDefTypeByStart.constFind(definition.markerRange.start);
    if (existing != existingDefTypeByStart.constEnd() && existing.value() == expectedType) {
      continue;
    }

    const auto& children = root.children();
    while (cursor < static_cast<qsizetype>(children.size()) &&
           children.at(static_cast<size_t>(cursor))->sourceRange().byteStart < definition.markerRange.start) {
      ++cursor;
    }
    const qsizetype insertAt = cursor;
    if (insertAt < static_cast<qsizetype>(children.size()) &&
        (isDefinitionSourceParagraph(*children.at(static_cast<size_t>(insertAt)), definition) ||
         (isVirtualEmptyParagraph(*children.at(static_cast<size_t>(insertAt))) &&
          children.at(static_cast<size_t>(insertAt))->sourceRange().byteStart == definition.markerRange.start))) {
      root.detachChild(insertAt);
    }
    root.insertChild(insertAt, createDefinitionNode(definition, lineOffsets));
    cursor = insertAt + 1;
  }
}

void insertTrailingEmptyParagraphAfterDefinition(
    QStringView markdown,
    MarkdownNode& root,
    const QVector<DefinitionParseResult>& definitions,
    const LineStartOffsetCache& lineOffsets) {
  if (root.type() != BlockType::Document || definitions.isEmpty()) {
    return;
  }

  const DefinitionBlock* trailingDefinition = nullptr;
  for (const DefinitionParseResult& parsedDefinition : definitions) {
    const DefinitionBlock& definition = parsedDefinition.definition;
    if (!definition.sourceRange.isValid()) {
      continue;
    }
    qsizetype cursor = definition.sourceRange.end;
    while (cursor < markdown.size() && markdown.at(cursor) == QLatin1Char('\r')) {
      ++cursor;
    }
    int newlineCount = 0;
    while (cursor < markdown.size() && markdown.at(cursor) == QLatin1Char('\n')) {
      ++newlineCount;
      ++cursor;
      if (cursor < markdown.size() && markdown.at(cursor) == QLatin1Char('\r')) {
        ++cursor;
      }
    }
    if (cursor == markdown.size() && newlineCount >= 2) {
      trailingDefinition = &definition;
    }
  }
  if (!trailingDefinition) {
    return;
  }

  for (const auto& child : root.children()) {
    if (isVirtualEmptyParagraph(*child) && child->sourceRange().byteStart >= trailingDefinition->sourceRange.end) {
      return;
    }
  }

  const int emptyLine = lineOffsets.lineForOffset(trailingDefinition->sourceRange.end) + 2;
  auto paragraph = std::make_unique<MarkdownNode>(BlockType::Paragraph);
  paragraph->inlines().push_back(InlineNode::text(QString()));

  SourceRange range;
  range.lineStart = emptyLine;
  range.lineEnd = emptyLine;
  range.columnStart = 1;
  range.columnEnd = 1;
  range.byteStart = markdown.size();
  range.byteEnd = markdown.size();
  paragraph->setSourceRange(range);
  root.appendChild(std::move(paragraph));
}

struct TableCellFieldRange {
  qsizetype start = -1;
  qsizetype end = -1;
};

struct FrontMatterScanResult {
  bool found = false;
  FrontMatterFormat format = FrontMatterFormat::None;
  QString literal;
  qsizetype sourceEnd = 0;
  int lineEnd = 0;
};

bool isHorizontalPadding(QChar ch) {
  return ch == QLatin1Char(' ') || ch == QLatin1Char('\t');
}

// True if a line has no non-whitespace character — equivalent to QString::trimmed().isEmpty() but
// over a zero-allocation QStringView, for the line-indexed VEP passes.
bool isBlankLine(QStringView line) {
  for (QChar ch : line) {
    if (!ch.isSpace()) {
      return false;
    }
  }
  return true;
}

qsizetype skipBom(QStringView text) {
  return !text.isEmpty() && text.front() == QChar(0xFEFF) ? 1 : 0;
}

qsizetype lineEndOffset(QStringView text, qsizetype lineStart) {
  const qsizetype newline = text.indexOf(QLatin1Char('\n'), lineStart);
  return newline < 0 ? text.size() : newline;
}

qsizetype nextLineStart(QStringView text, qsizetype lineStart) {
  const qsizetype end = lineEndOffset(text, lineStart);
  return end < text.size() ? end + 1 : text.size();
}

QStringView trimmedLine(QStringView text, qsizetype start, qsizetype end) {
  if (end > start && text.at(end - 1) == QLatin1Char('\r')) {
    --end;
  }
  while (start < end && isHorizontalPadding(text.at(start))) {
    ++start;
  }
  while (end > start && isHorizontalPadding(text.at(end - 1))) {
    --end;
  }
  return text.mid(start, end - start);
}

bool lineEquals(QStringView text, qsizetype start, QStringView expected) {
  return trimmedLine(text, start, lineEndOffset(text, start)) == expected;
}

FrontMatterScanResult scanFencedFrontMatter(QStringView markdown, QStringView fence, FrontMatterFormat format) {
  const qsizetype start = skipBom(markdown);
  if (!lineEquals(markdown, start, fence)) {
    return {};
  }

  qsizetype contentStart = nextLineStart(markdown, start);
  qsizetype lineStart = contentStart;
  int lineNumber = 2;
  while (lineStart < markdown.size()) {
    const qsizetype end = lineEndOffset(markdown, lineStart);
    if (trimmedLine(markdown, lineStart, end) == fence) {
      FrontMatterScanResult result;
      result.found = true;
      result.format = format;
      result.literal = markdown.mid(contentStart, qMax<qsizetype>(0, lineStart - contentStart)).toString();
      if (result.literal.endsWith(QLatin1Char('\n'))) {
        result.literal.chop(1);
      }
      if (result.literal.endsWith(QLatin1Char('\r'))) {
        result.literal.chop(1);
      }
      result.sourceEnd = end;
      result.lineEnd = lineNumber;
      return result;
    }
    lineStart = nextLineStart(markdown, lineStart);
    ++lineNumber;
  }
  return {};
}

qsizetype scanJsonObjectEnd(QStringView markdown, qsizetype start) {
  if (start >= markdown.size() || markdown.at(start) != QLatin1Char('{')) {
    return -1;
  }

  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (qsizetype i = start; i < markdown.size(); ++i) {
    const QChar ch = markdown.at(i);
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (ch == QLatin1Char('\\')) {
        escaped = true;
      } else if (ch == QLatin1Char('"')) {
        inString = false;
      }
      continue;
    }

    if (ch == QLatin1Char('"')) {
      inString = true;
    } else if (ch == QLatin1Char('{')) {
      ++depth;
    } else if (ch == QLatin1Char('}')) {
      --depth;
      if (depth == 0) {
        return i + 1;
      }
      if (depth < 0) {
        return -1;
      }
    }
  }
  return -1;
}

FrontMatterScanResult scanJsonFrontMatter(QStringView markdown) {
  const qsizetype start = skipBom(markdown);
  if (start >= markdown.size() || markdown.at(start) != QLatin1Char('{')) {
    return {};
  }

  const qsizetype end = scanJsonObjectEnd(markdown, start);
  if (end <= start) {
    return {};
  }
  if (end < markdown.size() && markdown.at(end) != QLatin1Char('\n') && markdown.at(end) != QLatin1Char('\r')) {
    return {};
  }

  const QString literal = markdown.mid(start, end - start).toString();
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(literal.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {};
  }

  FrontMatterScanResult result;
  result.found = true;
  result.format = FrontMatterFormat::Json;
  result.literal = literal;
  result.sourceEnd = end;
  result.lineEnd = 1;
  for (QChar ch : literal) {
    if (ch == QLatin1Char('\n')) {
      ++result.lineEnd;
    }
  }
  return result;
}

FrontMatterScanResult scanFrontMatter(QStringView markdown) {
  if (FrontMatterScanResult result = scanFencedFrontMatter(markdown, QStringLiteral("---"), FrontMatterFormat::Yaml); result.found) {
    return result;
  }
  if (FrontMatterScanResult result = scanFencedFrontMatter(markdown, QStringLiteral("+++"), FrontMatterFormat::Toml); result.found) {
    return result;
  }
  return scanJsonFrontMatter(markdown);
}

using muffin::shiftInlineSourcePositions;

// cmark-gfm decodes an escaped pipe `\|` to `|` inside a table cell (see
// unescape_pipes in extensions/table.c) but reports the inline node's source range as
// if the backslash were not there — one source char too short per `\|`, and a leading
// `\|` starts the range after the backslash. An inline node's serialized text therefore
// carries the decoded `|`, while the cell source keeps it escaped as `\|`, so a plain
// QString::indexOf of the serialized text misses the span entirely and the node is left
// with cmark's mis-reported range. Match the decoded needle against the escaped source
// instead: treat a `\|` pair in the source as a single decoded `|`. Returns the start
// offset and the source offset where the needle is fully consumed; `end` may exceed
// `start + needle.size()` because each `\|` consumes two source chars. On no match
// returns {-1, -1}.
struct EscapeAwareMatch {
  qsizetype start = -1;
  qsizetype end = -1;
};
EscapeAwareMatch escapeAwareFind(QStringView content, QStringView needle, qsizetype from) {
  if (needle.isEmpty()) {
    return {from, from};
  }
  for (qsizetype s = from; s < content.size(); ++s) {
    qsizetype ci = s;
    qsizetype ni = 0;
    while (ni < needle.size() && ci < content.size()) {
      const QChar c = content.at(ci);
      if (c == QLatin1Char('\\') && ci + 1 < content.size() && content.at(ci + 1) == QLatin1Char('|')) {
        if (needle.at(ni) != QLatin1Char('|')) {
          break;
        }
        ci += 2;
        ++ni;
      } else if (c == needle.at(ni)) {
        ++ci;
        ++ni;
      } else {
        break;
      }
    }
    if (ni == needle.size()) {
      return {s, ci};
    }
  }
  return {};
}

void annotateTableCellInlineSourceRanges(QVector<InlineNode>& inlines, const QString& content, qsizetype sourceBase) {
  qsizetype searchFrom = 0;
  for (InlineNode& inlineNode : inlines) {
    const QString markdown = MarkdownSerializer().serializeInline(inlineNode);
    const EscapeAwareMatch match = escapeAwareFind(content, markdown, searchFrom);
    if (match.start < 0) {
      continue;
    }
    const qsizetype start = match.start;
    const qsizetype end = match.end;
    InlineSourceRanges ranges;
    ranges.source = {sourceBase + start, sourceBase + end};
    ranges.content = ranges.source;

    const QString marker = inlineNode.marker();
    if (!marker.isEmpty() && end - start >= marker.size() * 2 &&
        (inlineNode.type() == InlineType::Emphasis || inlineNode.type() == InlineType::Strong ||
         inlineNode.type() == InlineType::Strikethrough)) {
      ranges.openMarker = {sourceBase + start, sourceBase + start + marker.size()};
      ranges.closeMarker = {sourceBase + end - marker.size(), sourceBase + end};
      ranges.content = {ranges.openMarker.end, ranges.closeMarker.start};
      inlineNode.setSourceRanges(ranges);
      annotateTableCellInlineSourceRanges(inlineNode.children(), content, sourceBase);
    } else if (inlineNode.type() == InlineType::Code || inlineNode.type() == InlineType::InlineMath) {
      const QChar delim = (inlineNode.type() == InlineType::Code) ? QLatin1Char('`') : QLatin1Char('$');
      const qsizetype openLen = countLeading(content, start, end, delim);
      const qsizetype closeLen = countTrailing(content, start, end, delim);
      if (openLen > 0 && closeLen >= openLen) {
        ranges.openMarker = {sourceBase + start, sourceBase + start + openLen};
        ranges.closeMarker = {sourceBase + end - closeLen, sourceBase + end};
        ranges.content = {ranges.openMarker.end, ranges.closeMarker.start};
        inlineNode.setSourceRanges(ranges);
      }
    } else if (inlineNode.type() == InlineType::Link) {
      ranges.openMarker = {sourceBase + start, sourceBase + start + 1};
      const qsizetype labelEndInSlice = content.mid(start, end - start).indexOf(QLatin1Char(']'));
      const qsizetype labelEnd = labelEndInSlice >= 0 ? start + labelEndInSlice : qsizetype(-1);
      ranges.content = labelEnd >= 0 ? InlineRange{sourceBase + start + 1, sourceBase + labelEnd}
                                     : InlineRange{ranges.openMarker.end, ranges.openMarker.end};
      inlineNode.setSourceRanges(ranges);
      annotateTableCellInlineSourceRanges(inlineNode.children(), content, sourceBase);
    } else if (inlineNode.type() == InlineType::Image) {
      ranges.openMarker = {sourceBase + start, sourceBase + start + 2};
      const qsizetype labelEndInSlice = content.mid(start, end - start).indexOf(QLatin1Char(']'));
      const qsizetype labelEnd = labelEndInSlice >= 0 ? start + labelEndInSlice : qsizetype(-1);
      ranges.content = labelEnd >= 0 ? InlineRange{sourceBase + start + 2, sourceBase + labelEnd}
                                     : InlineRange{ranges.openMarker.end, ranges.openMarker.end};
      inlineNode.setSourceRanges(ranges);
    } else {
      inlineNode.setSourceRanges(ranges);
    }
    searchFrom = end;
  }
}

void shiftSourceRanges(MarkdownNode& node, qsizetype delta, int lineDelta) {
  SourceRange range = node.sourceRange();
  if (range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
    range.byteStart += delta;
    range.byteEnd += delta;
    if (range.lineStart > 0) {
      range.lineStart += lineDelta;
    }
    if (range.lineEnd > 0) {
      range.lineEnd += lineDelta;
    }
    node.setSourceRange(range);
  }
  DefinitionBlock definition = node.definition();
  if (definition.isValid()) {
    auto shiftField = [delta](DefinitionFieldRange& field) {
      if (field.isValid()) {
        field.start += delta;
        field.end += delta;
      }
    };
    shiftField(definition.labelRange);
    shiftField(definition.destinationRange);
    shiftField(definition.titleRange);
    shiftField(definition.noteRange);
    shiftField(definition.markerRange);
    shiftField(definition.sourceRange);
    node.setDefinition(definition);
  }
  shiftInlineSourcePositions(node.inlines(), delta);
  for (const auto& child : node.children()) {
    shiftSourceRanges(*child, delta, lineDelta);
  }
}

QVector<TableCellFieldRange> tableRowFieldRanges(QStringView rowText, qsizetype rowStartOffset) {
  QVector<qsizetype> separators;
  bool escaped = false;
  for (qsizetype i = 0; i < rowText.size(); ++i) {
    const QChar ch = rowText.at(i);
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == QLatin1Char('\\')) {
      escaped = true;
      continue;
    }
    if (ch == QLatin1Char('|')) {
      separators.push_back(rowStartOffset + i);
    }
  }

  QVector<TableCellFieldRange> ranges;
  if (separators.isEmpty()) {
    return ranges;
  }
  if (separators.first() != rowStartOffset) {
    separators.prepend(rowStartOffset - 1);
  }
  const qsizetype rowEndOffset = rowStartOffset + rowText.size();
  if (separators.last() != rowEndOffset - 1) {
    separators.push_back(rowEndOffset);
  }

  ranges.reserve(qMax(0, separators.size() - 1));
  for (qsizetype i = 0; i + 1 < separators.size(); ++i) {
    qsizetype start = separators.at(i) + 1;
    qsizetype end = separators.at(i + 1);
    while (start < end && isHorizontalPadding(rowText.at(start - rowStartOffset))) {
      ++start;
    }
    while (end > start && isHorizontalPadding(rowText.at(end - rowStartOffset - 1))) {
      --end;
    }
    ranges.push_back({start, end});
  }
  return ranges;
}

void annotateTableCellSourceRanges(QStringView markdown, const LineStartOffsetCache& lineOffsets, MarkdownNode& node) {
  if (node.type() == BlockType::TableRow) {
    const SourceRange rowRange = node.sourceRange();
    const qsizetype rowStart = lineOffsets.offsetForLineColumn(rowRange.lineStart, 1);
    const qsizetype rowEnd = lineOffsets.lineEndOffset(rowRange.lineStart);
    if (rowStart >= 0 && rowEnd >= rowStart) {
      const QVector<TableCellFieldRange> fields = tableRowFieldRanges(markdown.mid(rowStart, rowEnd - rowStart), rowStart);
      int column = 0;
      for (const auto& child : node.children()) {
        if (child->type() == BlockType::TableCell && column < fields.size()) {
          SourceRange range = child->sourceRange();
          range.lineStart = rowRange.lineStart;
          range.lineEnd = rowRange.lineStart;
          range.byteStart = fields.at(column).start;
          range.byteEnd = fields.at(column).end;
          range.columnStart = static_cast<int>(range.byteStart - rowStart + 1);
          range.columnEnd = static_cast<int>(range.byteEnd - rowStart);
          child->setSourceRange(range);
          annotateTableCellInlineSourceRanges(
              child->inlines(),
              markdown.mid(range.byteStart, range.byteEnd - range.byteStart).toString(),
              range.byteStart);
        }
        ++column;
      }
    }
  }

  for (const auto& child : node.children()) {
    annotateTableCellSourceRanges(markdown, lineOffsets, *child);
  }
}

}  // namespace

CmarkGfmParser::CmarkGfmParser() {
  ensureExtensionsRegistered();
}

ParseResult CmarkGfmParser::parseDocument(QStringView markdown, const ParseOptions& options) {
  QElapsedTimer timer;
  timer.start();

  FrontMatterScanResult frontMatter;
  {
    ParsePerfTimer t("parse.frontMatter");
    frontMatter = options.enableFrontMatter ? scanFrontMatter(markdown) : FrontMatterScanResult{};
  }
  qsizetype markdownStart = 0;
  std::unique_ptr<MarkdownNode> frontMatterNode;
  if (frontMatter.found) {
    markdownStart = frontMatter.sourceEnd;
    if (markdownStart < markdown.size() && markdown.at(markdownStart) == QLatin1Char('\r')) {
      ++markdownStart;
    }
    if (markdownStart < markdown.size() && markdown.at(markdownStart) == QLatin1Char('\n')) {
      ++markdownStart;
    }

    frontMatterNode = std::make_unique<MarkdownNode>(BlockType::FrontMatter);
    frontMatterNode->setFrontMatterFormat(frontMatter.format);
    frontMatterNode->setLiteral(frontMatter.literal);
    SourceRange range;
    range.byteStart = 0;
    range.byteEnd = frontMatter.sourceEnd;
    range.lineStart = 1;
    range.lineEnd = frontMatter.lineEnd;
    range.columnStart = 1;
    range.columnEnd = 1;
    frontMatterNode->setSourceRange(range);
  }

  const QStringView markdownToParse = markdown.mid(markdownStart);
  // Build the UTF-8 buffer cmark parses. legacyMathDelimitersToDollar rewrites `\[`/`\]` display
  // math (byte-length-preserving) and remapUnicodePunctuation rewrites smart-punct Unicode; both
  // are skipped entirely when the document provably has nothing to rewrite. The whole point is that
  // the common case (no `\[`, remap off) is a single toUtf8 of the original view — no intermediate
  // 100MB QString copies from the old unconditional toString/split/join chain.
  const bool needsMathConvert = markdownToParse.indexOf(QStringLiteral("\\[")) >= 0;
  QString mathConverted;
  QString remapped;
  QStringView cmarkInput = markdownToParse;
  if (needsMathConvert) {
    ParsePerfTimer t("parse.mathConvert");
    mathConverted = legacyMathDelimitersToDollar(markdownToParse);
    cmarkInput = mathConverted;
  }
  if (options.enableUnicodeRemap) {
    ParsePerfTimer t("parse.unicodeRemap");
    remapped = remapUnicodePunctuation(cmarkInput);
    cmarkInput = remapped;
  }
  QByteArray utf8;
  {
    ParsePerfTimer t("parse.toUtf8");
    utf8 = cmarkInput.toUtf8();
  }
  // mathConverted / remapped only back `cmarkInput` for the toUtf8 above; the bytes now live in
  // `utf8` (cmark_parser_feed copies them again). Release the ~1x-document QString copies now
  // rather than at function return — a big slice of the open-time peak on files that trigger
  // math-convert (`\[`) or unicode remap. cmarkInput is unused past this point.
  mathConverted = {};
  remapped = {};
  cmarkInput = {};
  QVector<DefinitionParseResult> definitions;
  {
    ParsePerfTimer t("parse.scanDefinitions");
    definitions = scanDefinitionBlocks(markdownToParse);
  }
  cmark_parser* parser = nullptr;
  cmark_node* document = nullptr;
  {
    ParsePerfTimer t("parse.cmark");
    parser = cmark_parser_new(CMARK_OPT_DEFAULT | CMARK_OPT_FOOTNOTES);
    attachExtensions(parser, options);
    cmark_parser_feed(parser, utf8.constData(), static_cast<size_t>(utf8.size()));
    document = cmark_parser_finish(parser);
  }
  // cmark dups every literal (cmark_chunk_dup) into its own arena during feed/finish, so `utf8` is
  // never referenced again — not by convertBlock (it walks the cmark tree) nor by any later pass.
  // Drop it now instead of carrying ~1x the document through the whole parse tail. On a large file
  // this buffer surviving until function return is a big slice of the open-time memory peak.
  utf8 = {};

  const LineStartOffsetCache lineOffsets = [markdownToParse] {
    ParsePerfTimer t("parse.lineOffsets");
    return LineStartOffsetCache(markdownToParse);
  }();
  CmarkNodeAdapter adapter(&lineOffsets, markdownToParse);
  ParseResult result;
  {
    ParsePerfTimer t("parse.convertBlock");
    const bool perf = parsePerf().isDebugEnabled();
    CmarkNodeAdapter::setPerfEnabled(perf);
    CmarkNodeAdapter::resetPerfCounters();
    LineStartOffsetCache::setByteColPerfEnabled(perf);
    LineStartOffsetCache::resetByteColPerf();
    result.root = adapter.convertBlock(document);
    CmarkNodeAdapter::setPerfEnabled(false);
    LineStartOffsetCache::setByteColPerfEnabled(false);
    CmarkNodeAdapter::dumpConvertBreakdown();
    if (perf) {
      qCDebug(parsePerf).nospace() << "lineOffset.byteColumn " << LineStartOffsetCache::byteColPerfMs() << " ms";
    }
  }
  // The Muffin tree (result.root) is fully built; every remaining pass reads only it +
  // markdownToParse + lineOffsets — never the cmark tree. Free document + parser here instead of
  // carrying them through ~15 annotation passes (previously freed at function return). On a large
  // file this drops roughly the document's worth of UTF-8 again from the peak window, exactly when
  // the Muffin tree (which re-holds the same text as UTF-16) is at its largest.
  cmark_node_free(document);
  cmark_parser_free(parser);
  document = nullptr;
  parser = nullptr;
  {
    ParsePerfTimer t("parse.insertVirtualEmptyParagraphs");
    insertVirtualEmptyParagraphs(markdownToParse, *result.root, lineOffsets);
  }
  {
    ParsePerfTimer t("parse.annotateSourceOffsets");
    annotateSourceOffsets(lineOffsets, markdownToParse, *result.root);
  }
  {
    ParsePerfTimer t("parse.annotateMathDelimiters");
    annotateMathDelimiters(markdownToParse, *result.root);
  }
  if (options.enableAlertBox) {
    ParsePerfTimer t("parse.annotateAlertKinds");
    annotateAlertKinds(*result.root);
  }
  if (options.enableHighlight) {
    ParsePerfTimer t("parse.splitHighlight");
    splitDelimInlines(*result.root, markdownToParse, specForHighlight());
  }
  // cmark's strikethrough extension is never attached (see attachExtensions), so `~~` is always
  // handled here (run 2). It MUST run before the subscript pass (run 1) so a `~~` pair is consumed
  // first and only single `~` is left for subscript.
  if (options.enableStrikethrough) {
    ParsePerfTimer t("parse.splitStrikethrough");
    splitDelimInlines(*result.root, markdownToParse, specForStrikethrough());
  }
  if (options.enableSubscript) {
    ParsePerfTimer t("parse.splitSubscript");
    splitDelimInlines(*result.root, markdownToParse, specForSubscript());
  }
  if (options.enableSuperscript) {
    ParsePerfTimer t("parse.splitSuperscript");
    splitDelimInlines(*result.root, markdownToParse, specForSuperscript());
  }
  {
    ParsePerfTimer t("parse.insertVEPInBlockQuotes");
    insertVirtualEmptyParagraphsInBlockQuotes(markdownToParse, *result.root, lineOffsets);
  }
  {
    ParsePerfTimer t("parse.insertMissingDefinitions");
    insertMissingDefinitions(*result.root, definitions, lineOffsets);
  }
  {
    ParsePerfTimer t("parse.annotateDefinitionBlocks");
    annotateDefinitionBlocks(*result.root, definitions, lineOffsets);
  }
  {
    ParsePerfTimer t("parse.insertTrailingVEPAfterDefinition");
    insertTrailingEmptyParagraphAfterDefinition(markdownToParse, *result.root, definitions, lineOffsets);
  }
  {
    ParsePerfTimer t("parse.annotateTableCellRanges");
    annotateTableCellSourceRanges(markdownToParse, lineOffsets, *result.root);
  }
  // cmark turns a lone `*`/`-`/`+`/`1.` (trailing newline satisfies its bullet check) into an empty
  // list item the editor can't edit (its list-marker validation requires a space). Demote those to
  // paragraphs at parse time so load and edit paths agree. Done last, on the pre-front-matter tree.
  {
    ParsePerfTimer t("parse.demoteListMarkers");
    demotePendingListMarkers(*result.root, markdownToParse);
  }
  // cmark emits a childless BlockQuote for a `>`/`> ` line that has no following content (a
  // blockquote the user is still typing). A blockquote is only editable through its child paragraph,
  // so an empty one leaves the caret nowhere to land — typing `>` would drop the caret and reject
  // the next keystroke. Fold empty blockquotes back into a Paragraph holding the marker, mirroring
  // the lone-list demotion above; once real content follows (`> text`) cmark emits the child and the
  // quote materialises normally.
  {
    ParsePerfTimer t("parse.demoteEmptyBlockQuotes");
    demoteEmptyBlockQuotes(*result.root, markdownToParse);
  }

  if (frontMatterNode) {
    const int lineDelta = frontMatter.lineEnd;
    for (const auto& child : result.root->children()) {
      shiftSourceRanges(*child, markdownStart, lineDelta);
    }
    result.root->insertChild(0, std::move(frontMatterNode));
  }

  result.elapsedMs = timer.elapsed();
  // cmark document + parser freed right after convertBlock above; nothing past that point touches
  // the cmark tree.
  return result;
}

ParseResult CmarkGfmParser::parseBlock(QStringView markdown, BlockType, const ParseOptions& options) {
  return parseDocument(markdown, options);
}

void CmarkGfmParser::ensureExtensionsRegistered() {
  cmark_gfm_core_extensions_ensure_registered();
}

void CmarkGfmParser::attachExtensions(cmark_parser* parser, const ParseOptions& options) {
  const auto attach = [parser](const char* name) {
    if (cmark_syntax_extension* extension = cmark_find_syntax_extension(name)) {
      cmark_parser_attach_syntax_extension(parser, extension);
    }
  };

  if (options.enableTable) attach("table");
  // cmark-gfm's strikethrough extension matches a SINGLE `~` as well as `~~`. Attaching it would turn
  // `H~2~O` (subscript off) into a strikethrough, which is surprising and inconsistent with the
  // subscript-on case where `~` means subscript. We always own tilde runs ourselves in
  // splitDelimInlines: `~~` -> strikethrough (run 2), single `~` -> subscript when enabled else
  // literal text. So cmark's extension is never attached, and `~` semantics never depend on the
  // subscript toggle.
  // if (options.enableStrikethrough) attach("strikethrough");  // intentionally disabled — see above
  if (options.enableAutolink) attach("autolink");
  if (options.enableTaskList) attach("tasklist");
  if (options.enableMath) attach("math");
}

void CmarkGfmParser::insertVirtualEmptyParagraphs(QStringView markdown, MarkdownNode& root, const LineStartOffsetCache& lineOffsets) const {
  if (root.type() != BlockType::Document) {
    return;
  }

  // Index lines through lineOffsets (a view over markdown) instead of markdown.toString() +
  // split('\n'), which on a large document allocates a full copy plus one QString per line. The
  // blank-line predicate is isBlankLine(lineText(...)) — identical to the old lines.at(i).trimmed().
  const int totalLines = lineOffsets.lineCount();
  qsizetype childIndex = 0;
  int previousEndLine = 0;

  if (root.children().empty()) {
    if (markdown.isEmpty()) {
      root.appendChild(createVirtualEmptyParagraph(1));
      return;
    }
    const int emptyCount = totalLines / 2;
    for (int i = 0; i < emptyCount; ++i) {
      const int emptyLine = 1 + i * 2;
      if (emptyLine >= 1 && emptyLine <= totalLines && isBlankLine(lineOffsets.lineText(markdown, emptyLine))) {
        root.appendChild(createVirtualEmptyParagraph(emptyLine));
      }
    }
    return;
  }

  while (childIndex < root.children().size()) {
    MarkdownNode* child = root.children().at(static_cast<size_t>(childIndex)).get();
    const SourceRange range = child->sourceRange();
    const int startLine = range.lineStart;
    // Count ACTUAL blank lines in the gap [previousEndLine+1, startLine) rather than trusting
    // startLine-previousEndLine-1. cmark-gfm under-reports the end line of some block types
    // (a display-math block excludes its closing $$/\] line; an HTML block can exclude its closing
    // tag line), so the apparent gap can include a non-blank closer line. Counting real blank lines
    // yields the correct virtual-empty-paragraph count regardless — otherwise every display-math /
    // HTML block followed by a single blank line gained a spurious clickable empty paragraph.
    int blankLines = 0;
    if (startLine > 0) {
      for (int line = previousEndLine + 1; line < startLine; ++line) {
        if (line >= 1 && line <= totalLines && isBlankLine(lineOffsets.lineText(markdown, line))) {
          ++blankLines;
        }
      }
    }
    const int emptyCount = qMax(0, blankLines / 2);
    const int firstEmptyLine = previousEndLine == 0 ? 1 : previousEndLine + 2;
    for (int i = 0; i < emptyCount; ++i) {
      const int emptyLine = firstEmptyLine + i * 2;
      if (emptyLine >= 1 && emptyLine <= totalLines && isBlankLine(lineOffsets.lineText(markdown, emptyLine))) {
        root.insertChild(childIndex, createVirtualEmptyParagraph(emptyLine));
        ++childIndex;
      }
    }

    previousEndLine = qMax(previousEndLine, range.lineEnd);
    ++childIndex;
  }

  // Container blocks (Lists, BlockQuotes) absorb trailing blank lines into
  // their lineEnd, so previousEndLine may overestimate where the content ends.
  // Use the actual last non-blank line for trailing blank-line counting.
  int lastContentLine = 0;
  for (int i = totalLines; i >= 1; --i) {
    if (!isBlankLine(lineOffsets.lineText(markdown, i))) {
      lastContentLine = i;  // 1-indexed
      break;
    }
  }
  const int trailingLines = totalLines - lastContentLine;
  const int trailingEmptyCount = qMax(0, trailingLines / 2);
  for (int i = 0; i < trailingEmptyCount; ++i) {
    const int emptyLine = lastContentLine + 2 + i * 2;
    if (emptyLine >= 1 && emptyLine <= totalLines && isBlankLine(lineOffsets.lineText(markdown, emptyLine))) {
      root.appendChild(createVirtualEmptyParagraph(emptyLine));
    }
  }
}

void CmarkGfmParser::insertVirtualEmptyParagraphsInBlockQuotes(QStringView markdown, MarkdownNode& root, const LineStartOffsetCache& lineOffsets) const {
  // Line content and start offsets come from lineOffsets (a view over markdown) instead of
  // markdown.toString() + split('\n'); the old lineStartOffset lambda was also O(line) per call,
  // now O(1) via lineStarts_.
  const auto quoteContentOffset = [](QStringView line, int depth) -> int {
    int index = 0;
    for (int currentDepth = 0; currentDepth < depth; ++currentDepth) {
      while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
        ++index;
      }
      if (index >= line.size() || line.at(index) != QLatin1Char('>')) {
        return -1;
      }
      ++index;
      if (index < line.size() && line.at(index) == QLatin1Char(' ')) {
        ++index;
      }
    }
    return index;
  };

  const auto isEmptyQuoteLine = [&](int line, int depth, int& contentColumn, qsizetype& contentOffset) {
    const QStringView sourceLine = lineOffsets.lineText(markdown, line);
    if (sourceLine.isNull()) {
      return false;  // out of range
    }
    const int contentIndex = quoteContentOffset(sourceLine, depth);
    if (contentIndex < 0 || !sourceLine.mid(contentIndex).trimmed().isEmpty()) {
      return false;
    }
    const qsizetype startOffset = lineOffsets.lineStartOffset(line);
    if (startOffset < 0) {
      return false;
    }
    contentColumn = contentIndex + 1;
    contentOffset = startOffset + contentIndex;
    return true;
  };

  const auto quoteDepth = [](const MarkdownNode& node) {
    int depth = 0;
    for (const MarkdownNode* current = &node; current; current = current->parent()) {
      if (current->type() == BlockType::BlockQuote) {
        ++depth;
      }
    }
    return depth;
  };

  const auto visit = [&](const auto& self, MarkdownNode& node) -> void {
    for (const auto& child : node.children()) {
      self(self, *child);
    }
    if (node.type() != BlockType::BlockQuote || node.children().empty()) {
      return;
    }

    const int depth = quoteDepth(node);
    qsizetype childIndex = 0;
    int previousEndLine = node.sourceRange().lineStart - 1;
    while (childIndex < static_cast<qsizetype>(node.children().size())) {
      MarkdownNode* child = node.children().at(static_cast<size_t>(childIndex)).get();
      const SourceRange range = child->sourceRange();
      const int startLine = range.lineStart;
      const int blankLines = startLine > 0 ? startLine - previousEndLine - 1 : 0;
      const int emptyCount = qMax(0, blankLines / 2);
      const int firstEmptyLine = previousEndLine + 2;
      for (int i = 0; i < emptyCount; ++i) {
        const int emptyLine = firstEmptyLine + i * 2;
        int contentColumn = 1;
        qsizetype contentOffset = -1;
        if (isEmptyQuoteLine(emptyLine, depth, contentColumn, contentOffset)) {
          node.insertChild(childIndex, createVirtualEmptyParagraph(emptyLine, contentColumn, contentOffset));
          ++childIndex;
        }
      }

      previousEndLine = qMax(previousEndLine, range.lineEnd);
      ++childIndex;
    }

    const int trailingLines = node.sourceRange().lineEnd - previousEndLine;
    const int trailingEmptyCount = qMax(0, trailingLines / 2);
    for (int i = 0; i < trailingEmptyCount; ++i) {
      const int emptyLine = previousEndLine + 2 + i * 2;
      int contentColumn = 1;
      qsizetype contentOffset = -1;
      if (isEmptyQuoteLine(emptyLine, depth, contentColumn, contentOffset)) {
        node.appendChild(createVirtualEmptyParagraph(emptyLine, contentColumn, contentOffset));
      }
    }
  };

  visit(visit, root);
}

std::unique_ptr<MarkdownNode> CmarkGfmParser::createVirtualEmptyParagraph(int line) const {
  return createVirtualEmptyParagraph(line, 1, 0);
}

std::unique_ptr<MarkdownNode> CmarkGfmParser::createVirtualEmptyParagraph(int line, int column, qsizetype sourceOffset) const {
  auto paragraph = std::make_unique<MarkdownNode>(BlockType::Paragraph);
  paragraph->inlines().push_back(InlineNode::text(QString()));

  SourceRange range;
  range.lineStart = line;
  range.lineEnd = line;
  range.columnStart = column;
  range.columnEnd = column;
  range.byteStart = sourceOffset;
  range.byteEnd = sourceOffset;
  paragraph->setSourceRange(range);
  return paragraph;
}

}  // namespace muffin
