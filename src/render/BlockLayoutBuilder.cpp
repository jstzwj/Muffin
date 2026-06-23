#include "render/BlockLayoutBuilder.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "blocks/html/HtmlSanitizer.h"
#include "document/BlockPredicates.h"
#include "document/PendingBlockMarker.h"
#include "document/SourceRangeUtil.h"
#include "projection/InlineProjection.h"
#include "spellcheck/SpellChecker.h"

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
namespace {

Q_LOGGING_CATEGORY(blockBuildPerf, "muffin.perf", QtWarningMsg)

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

// markdown/showLineNumbers (default off): reserve a left gutter in code fences for line numbers.
bool showLineNumbersEnabled() {
  return QSettings().value(QStringLiteral("markdown/showLineNumbers"), false).toBool();
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

// markdown/codeBlockWrap (default on): whether code-fence source lines soft-wrap. Mirrors the
// same-named helper in BlockLayout.cpp so build-time height/scroll math and paint agree.
bool codeBlockWrapEnabled() {
  return QSettings().value(QStringLiteral("markdown/codeBlockWrap"), true).toBool();
}

// markdown/breakOnSingleNewline (default on): render a single '\n' soft break as a line break
// (Obsidian/Typora) instead of joining it into the paragraph (CommonMark). Read at build time so a
// preference toggle + refreshVisibleBlocks re-renders without a reparse — same model as codeBlockWrap.
bool breakOnSingleNewlineEnabled() {
  return QSettings().value(QStringLiteral("markdown/breakOnSingleNewline"), true).toBool();
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

bool isEmptyDocumentParagraph(const QString& markdown, const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  return markdown.isEmpty() && node.type() == BlockType::Paragraph && range.byteStart == 0 && range.byteEnd == 0;
}

QVector<qreal> tableColumnWidths(const MarkdownNode& table, const RenderTheme& theme, qreal width) {
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
      preferred = qMax(preferred, maxLiteralLineWidth(InlineProjection::plainTextForInlines(cell.inlines(), breakOnSingleNewlineEnabled()), font));
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

void BlockLayoutBuilder::setMarkdownText(const QString& markdownText, const LineStartOffsetCache& lineOffsets) {
  markdownText_ = &markdownText;  // non-owning view; the document outlives the build
  lineOffsets_ = &lineOffsets;
}

const QString& BlockLayoutBuilder::md() const {
  return *markdownText_;  // defaults to emptyText_; configureBuilder refreshes it before each build
}

void BlockLayoutBuilder::setSelection(SelectionRange selection) {
  selection_ = selection;
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

BlockLayoutBuilder::BlockLayoutBuilder() : perfEnabled_(blockBuildPerf().isDebugEnabled()) {}

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
  if (isEmptyDocumentParagraph(md(), node)) {
    layout->setPlaceholderText(QCoreApplication::translate("muffin::BlockLayoutBuilder", "Start writing..."));
  }

  auto inlineLayout = std::make_unique<InlineLayout>();
  const QFont font = node.type() == BlockType::Heading ? theme.headingFont(node.headingLevel()) : theme.paragraphFont();
  const QMarginsF headingPadding = node.type() == BlockType::Heading ? theme.headingPadding(node.headingLevel()) : QMarginsF();
  // Text width accounts for heading padding so content wraps within the padded area.
  // The rect itself stays at the original x position so hitTest/cursor calculations
  // remain consistent with the paint offset.
  const qreal textWidth = qMax<qreal>(1.0, width - headingPadding.left() - headingPadding.right());
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
  options.breakOnSingleNewline = breakOnSingleNewlineEnabled();
  // Per-heading text colour from the theme (CSS themes give h1-h6 their
  // own colours). Invalid for themes that don't → falls back to textColor.
  if (node.type() == BlockType::Heading) {
    options.baseTextColor = theme.headingColor(node.headingLevel());
  }
  options.lineHeightMultiplier = theme.lineHeightMultiplier(node.type(), node.headingLevel());
  options.alignment = theme.textAlignment(node.type(), node.headingLevel());
  {
    BuildAccumTimer t(inlineLayoutNs_, perfEnabled_);
    inlineLayout->build(node.inlines(), editableSource, theme, textWidth, font, options);
  }
  qreal height = inlineLayout->height();
  if (node.type() == BlockType::Heading &&
      ((theme.headingBorderBottomColor(node.headingLevel()).isValid() && theme.headingBorderBottomWidth(node.headingLevel()) > 0.0) ||
       hasHeadingAfterDecoration(theme, node.headingLevel()))) {
    height += theme.blockSpacing() * 0.35;
  }
  layout->setContentSourceStart(projectionBase);
  layout->setRect(QRectF(x, y, width, height));


  layout->setInlineLayout(std::move(inlineLayout));
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

  const qreal childX = node.type() == BlockType::BlockQuote ? x + theme.blockQuoteIndent() : x;
  const qreal childWidth = node.type() == BlockType::BlockQuote ? width - theme.blockQuoteIndent() : width;
  qreal cursorY = y;
  std::vector<std::unique_ptr<BlockLayout>> children;

  for (const auto& child : node.children()) {
    auto childLayout = build(*child, theme, childX, cursorY, childWidth, depth + 1);
    cursorY = childLayout->rect().bottom() + theme.blockSpacing();
    children.push_back(std::move(childLayout));
  }

  const qreal height = children.empty() ? QFontMetricsF(theme.paragraphFont()).height() : qMax<qreal>(0, cursorY - y - theme.blockSpacing());
  layout->setRect(QRectF(x, y, width, height));
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
    layout->setListMarker(textForListMarker(*listParent, itemIndex));
    layout->setListMarkerKind(markerKindForListItem(node));
  } else {
    layout->setListMarkerKind(BlockLayout::ListMarkerKind::BulletDisc);
    layout->setListMarker(QStringLiteral("•"));
  }

  const qreal markerGap = theme.listIndent() * 0.2;
  qreal contentIndent = theme.listIndent();
  if (layout->listMarkerKind() == BlockLayout::ListMarkerKind::OrderedText && listParent) {
    const QFontMetricsF metrics(theme.paragraphFont());
    qreal widestMarker = 0.0;
    for (qsizetype index = 0; index < static_cast<qsizetype>(listParent->children().size()); ++index) {
      widestMarker = qMax(widestMarker, metrics.horizontalAdvance(textForListMarker(*listParent, index)));
    }
    contentIndent = qMax(theme.listIndent(), widestMarker + markerGap);
  }
  layout->setListContentIndent(contentIndent);

  const qreal contentX = x + contentIndent;
  const qreal contentWidth = qMax<qreal>(1.0, width - contentIndent);
  qreal cursorY = y;

  auto inlineLayout = std::make_unique<InlineLayout>();
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
  options.breakOnSingleNewline = breakOnSingleNewlineEnabled();
  {
    BuildAccumTimer t(inlineLayoutNs_, perfEnabled_);
    inlineLayout->build(primaryInlinesForListItem(node), listSourceText, theme, contentWidth, theme.paragraphFont(), options);
  }
  layout->setInlineLayout(std::move(inlineLayout));

  qreal height = layout->inlineLayout() ? layout->inlineLayout()->height() : QFontMetricsF(theme.paragraphFont()).height();
  std::vector<std::unique_ptr<BlockLayout>> children;

  bool skippedPrimaryParagraph = false;
  for (const auto& child : node.children()) {
    if (!skippedPrimaryParagraph && child->type() == BlockType::Paragraph) {
      skippedPrimaryParagraph = true;
      continue;
    }
    cursorY = y + height + theme.blockSpacing();
    auto childLayout = build(*child, theme, contentX, cursorY, contentWidth, depth + 1);
    height = childLayout->rect().bottom() - y;
    children.push_back(std::move(childLayout));
  }

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
      (node.type() == BlockType::CodeFence && showLineNumbersEnabled())
          ? codeLineNumberGutterWidth(layout->literal(), theme)
          : 0.0;
  layout->setLineNumberGutterWidth(lineNumberGutter);
  // Code fences honor markdown/codeBlockWrap; other literal blocks always wrap.
  const bool codeWrap = node.type() == BlockType::CodeFence ? codeBlockWrapEnabled() : true;
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
    if (htmlResult->valid()) {
      height = std::ceil(htmlResult->size().height() + theme.codePadding().top() + theme.codePadding().bottom());
      layout->setHtmlLayout(std::move(htmlResult));
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

  const QVector<qreal> columnWidths = tableColumnWidths(node, theme, width);
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
      options.breakOnSingleNewline = breakOnSingleNewlineEnabled();
  options.smartPunct = smartPunctRenderOptions();
      {
        BuildAccumTimer t(inlineLayoutNs_, perfEnabled_);
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
  const qreal lineHeight = std::ceil(metrics.height() * 1.16);
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

}  // namespace

qreal BlockLayoutBuilder::estimateLineHeight(const QFont& font) const {
  // Matches InlineLayout's per-line height: ceil(QFontMetricsF::height() * 1.16).
  return std::ceil(QFontMetricsF(font).height() * 1.16);
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
  const QFont font = isHeading ? theme.headingFont(node.headingLevel()) : theme.paragraphFont();
  const qreal lineHeight = estimateLineHeight(font);
  const QString text = InlineProjection::plainTextForInlines(node.inlines(), breakOnSingleNewlineEnabled());
  const qreal charsPerLine = std::max(qreal(1.0), std::floor(std::max<qreal>(1.0, width) / avgCharWidthForText(QStringView(text), font)));
  qreal height = estimateWrappedLines(QStringView(text), charsPerLine) * lineHeight;
  if (isHeading && node.headingLevel() <= 2) {
    height += theme.blockSpacing() * 0.35;
  }
  return {height, inlinesContainSizedContent(node.inlines())};
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateContainer(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const {
  if (depth > 64) {
    return {estimateLineHeight(theme.paragraphFont()), true};
  }
  if (node.children().empty()) {
    return {QFontMetricsF(theme.paragraphFont()).height(), false};
  }
  const bool isQuote = node.type() == BlockType::BlockQuote;
  const qreal childWidth = isQuote ? std::max<qreal>(1.0, width - theme.blockQuoteIndent()) : width;
  qreal total = 0;
  bool mustMeasure = false;
  for (const auto& child : node.children()) {
    const EstimateResult r = estimateHeight(*child, theme, childWidth, depth + 1);
    total += r.height;
    mustMeasure = mustMeasure || r.mustMeasure;
  }
  total += theme.blockSpacing() * static_cast<qreal>(static_cast<qsizetype>(node.children().size()) - 1);
  return {total, mustMeasure};
}

BlockLayoutBuilder::EstimateResult BlockLayoutBuilder::estimateListItem(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const {
  const MarkdownNode* listParent = node.parent();
  qreal contentIndent = theme.listIndent();
  const bool ordered = listParent && listParent->listKind() == ListKind::Ordered;
  if (ordered && listParent) {
    const QFontMetricsF metrics(theme.paragraphFont());
    qreal widestMarker = 0;
    for (qsizetype i = 0; i < static_cast<qsizetype>(listParent->children().size()); ++i) {
      widestMarker = std::max(widestMarker, metrics.horizontalAdvance(textForListMarker(*listParent, i)));
    }
    contentIndent = std::max(theme.listIndent(), widestMarker + theme.listIndent() * 0.2);
  }
  const qreal contentWidth = std::max<qreal>(1.0, width - contentIndent);

  const QVector<InlineNode> primary = primaryInlinesForListItem(node);
  qreal height = QFontMetricsF(theme.paragraphFont()).height();
  bool mustMeasure = false;
  if (!primary.isEmpty()) {
    const QFont font = theme.paragraphFont();
    const QString text = InlineProjection::plainTextForInlines(primary, breakOnSingleNewlineEnabled());
    const qreal charsPerLine = std::max(qreal(1.0), std::floor(contentWidth / avgCharWidthForText(QStringView(text), font)));
    height = estimateWrappedLines(QStringView(text), charsPerLine) * estimateLineHeight(font);
    mustMeasure = inlinesContainSizedContent(primary);
  }

  bool skippedPrimary = false;
  for (const auto& child : node.children()) {
    if (!skippedPrimary && child->type() == BlockType::Paragraph) {
      skippedPrimary = true;
      continue;
    }
    const EstimateResult r = estimateHeight(*child, theme, contentWidth, depth + 1);
    height += theme.blockSpacing() + r.height;
    mustMeasure = mustMeasure || r.mustMeasure;
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
      (node.type() == BlockType::CodeFence && showLineNumbersEnabled()) ? codeLineNumberGutterWidth(literal, theme) : 0.0;
  const qreal innerWidth = std::max<qreal>(1.0, width - padding.left() - padding.right() - lineNumberGutter);
  const qreal avgCharWidth = avgCharWidthForText(QStringView(literal), font);
  qreal lines;
  qreal reservedStrip = 0.0;
  if (node.type() == BlockType::CodeFence && !codeBlockWrapEnabled()) {
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
  const QVector<qreal> columnWidths = tableColumnWidths(node, theme, width);
  const QMarginsF padding = theme.tableCellPadding();
  const QFont paraFont = theme.paragraphFont();
  const QFont headFont = theme.headingFont(6);
  const qreal paraLineHeight = estimateLineHeight(paraFont);
  const qreal headLineHeight = estimateLineHeight(headFont);
  qreal total = 0;
  bool mustMeasure = false;
  for (const auto& row : node.children()) {
    qreal rowHeight = QFontMetricsF(theme.paragraphFont()).height() + padding.top() + padding.bottom();
    int column = 0;
    for (const auto& cell : row->children()) {
      const qreal columnWidth = column < columnWidths.size() ? columnWidths.at(column) : width / columnCount;
      const qreal innerWidth = std::max<qreal>(1.0, columnWidth - padding.left() - padding.right());
      const bool header = row->tableRowIsHeader();
      const QString text = InlineProjection::plainTextForInlines(cell->inlines(), breakOnSingleNewlineEnabled());
      const qreal charsPerLine = std::max(qreal(1.0), std::floor(innerWidth / avgCharWidthForText(QStringView(text), header ? headFont : paraFont)));
      const qreal lines = estimateWrappedLines(QStringView(text), charsPerLine);
      const qreal cellHeight = lines * (header ? headLineHeight : paraLineHeight) + padding.top() + padding.bottom();
      rowHeight = std::max(rowHeight, cellHeight);
      if (inlinesContainSizedContent(cell->inlines())) {
        mustMeasure = true;
      }
      ++column;
    }
    total += rowHeight;
  }
  return {total, mustMeasure};
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
