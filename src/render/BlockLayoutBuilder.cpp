#include "render/BlockLayoutBuilder.h"
#include "render/RenderMetrics.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "blocks/html/HtmlSanitizer.h"
#include "document/BlockPredicates.h"
#include "document/PendingBlockMarker.h"
#include "document/SourceRangeUtil.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "projection/InlineProjection.h"
#include "spellcheck/SpellChecker.h"
#include "theme/CssContent.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QLoggingCategory>
#include <QSettings>
#include <QStringList>
#include <QStringView>
#include <QTextLayout>
#include <QTextOption>

#include <functional>
#include <utility>

namespace muffin {
using namespace muffin::mermaid::editor;
namespace {

Q_LOGGING_CATEGORY(blockBuildPerf, "muffin.perf", QtWarningMsg)

bool isOrderedListStyle(const QString& style) {
  static const QSet<QString> ordered = {
      QStringLiteral("decimal"), QStringLiteral("decimal-leading-zero"),
      QStringLiteral("lower-alpha"), QStringLiteral("upper-alpha"),
      QStringLiteral("lower-latin"), QStringLiteral("upper-latin"),
      QStringLiteral("lower-roman"), QStringLiteral("upper-roman"),
      QStringLiteral("lower-greek")};
  return ordered.contains(style);
}

bool isInsideBlockquote(const MarkdownNode& node) {
  for (const MarkdownNode* p = node.parent(); p; p = p->parent()) {
    if (p->type() == BlockType::BlockQuote) { return true; }
  }
  return false;
}

bool hasHeadingAfterDecoration(const RenderTheme& theme, int level) {
  const QString host = QStringLiteral("h%1").arg(level);
  for (const PseudoElementRule& rule : theme.decorations().pseudos) {
    if (rule.host == host && rule.pseudo == QStringLiteral("after") &&
        (rule.background.kind != GradientSpec::Kind::None || rule.backgroundColor.isValid() ||
         (rule.borderBottomColor.isValid() && rule.borderBottomWidth > 0.0) || !rule.svgData.isEmpty())) {
      return true;
    }
  }
  return false;
}

qreal measuredHeadingBeforeAdvance(const RenderTheme& theme, int level,
                                   const QString& resolvedText, const QFont& font) {
  const qreal fallback = theme.headingBeforeAdvance(level);
  if (resolvedText.isEmpty()) return fallback;

  qreal marginRight = 0.0;
  const QString host = QStringLiteral("h%1").arg(level);
  for (const PseudoElementRule& rule : theme.decorations().pseudos) {
    if (rule.host == host && rule.pseudo == QStringLiteral("before")) {
      marginRight = rule.marginRight;
      break;
    }
  }
  // Counter text is known at layout time, so use its exact inline advance.
  // Keeping the mapper's 1em fallback here leaves spare space for short values
  // such as "1" and right-aligns them away from the shared heading edge.
  return QFontMetricsF(font).horizontalAdvance(resolvedText) + marginRight;
}

// `fast` skips the per-node structural CSS cascade (mirrors spacingBetweenBlocks' fast path): the
// estimate path passes fast=true to resolve load-time PROTOTYPE margins (nullptr node → elementStyle,
// O(1)) instead of elementStyleForNode (O(sibling chain) on github). estimateContainer/estimateListItem
// call these once per child, so the cascade was ~8s of the dense-file estimate.
QMarginsF blockMarginForFlow(const MarkdownNode& node, const RenderTheme& theme, bool fast = false) {
  if (!fast && node.type() == BlockType::Paragraph && isInsideBlockquote(node)) {
    const ThemeElementBoxStyle quoteParagraph = theme.elementBoxStyle(QStringLiteral("blockquote p"), &node);
    if (quoteParagraph.present && !quoteParagraph.margin.isNull()) { return quoteParagraph.margin; }
  }
  return theme.blockMargin(node.type(), node.headingLevel(), fast ? nullptr : &node);
}

bool hasCssFlowMargin(const MarkdownNode& node, const RenderTheme& theme, bool fast = false) {
  return !blockMarginForFlow(node, theme, fast).isNull();
}

qreal spacingBeforeInFlow(const MarkdownNode& node, const RenderTheme& theme, bool fast = false) {
  const QMarginsF margin = blockMarginForFlow(node, theme, fast);
  return !margin.isNull() ? margin.top() : 0.0;
}

qreal spacingAfterInFlow(const MarkdownNode& node, const RenderTheme& theme, bool fast = false) {
  const QMarginsF margin = blockMarginForFlow(node, theme, fast);
  return !margin.isNull() ? margin.bottom() : theme.blockSpacing();
}

bool isTightListItemSiblingPair(const MarkdownNode& prev, const MarkdownNode& next) {
  const MarkdownNode* parent = prev.parent();
  return parent && parent == next.parent() && parent->type() == BlockType::List && parent->listTight() &&
         prev.type() == BlockType::ListItem && next.type() == BlockType::ListItem;
}

bool isTightListNestedChildPair(const MarkdownNode& prev, const MarkdownNode& next) {
  if (prev.type() != BlockType::Paragraph || next.type() != BlockType::List) { return false; }
  const MarkdownNode* item = next.parent();
  const MarkdownNode* list = item ? item->parent() : nullptr;
  return item && item == prev.parent() && item->type() == BlockType::ListItem && list && list->type() == BlockType::List && list->listTight();
}

qreal spacingBetweenInFlow(const MarkdownNode& prev, const MarkdownNode& next, const RenderTheme& theme, bool fast = false) {
  if (isTightListItemSiblingPair(prev, next) || isTightListNestedChildPair(prev, next)) { return 0.0; }
  const qreal after = spacingAfterInFlow(prev, theme, fast);
  const qreal before = spacingBeforeInFlow(next, theme, fast);
  return (hasCssFlowMargin(prev, theme, fast) || hasCssFlowMargin(next, theme, fast)) ? qMax(after, before) : after + before;
}

bool isVirtualEmptyParagraphNode(const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  return node.type() == BlockType::Paragraph && range.byteStart >= 0 && range.byteEnd == range.byteStart;
}

bool omitVirtualEmptyParagraphInRenderFlow(const MarkdownNode& node, const SelectionRange& selection) {
  if (!isVirtualEmptyParagraphNode(node) || !isInsideBlockquote(node)) {
    return false;
  }
  // Render (a) the quote's TRAILING VEP — Typora shows trailing blank lines, and it's
  // where the caret lands after Enter at the end of the last quote line — and (b) the
  // VEP the caret is currently ON, i.e. the new empty line Enter creates BETWEEN other
  // blocks (e.g. an outer-quote paragraph and a nested quote). Without (b), pressing
  // Enter mid-quote changed the source but the view didn't change at all: the caret's
  // VEP was omitted, so it had no BlockLayout (the caret vanished) and added no height
  // (no new line appeared). Other (inter-paragraph separator) VEPs stay omitted — they
  // are already expressed as paragraph spacing, and rendering them would double-space
  // the quote.
  const MarkdownNode* parent = node.parent();
  const bool trailing = parent != nullptr && !parent->children().empty() && parent->children().back().get() == &node;
  const auto caretOnVep = [&](const CursorPosition& p) {
    return p.text.nodeId == node.id() ||
           (p.text.sourceOffset >= 0 && node.sourceRange().byteStart == p.text.sourceOffset);
  };
  if (trailing || caretOnVep(selection.focus) || caretOnVep(selection.anchor)) {
    return false;
  }
  return true;
}

// Width to right-align 1..lineCount in a code-fence gutter, measured with the zoom-aware code font
// (so the gutter scales with zoom). (digits + 1) glyph widths: the digits plus a half-glyph gap on
// each side, matching how paintCodeLineNumbers right-aligns numbers just left of the code text.
qreal codeLineNumberGutterWidth(const QString& literal, const RenderTheme& theme) {
  const QFontMetricsF metrics(theme.codeFont());
  const int lineCount = literal.isEmpty() ? 1 : int(literal.count(QLatin1Char('\n'))) + 1;
  int digits = 1;
  for (int n = lineCount; n >= 10; n /= 10) {
    ++digits;
  }
  const qreal digitWidth = qMax<qreal>(1.0, metrics.horizontalAdvance(QStringLiteral("8")));
  // Gutter = 1-char left padding + the digits + a 2-char gap to the code. paintCodeLineNumbers
  // right-aligns the number leaving that 2-char gap (numRightX = codeRect.left() - 2*digitWidth).
  return static_cast<qreal>(digits + 1 + 2) * digitWidth;
}

// markdown/convertOnRendering + the smart-quotes/dashes sub-toggles drive display-only SmartyPants
// conversion. Read at build time so a menu/preference toggle + refreshVisibleBlocks re-renders
// without a reparse. Mirrors how the input path (InputController) interprets the same keys.
SmartPunctRenderOptions smartPunctRenderOptions() {
  SmartPunctRenderOptions opts;
  const bool rendering = QSettings().value(QStringLiteral("markdown/convertOnRendering"), false).toBool();
  opts.convertQuotes = rendering && QSettings().value(QStringLiteral("markdown/smartQuotes"), false).toBool();
  opts.convertDashes = rendering && QSettings().value(QStringLiteral("markdown/smartDashes"), false).toBool();
  opts.convertEllipsis = opts.convertDashes;  // ellipsis rides on Smart Dashes, matching other editors
  opts.doubleQuoteStyle = QSettings().value(QStringLiteral("markdown/doubleQuoteStyle"), 0).toInt();
  opts.singleQuoteStyle = QSettings().value(QStringLiteral("markdown/singleQuoteStyle"), 0).toInt();
  return opts;
}

// Pixel width of the widest physical line in `literal` under `font`. Drives whether a code fence is
// horizontally scrollable (wrap off) and the scrollbar thumb ratio.
qreal maxLiteralLineWidth(const QString& literal, const QFont& font) {
  const QFontMetricsF metrics(font);
  qreal max = 1.0;
  qsizetype start = 0;
  while (start <= literal.size()) {
    const qsizetype nl = literal.indexOf(QLatin1Char('\n'), start);
    const qsizetype end = nl < 0 ? literal.size() : nl;
    max = qMax(max, metrics.horizontalAdvance(literal.mid(start, end - start)));
    if (nl < 0) {
      break;
    }
    start = nl + 1;
  }
  return max;
}

// Accumulates elapsed nanoseconds into a bucket when measurement is enabled; otherwise
// just two cheap branch checks. Aggregates per-block build() costs across one rebuild
// without emitting one log line per block.
class BuildAccumTimer {
public:
  BuildAccumTimer(qint64& bucket, bool enabled) : bucket_(bucket), enabled_(enabled) {
    if (enabled_) {
      timer_.start();
    }
  }
  ~BuildAccumTimer() {
    if (enabled_) {
      bucket_ += timer_.nsecsElapsed();
    }
  }

private:
  qint64& bucket_;
  bool enabled_;
  QElapsedTimer timer_;
};

// A parsed node's byte range is trustworthy when the parser adapter resolved
// it to real offsets. An empty table cell legitimately resolves to a zero-width
// range (byteStart == byteEnd) — its content is empty, not missing — so we must
// not treat zero-width as "unset". The only genuinely-unset range left by the
// adapter is the default (0, 0) marker; everything else with byteEnd >= byteStart
// and a non-zero anchor was computed from the source and should be used as-is.
// Using the fallback (line/column) for an empty cell instead swallows everything
// from the cell's column to the end of the row, rendering stray pipes and the
// following cell's text inside the empty cell.
bool hasResolvedByteRange(const SourceRange& range) {
  return range.byteEnd >= range.byteStart && (range.byteStart > 0 || range.byteEnd > 0);
}

// Predicate for the rendered-mode spell-check overlay. Returns a null std::function when
// spell checking is off, so InlineLayout skips the per-word scan entirely.
std::function<bool(QStringView)> spellMisspelledPredicate() {
  if (!SpellChecker::instance().isEnabled()) {
    return {};
  }
  return [](QStringView word) { return !SpellChecker::instance().isCorrect(word); };
}

qreal layoutTextHeight(const QString& text, const QFont& font, qreal lineHeight, qreal width) {
  QTextLayout layout(text.isEmpty() ? QStringLiteral(" ") : text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(option);
  layout.beginLayout();

  qreal height = 0;
  while (true) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(qMax<qreal>(1.0, width));
    line.setPosition(QPointF(0, height));
    height += qMax<qreal>(lineHeight, line.height());
  }
  layout.endLayout();
  return qMax(height, lineHeight);
}

qreal layoutLiteralHeight(const QString& text, const QFont& font, qreal lineHeight, qreal width, bool wrap = true) {
  const QStringList lines = text.isEmpty() ? QStringList{QString()} : text.split(QLatin1Char('\n'));
  if (!wrap) {
    // NoWrap: one visual line per physical line, regardless of width.
    return qMax<qreal>(static_cast<qreal>(lines.size()) * lineHeight, lineHeight);
  }
  qreal height = 0;
  for (const QString& line : lines) {
    height += layoutTextHeight(line.isEmpty() ? QStringLiteral(" ") : line, font, lineHeight, width);
  }
  return qMax(height, lineHeight);
}

QString displayLiteralFor(const MarkdownNode& node) {
  // Math literals are extracted clean (no wrapping newlines) at parse time and are edited verbatim
  // through the literal editor, so the layout literal must match the node literal 1:1 — otherwise a
  // trailing newline typed via Enter (e.g. "x\n") gets trimmed to "x", the source panel drops the
  // new line, and the caret (offset past the '\n') gets clamped back onto line 1.
  if (node.type() == BlockType::CodeFence || node.type() == BlockType::FrontMatter ||
      node.type() == BlockType::MathBlock) {
    return node.literal();
  }
  return node.literal().trimmed();
}

qsizetype pendingPrefixLengthFor(const MarkdownNode& node, const QString& source) {
  if (node.type() != BlockType::Paragraph) {
    return 0;
  }
  const PendingBlockMarker marker = detectPendingBlockMarker(QStringView(source));
  return marker.highlightPrefix ? marker.prefixLength : 0;
}

QString languageForFrontMatter(FrontMatterFormat format) {
  switch (format) {
    case FrontMatterFormat::Yaml:
      return QStringLiteral("yaml");
    case FrontMatterFormat::Toml:
      return QStringLiteral("toml");
    case FrontMatterFormat::Json:
      return QStringLiteral("json");
    case FrontMatterFormat::None:
    default:
      return {};
  }
}

bool selectionFocusesNode(const SelectionRange& selection, NodeId nodeId) {
  return nodeId.isValid() && selection.focus.blockId == nodeId && selection.focus.text.nodeId == nodeId;
}

// A `[TOC]` marker: a paragraph whose single inline is the literal text "[TOC]"
// (case-insensitive, surrounding whitespace tolerated). cmark parses it as an
// ordinary paragraph; this predicate is what lets the builder special-case it.
bool isTocMarkerParagraph(const MarkdownNode& node) {
  if (node.type() != BlockType::Paragraph || node.inlines().size() != 1) {
    return false;
  }
  const InlineNode& only = node.inlines().constFirst();
  return only.type() == InlineType::Text &&
         only.text().trimmed().compare(QStringLiteral("[TOC]"), Qt::CaseInsensitive) == 0;
}

// Templated so it reads the document text from either a QString or a PieceTable -- both expose
// isEmpty(). The builder passes its PieceTable view (md()).
template <typename Text>
bool isEmptyDocumentParagraph(const Text& markdown, const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  return markdown.isEmpty() && node.type() == BlockType::Paragraph && range.byteStart == 0 && range.byteEnd == 0;
}

QVector<qreal> tableColumnWidths(const MarkdownNode& table, const RenderTheme& theme, qreal width, bool breakOnSingleNewline) {
  int columnCount = 0;
  for (const auto& row : table.children()) {
    columnCount = qMax(columnCount, static_cast<int>(row->children().size()));
  }
  QVector<qreal> widths(columnCount, width / qMax(1, columnCount));
  if (columnCount == 0) {
    return widths;
  }

  const QMarginsF padding = theme.tableCellPadding();
  qreal preferredTotal = 0.0;
  for (int column = 0; column < columnCount; ++column) {
    qreal preferred = 0.0;
    for (const auto& row : table.children()) {
      if (column >= static_cast<int>(row->children().size())) {
        continue;
      }
      const MarkdownNode& cell = *row->children().at(static_cast<size_t>(column));
      const QFont font = row->tableRowIsHeader() ? theme.headingFont(6) : theme.paragraphFont();
      preferred = qMax(preferred, maxLiteralLineWidth(InlineProjection::plainTextForInlines(cell.inlines(), breakOnSingleNewline), font));
    }
    widths[column] = preferred + padding.left() + padding.right();
    preferredTotal += widths[column];
  }

  const qreal minimum = qMax<qreal>(56.0, width * 0.23);
  for (qreal& columnWidth : widths) {
    columnWidth = qMax(columnWidth, minimum);
  }

  preferredTotal = 0.0;
  for (qreal columnWidth : widths) {
    preferredTotal += columnWidth;
  }
  if (preferredTotal <= 0.0) {
    return QVector<qreal>(columnCount, width / columnCount);
  }
  if (preferredTotal < width) {
    widths[columnCount - 1] += width - preferredTotal;
  } else if (preferredTotal > width) {
    const qreal scale = width / preferredTotal;
    for (qreal& columnWidth : widths) {
      columnWidth *= scale;
    }
  }
  return widths;
}

}  // namespace

void BlockLayoutBuilder::setMarkdownText(const PieceTable& markdownText, const LineStartOffsetCache& lineOffsets) {
  markdownText_ = &markdownText;  // non-owning view; the document outlives the build
  lineOffsets_ = &lineOffsets;
}

const PieceTable& BlockLayoutBuilder::md() const {
  return *markdownText_;  // defaults to emptyText_; configureBuilder refreshes it before each build
}

void BlockLayoutBuilder::setSelection(SelectionRange selection) {
  selection_ = selection;
}

void BlockLayoutBuilder::setPreedit(QString text, QVector<QTextLayout::FormatRange> formats, int cursor) {
  preeditText_ = std::move(text);
  preeditFormats_ = std::move(formats);
  preeditCursor_ = cursor;
}

void BlockLayoutBuilder::applyPreedit(InlineLayout::BuildOptions& options) const {
  // cursorSourceOffset >= 0 identifies the caret block (and the caret's content-local source offset).
  // Source-offset (not visible-offset) is the splice anchor: a caret inside a revealed-syntax span
  // (e.g. a link's URL) has a granular source offset but a visible offset that collapses to the span
  // boundary, which would splice the preedit at the wrong place.
  if (preeditText_.isEmpty() || options.projectionState.cursorSourceOffset < 0) {
    return;
  }
  options.preeditText = preeditText_;
  options.preeditFormats = preeditFormats_;
  options.preeditCursor = preeditCursor_;
  options.preeditInsertAtSourceOffset = options.projectionState.cursorSourceOffset;
}

void BlockLayoutBuilder::setEditingHtmlBlock(NodeId id) {
  editingHtmlBlockId_ = id;
}

void BlockLayoutBuilder::setDocumentPath(QString path) {
  documentPath_ = std::move(path);
}

void BlockLayoutBuilder::setCodeFenceScroll(CodeFenceScrollController* controller) {
  codeFenceScroll_ = controller;
}

void BlockLayoutBuilder::setMermaidRenderCache(mermaid::editor::MermaidRenderCache* cache) {
  mermaidCache_ = cache;
}

void BlockLayoutBuilder::setMermaidSyncMode(bool sync) {
  mermaidSyncMode_ = sync;
}

void BlockLayoutBuilder::setHeadingCounterText(const QHash<NodeId, QString>* map) {
  headingCounterText_ = map;
}

void BlockLayoutBuilder::setTocEntries(const QVector<OutlineEntry>* entries) {
  tocEntries_ = entries;
}

BlockLayoutBuilder::BlockLayoutBuilder() : perfEnabled_(blockBuildPerf().isDebugEnabled()) {}

void BlockLayoutBuilder::refreshRenderSettings() {
  // One QSettings hit per setting per layout pass, not per block. See header note.
  QSettings s;
  breakOnSingleNewline_ = s.value(QStringLiteral("markdown/breakOnSingleNewline"), true).toBool();
  codeBlockWrap_ = s.value(QStringLiteral("markdown/codeBlockWrap"), true).toBool();
  showLineNumbers_ = s.value(QStringLiteral("markdown/showLineNumbers"), false).toBool();
  renderEmoji_ = s.value(QStringLiteral("markdown/renderEmoji"), true).toBool();
  renderDiagrams_ = s.value(QStringLiteral("markdown/diagrams"), true).toBool();
  showMermaidAsSource_ = s.value(QStringLiteral("editor/showMermaidAsSource"), false).toBool();
  // The estimate caches are keyed by elementKey|headingLevel only (no theme dimension), so a theme
  // switch would otherwise keep serving the previous theme's lineHeight/avgCharWidth. Clearing here
  // (once per layout pass) keeps them fresh: they re-populate within a single estimate pass — many
  // blocks share the same elementKey — but never leak across passes or theme changes.
  // fontMetricsCache_ is keyed by QFont::key() and already self-invalidates on a font change, so
  // it is left alone.
  lineHeightCache_.clear();
  avgCharWidthCache_.clear();
  // Same per-pass freshness reasoning: an ordered list's widest marker can change with the
  // theme (decimal → roman) or with item add/remove, both of which force a rebuild pass.
  widestOrderedMarkerCache_.clear();
}

void BlockLayoutBuilder::dumpBuildBreakdown() const {
  if (!perfEnabled_) {
    return;
  }
  qCDebug(blockBuildPerf).nospace() << "build.inlineLayout " << inlineLayoutNs_ / 1000000.0 << " ms";
  qCDebug(blockBuildPerf).nospace() << "build.codeHighlight " << codeHighlightNs_ / 1000000.0 << " ms";
  qCDebug(blockBuildPerf).nospace() << "build.mathRender " << mathRenderNs_ / 1000000.0 << " ms";
  qCDebug(blockBuildPerf).nospace() << "build.htmlRender " << htmlRenderNs_ / 1000000.0 << " ms";
  qCDebug(blockBuildPerf).nospace() << "build.literalText " << literalTextNs_ / 1000000.0 << " ms";
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::build(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  switch (node.type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
      return buildParagraphLike(node, theme, x, y, width, depth);
    case BlockType::BlockQuote:
    case BlockType::List:
      return buildContainer(node, theme, x, y, width, depth);
    case BlockType::ListItem:
      return buildListItem(node, theme, x, y, width, depth);
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
    case BlockType::MathBlock:
      return buildLiteralBlock(node, theme, x, y, width, depth);
    case BlockType::Table:
      return buildTable(node, theme, x, y, width, depth);
    case BlockType::ThematicBreak:
      return buildThematicBreak(node, theme, x, y, width, depth);
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      return buildDefinition(node, theme, x, y, width, depth);
    case BlockType::Document:
    default:
      return buildContainer(node, theme, x, y, width, depth);
  }
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildParagraphLike(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);
  layout->setHeadingLevel(node.headingLevel());
  // [TOC] marker: while the caret is NOT in this block (and the document has at
  // least one heading), render a generated indented heading list. When the caret
  // is in the block, fall through to the normal paragraph build below so the
  // literal "[TOC]" shows for editing — the caret-move triggers a single-block
  // rebuild that flips isToc off. Empty outline ⇒ literal "[TOC]" too.
  if (node.type() == BlockType::Paragraph && isTocMarkerParagraph(node) &&
      !selectionFocusesNode(selection_, node.id()) && tocEntries_ && !tocEntries_->isEmpty()) {
    return buildTocPreview(node, theme, x, y, width, depth);
  }
  // Counter-driven ::before text (e.g. "1. ") for heading auto-numbering themes.
  // The map is computed once per structural/full layout pass (DocumentLayout) and
  // reused verbatim on per-keystroke single-block rebuilds, where the document
  // outline is unchanged — so a heading always reads its correct ordinal.
  if (headingCounterText_ && node.type() == BlockType::Heading) {
    const auto it = headingCounterText_->constFind(node.id());
    if (it != headingCounterText_->constEnd()) { layout->setHeadingBeforeText(it.value()); }
  }
  if (isEmptyDocumentParagraph(md(), node)) {
    layout->setPlaceholderText(QCoreApplication::translate("muffin::BlockLayoutBuilder", "Start writing..."));
  }

  auto inlineLayout = std::make_unique<InlineLayout>();
  const QString elementKey = node.type() == BlockType::Heading
      ? QStringLiteral("h%1").arg(node.headingLevel())
      : (isInsideBlockquote(node) ? QStringLiteral("blockquote p") : QStringLiteral("p"));
  const QFont font = node.type() == BlockType::Heading ? theme.headingFont(node.headingLevel()) : theme.textFontForElement(elementKey, &node);
  const QMarginsF headingPadding = node.type() == BlockType::Heading ? theme.headingPadding(node.headingLevel()) : QMarginsF();
  // A heading with an inline `::before` marker (h4/h5/h6) reserves left space for
  // it (headingBeforeAdvance); the text wraps within the remaining width. The
  // block rect stays full width so hit-test/selection geometry is unchanged.
  const qreal beforeAdvance = node.type() == BlockType::Heading
      ? measuredHeadingBeforeAdvance(theme, node.headingLevel(), layout->headingBeforeText(), font)
      : 0.0;
  // Text width accounts for heading padding (+ before-marker advance) so content
  // wraps within the padded/marked area. The rect itself stays at the original x
  // position so hitTest/cursor calculations remain consistent with the paint offset.
  const qreal textWidth = qMax<qreal>(1.0, width - headingPadding.left() - headingPadding.right() - beforeAdvance);
  // A heading projects content-only: its `# ` prefix region [blockStart, contentStart) is never part
  // of the editable projection (it is empty for Setext headings, whose byteStart == contentStart).
  // The level is conveyed by font size and changed via the heading-level commands, so the prefix is
  // not needed for editing. The prefix still shows while the user is typing it, because at that
  // point the line is a Paragraph/pending marker, not yet a Heading node. Keeping projectionBase at
  // contentStart for both the active and inactive cases means document<->local offset conversion
  // stays identical — this base MUST match what is stored on the layout (contentSourceStart).
  const qsizetype contentStart = sourceContentStartForEditableNode(node);
  const qsizetype projectionBase = contentStart;
  QString editableSource = sourceTextForEditableNode(node);
  InlineLayout::BuildOptions options;
  options.projectionState = InlineProjectionState::forSelection(selection_, node.id(), projectionBase);
  // Inlines are stored relative to the owning top-level block's byteStart. The projection wants
  // content-local spans, so sourceBase = content offset within the block (contentStart - the
  // top-level block's byteStart). localRange then yields the same content-local spans as before.
  // projectionBase (setContentSourceStart / forSelection) stays ABSOLUTE — cursor math is absolute.
  options.sourceBase = projectionBase - node.topLevelBlock()->sourceRange().byteStart;
  options.pendingPrefixLength = pendingPrefixLengthFor(node, editableSource);
  options.isMisspelled = spellMisspelledPredicate();
  options.smartPunct = smartPunctRenderOptions();
  options.breakOnSingleNewline = breakOnSingleNewline_;
  options.renderEmoji = renderEmoji_;
  // Element-level computed text style: headings keep their legacy heading getter
  // fallback, while paragraphs can now differ by context (`p` vs `blockquote p`).
  if (node.type() == BlockType::Heading) {
    options.baseTextColor = theme.headingColor(node.headingLevel());
    // CSS `:hover { color }` target — the inline layout recolours only the runs
    // that inherit the heading colour on hover (links/code keep their own).
    if (const ThemeElementStyle* hover = theme.elementStyle(QStringLiteral("h%1:hover").arg(node.headingLevel()))) {
      if (hover->paint.color.isValid()) {
        options.hoverTextColor = hover->paint.color;
      }
    }
    // CSS `:focus { color }` target — same mechanism, blended on top of hover.
    if (const ThemeElementStyle* focus = theme.elementStyle(QStringLiteral("h%1:focus").arg(node.headingLevel()))) {
      if (focus->paint.color.isValid()) {
        options.focusTextColor = focus->paint.color;
      }
    }
  } else {
    options.baseTextColor = theme.textColorForElement(elementKey, &node);
  }
  options.lineHeightMultiplier = theme.lineHeightMultiplierForElement(elementKey, node.type(), node.headingLevel(), &node);
  options.wordSpacing = theme.wordSpacingForElement(elementKey, &node);
  options.alignment = theme.textAlignmentForElement(elementKey, node.type(), node.headingLevel(), &node);
  options.textTransform = static_cast<TextTransform>(theme.textTransformForElement(elementKey, &node));
  options.textShadow = theme.textShadowForElement(elementKey, &node);
  {
    BuildAccumTimer t(inlineLayoutNs_, perfEnabled_);
    applyPreedit(options);
    inlineLayout->build(node.inlines(), editableSource, theme, textWidth, font, options);
  }
  qreal height = inlineLayout->height();
  if (node.type() == BlockType::Heading &&
      ((theme.headingBorderBottomColor(node.headingLevel()).isValid() && theme.headingBorderBottomWidth(node.headingLevel()) > 0.0) ||
       hasHeadingAfterDecoration(theme, node.headingLevel()))) {
    height += theme.blockSpacing() * 0.35;
  }
  layout->setContentSourceStart(projectionBase);
  const QRectF flowRect(x, y, width, height);
  layout->setRect(flowRect);
  if (node.type() == BlockType::Heading) {
    BlockLayout::CssBoxGeometry box;
    box.hostKey = QStringLiteral("h%1").arg(node.headingLevel());
    box.flowRect = flowRect;
    box.borderBox = flowRect;
    const QRectF visual = inlineLayout->visualTextBounds();
    if (theme.headingFitContent(node.headingLevel()) && visual.isValid() && inlineLayout->height() > 0.0) {
      // Pill left edge = block left + the text's intra-content-box left offset.
      // The headingPadding.left()/beforeAdvance that position the text origin are
      // symmetric with the pill's own left padding, so they cancel: the pill hugs
      // the text's left side. (Exact for h1–h3, whose beforeAdvance is 0.)
      const qreal left = qBound(flowRect.left(), flowRect.left() + visual.left(), flowRect.right());
      const qreal right = qBound(left, flowRect.left() + headingPadding.left() + beforeAdvance + visual.right() + headingPadding.right(), flowRect.right());
      box.borderBox = QRectF(left, flowRect.top(), qMax<qreal>(1.0, right - left), inlineLayout->height());
    }
    box.paddingBox = box.borderBox.marginsRemoved(headingPadding);
    box.contentBox = box.paddingBox;
    // QTextLayout still lays out against the full heading content width so CSS
    // text-align:center/right works like a browser block. The fit-content border
    // box is paint/hover geometry only; tying the text origin to that shrunken box
    // double-applies the centred visual offset and pushes h1/h3 far right.
    box.inlineTextOrigin = QPointF(flowRect.left() + headingPadding.left() + beforeAdvance, flowRect.top());
    qreal overflow = 0.0;
    if (const ThemeElementStyle* hover = theme.elementStyle(box.hostKey + QStringLiteral(":hover"))) {
      overflow = qMax(overflow, hover->paint.boxShadowBlur);
    }
    for (const HoverEffect& he : theme.decorations().hoverEffects) {
      if (he.host == box.hostKey) { overflow = qMax(overflow, he.glowBlur); }
    }
    box.visualOverflow = box.borderBox.adjusted(-overflow, -overflow, overflow, overflow);
    box.valid = true;
    layout->setCssBoxGeometry(box);
  }

  layout->setInlineLayout(std::move(inlineLayout));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildTocPreview(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  // Generated, non-editable preview: one row per document heading, indented by
  // level, painted in the link colour (paintToc) and Ctrl+clickable to scroll to
  // the heading (hitSelf emits a `#toc:<nodeId>` href). Height = rows × line
  // height; the row rects are document-absolute so paint and hit-test share them.
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::Paragraph);
  layout->setDepth(depth);
  layout->setIsToc(true);

  const QString elementKey = QStringLiteral("p");
  const QFont font = theme.textFontForElement(elementKey, &node);
  const QFontMetricsF fm(font);
  qreal multiplier = theme.lineHeightMultiplierForElement(elementKey, node.type(), node.headingLevel(), &node);
  if (multiplier <= 0.0) {
    multiplier = 1.0;
  }
  const qreal lineHeight = fm.height() * multiplier;

  QVector<BlockLayout::TocEntryLayout> entries;
  entries.reserve(tocEntries_->size());
  for (int i = 0; i < tocEntries_->size(); ++i) {
    const OutlineEntry& e = tocEntries_->at(i);
    BlockLayout::TocEntryLayout row;
    row.rect = QRectF(x, y + static_cast<qreal>(i) * lineHeight, width, lineHeight);
    row.target = e.nodeId;
    row.title = e.title;
    row.level = e.level;
    entries.append(std::move(row));
  }
  layout->setTocEntries(std::move(entries));

  layout->setContentSourceStart(sourceContentStartForEditableNode(node));
  layout->setRect(QRectF(x, y, width, static_cast<qreal>(tocEntries_->size()) * lineHeight));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildContainer(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);
  layout->setAlertKind(node.alertKind());

  // Phase 4a: a CSS-themed blockquote applies its real box model to container
  // flow. Padding and border inset the children; margin stays outside this block
  // and is handled by the parent flow. Non-themed quotes keep the legacy fixed left
  // indent.
  const bool quoteBox = node.type() == BlockType::BlockQuote && theme.blockquoteBoxThemed();
  const ThemeElementBoxStyle qbox = quoteBox ? theme.elementBoxStyle(QStringLiteral("blockquote")) : ThemeElementBoxStyle{};
  const QMarginsF qpad = quoteBox ? qbox.padding : QMarginsF();
  const QMarginsF qborder = quoteBox ? QMarginsF(qbox.borderLeftWidth, qbox.borderTopWidth, qbox.borderRightWidth, qbox.borderBottomWidth) : QMarginsF();
  const qreal quoteIndent = (node.type() == BlockType::BlockQuote && !quoteBox) ? theme.blockQuoteIndent() : 0.0;
  const qreal childX = x + quoteIndent + qborder.left() + qpad.left();
  const qreal childWidth = qMax<qreal>(1.0, width - quoteIndent - qborder.left() - qborder.right() - qpad.left() - qpad.right());
  qreal cursorY = y + qborder.top() + qpad.top();
  std::vector<std::unique_ptr<BlockLayout>> children;
  const MarkdownNode* previousChild = nullptr;
  bool omittedOnlyRenderChildren = !node.children().empty();

  const bool firstChildMarginCollapses = qFuzzyIsNull(qborder.top() + qpad.top());
  const bool lastChildMarginCollapses = qFuzzyIsNull(qborder.bottom() + qpad.bottom());
  for (const auto& child : node.children()) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_)) { continue; }
    omittedOnlyRenderChildren = false;
    if (previousChild) { cursorY += spacingBetweenInFlow(*previousChild, *child, theme); }
    else if (!firstChildMarginCollapses) { cursorY += spacingBeforeInFlow(*child, theme); }
    auto childLayout = build(*child, theme, childX, cursorY, childWidth, depth + 1);
    cursorY = childLayout->rect().bottom();
    previousChild = child.get();
    children.push_back(std::move(childLayout));
  }

  qreal height = 0.0;
  if (children.empty()) {
    height = quoteBox && omittedOnlyRenderChildren
                 ? qborder.top() + qpad.top() + qpad.bottom() + qborder.bottom()
                 : qborder.top() + qpad.top() + QFontMetricsF(theme.paragraphFont()).height() + qpad.bottom() + qborder.bottom();
  } else {
    const qreal trailingChildMargin = lastChildMarginCollapses ? 0.0 : spacingAfterInFlow(*previousChild, theme);
    height = cursorY + trailingChildMargin + qpad.bottom() + qborder.bottom() - y;
  }
  const QRectF flowRect(x, y, width, height);
  layout->setRect(flowRect);
  if (quoteBox) {
    BlockLayout::CssBoxGeometry box;
    box.hostKey = QStringLiteral("blockquote");
    box.flowRect = flowRect;
    box.borderBox = flowRect;
    box.paddingBox = flowRect.marginsRemoved(qborder);
    box.contentBox = box.paddingBox.marginsRemoved(qpad);
    box.inlineTextOrigin = box.contentBox.topLeft();
    qreal overflow = 0.0;
    if (const ThemeElementStyle* hover = theme.elementStyle(box.hostKey + QStringLiteral(":hover"))) {
      overflow = qMax(overflow, hover->paint.boxShadowBlur);
    }
    for (const HoverEffect& he : theme.decorations().hoverEffects) {
      if (he.host == box.hostKey) { overflow = qMax(overflow, he.glowBlur); }
    }
    box.visualOverflow = box.borderBox.adjusted(-overflow, -overflow, overflow, overflow);
    box.valid = true;
    layout->setCssBoxGeometry(box);
  }
  layout->setChildren(std::move(children));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildListItem(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::ListItem);
  layout->setDepth(depth);

  // Resolve the list marker up front so the content gutter can be sized to the widest
  // sibling marker — the same way browsers size an <ol> marker box. For ordered lists
  // this keeps multi-digit numbers ("34.", "100.") from overlapping the content and the
  // caret; bullet/task lists keep the fixed theme indent.
  const MarkdownNode* listParent = node.parent();
  qsizetype itemIndex = 0;
  if (listParent) {
    for (const auto& sibling : listParent->children()) {
      if (sibling.get() == &node) {
        break;
      }
      ++itemIndex;
    }
    const ResolvedMarker marker = resolveListMarker(node, theme, itemIndex);
    layout->setListMarkerKind(marker.kind);
    layout->setListMarker(marker.text);
  } else {
    layout->setListMarkerKind(BlockLayout::ListMarkerKind::BulletDisc);
    layout->setListMarker(QStringLiteral("•"));
  }

  const qreal markerGap = theme.listMarkerGap();
  qreal contentIndent = theme.listIndent();
  if (layout->listMarkerKind() == BlockLayout::ListMarkerKind::OrderedText && listParent) {
    const QFontMetricsF metrics(theme.paragraphFont());
    // Measure the widest sibling marker once per list per pass (cached by the list NodeId);
    // previously every item re-scanned all siblings → O(N²) on large ordered lists. All items
    // of a list see the same widestMarker, so caching changes only the cost, not the result.
    const auto cached = widestOrderedMarkerCache_.constFind(listParent->id());
    qreal widestMarker = 0.0;
    if (cached != widestOrderedMarkerCache_.cend()) {
      widestMarker = cached.value();
    } else {
      const qsizetype itemCount = static_cast<qsizetype>(listParent->children().size());
      for (qsizetype index = 0; index < itemCount; ++index) {
        // Size the gutter to the widest CSS-styled marker too (e.g. roman "viii").
        widestMarker = qMax(widestMarker, metrics.horizontalAdvance(resolveListMarker(*listParent->children().at(index), theme, index).text));
      }
      widestOrderedMarkerCache_.insert(listParent->id(), widestMarker);
    }
    contentIndent = qMax(theme.listIndent(), widestMarker + markerGap);
  }
  layout->setListContentIndent(contentIndent);

  const qreal contentX = x + contentIndent;
  const qreal contentWidth = qMax<qreal>(1.0, width - contentIndent);

  auto inlineLayout = std::make_unique<InlineLayout>();
  const QString elementKey = isInsideBlockquote(node) ? QStringLiteral("blockquote p") : QStringLiteral("li");
  InlineLayout::BuildOptions options;
  QString listSourceText;
  if (const MarkdownNode* paragraph = primaryParagraph(node)) {
    listSourceText = sourceTextForEditableNode(*paragraph);
    const qsizetype contentStart = sourceContentStartForEditableNode(*paragraph);
    layout->setContentSourceStart(contentStart);
    options.projectionState = InlineProjectionState::forSelection(selection_, node.id(), contentStart);
    options.sourceBase = contentStart - node.topLevelBlock()->sourceRange().byteStart;
  }
  options.isMisspelled = spellMisspelledPredicate();
  options.smartPunct = smartPunctRenderOptions();
  options.breakOnSingleNewline = breakOnSingleNewline_;
  options.renderEmoji = renderEmoji_;
  {
    BuildAccumTimer t(inlineLayoutNs_, perfEnabled_);
    options.baseTextColor = theme.textColorForElement(elementKey, &node);
    options.lineHeightMultiplier = theme.lineHeightMultiplierForElement(elementKey, BlockType::Paragraph, 0, &node);
    options.wordSpacing = theme.wordSpacingForElement(elementKey, &node);
    options.alignment = theme.textAlignmentForElement(elementKey, BlockType::Paragraph, 0, &node);
    options.textTransform = static_cast<TextTransform>(theme.textTransformForElement(elementKey, &node));
    options.textShadow = theme.textShadowForElement(elementKey, &node);
    applyPreedit(options);
    inlineLayout->build(primaryInlinesForListItem(node), listSourceText, theme, contentWidth, theme.textFontForElement(elementKey, &node), options);
  }
  layout->setInlineLayout(std::move(inlineLayout));

  const qreal inlineHeight = layout->inlineLayout() ? layout->inlineLayout()->height() : QFontMetricsF(theme.paragraphFont()).height();
  qreal flowBottom = y + inlineHeight;
  std::vector<std::unique_ptr<BlockLayout>> children;

  bool skippedPrimaryParagraph = false;
  const MarkdownNode* previousChild = nullptr;
  for (const auto& child : node.children()) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_)) { continue; }
    if (!skippedPrimaryParagraph && child->type() == BlockType::Paragraph) {
      skippedPrimaryParagraph = true;
      previousChild = child.get();
      continue;
    }
    flowBottom += previousChild ? spacingBetweenInFlow(*previousChild, *child, theme) : theme.blockSpacing();
    auto childLayout = build(*child, theme, contentX, flowBottom, contentWidth, depth + 1);
    flowBottom = childLayout->rect().bottom();
    previousChild = child.get();
    children.push_back(std::move(childLayout));
  }
  const qreal height = flowBottom - y;

  // Identity ("is this a task item at all?") and state ("is it checked?") are
  // independent: an unchecked task item has isTaskItem()==true but
  // taskChecked()==false. Driving the first arg off taskChecked() collapsed the
  // two, so unchecked items fell through to the bullet branch and rendered with
  // no checkbox at all — leaving nothing to click to re-check them. The identity
  // flag must come from isTaskItem().
  layout->setTaskListItem(node.isTaskItem(), node.taskChecked());

  layout->setRect(QRectF(x, y, width, height));
  layout->setChildren(std::move(children));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildLiteralBlock(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);
  layout->setLiteral(displayLiteralFor(node));
  if (node.type() == BlockType::MathBlock) {
    layout->setMathDelimiter(node.mathDelimiter());
  }
  if (node.type() == BlockType::CodeFence) {
    layout->setCodeLanguage(node.codeLanguage());
    {
      BuildAccumTimer t(codeHighlightNs_, perfEnabled_);
      layout->setCodeHighlightSpans(codeHighlighter_.highlight(node.codeLanguage(), layout->literal()));
    }
  } else if (node.type() == BlockType::FrontMatter) {
    const QString language = languageForFrontMatter(node.frontMatterFormat());
    layout->setCodeLanguage(language);
    {
      BuildAccumTimer t(codeHighlightNs_, perfEnabled_);
      layout->setCodeHighlightSpans(codeHighlighter_.highlight(language, layout->literal()));
    }
  }
  const bool editingLiteral = (node.type() == BlockType::MathBlock && selectionFocusesNode(selection_, node.id())) ||
                              (node.type() == BlockType::HtmlBlock && editingHtmlBlockId_ == node.id());
  layout->setLiteralEditing(editingLiteral);
  const qreal lineNumberGutter =
      (node.type() == BlockType::CodeFence && showLineNumbers_)
          ? codeLineNumberGutterWidth(layout->literal(), theme)
          : 0.0;
  layout->setLineNumberGutterWidth(lineNumberGutter);
  // Code fences honor markdown/codeBlockWrap; other literal blocks always wrap.
  const bool codeWrap = node.type() == BlockType::CodeFence ? codeBlockWrap_ : true;
  // Measure the widest source line so the block knows whether it is horizontally scrollable and
  // the scrollbar thumb ratio. Reserved strip height makes room for the always-on scrollbar.
  qreal reservedStrip = 0.0;
  if (node.type() == BlockType::CodeFence && !codeWrap) {
    const qreal maxLineW = maxLiteralLineWidth(layout->literal(), theme.codeFont());
    layout->setCodeMaxLineWidth(maxLineW);
    if (codeFenceScroll_ != nullptr) {
      codeFenceScroll_->setContentWidth(node.id(), maxLineW);
    }
    const qreal visibleW =
        qMax<qreal>(1.0, width - lineNumberGutter - theme.codePadding().left() - theme.codePadding().right());
    if (maxLineW > visibleW + 0.5) {
      reservedStrip = BlockLayout::scrollBarStripHeight(theme);
    }
  }
  qreal height;
  {
    BuildAccumTimer t(literalTextNs_, perfEnabled_);
    height = textHeight(
        layout->literal(),
        node.type() == BlockType::MathBlock ? theme.mathFont() : theme.codeFont(),
        node.type() == BlockType::MathBlock ? qMax<qreal>(14.0, QFontMetricsF(theme.mathFont()).height()) : theme.codeLineHeight(),
        width - lineNumberGutter,
        theme.codePadding(),
        codeWrap);
  }
  height += reservedStrip;
  if (node.type() == BlockType::MathBlock) {
    std::shared_ptr<math::MathLayoutResult> mathLayout;
    {
      BuildAccumTimer t(mathRenderNs_, perfEnabled_);
      mathLayout = std::make_shared<math::MathLayoutResult>(mathRenderer_.render(layout->literal(), theme, true, width));
    }
    if (mathLayout->valid()) {
      if (!editingLiteral) {
        height = std::ceil(mathLayout->size.height() + theme.codePadding().top() + theme.codePadding().bottom());
      } else {
        const qreal contentWidth = qMax<qreal>(1.0, width - theme.codePadding().left() - theme.codePadding().right());
        const qreal markerLine = theme.codeLineHeight();
        const qreal sourceHeight = textHeight(layout->literal(), theme.codeFont(), theme.codeLineHeight(), contentWidth, QMarginsF());
        const qreal previewHeight = mathLayout->size.height();
        height = std::ceil(theme.codePadding().top() + markerLine + sourceHeight + markerLine +
                           theme.codePadding().bottom() + theme.codePadding().top() + previewHeight + theme.codePadding().bottom());
      }
      layout->setMathLayout(std::move(mathLayout));
    }
  }
  // Mermaid diagram (milestone I): always validate while diagrams are enabled.
  // A focused fence uses the debounced async path and keeps painting its source;
  // a Ready scene replaces the source only after focus leaves and show-as-source
  // is off. Error/Unsupported retain the source and add a diagnostic panel.
  if (node.type() == BlockType::CodeFence && node.codeLanguage() == QLatin1String("mermaid") &&
      renderDiagrams_ && mermaidCache_ != nullptr) {
    const bool editingMermaid = selectionFocusesNode(selection_, node.id());
    const bool keepSource = editingMermaid || showMermaidAsSource_;
    const QString source = layout->literal();
    const MermaidRenderKey key = mermaidCache_->makeKey(source);
    const MermaidRenderEntry entry = mermaidSyncMode_
        ? mermaidCache_->getSync(key, source)
        : editingMermaid ? mermaidCache_->requestDebounced(key, source)
                         : mermaidCache_->request(key, source);
    layout->setMermaidState(static_cast<BlockLayout::MermaidState>(
        static_cast<int>(entry.status)));  // MermaidRenderStatus ↔ BlockLayout::MermaidState are ordered identically
    if (!keepSource && entry.status == MermaidRenderStatus::Ready &&
        entry.scene) {
      layout->setMermaidViewportCullingEnabled(!mermaidSyncMode_);
      layout->setMermaidScene(entry.scene, entry.naturalSize, entry.metadata);
      const int contentWidth = static_cast<int>(qMax<qreal>(1.0, width - theme.codePadding().left() - theme.codePadding().right()));
      const qreal natW = entry.naturalSize.width();
      const qreal scale = natW > 0.0 ? qMin<qreal>(1.0, contentWidth / natW) : 1.0;
      height = std::ceil(entry.naturalSize.height() * scale + theme.codePadding().top() + theme.codePadding().bottom());
    } else if (entry.status == MermaidRenderStatus::Error ||
               entry.status == MermaidRenderStatus::Unsupported) {
      layout->setMermaidDiagnostic(entry.diagnostic);
      height += BlockLayout::mermaidDiagnosticFootprint(
          entry.diagnostic, theme, width);
    }
  }
  if (node.type() == BlockType::HtmlBlock && editingLiteral) {
    BuildAccumTimer t(codeHighlightNs_, perfEnabled_);
    layout->setCodeHighlightSpans(codeHighlighter_.highlight(QStringLiteral("html"), layout->literal()));
  }
  if (node.type() == BlockType::HtmlBlock && !editingLiteral) {
    const qreal contentWidth = qMax<qreal>(1.0, width - theme.codePadding().left() - theme.codePadding().right());
    qreal fontSize = theme.paragraphFont().pointSizeF();
    if (fontSize <= 0) {
      fontSize = qMax<qreal>(1.0, theme.paragraphFont().pixelSize());
    }
    const QString baseDirectory = documentPath_.isEmpty() ? QString() : QFileInfo(documentPath_).absolutePath();
    QString sanitizedHtml;
    std::shared_ptr<html::HtmlLayoutResult> htmlResult;
    {
      BuildAccumTimer t(htmlRenderNs_, perfEnabled_);
      sanitizedHtml = HtmlSanitizer().sanitizedPreview(layout->literal());
      html::HtmlColorPalette htmlPalette;
      htmlPalette.text = theme.textColor();
      htmlPalette.background = theme.backgroundColor();
      htmlPalette.muted = theme.mutedTextColor();
      htmlPalette.link = theme.linkColor();
      htmlPalette.codeBackground = theme.codeBackgroundColor();
      htmlPalette.codeBorder = theme.codeBorderColor();
      htmlPalette.quoteBorder = theme.quoteBorderColor();
      htmlPalette.tableBorder = theme.tableBorderColor();
      htmlPalette.tableHeaderBackground = theme.tableHeaderBackgroundColor();
      htmlPalette.highlight = theme.highlightBackgroundColor();
      htmlResult = std::make_shared<html::HtmlLayoutResult>(
          htmlRenderer_.render(sanitizedHtml, fontSize, contentWidth, baseDirectory, htmlPalette));
    }
    if (htmlResult->valid() && htmlResult->hasVisibleContent()) {
      height = std::ceil(htmlResult->size().height() + theme.codePadding().top() + theme.codePadding().bottom());
      layout->setHtmlLayout(std::move(htmlResult));
    } else {
      // The HTML rendered to nothing readable — invalid, or valid but with no visible content
      // (e.g. just <div>/<br>/whitespace). Fall back to showing the highlighted source so the
      // block is not an unexplained blank. Spans feed paintLiteralSource() in the paint fallback.
      BuildAccumTimer t(codeHighlightNs_, perfEnabled_);
      layout->setCodeHighlightSpans(codeHighlighter_.highlight(QStringLiteral("html"), layout->literal()));
    }
  }
  layout->setRect(QRectF(x, y, width, height));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildTable(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::Table);
  layout->setDepth(depth);

  const int rowCount = static_cast<int>(node.children().size());
  int columnCount = 0;
  for (const auto& row : node.children()) {
    columnCount = qMax(columnCount, static_cast<int>(row->children().size()));
  }

  if (rowCount == 0 || columnCount == 0) {
    layout->setRect(QRectF(x, y, width, QFontMetricsF(theme.paragraphFont()).height()));
    return layout;
  }

  const QVector<qreal> columnWidths = tableColumnWidths(node, theme, width, breakOnSingleNewline_);
  const QMarginsF padding = theme.tableCellPadding();
  const QVector<TableAlignment> alignments = node.tableAlignments();
  std::vector<BlockLayout::TableRowLayout> rows;
  qreal cursorY = y;
  int rowIndex = 0;

  for (const auto& rowNode : node.children()) {
    std::vector<BlockLayout::TableCellLayout> cells;
    qreal rowHeight = 0;
    qreal cellX = x;
    int column = 0;
    for (const auto& cellNode : rowNode->children()) {
      const qreal columnWidth = column < columnWidths.size() ? columnWidths.at(column) : width / columnCount;
      BlockLayout::TableCellLayout cell;
      cell.nodeId = cellNode->id();
      cell.contentSourceStart = sourceContentStartForEditableNode(*cellNode);
      cell.header = rowNode->tableRowIsHeader();
      cell.alternate = rowIndex % 2 == 1;
      cell.alignment = column < alignments.size() ? alignments.at(column) : TableAlignment::None;
      InlineLayout::BuildOptions options;
      options.sourceBase = sourceContentStartForEditableNode(*cellNode) - cellNode->topLevelBlock()->sourceRange().byteStart;
      if (selection_.focus.text.nodeId == cellNode->id()) {
        options.projectionState = InlineProjectionState::forSelection(selection_, selection_.focus.blockId, sourceContentStartForEditableNode(*cellNode));
      }
      options.isMisspelled = spellMisspelledPredicate();
      options.smartPunct = smartPunctRenderOptions();
      options.breakOnSingleNewline = breakOnSingleNewline_;
  options.renderEmoji = renderEmoji_;
      {
        BuildAccumTimer t(inlineLayoutNs_, perfEnabled_);
        applyPreedit(options);  // only the focused cell has projectionState.cursorSourceOffset set
        cell.text.build(
            cellNode->inlines(),
            sourceTextForEditableNode(*cellNode),
            theme,
            qMax<qreal>(1.0, columnWidth - padding.left() - padding.right()),
            cell.header ? theme.headingFont(6) : theme.paragraphFont(),
            options);
      }
      rowHeight = qMax(rowHeight, cell.text.height() + padding.top() + padding.bottom());
      cell.rect = QRectF(cellX, cursorY, columnWidth, 0);
      cells.push_back(std::move(cell));
      cellX += columnWidth;
      ++column;
    }
    while (column < columnCount) {
      const qreal columnWidth = column < columnWidths.size() ? columnWidths.at(column) : width / columnCount;
      BlockLayout::TableCellLayout cell;
      cell.nodeId = rowNode->id();
      cell.alternate = rowIndex % 2 == 1;
      cell.alignment = column < alignments.size() ? alignments.at(column) : TableAlignment::None;
      cell.rect = QRectF(cellX, cursorY, columnWidth, 0);
      rowHeight = qMax(rowHeight, QFontMetricsF(theme.paragraphFont()).height() + padding.top() + padding.bottom());
      cells.push_back(std::move(cell));
      cellX += columnWidth;
      ++column;
    }
    for (BlockLayout::TableCellLayout& cell : cells) {
      cell.rect.setHeight(rowHeight);
    }
    BlockLayout::TableRowLayout row;
    row.rect = QRectF(x, cursorY, width, rowHeight);
    row.cells = std::move(cells);
    rows.push_back(std::move(row));
    cursorY += rowHeight;
    ++rowIndex;
  }

  layout->setRect(QRectF(x, y, width, cursorY - y));
  layout->setTableRows(std::move(rows));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildThematicBreak(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::ThematicBreak);
  layout->setDepth(depth);
  layout->setRect(QRectF(x, y, width, theme.blockSpacing() * 2.0));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildDefinition(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);
  const DefinitionBlock definition = node.definition();
  layout->setDefinition(definition);
  layout->setContentSourceStart(definition.markerRange.isValid() ? definition.markerRange.start : node.sourceRange().byteStart);
  const bool definitionFocused = selection_.isCollapsed() && selection_.focus.blockId == node.id();

  const QFont font = theme.paragraphFont();
  const QFontMetricsF metrics(font);
  const qreal lineHeight = std::ceil(metrics.height() * kLineHeightFactor);
  qreal cursorX = x;
  QVector<BlockLayout::DefinitionTokenLayout> definitionTokens;
  auto syntax = [&](const QString& text) {
    BlockLayout::DefinitionTokenLayout token;
    token.kind = BlockLayout::DefinitionTokenLayout::Kind::Syntax;
    token.text = text;
    token.rect = QRectF(cursorX, y, qMax<qreal>(1.0, metrics.horizontalAdvance(text)), lineHeight);
    token.sourceStart = -1;
    token.sourceEnd = -1;
    token.editable = false;
    definitionTokens.push_back(token);
    cursorX = token.rect.right();
  };
  auto slot = [&](BlockLayout::DefinitionSlotLayout::Field field,
                  const DefinitionFieldRange& sourceRange,
                  const QString& text,
                  const QString& placeholder) {
    BlockLayout::DefinitionTokenLayout token;
    token.kind = BlockLayout::DefinitionTokenLayout::Kind::Slot;
    token.field = field;
    token.text = text;
    token.placeholder = placeholder;
    token.sourceStart = sourceRange.start;
    token.sourceEnd = sourceRange.end;
    token.editable = true;
    const CursorPosition focus = selection_.focus;
    token.focused = selection_.isCollapsed() && focus.blockId == node.id() &&
                    focus.text.sourceOffset >= token.sourceStart && focus.text.sourceOffset <= token.sourceEnd;
    const QString display = text.isEmpty() ? placeholder : text;
    const qreal displayWidth = text.isEmpty() && token.focused ? 1.0 : metrics.horizontalAdvance(display);
    token.rect = QRectF(cursorX, y, qMax<qreal>(1.0, displayWidth), lineHeight);
    cursorX = token.rect.right();
    definitionTokens.push_back(token);
  };
  auto titleOpeningSyntax = [&definition]() {
    switch (definition.titleDelimiter) {
      case DefinitionBlock::TitleDelimiter::SingleQuote:
        return QStringLiteral("  '");
      case DefinitionBlock::TitleDelimiter::Parentheses:
        return QStringLiteral("  (");
      case DefinitionBlock::TitleDelimiter::DoubleQuote:
      case DefinitionBlock::TitleDelimiter::None:
      default:
        return QStringLiteral("  \"");
    }
  };
  auto titleClosingSyntax = [&definition]() {
    switch (definition.titleDelimiter) {
      case DefinitionBlock::TitleDelimiter::SingleQuote:
        return QStringLiteral("'");
      case DefinitionBlock::TitleDelimiter::Parentheses:
        return QStringLiteral(")");
      case DefinitionBlock::TitleDelimiter::DoubleQuote:
      case DefinitionBlock::TitleDelimiter::None:
      default:
        return QStringLiteral("\"");
    }
  };

  syntax(QStringLiteral("["));
  if (definition.kind == DefinitionBlock::Kind::Footnote) {
    syntax(QStringLiteral("^"));
  }
  slot(BlockLayout::DefinitionSlotLayout::Field::Label,
       definition.labelRange,
       definition.label,
       QStringLiteral("name"));
  syntax(QStringLiteral("]:"));

  if (definition.kind == DefinitionBlock::Kind::Footnote) {
    syntax(QStringLiteral(" "));
    slot(BlockLayout::DefinitionSlotLayout::Field::Note,
         definition.noteRange,
         definition.note,
         QStringLiteral("input description here"));

    // Extract continuation lines for multi-line footnotes
    if (definition.sourceRange.isValid() && definition.noteRange.isValid() &&
        definition.sourceRange.end > definition.noteRange.end && !md().isEmpty()) {
      // Find end of the first line in the source range
      const qsizetype srcStart = definition.sourceRange.start;
      const qsizetype firstLineEnd = md().indexOf(
          QLatin1Char('\n'), definition.noteRange.end);
      if (firstLineEnd >= 0 && firstLineEnd < definition.sourceRange.end) {
        QString continuation;
        qsizetype pos = firstLineEnd + 1;
        while (pos < definition.sourceRange.end) {
          // Strip leading indentation (up to 4 spaces or 1 tab)
          int indent = 0;
          while (pos < definition.sourceRange.end && indent < 4 &&
                 md().at(pos) == QLatin1Char(' ')) {
            ++pos;
            ++indent;
          }
          if (pos < definition.sourceRange.end && indent < 4 &&
              md().at(pos) == QLatin1Char('\t')) {
            ++pos;
          }
          // Read to end of line
          const qsizetype contentStart = pos;
          while (pos < definition.sourceRange.end &&
                 md().at(pos) != QLatin1Char('\n') &&
                 md().at(pos) != QLatin1Char('\r')) {
            ++pos;
          }
          if (!continuation.isEmpty()) {
            continuation += QLatin1Char('\n');
          }
          continuation += md().mid(contentStart, pos - contentStart);
          if (pos < definition.sourceRange.end &&
              md().at(pos) == QLatin1Char('\r')) {
            ++pos;
          }
          if (pos < definition.sourceRange.end &&
              md().at(pos) == QLatin1Char('\n')) {
            ++pos;
          }
        }
        if (!continuation.isEmpty()) {
          layout->setLiteral(continuation);
        }
      }
    }
  } else {
    syntax(QStringLiteral("  "));
    slot(BlockLayout::DefinitionSlotLayout::Field::Destination,
         definition.destinationRange,
         definition.destination,
         QStringLiteral("input link url here"));
    if (definition.titleDelimiter != DefinitionBlock::TitleDelimiter::None || definitionFocused) {
      const bool explicitEmptyTitle = definition.titleDelimiter != DefinitionBlock::TitleDelimiter::None &&
                                      definition.title.isEmpty();
      syntax(titleOpeningSyntax());
      slot(BlockLayout::DefinitionSlotLayout::Field::Title,
           definition.titleRange,
           definition.title,
           explicitEmptyTitle ? QString() : QStringLiteral("title (optional)"));
      syntax(titleClosingSyntax());
    }
  }

  QVector<BlockLayout::DefinitionSlotLayout> definitionSlots;
  for (const BlockLayout::DefinitionTokenLayout& token : definitionTokens) {
    if (token.kind != BlockLayout::DefinitionTokenLayout::Kind::Slot) {
      continue;
    }
    BlockLayout::DefinitionSlotLayout slotLayout;
    slotLayout.field = token.field;
    slotLayout.rect = token.rect;
    slotLayout.text = token.text;
    slotLayout.placeholder = token.placeholder;
    slotLayout.sourceStart = token.sourceStart;
    slotLayout.sourceEnd = token.sourceEnd;
    slotLayout.focused = token.focused;
    definitionSlots.push_back(slotLayout);
  }
  layout->setDefinitionSlots(std::move(definitionSlots));
  layout->setDefinitionTokens(std::move(definitionTokens));

  // Compute total height including footnote continuation lines
  qreal totalHeight = lineHeight;
  if (!layout->literal().isEmpty() && definition.kind == DefinitionBlock::Kind::Footnote) {
    // Find the note slot's X position for continuation indentation
    qreal noteX = cursorX;
    for (const auto& token : definitionTokens) {
      if (token.field == BlockLayout::DefinitionSlotLayout::Field::Note) {
        noteX = token.rect.left();
        break;
      }
    }
    const qreal continuationWidth = qMax<qreal>(1.0, x + width - noteX);
    totalHeight += layoutTextHeight(layout->literal(), font, lineHeight, continuationWidth);
  }
  layout->setRect(QRectF(x, y, width, totalHeight));
  return layout;
}

QString BlockLayoutBuilder::textForListMarker(const MarkdownNode& listNode, qsizetype index) const {
  if (listNode.listKind() == ListKind::Ordered) {
    return QStringLiteral("%1.").arg(listNode.listStart() + static_cast<int>(index));
  }
  return QStringLiteral("•");
}

BlockLayout::ListMarkerKind BlockLayoutBuilder::markerKindForListItem(const MarkdownNode& itemNode) const {
  const MarkdownNode* listNode = itemNode.parent();
  if (!listNode || listNode->type() != BlockType::List) {
    return BlockLayout::ListMarkerKind::None;
  }
  if (listNode->listKind() == ListKind::Ordered) {
    return BlockLayout::ListMarkerKind::OrderedText;
  }

  int unorderedDepth = 0;
  for (const MarkdownNode* node = listNode; node; node = node->parent()) {
    if (node->type() == BlockType::List && node->listKind() == ListKind::Bullet) {
      ++unorderedDepth;
    }
  }
  switch (unorderedDepth) {
    case 1:
      return BlockLayout::ListMarkerKind::BulletDisc;
    case 2:
      return BlockLayout::ListMarkerKind::BulletCircle;
    default:
      return BlockLayout::ListMarkerKind::BulletSquare;
  }
}

BlockLayoutBuilder::ResolvedMarker BlockLayoutBuilder::resolveListMarker(
    const MarkdownNode& itemNode, const RenderTheme& theme, qsizetype itemIndex) const {
  const MarkdownNode* listNode = itemNode.parent();
  // `li::marker { content: … counter(list-item) … }` — content-driven marker,
  // resolved against the implicit list-item counter (this item's position).
  const auto& contentTokens = theme.decorations().listMarkerContent;
  if (!contentTokens.empty()) {
    const auto value = [&itemNode, listNode, itemIndex](const QString& name) -> int {
      if (name == QStringLiteral("list-item")) {
        const int start = (listNode && listNode->listKind() == ListKind::Ordered) ? listNode->listStart() : 1;
        return start + int(itemIndex);
      }
      return 0;  // named counters are not tracked in the marker path
    };
    const auto chain = [&itemNode](const QString& name) -> QVector<int> {
      QVector<int> levels;  // outermost first
      if (name != QStringLiteral("list-item")) { return levels; }
      for (const MarkdownNode* item = &itemNode; item && item->type() == BlockType::ListItem; ) {
        const MarkdownNode* list = item->parent();
        if (!list) { break; }
        int idx = 0;
        for (const auto& s : list->children()) { if (s.get() == item) { break; } ++idx; }
        const int start = (list->listKind() == ListKind::Ordered) ? list->listStart() : 1;
        levels.prepend(start + idx);
        item = list->parent();  // next outer list level (a ListItem, or null at top)
      }
      return levels;
    };
    const QString text = resolveContentTokens(contentTokens, value, chain);
    return {text.isEmpty() ? BlockLayout::ListMarkerKind::None : BlockLayout::ListMarkerKind::OrderedText, text};
  }
  QString styleType;
  if (listNode) {
    const bool ordered = listNode->listKind() == ListKind::Ordered;
    styleType = theme.listStyleTypeForItem(ordered);
  }
  if (styleType.isEmpty()) {
    // No CSS list-style-type → legacy depth/kind-based marker.
    if (listNode) { return {markerKindForListItem(itemNode), textForListMarker(*listNode, itemIndex)}; }
    return {BlockLayout::ListMarkerKind::BulletDisc, QStringLiteral("•")};
  }
  if (styleType == QStringLiteral("none")) { return {BlockLayout::ListMarkerKind::None, QString()}; }
  if (styleType == QStringLiteral("disc")) { return {BlockLayout::ListMarkerKind::BulletDisc, QStringLiteral("•")}; }
  if (styleType == QStringLiteral("circle")) { return {BlockLayout::ListMarkerKind::BulletCircle, QStringLiteral("•")}; }
  if (styleType == QStringLiteral("square")) { return {BlockLayout::ListMarkerKind::BulletSquare, QStringLiteral("•")}; }
  if (isOrderedListStyle(styleType)) {
    const int start = (listNode && listNode->listKind() == ListKind::Ordered) ? listNode->listStart() : 1;
    return {BlockLayout::ListMarkerKind::OrderedText, formatCounterValue(start + int(itemIndex), styleType) + QStringLiteral(".")};
  }
  if (listNode) { return {markerKindForListItem(itemNode), textForListMarker(*listNode, itemIndex)}; }
  return {BlockLayout::ListMarkerKind::BulletDisc, QStringLiteral("•")};
}

QVector<InlineNode> BlockLayoutBuilder::primaryInlinesForListItem(const MarkdownNode& node) const {
  if (!node.inlines().isEmpty()) {
    return node.inlines();
  }
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child->inlines();
    }
  }
  return {};
}

QString BlockLayoutBuilder::sourceTextForEditableNode(const MarkdownNode& node) const {
  const qsizetype start = sourceContentStartForEditableNode(node);
  const qsizetype end = sourceContentEndForEditableNode(node);
  if (start < 0 || end < start) {
    return {};
  }
  return md().mid(start, end - start);
}

qsizetype BlockLayoutBuilder::sourceContentStartForEditableNode(const MarkdownNode& node) const {
  const SourceRange range = node.sourceRange();
  qsizetype start = hasResolvedByteRange(range)
                     ? range.byteStart
                     : sourceOffsetForLineColumn(range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = sourceContentEndForEditableNode(node);
  if (start < 0 || end < start) {
    return -1;
  }
  if (isEmptyDocumentParagraph(md(), node)) {
    return 0;
  }
  if (node.type() == BlockType::Heading) {
    while (start < end && md().at(start) == QLatin1Char('#')) {
      ++start;
    }
    if (start < end && md().at(start).isSpace()) {
      ++start;
    }
  } else if (node.type() == BlockType::Paragraph) {
    start = paragraphContentStartIncludingCommonMarkIndent(md(), start);
    if (node.parent() && node.parent()->type() == BlockType::ListItem) {
      qsizetype lineStart = start;
      while (lineStart > 0 && md().at(lineStart - 1) != QLatin1Char('\n')) {
        --lineStart;
      }
      qsizetype lineEnd = start;
      while (lineEnd < md().size() && md().at(lineEnd) != QLatin1Char('\n')) {
        ++lineEnd;
      }
      const ListLineInfo info = listLineInfoFor(md().mid(lineStart, lineEnd - lineStart));
      if (info.valid && info.task) {
        start = lineStart + info.taskContentStart;
      }
    }
  }
  return start;
}

qsizetype BlockLayoutBuilder::sourceContentEndForEditableNode(const MarkdownNode& node) const {
  const SourceRange range = node.sourceRange();
  qsizetype end = node.type() == BlockType::Heading
                    ? headingContentEndOffset(node, md())
                    : (hasResolvedByteRange(range)
                           ? range.byteEnd
                           : sourceOffsetForLineEnd(range.lineEnd));
  const qsizetype start = sourceOffsetForLineColumn(range.lineStart, qMax(1, range.columnStart));
  if (isEmptyDocumentParagraph(md(), node)) {
    return 0;
  }
  if (start < 0 || end < start) {
    return -1;
  }
  if (node.type() == BlockType::TableCell) {
    while (end > start && md().at(end - 1).isSpace()) {
      --end;
    }
  }
  return end;
}

qsizetype BlockLayoutBuilder::sourceOffsetForLineColumn(int line, int column) const {
  return lineOffsets_ ? lineOffsets_->offsetForLineColumn(line, column) : -1;
}

qsizetype BlockLayoutBuilder::sourceOffsetForLineEnd(int line) const {
  return lineOffsets_ ? lineOffsets_->lineEndOffset(line) : -1;
}

qreal BlockLayoutBuilder::textHeight(const QString& text, const QFont& font, qreal lineHeight, qreal width, const QMarginsF& padding, bool wrap) const {
  const qreal innerWidth = qMax<qreal>(1.0, width - padding.left() - padding.right());
  return std::ceil(layoutLiteralHeight(text, font, lineHeight, innerWidth, wrap) + padding.top() + padding.bottom() + 2.0);
}

namespace {

// True if the inline set renders any content whose size is not cheaply derivable from
// font metrics (inline images / inline math). Such blocks must be fully measured.
bool inlinesContainSizedContent(const QVector<InlineNode>& inlines) {
  for (const InlineNode& node : inlines) {
    if (node.type() == InlineType::Image || node.type() == InlineType::InlineMath) {
      return true;
    }
    if (inlinesContainSizedContent(node.children())) {
      return true;
    }
  }
  return false;
}

// Estimate wrapped line count from plain text and an average characters-per-line capacity,
// respecting explicit newlines (a trailing '\n' yields one extra line, matching QTextLayout).
qreal estimateWrappedLines(QStringView text, qreal charsPerLine) {
  if (text.isEmpty()) {
    return 1.0;
  }
  const qreal cpl = std::max(charsPerLine, qreal(1.0));
  qreal lines = 0;
  qsizetype segStart = 0;
  while (true) {
    const qsizetype nl = text.indexOf(QLatin1Char('\n'), segStart);
    const qsizetype segEnd = nl < 0 ? text.length() : nl;
    lines += std::max(qreal(1.0), std::ceil(static_cast<qreal>(segEnd - segStart) / cpl));
    if (nl < 0) {
      break;
    }
    segStart = nl + 1;
    if (segStart >= text.length()) {
      lines += 1.0;  // trailing newline -> extra empty line
      break;
    }
  }
  return std::max(lines, qreal(1.0));
}

// O(1) wrapped-line estimate from a plain character count and an average characters-per-line
// capacity — no text scan. Paragraph source rarely embeds hard newlines (SoftBreak inlines split
// at parse time), so a pure char-budget count is a close approximation; the visible-window build
// still resolves exact heights on promotion. Used by the estimate path to avoid materializing
// inline text (plainTextForInlines + per-char walks), which was ~20-30s of the open estimate on a
// 250k-block / 2.1M-inline doc.
qreal estimateWrappedLinesFromCharCount(qsizetype charCount, qreal charsPerLine) {
  const qreal cpl = std::max(charsPerLine, qreal(1.0));
  return std::max(qreal(1.0), std::ceil(static_cast<qreal>(std::max<qsizetype>(charCount, 0)) / cpl));
}

}  // namespace

qreal BlockLayoutBuilder::estimateLineHeight(const QFont& font) const {
  // Matches InlineLayout's fallback per-line height: ceil(QFontMetricsF::height() * 1.16).
  return std::ceil(QFontMetricsF(font).height() * kLineHeightFactor);
}

qreal estimateLineHeightForElement(const RenderTheme& theme, const QString& elementKey, BlockType type,
                                   const MarkdownNode* node = nullptr, int headingLevel = 0) {
  const QFont font = type == BlockType::Heading ? theme.headingFont(headingLevel) : theme.textFontForElement(elementKey, node);
  const qreal multiplier = theme.lineHeightMultiplierForElement(elementKey, type, headingLevel, node);
  if (multiplier > 0.0) {
    const qreal pointSize = font.pointSizeF() > 0.0 ? font.pointSizeF() : 12.0;
    return std::ceil(pointSize * (96.0 / 72.0) * multiplier);
  }
  return std::ceil(QFontMetricsF(font).height() * kLineHeightFactor);
}

qreal BlockLayoutBuilder::avgCharWidthForText(QStringView text, const QFont& font) const {
  const QString key = font.key();
  auto it = fontMetricsCache_.constFind(key);
  qreal wideAdvance;
  qreal narrowAdvance;
  if (it != fontMetricsCache_.constEnd()) {
    wideAdvance = it.value().first;
    narrowAdvance = it.value().second;
  } else {
    const QFontMetricsF metrics(font);
    // One representative wide (fullwidth/CJK) glyph and an ASCII average; cached per font so the
    // estimate pass never measures full block text.
    wideAdvance = metrics.horizontalAdvance(QChar(0x5B57));    // '字' (CJK ideograph)
    narrowAdvance = metrics.horizontalAdvance(QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789 ")) /
                    static_cast<qreal>(37);
    fontMetricsCache_.insert(key, {wideAdvance, narrowAdvance});
  }
  if (text.isEmpty()) {
    return wideAdvance;  // placeholder; estimateWrappedLines treats empty as one line anyway
  }
  // Cheap per-char classification: East-Asian wide scripts (>= Hangul Jamo) advance ~1em, the rest
  // ~half-em. Tracks CJK vs ASCII density per block without measuring the whole run.
  qreal total = 0;
  qsizetype count = 0;
  for (const QChar c : text) {
    total += (c.unicode() >= 0x1100) ? wideAdvance : narrowAdvance;
    ++count;
  }
  return count > 0 ? total / static_cast<qreal>(count) : wideAdvance;
}

// O(1) variant of avgCharWidthForText: returns the cached narrow advance only, with no per-char
// classification walk. The estimate path assumes narrow-char density (exact for ASCII; for CJK it
// over-estimates chars-per-line, under-estimating wrapped lines). Since the estimate is only a
// scrollbar placeholder — mustMeasure blocks and viewport promotion resolve exact heights — the CJK
// imprecision is acceptable and lets the lazy estimate loop skip inline-text materialization.
qreal BlockLayoutBuilder::avgCharWidthForFont(const QFont& font) const {
  const QString key = font.key();
  auto it = fontMetricsCache_.constFind(key);
  if (it != fontMetricsCache_.constEnd()) {
    return it.value().second;  // narrowAdvance
  }
  const QFontMetricsF metrics(font);
  const qreal wideAdvance = metrics.horizontalAdvance(QChar(0x5B57));    // '字' (CJK ideograph)
  const qreal narrowAdvance = metrics.horizontalAdvance(QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789 ")) /
                              static_cast<qreal>(37);
  fontMetricsCache_.insert(key, {wideAdvance, narrowAdvance});
  return narrowAdvance;
}

qreal BlockLayoutBuilder::cachedEstimateLineHeight(const RenderTheme& theme, const QString& elementKey, BlockType type, int headingLevel) const {
  const QString cacheKey = elementKey + QLatin1Char('|') + QString::number(headingLevel);
  auto it = lineHeightCache_.constFind(cacheKey);
  if (it != lineHeightCache_.constEnd()) {
    return it.value();
  }
  const qreal h = estimateLineHeightForElement(theme, elementKey, type, nullptr, headingLevel);
  lineHeightCache_.insert(cacheKey, h);
  return h;
}

qreal BlockLayoutBuilder::cachedAvgCharWidthForElement(const RenderTheme& theme, const QString& elementKey, bool isHeading, int headingLevel) const {
  const QString cacheKey = elementKey + QLatin1Char('|') + QString::number(headingLevel);
  auto it = avgCharWidthCache_.constFind(cacheKey);
  if (it != avgCharWidthCache_.constEnd()) {
    return it.value();
  }
  const QFont font = isHeading ? theme.headingFont(headingLevel) : theme.textFontForElement(elementKey, nullptr);
  const qreal w = avgCharWidthForFont(font);
  avgCharWidthCache_.insert(cacheKey, w);
  return w;
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateHeight(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const {
  switch (node.type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
      return estimateParagraphLike(node, theme, width);
    case BlockType::BlockQuote:
    case BlockType::List:
      return estimateContainer(node, theme, width, depth);
    case BlockType::ListItem:
      return estimateListItem(node, theme, width, depth);
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
    case BlockType::MathBlock:
      return estimateLiteralBlock(node, theme, width);
    case BlockType::Table:
      return estimateTable(node, theme, width);
    case BlockType::ThematicBreak:
      return {theme.blockSpacing() * 2.0, false};
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      return estimateDefinition(node, theme, width);
    case BlockType::Document:
    default:
      return estimateContainer(node, theme, width, depth);
  }
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateParagraphLike(const MarkdownNode& node, const RenderTheme& theme, qreal width) const {
  const bool isHeading = node.type() == BlockType::Heading;
  const QString elementKey = isHeading ? QStringLiteral("h%1").arg(node.headingLevel())
                                      : (isInsideBlockquote(node) ? QStringLiteral("blockquote p") : QStringLiteral("p"));
  // Estimate resolves the load-time PROTOTYPE style only (nullptr node → elementStyle fast path),
  // skipping the per-node structural cascade. github's structural selectors match only lists/tables,
  // so paragraph estimates are identical to the structural result; the visible-window build
  // (promoteSlot → buildParagraphLike) still resolves structural style for exact heights.
  const qreal lineHeight = cachedEstimateLineHeight(theme, elementKey, node.type(), node.headingLevel());
  // O(1) estimate: derive the wrapped-line count from the block's source char count (sourceRange is
  // UTF-16 code units ≈ visible chars) + a cached per-font narrow advance, WITHOUT materializing
  // the inline text. plainTextForInlines + per-char walks were ~20-30s of open on a 2.1M-inline doc.
  // sourceRange().byteLength() over-counts inline markup (`**`, `[](url)`) and markers, but the
  // estimate is only a scrollbar placeholder (mustMeasure + viewport promotion resolve exact
  // heights), so the imprecision is harmless.
  const qsizetype charCount = node.sourceRange().byteLength();
  // Mirror buildParagraphLike: an inline ::before marker narrows the wrap width.
  const qreal beforeAdvance = isHeading ? theme.headingBeforeAdvance(node.headingLevel()) : 0.0;
  const qreal avgCharWidth = cachedAvgCharWidthForElement(theme, elementKey, isHeading, node.headingLevel());
  const qreal charsPerLine = std::max(qreal(1.0), std::floor(std::max<qreal>(1.0, width - beforeAdvance) / avgCharWidth));
  qreal height = estimateWrappedLinesFromCharCount(charCount, charsPerLine) * lineHeight;
  if (isHeading && node.headingLevel() <= 2) {
    height += theme.blockSpacing() * 0.35;
  }
  // mustMeasure dropped: DocumentLayout never reads EstimateResult.mustMeasure (promotion is purely
  // viewport-visibility-driven), so the inlinesContainSizedContent walk was pure waste on the
  // estimate path (~2s of the 250k-block open estimate).
  return {height, false};
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateContainer(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const {
  if (depth > 64) {
    return {estimateLineHeight(theme.paragraphFont()), true};
  }
  if (node.children().empty()) {
    return {QFontMetricsF(theme.paragraphFont()).height(), false};
  }
  const bool isQuote = node.type() == BlockType::BlockQuote;
  const bool quoteBox = isQuote && theme.blockquoteBoxThemed();
  const ThemeElementBoxStyle qbox = quoteBox ? theme.elementBoxStyle(QStringLiteral("blockquote")) : ThemeElementBoxStyle{};
  const QMarginsF qpad = quoteBox ? qbox.padding : QMarginsF();
  const QMarginsF qborder = quoteBox ? QMarginsF(qbox.borderLeftWidth, qbox.borderTopWidth, qbox.borderRightWidth, qbox.borderBottomWidth) : QMarginsF();
  const qreal quoteIndent = (isQuote && !quoteBox) ? theme.blockQuoteIndent() : 0.0;
  const qreal childWidth = std::max<qreal>(1.0, width - quoteIndent - qborder.left() - qborder.right() - qpad.left() - qpad.right());
  const bool firstChildMarginCollapses = qFuzzyIsNull(qborder.top() + qpad.top());
  const bool lastChildMarginCollapses = qFuzzyIsNull(qborder.bottom() + qpad.bottom());
  qreal total = qborder.top() + qpad.top();
  bool mustMeasure = false;
  const MarkdownNode* previousChild = nullptr;
  bool omittedOnlyRenderChildren = !node.children().empty();
  for (const auto& child : node.children()) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_)) { continue; }
    omittedOnlyRenderChildren = false;
    if (previousChild) { total += spacingBetweenInFlow(*previousChild, *child, theme, /*fast=*/true); }
    else if (!firstChildMarginCollapses) { total += spacingBeforeInFlow(*child, theme, /*fast=*/true); }
    const EstimateResult r = estimateHeight(*child, theme, childWidth, depth + 1);
    total += r.height;
    mustMeasure = mustMeasure || r.mustMeasure;
    previousChild = child.get();
  }
  if (previousChild && !lastChildMarginCollapses) { total += spacingAfterInFlow(*previousChild, theme, /*fast=*/true); }
  total += qpad.bottom() + qborder.bottom();
  if (!previousChild && !(quoteBox && omittedOnlyRenderChildren)) { total += QFontMetricsF(theme.paragraphFont()).height(); }
  return {total, mustMeasure};
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateListItem(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const {
  const MarkdownNode* listParent = node.parent();
  qreal contentIndent = theme.listIndent();
  const bool ordered = listParent && listParent->listKind() == ListKind::Ordered;
  if (ordered && listParent) {
    // O(1) marker width: an ordered marker is at most "<itemCount>.", so its digit count bounds the
    // width. The old loop measured EVERY sibling's marker here (O(N) per item, and estimateContainer
    // calls this once per item → O(N²) per list — ~5s on the dense-file estimate).
    const QFontMetricsF metrics(theme.paragraphFont());
    const qsizetype itemCount = listParent->children().size();
    const int digits = itemCount <= 0 ? 1 : static_cast<int>(std::log10(static_cast<qreal>(itemCount))) + 1;
    const qreal widestMarker =
        metrics.horizontalAdvance(QLatin1Char('0')) * digits + metrics.horizontalAdvance(QStringLiteral("."));
    contentIndent = std::max(theme.listIndent(), widestMarker + theme.listMarkerGap());
  }
  const qreal contentWidth = std::max<qreal>(1.0, width - contentIndent);

  const QVector<InlineNode> primary = primaryInlinesForListItem(node);
  const QString elementKey = isInsideBlockquote(node) ? QStringLiteral("blockquote p") : QStringLiteral("li");
  // Estimate uses the prototype style only (see estimateParagraphLike) — no per-node structural cascade.
  const qreal lineHeight = cachedEstimateLineHeight(theme, elementKey, BlockType::Paragraph, 0);
  qreal inlineHeight = lineHeight;
  bool mustMeasure = false;
  if (!primary.isEmpty()) {
    // O(1) estimate from the item's source char count (see estimateParagraphLike). byteLength
    // includes the list marker and any nested-block source, so it over-counts, but the estimate is
    // only a scrollbar placeholder (promotion resolves the exact height).
    const qsizetype charCount = node.sourceRange().byteLength();
    const qreal charsPerLine = std::max(qreal(1.0), std::floor(contentWidth / cachedAvgCharWidthForElement(theme, elementKey, false, 0)));
    inlineHeight = estimateWrappedLinesFromCharCount(charCount, charsPerLine) * lineHeight;
    // mustMeasure dropped (see estimateParagraphLike) — unconsumed by DocumentLayout.
  }
  qreal height = inlineHeight;

  bool skippedPrimary = false;
  const MarkdownNode* previousChild = nullptr;
  for (const auto& child : node.children()) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_)) { continue; }
    if (!skippedPrimary && child->type() == BlockType::Paragraph) {
      skippedPrimary = true;
      previousChild = child.get();
      continue;
    }
    const EstimateResult r = estimateHeight(*child, theme, contentWidth, depth + 1);
    height += (previousChild ? spacingBetweenInFlow(*previousChild, *child, theme, /*fast=*/true) : theme.blockSpacing()) + r.height;
    mustMeasure = mustMeasure || r.mustMeasure;
    previousChild = child.get();
  }
  return {height, mustMeasure};
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateLiteralBlock(const MarkdownNode& node, const RenderTheme& theme, qreal width) const {
  const QString literal = displayLiteralFor(node);
  const bool isMath = node.type() == BlockType::MathBlock;
  const QFont font = isMath ? theme.mathFont() : theme.codeFont();
  const qreal lineHeight = isMath ? std::max<qreal>(14.0, QFontMetricsF(theme.mathFont()).height()) : theme.codeLineHeight();
  const QMarginsF padding = theme.codePadding();
  const qreal lineNumberGutter =
      (node.type() == BlockType::CodeFence && showLineNumbers_) ? codeLineNumberGutterWidth(literal, theme) : 0.0;
  const qreal innerWidth = std::max<qreal>(1.0, width - padding.left() - padding.right() - lineNumberGutter);
  const qreal avgCharWidth = avgCharWidthForFont(font);
  qreal lines;
  qreal reservedStrip = 0.0;
  if (node.type() == BlockType::CodeFence && !codeBlockWrap_) {
    // NoWrap: one visual line per physical line; reserve the scrollbar strip if the widest line
    // (estimated) overflows, matching the build path so the scrollbar range doesn't jump on promotion.
    lines = literal.isEmpty() ? 1.0 : qreal(literal.count(QLatin1Char('\n'))) + 1.0;
    int maxLineChars = 1;
    qsizetype start = 0;
    while (start <= literal.size()) {
      const qsizetype nl = literal.indexOf(QLatin1Char('\n'), start);
      const qsizetype end = nl < 0 ? literal.size() : nl;
      maxLineChars = std::max(maxLineChars, int(end - start));
      if (nl < 0) {
        break;
      }
      start = nl + 1;
    }
    if (avgCharWidth * maxLineChars > innerWidth + 0.5) {
      reservedStrip = BlockLayout::scrollBarStripHeight(theme);
    }
  } else {
    const qreal charsPerLine = std::max(qreal(1.0), std::floor(innerWidth / avgCharWidth));
    lines = estimateWrappedLines(QStringView(literal), charsPerLine);
  }
  const qreal height = std::ceil(lines * lineHeight + padding.top() + padding.bottom() + 2.0 + reservedStrip);
  // Math/HTML rendered size is not derivable from metrics; tree-sitter only colors code.
  const bool mustMeasure = isMath || node.type() == BlockType::HtmlBlock;
  return {height, mustMeasure};
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateTable(const MarkdownNode& node, const RenderTheme& theme, qreal width) const {
  const int rowCount = static_cast<int>(node.children().size());
  int columnCount = 0;
  for (const auto& row : node.children()) {
    columnCount = std::max(columnCount, static_cast<int>(row->children().size()));
  }
  if (rowCount == 0 || columnCount == 0) {
    return {QFontMetricsF(theme.paragraphFont()).height(), false};
  }
  // O(1) per cell: equal-split column widths + each cell's source char count, instead of
  // tableColumnWidths (which materializes every cell's inline text) and per-cell plainTextForInlines.
  // The build path (buildTable) still resolves exact column widths via tableColumnWidths; this
  // estimate only sizes the scrollbar (mustMeasure/promotion resolve exact heights).
  const QMarginsF padding = theme.tableCellPadding();
  const qreal columnWidth = width / columnCount;
  const qreal innerWidth = std::max<qreal>(1.0, columnWidth - padding.left() - padding.right());
  const QFont paraFont = theme.paragraphFont();
  const QFont headFont = theme.headingFont(6);
  const qreal paraLineHeight = estimateLineHeight(paraFont);
  const qreal headLineHeight = estimateLineHeight(headFont);
  const qreal paraCharsPerLine = std::max(qreal(1.0), std::floor(innerWidth / avgCharWidthForFont(paraFont)));
  const qreal headCharsPerLine = std::max(qreal(1.0), std::floor(innerWidth / avgCharWidthForFont(headFont)));
  qreal total = 0;
  for (const auto& row : node.children()) {
    qreal rowHeight = QFontMetricsF(paraFont).height() + padding.top() + padding.bottom();
    const bool header = row->tableRowIsHeader();
    for (const auto& cell : row->children()) {
      const qsizetype charCount = cell->sourceRange().byteLength();
      const qreal lines = estimateWrappedLinesFromCharCount(charCount, header ? headCharsPerLine : paraCharsPerLine);
      const qreal cellHeight = lines * (header ? headLineHeight : paraLineHeight) + padding.top() + padding.bottom();
      rowHeight = std::max(rowHeight, cellHeight);
    }
    total += rowHeight;
  }
  return {total, false};  // mustMeasure dropped — unconsumed by DocumentLayout
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateDefinition(const MarkdownNode& node, const RenderTheme& theme, qreal width) const {
  Q_UNUSED(width);
  // Definition blocks are rare (footnote/link defs) and render precise token rects;
  // measure them when they come into view. One line is a safe placeholder.
  const qreal lineHeight = estimateLineHeight(theme.paragraphFont());
  qreal height = lineHeight;
  if (node.type() == BlockType::FootnoteDefinition && !node.literal().isEmpty()) {
    // Multi-line footnote continuation: rough estimate from source line count.
    height += lineHeight * (static_cast<qreal>(node.literal().count(QLatin1Char('\n'))) + 1.0);
  }
  return {height, true};
}

}  // namespace muffin
