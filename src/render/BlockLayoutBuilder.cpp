#include "render/BlockLayoutBuilder.h"
#include "theme/MarkdownFlow.h"
#include "render/BlockLayoutRules.h"
#include "render/BlockFormattingCache.h"
#include "theme/NodeCssElement.h"
#include "render/LayoutBox.h"
#include "render/RenderMetrics.h"
#include "diagnostics/ScopedPerfProbe.h"

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
#include "render/TextLayout.h"
#include <QLoggingCategory>
#include <QSettings>
#include <QScopedValueRollback>
#include "render/LayoutResources.h"
#include <QDataStream>
#include <QCryptographicHash>
#include <QStringList>
#include <QStringView>
#include <QTextOption>

#include <functional>
#include <utility>

namespace muffin {
using namespace muffin::mermaid::editor;
using namespace layout_rules;
namespace {

Q_LOGGING_CATEGORY(blockBuildPerf, "muffin.perf", QtWarningMsg)
Q_LOGGING_CATEGORY(blockBuildDetailPerf, "muffin.perf.detail", QtWarningMsg)

qreal containerSourceLineHeight(const MarkdownNode& node, const RenderTheme& theme, const QString& key) {
  const auto font = theme.textFontForElement(key, &node);
  const auto multiplier = theme.lineHeightMultiplierForElement(key, &node);
  return multiplier > 0 ? cssLineHeightPx(font.pointSizeF(), multiplier)
                        : std::ceil(TextFontMetrics(font).height() * kLineHeightFactor);
}

bool isOrderedListStyle(const QString& style) {
  static const QSet<QString> ordered = {
      QStringLiteral("decimal"),     QStringLiteral("decimal-leading-zero"), QStringLiteral("lower-alpha"),
      QStringLiteral("upper-alpha"), QStringLiteral("lower-latin"),          QStringLiteral("upper-latin"),
      QStringLiteral("lower-roman"), QStringLiteral("upper-roman"),          QStringLiteral("lower-greek")};
  return ordered.contains(style);
}

// markdown/convertOnRendering + the smart-quotes/dashes sub-toggles drive display-only SmartyPants
// conversion. Read at build time so a menu/preference toggle + refreshVisibleBlocks re-renders
// without a reparse. Mirrors how the input path (InputController) interprets the same keys.
SmartPunctRenderOptions smartPunctRenderOptions() {
  diag::ScopedPerfProbe perf("block.smartPunctSettings", blockBuildDetailPerf());
  QSettings settings;
  SmartPunctRenderOptions opts;
  const bool rendering = settings.value(QStringLiteral("markdown/convertOnRendering"), false).toBool();
  opts.convertQuotes = rendering && settings.value(QStringLiteral("markdown/smartQuotes"), false).toBool();
  opts.convertDashes = rendering && settings.value(QStringLiteral("markdown/smartDashes"), false).toBool();
  opts.convertEllipsis = opts.convertDashes; // ellipsis rides on Smart Dashes, matching other editors
  opts.doubleQuoteStyle = settings.value(QStringLiteral("markdown/doubleQuoteStyle"), 0).toInt();
  opts.singleQuoteStyle = settings.value(QStringLiteral("markdown/singleQuoteStyle"), 0).toInt();
  return opts;
}

// Pixel width of the widest physical line in `literal` under `font`. Drives whether a code fence is
// horizontally scrollable (wrap off) and the scrollbar thumb ratio.
qreal maxLiteralLineWidth(const QString& literal, const QFont& font) {
  const TextFontMetrics metrics(font);
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
  TextLayout layout(text.isEmpty() ? QStringLiteral(" ") : text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(option);
  layout.beginLayout();

  qreal height = 0;
  while (true) {
    TextLine line = layout.createLine();
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
  // The trailing empty line reuses the last block's IDs without editing it.
  return !selection.focus.afterBlock && nodeId.isValid() && selection.focus.blockId == nodeId &&
         selection.focus.text.nodeId == nodeId;
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
template <typename Text> bool isEmptyDocumentParagraph(const Text& markdown, const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  return markdown.isEmpty() && node.type() == BlockType::Paragraph && range.byteStart == 0 && range.byteEnd == 0;
}

QVector<qreal> tableColumnWidths(const MarkdownNode& table, const RenderTheme& theme, qreal width,
                                 bool breakOnSingleNewline) {
  int columnCount = 0;
  for (const auto& row : table.children()) {
    columnCount = qMax(columnCount, static_cast<int>(row->children().size()));
  }
  QVector<qreal> widths(columnCount, width / qMax(1, columnCount));
  if (columnCount == 0) {
    return widths;
  }

  qreal preferredTotal = 0.0;
  for (int column = 0; column < columnCount; ++column) {
    qreal preferred = 0.0;
    for (const auto& row : table.children()) {
      if (column >= static_cast<int>(row->children().size())) {
        continue;
      }
      const MarkdownNode& cell = *row->children().at(static_cast<size_t>(column));
      const QFont font =
          theme.textFontForElement(row->tableRowIsHeader() ? QStringLiteral("th") : QStringLiteral("td"), &cell);
      const auto padding = LayoutBox::insets(
          theme.elementBoxStyle(row->tableRowIsHeader() ? QStringLiteral("th") : QStringLiteral("td"), &cell, width));
      preferred =
          qMax(preferred,
               maxLiteralLineWidth(InlineProjection::plainTextForInlines(cell.inlines(), breakOnSingleNewline), font) +
                   padding.left() + padding.right());
    }
    widths[column] = preferred;
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

} // namespace

void BlockLayoutBuilder::setMarkdownText(const PieceTable& markdownText, const LineStartOffsetCache& lineOffsets) {
  markdownText_ = &markdownText; // non-owning view; the document outlives the build
  lineOffsets_ = &lineOffsets;
}

const PieceTable& BlockLayoutBuilder::md() const {
  return *markdownText_; // defaults to emptyText_; configureBuilder refreshes it before each build
}

void BlockLayoutBuilder::setSelection(SelectionRange selection) {
  selection_ = selection;
  heightEstimator_.setSelection(selection_);
}

void BlockLayoutBuilder::setPreedit(QString text, QVector<QTextLayout::FormatRange> formats, int cursor) {
  preeditText_ = std::move(text);
  preeditFormats_ = std::move(formats);
  preeditCursor_ = cursor;
}

void BlockLayoutBuilder::applyPreedit(InlineLayout::BuildOptions& options) const {
  options.styleCache = &inlineStyleCache_;
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

void BlockLayoutBuilder::setHeadingCounterText(const QHash<NodeId, QPair<QString, QString>>* map) {
  headingCounterText_ = map;
}

void BlockLayoutBuilder::setTocEntries(const QVector<OutlineEntry>* entries) {
  tocEntries_ = entries;
}

BlockLayoutBuilder::BlockLayoutBuilder() : perfEnabled_(blockBuildPerf().isDebugEnabled()) {
}

void BlockLayoutBuilder::refreshRenderSettings() {
  const auto fonts = LayoutResources::instance().read(LayoutResources::fontKey()).geometry;
  if (fonts != fontGeneration_) {
    fontGeneration_ = fonts;
  }
  // One QSettings hit per setting per layout pass, not per block. See header note.
  QSettings s;
  renderSettingsSignature_.clear();
  QDataStream settingsStream(&renderSettingsSignature_, QIODevice::WriteOnly);
  breakOnSingleNewline_ = s.value(QStringLiteral("markdown/breakOnSingleNewline"), true).toBool();
  codeBlockWrap_ = s.value(QStringLiteral("markdown/codeBlockWrap"), true).toBool();
  showLineNumbers_ = s.value(QStringLiteral("markdown/showLineNumbers"), false).toBool();
  renderEmoji_ = s.value(QStringLiteral("markdown/renderEmoji"), true).toBool();
  renderDiagrams_ = s.value(QStringLiteral("markdown/diagrams"), true).toBool();
  showMermaidAsSource_ = s.value(QStringLiteral("editor/showMermaidAsSource"), false).toBool();
  const auto punctuation = smartPunctRenderOptions();
  settingsStream << breakOnSingleNewline_ << codeBlockWrap_ << showLineNumbers_ << renderEmoji_ << renderDiagrams_
                 << showMermaidAsSource_ << punctuation.convertQuotes << punctuation.convertDashes
                 << punctuation.convertEllipsis << punctuation.doubleQuoteStyle << punctuation.singleQuoteStyle;
  // The estimate caches are keyed by elementKey|headingLevel only (no theme dimension), so a theme
  // switch would otherwise keep serving the previous theme's lineHeight/avgCharWidth. Clearing here
  // (once per layout pass) keeps them fresh: they re-populate within a single estimate pass — many
  // blocks share the same elementKey — but never leak across passes or theme changes.
  // A changed font resource can replace a face without changing QFont::key().
  heightEstimator_.beginPass(fonts, codeBlockWrap_, showLineNumbers_);
  // Same per-pass freshness reasoning: an ordered list's widest marker can change with the
  // theme (decimal → roman) or with item add/remove, both of which force a rebuild pass.
  listMarkerLayouts_.clear();
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

void BlockLayoutBuilder::retainLayouts(std::unique_ptr<BlockLayout>& layout) {
  if (!layout)
    return;
  retainedLayouts_.insert(layout->nodeId(), &layout);
  for (auto& child : layout->children())
    retainLayouts(child);
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::build(const MarkdownNode& node, const RenderTheme& theme, qreal x,
                                                       qreal y, qreal width, int depth) {
  if (!materializing_)
    return buildFresh(node, theme, x, y, width, depth);
  LayoutResourceScope resources;
  LayoutResources::instance().read(LayoutResources::fontKey());
  const auto signature = formattingSignature(node, theme);
  QByteArray constraints;
  QDataStream stream(&constraints, QIODevice::WriteOnly);
  stream << width << depth << allocations_.contains(&node);
  if (allocations_.contains(&node))
    stream << allocations_.value(&node).first << allocations_.value(&node).second;
  stream << cssMeasureKey({width, width, -1, -1, gridInheritance_.value(&node), CssLayoutPhase::Final});
  const auto old = retainedLayouts_.value(node.id(), nullptr);
  if (old && *old && !signature.isEmpty() && (*old)->reuse.signature == signature &&
      (*old)->reuse.constraints == constraints && LayoutResources::instance().matches((*old)->reuse.resources)) {
    auto result = std::move(*old);
    const auto delta = QPointF(x, y) - result->reuse.origin;
    result->translate(delta.x(), delta.y());
    result->shiftSourceOffsets(node.sourceRange().byteStart - result->reuse.sourceStart);
    result->reuse.sourceStart = node.sourceRange().byteStart;
    result->reuse.origin = {x, y};
    LayoutResources::instance().record(result->reuse.resources);
    ++reusedLayouts;
    return result;
  }
  auto result = buildFresh(node, theme, x, y, width, depth);
  result->reuse = {signature, constraints, resources.dependencies, node.sourceRange().byteStart, {x, y}};
  ++builtLayouts;
  return result;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildFresh(const MarkdownNode& node, const RenderTheme& theme, qreal x,
                                                            qreal y, qreal width, int depth) {
  diag::ScopedPerfProbe perf("block.buildFresh", blockBuildDetailPerf());
  const auto build = [&]() -> std::unique_ptr<BlockLayout> {
    const auto flow = theme.markdownFlow(node);
    const auto key = flow->container(node) ? flow->container(node)->attributes.tag : cssTagForNode(node);
    const auto* style = key.isEmpty() ? nullptr : theme.elementStyleForNode(node, key);
    if (style && style->layout.display == "none") {
      auto hidden = std::make_unique<BlockLayout>(node.id());
      hidden->setType(node.type());
      hidden->setRect({x, y, 0, 0});
      return hidden;
    }
    if (style && style->layout.establishesFormattingContext() && (!node.children().empty() || flow->container(node)))
      return buildFormattingContainer(node, theme, x, y, width, depth);
    if (flow->container(node))
      return buildContainer(node, theme, x, y, width, depth);
    if (flow->closingBoundary(node) && editingHtmlBlockId_ != node.id()) {
      auto boundary = std::make_unique<BlockLayout>(node.id());
      boundary->setType(BlockType::HtmlBlock);
      boundary->setHtmlContainer(true);
      boundary->setLiteral(node.literal());
      boundary->setRect({x, y, width, 0});
      return boundary;
    }
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
  };
  auto result = build();
  if (result)
    result->setLayoutSelection(selection_);
  if (result && result->rect().width() > 0) {
    diag::ScopedPerfProbe generatedPerf("block.generatedContent", blockBuildDetailPerf());
    result->layoutGeneratedContent(
        node, theme, headingCounterText_ ? headingCounterText_->value(node.id()) : QPair<QString, QString>());
  }
  return result;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildParagraphLike(const MarkdownNode& node, const RenderTheme& theme,
                                                                    qreal x, qreal y, qreal width, int depth) {
  diag::ScopedPerfProbe perf("block.paragraph", blockBuildDetailPerf());
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
    if (it != headingCounterText_->constEnd()) {
      layout->setHeadingBeforeText(it.value().first);
    }
  }
  if (isEmptyDocumentParagraph(md(), node)) {
    layout->setPlaceholderText(QCoreApplication::translate("muffin::BlockLayoutBuilder", "Start writing..."));
  }

  auto inlineLayout = std::make_unique<InlineLayout>();
  const QString elementKey = node.type() == BlockType::Heading
                                 ? QStringLiteral("h%1").arg(node.headingLevel())
                                 : (isInsideBlockquote(node) ? QStringLiteral("blockquote p") : QStringLiteral("p"));
  const qreal containingWidth = width;
  const auto usedBox = boxFor(node, theme, elementKey, containingWidth);
  const auto* resolvedStyle = theme.elementStyleForNode(node, elementKey);
  const auto margins = usedBox.margin;
  x += margins.left();
  width = qMax<qreal>(1, width - margins.left() - margins.right());
  const qreal availableWidth = width;
  width = LayoutBox::borderWidth(usedBox, availableWidth);
  const QFont font = theme.textFontForElement(elementKey, &node);
  const auto insets = LayoutBox::insets(usedBox);
  // Generated heading pseudo-elements are part of InlineLayout now. The text
  // width must be the complete content box; subtracting a guessed marker
  // advance here would make wrapping and hit testing disagree with paint.
  const qreal textWidth = qMax<qreal>(1, width - insets.left() - insets.right());
  const auto intrinsicKind = resolvedStyle ? resolvedStyle->layout.sizes[0] : CssIntrinsicSize::Auto;
  const bool intrinsic = !allocations_.contains(&node) && (intrinsicKind == CssIntrinsicSize::MinContent ||
                                                           intrinsicKind == CssIntrinsicSize::MaxContent ||
                                                           intrinsicKind == CssIntrinsicSize::FitContent);
  // A heading projects content-only: its `# ` prefix region [blockStart, contentStart) is never part
  // of the editable projection (it is empty for Setext headings, whose byteStart == contentStart).
  // The level is conveyed by font size and changed via the heading-level commands, so the prefix is
  // not needed for editing. The prefix still shows while the user is typing it, because at that
  // point the line is a Paragraph/pending marker, not yet a Heading node. Keeping projectionBase at
  // contentStart for both the active and inactive cases means document<->local offset conversion
  // stays identical — this base MUST match what is stored on the layout (contentSourceStart).
  const qsizetype contentStart = sourceContentStartForEditableNode(node);
  const qsizetype projectionBase = contentStart;
  QString editableSource = [&] {
    diag::ScopedPerfProbe sourcePerf("block.source", blockBuildDetailPerf());
    return sourceTextForEditableNode(node);
  }();
  InlineLayout::BuildOptions options;
  options.documentPath = documentPath_;
  options.imageResources = imageResources_;
  options.projectionState = InlineProjectionState::forSelection(selection_, node.id(), projectionBase);
  // Inlines are stored relative to the owning top-level block's byteStart. The projection wants
  // content-local spans, so sourceBase = content offset within the block (contentStart - the
  // top-level block's byteStart). localRange then yields the same content-local spans as before.
  // projectionBase (setContentSourceStart / forSelection) stays ABSOLUTE — cursor math is absolute.
  options.sourceBase = projectionBase - node.topLevelBlock()->sourceRange().byteStart;
  if (isInsideBlockquote(node)) {
    // Only hide continuation prefixes outside parser-provided inline ranges.
    qsizetype candidate = 0;
    for (qsizetype line = editableSource.indexOf('\n'); line >= 0; line = editableSource.indexOf('\n', line + 1)) {
      qsizetype end = line + 1;
      while (end < editableSource.size() && (editableSource[end] == ' ' || editableSource[end] == '\t'))
        ++end;
      bool quote = false;
      while (end < editableSource.size() && editableSource[end] == '>') {
        quote = true;
        ++end;
        if (end < editableSource.size() && editableSource[end] == ' ')
          ++end;
      }
      if (!quote)
        continue;
      while (candidate < node.inlines().size() &&
             (node.inlines()[candidate].sourceEnd() <= options.sourceBase + line + 1 ||
              node.inlines()[candidate].type() == InlineType::SoftBreak ||
              node.inlines()[candidate].type() == InlineType::LineBreak))
        ++candidate;
      const bool parsedText =
          candidate < node.inlines().size() && node.inlines()[candidate].sourceStart() < options.sourceBase + end;
      if (!parsedText)
        options.sourceReplacements.push_back({{line + 1, end}, {}});
    }
    const auto* parent = node.parent();
    if (parent && parent->alertKind() != AlertKind::None && !parent->children().empty() &&
        parent->children().front().get() == &node && !node.inlines().isEmpty()) {
      const auto marker = node.inlines().front().text();
      if (marker.startsWith("[!") && marker.endsWith(']')) {
        const auto start = node.inlines().front().sourceStart() - options.sourceBase;
        options.sourceReplacements.push_back({{start, start + marker.size()}, marker.mid(2, marker.size() - 3), true});
        options.breakOnSingleNewline = true;
      }
    }
  }
  options.pendingPrefixLength = pendingPrefixLengthFor(node, editableSource);
  options.isMisspelled = spellMisspelledPredicate();
  options.smartPunct = smartPunctRenderOptions();
  options.breakOnSingleNewline = options.breakOnSingleNewline || breakOnSingleNewline_;
  options.renderEmoji = renderEmoji_;
  options.styleNode = &node;
  options.pseudoBefore = theme.pseudoForNode(node, QStringLiteral("before"));
  options.pseudoAfter = theme.pseudoForNode(node, QStringLiteral("after"));
  options.pseudoBeforeText = layout->headingBeforeText();
  if (headingCounterText_)
    options.pseudoBeforeText = headingCounterText_->value(node.id()).first;
  if (headingCounterText_)
    options.pseudoAfterText = headingCounterText_->value(node.id()).second;
  if (resolvedStyle) {
    options.anywhereMinimum =
        resolvedStyle->layout.overflowWrap == "anywhere" || resolvedStyle->layout.wordBreak == "break-all";
    options.wrapMode = resolvedStyle->layout.wordBreak == "break-all"   ? QTextOption::WrapAnywhere
                       : resolvedStyle->layout.overflowWrap == "normal" ? QTextOption::WordWrap
                                                                        : QTextOption::WrapAtWordBoundaryOrAnywhere;
  }
  options.baseTextColor = theme.textColorForElement(elementKey, &node);
  if (const auto* hovered = theme.elementStyleForNode(node, elementKey + QStringLiteral(":hover")))
    options.hoverTextColor = hovered->paint.color;
  if (const auto* focused = theme.elementStyleForNode(node, elementKey + QStringLiteral(":focus")))
    options.focusTextColor = focused->paint.color;
  options.lineHeightMultiplier = theme.lineHeightMultiplierForElement(elementKey, &node);
  options.alignment = theme.textAlignmentForElement(elementKey, &node);
  options.textTransform = static_cast<TextTransform>(theme.textTransformForElement(elementKey, &node));
  options.textShadow = theme.textShadowForElement(elementKey, &node);
  {
    diag::ScopedPerfProbe inlinePerf("block.inline", blockBuildDetailPerf());
    BuildAccumTimer t(inlineLayoutNs_, perfEnabled_);
    applyPreedit(options);
    inlineLayout->build(node.inlines(), editableSource, theme, intrinsic ? 1e6 : textWidth, font, options);
  }
  if (intrinsic) {
    const auto metrics = inlineLayout->intrinsicWidths();
    const qreal extra = insets.left() + insets.right();
    const qreal content = intrinsicKind == CssIntrinsicSize::MinContent ? metrics.first
                          : intrinsicKind == CssIntrinsicSize::MaxContent
                              ? metrics.second
                              : qMax(metrics.first, qMin(metrics.second, availableWidth - extra));
    auto intrinsicBox = usedBox;
    intrinsicBox.widthFitContent = false;
    intrinsicBox.widthLength = {CssLengthStatus::Valid, content + extra - (usedBox.borderBox ? 0 : extra)};
    width = LayoutBox::borderWidth(intrinsicBox, availableWidth);
    inlineLayout->build(node.inlines(), editableSource, theme, qMax<qreal>(1, width - insets.left() - insets.right()),
                        font, options);
  }
  if (usedBox.marginLeftAuto)
    x += qMax<qreal>(0, availableWidth - width) / (usedBox.marginRightAuto ? 2 : 1);
  diag::ScopedPerfProbe geometryPerf("block.geometry", blockBuildDetailPerf());
  const qreal height = LayoutBox::borderHeight(usedBox, inlineLayout->height());
  layout->setContentSourceStart(projectionBase);
  const QRectF flowRect(x, y, width, height);
  layout->setRect(flowRect);
  auto fragment =
      LayoutBox::place(elementKey, resolvedStyle ? *resolvedStyle : ThemeElementStyle{}, usedBox, flowRect, font);
  if (const auto* style = theme.elementStyleForNode(node, elementKey + QStringLiteral(":hover")))
    fragment.hoverPaint = style->paint;
  if (const auto* style = theme.elementStyleForNode(node, elementKey + QStringLiteral(":focus")))
    fragment.focusPaint = style->paint;
  const qreal overflow = std::max({fragment.style.paint.boxShadowBlur, fragment.hoverPaint.boxShadowBlur,
                                   fragment.focusPaint.boxShadowBlur, fragment.style.paint.filterBlur + 2});
  fragment.visualOverflow = fragment.borderBox.adjusted(-overflow, -overflow, overflow, overflow);
  fragment.visualOverflow =
      fragment.visualOverflow.united(inlineLayout->visualTextBounds().translated(fragment.inlineTextOrigin));
  layout->setCssBoxGeometry(std::move(fragment));

  layout->setInlineLayout(std::move(inlineLayout));
  return layout;
}

ThemeElementBoxStyle BlockLayoutBuilder::boxFor(const MarkdownNode& node, const RenderTheme& theme, const QString& key,
                                                qreal containingWidth) const {
  const auto allocation = allocations_.constFind(&node);
  auto box =
      theme.elementBoxStyle(key, &node, allocation == allocations_.cend() ? containingWidth : allocation->second);
  if (allocation == allocations_.cend())
    return box;
  box.margin = {};
  box.marginLengths = {};
  box.marginLeftAuto = box.marginRightAuto = false;
  box.borderBox = true;
  box.widthFitContent = false;
  box.widthLength = {CssLengthStatus::Valid, allocation->first.width()};
  box.minWidthLength = {};
  box.maxWidthLength = {};
  if (allocation->first.height() >= 0) {
    box.heightLength = {CssLengthStatus::Valid, allocation->first.height()};
    box.minHeightLength = {};
    box.maxHeightLength = {};
  }
  return box;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildAllocated(const MarkdownNode& node, const RenderTheme& theme,
                                                                QRectF allocation, qreal containingWidth, int depth,
                                                                const CssGridInheritance& inherited) {
  const auto savedGrid = gridInheritance_.value(&node);
  const bool hadGrid = gridInheritance_.contains(&node);
  gridInheritance_.insert(&node, inherited);
  const auto saved = allocations_.value(&node);
  const bool existed = allocations_.contains(&node);
  allocations_.insert(&node, {allocation.size(), containingWidth});
  auto result = build(node, theme, allocation.x(), allocation.y(), allocation.width(), depth);
  if (existed)
    allocations_.insert(&node, saved);
  else
    allocations_.remove(&node);
  if (hadGrid)
    gridInheritance_.insert(&node, savedGrid);
  else
    gridInheritance_.remove(&node);
  return result;
}

CssIntrinsicMetrics BlockLayoutBuilder::intrinsicMetrics(const MarkdownNode& node, const RenderTheme& theme,
                                                         qreal containingWidth) {
  const auto* computed = theme.elementStyleForNode(node, cssTagForNode(node));
  if (computed && computed->layout.establishesFormattingContext() && !node.children().empty())
    return formattingItem(node, theme, containingWidth).intrinsic;
  QScopedValueRollback measuring(materializing_, false);
  const auto built = buildAllocated(node, theme, {0, 0, 1e6, -1}, containingWidth);
  if (const auto* text = built->inlineLayout()) {
    const auto widths = text->intrinsicWidths();
    return {widths.first, widths.second};
  }
  if (!node.children().empty() || theme.markdownFlow(node)->container(node)) {
    CssIntrinsicMetrics result;
    const auto* style = theme.elementStyleForNode(node, cssTagForNode(node));
    const bool row = style && style->layout.isFlex() && style->layout.direction.startsWith("row");
    for (const auto* child : theme.markdownFlow(node)->children(node)) {
      const auto metrics = intrinsicMetrics(*child, theme, containingWidth);
      result.minContent = qMax(result.minContent, metrics.minContent);
      result.maxContent = row ? result.maxContent + metrics.maxContent : qMax(result.maxContent, metrics.maxContent);
    }
    return result;
  }
  return {built->codeMaxLineWidth(), built->codeMaxLineWidth()};
}

void BlockLayoutBuilder::beginFormattingPass(bool incremental, QSet<NodeId> dirty) {
  formattingCache_.beginPass(incremental, std::move(dirty));
  retainedLayouts_.clear();
  reusedLayouts = builtLayouts = reusedContexts = solvedContexts = 0;
}

QPair<quint64, quint64> BlockLayoutBuilder::finishFormattingPass() {
  retainedLayouts_.clear();
  return formattingCache_.finishPass();
}

QByteArray BlockLayoutBuilder::formattingSignature(const MarkdownNode& node, const RenderTheme& theme) {
  diag::ScopedPerfProbe perf("block.formattingSignature", blockBuildDetailPerf());
  if (const auto cached = formattingCache_.signature(node.id()))
    return *cached;
  // Generated outlines have a separate document-wide dependency.
  if (sourceTextForEditableNode(node).trimmed() == "[TOC]")
    return {};
  QByteArray data;
  QDataStream stream(&data, QIODevice::WriteOnly);
  stream << formattingCache_.themeSignature(theme) << renderStateGeneration_;
  const auto range = node.sourceRange();
  stream << node.id().toString() << int(node.type())
         << md().mid(range.byteStart, qMax<qsizetype>(0, range.byteEnd - range.byteStart)) << node.literal()
         << theme.zoomPercent() << theme.fontSizePx() << renderSettingsSignature_ << documentPath_;
  stream << theme.paragraphFont().key() << theme.codeFont().key();
  const auto key = cssTagForNode(node);
  for (const auto& state : {QString(), QString(":hover"), QString(":focus")}) {
    const auto* style = theme.elementStyleForNode(node, key + state);
    stream << (style ? style->fingerprint : quint64(0));
  }
  for (const auto& pseudo : {QStringLiteral("before"), QStringLiteral("after")})
    for (int bits = 0; bits < 4; ++bits) {
      CssElementState state;
      state.hover = bits & 1;
      state.focus = bits & 2;
      const auto generated = theme.pseudoForElement(*theme.cssElementForNode(node), pseudo, state);
      stream << (generated && generated->computed ? generated->computed->fingerprint() : quint64(0));
    }
  const auto projection =
      InlineProjectionState::forSelection(selection_, node.id(), sourceContentStartForEditableNode(node));
  stream << projection.cursorSourceOffset << projection.cursorVisibleOffset << projection.revealMarkdownMarkers;
  if (projection.cursorSourceOffset >= 0 || projection.cursorVisibleOffset >= 0) {
    stream << preeditText_ << preeditCursor_;
    for (const auto& format : preeditFormats_)
      stream << format.start << format.length << format.format;
  }
  if (headingCounterText_)
    stream << headingCounterText_->value(node.id());
  stream << theme.generatedCounterFingerprint(node.id());
  if (node.type() == BlockType::ListItem) {
    const auto marker = listMarkerLayout(node, theme);
    stream << int(marker.marker.kind) << marker.marker.text << marker.contentIndent;
  }
  if (node.type() == BlockType::Table) {
    const auto* wrapper = theme.elementStyleForNode(node, QStringLiteral(".table-figure"));
    stream << (wrapper ? wrapper->fingerprint : quint64(0));
  }
  if (formattingCache_.isDirty(node.id()))
    stream << formattingCache_.pass();
  stream << editingHtmlBlockId_.toString() << mermaidSyncMode_;
  if (node.type() == BlockType::CodeFence && node.codeLanguage() == "mermaid" && mermaidCache_)
    stream << mermaidCache_->resourceKey(mermaidCache_->makeKey(node.literal()));
  bool reusable = true;
  const auto inlines = [&](const auto& self, const QVector<InlineNode>& nodes) -> void {
    for (const auto& inlineNode : nodes) {
      const auto offset =
          inlineNode.contentRange().isValid() ? inlineNode.contentRange().start : inlineNode.sourceRange().start;
      stream << int(inlineNode.type()) << inlineNode.text() << inlineNode.href() << inlineNode.title()
             << inlineNode.alt() << theme.inlineStyleForNode(node, offset).fingerprint;
      if (const auto* link = theme.cssInlineElement(node, offset, QStringLiteral("a")))
        for (const auto& pseudo : {QStringLiteral("before"), QStringLiteral("after")}) {
          const auto generated = theme.pseudoForElement(*link, pseudo);
          stream << (generated && generated->computed ? generated->computed->fingerprint() : quint64(0));
        }
      self(self, inlineNode.children());
    }
  };
  inlines(inlines, node.inlines());
  for (const auto* child : theme.markdownFlow(node)->children(node)) {
    const auto signature = formattingSignature(*child, theme);
    if (signature.isEmpty())
      reusable = false;
    stream << signature;
  }
  const auto result = reusable ? QCryptographicHash::hash(data, QCryptographicHash::Sha256) : QByteArray();
  formattingCache_.rememberSignature(node.id(), result);
  return result;
}

CssFormattingItem BlockLayoutBuilder::formattingItem(const MarkdownNode& node, const RenderTheme& theme,
                                                     qreal containingWidth, int depth) {
  const auto flow = theme.markdownFlow(node);
  const auto key = flow->container(node) ? flow->container(node)->attributes.tag : cssTagForNode(node);
  const auto* style = theme.elementStyleForNode(node, key);
  auto projected = style ? *style : ThemeElementStyle{};
  projected.box = theme.elementBoxStyle(key, &node, containingWidth);
  const qreal zoom = theme.zoomPercent() / 100.0;
  for (auto* sides : {&projected.box.marginLengths, &projected.box.paddingLengths})
    for (auto& length : sides->sides)
      length.px *= zoom;
  projected.layout.scaleLengths(zoom);
  CssFormattingItem item;
  item.style = projected;
  const auto signature = formattingSignature(node, theme);
  const auto cached = formattingCache_.measurement(node.id(), signature);
  LayoutResourceScope resourceScope;
  for (auto it = cached->resources.cbegin(); it != cached->resources.cend(); ++it)
    LayoutResources::instance().read(it.key());
  LayoutResources::instance().read(LayoutResources::fontKey());
  item.measurements = cached->measurements;
  if (projected.layout.establishesFormattingContext() && (!node.children().empty() || flow->container(node))) {
    item.children.emplace();
    for (const auto* child : flow->children(node)) {
      if (!omitVirtualEmptyParagraphInRenderFlow(*child, selection_))
        item.children->push_back(formattingItem(*child, theme, containingWidth, depth + 1));
    }
    item.intrinsic = projected.layout.isGrid() ? intrinsicGridWidths(projected, *item.children)
                                               : intrinsicFlexWidths(projected, *item.children);
  } else {
    if (!cached->intrinsicValid || cached->containingWidth != containingWidth) {
      cached->intrinsic = intrinsicMetrics(node, theme, containingWidth);
      cached->intrinsicValid = true;
      cached->containingWidth = containingWidth;
    }
    item.intrinsic = cached->intrinsic;
  }
  cached->resources = resourceScope.dependencies;
  item.measurementIdentity = node.id().toString();
  item.measure = [this, &node, &theme, key, depth, cached](const CssMeasureRequest& request) {
    QScopedValueRollback measuring(materializing_, false);
    LayoutResourceScope resourceScope;
    LayoutResources::instance().read(LayoutResources::fontKey());
    const auto width = request.width, reference = request.containingWidth;
    const auto& inherited = request.inherited;
    const auto inset = LayoutBox::insets(theme.elementBoxStyle(key, &node, reference));
    const auto built = buildAllocated(
        node, theme, {0, 0, width < 0 ? 1e6 : width + inset.left() + inset.right(), request.allocatedHeight}, reference,
        depth, inherited);
    for (auto it = resourceScope.dependencies.cbegin(); it != resourceScope.dependencies.cend(); ++it)
      cached->resources.insert(it.key(), it.value());
    const auto* text = built->inlineLayout();
    const qreal baseline = built->firstBaseline() < 0 ? -1 : built->firstBaseline() - inset.top();
    const qreal lastBaseline = built->lastBaseline() < 0 ? -1 : built->lastBaseline() - inset.top();
    const qreal measuredWidth = text ? text->visualTextBounds().width() : width;
    return CssMeasuredContent{{width < 0 ? measuredWidth : qMin(width, measuredWidth),
                               qMax<qreal>(0, built->height() - inset.top() - inset.bottom())},
                              baseline,
                              lastBaseline};
  };
  return item;
}

CssFormattingResult BlockLayoutBuilder::solveFormatting(NodeId id, const ThemeElementStyle& style,
                                                        const std::vector<CssFormattingItem>& items, qreal width,
                                                        qreal height, const CssGridInheritance& inherited) {
  auto solution = formattingCache_.solve(id, style, items, width, height, inherited);
  solution.reused ? ++reusedContexts : ++solvedContexts;
  return std::move(solution.result);
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildFormattingContainer(const MarkdownNode& node,
                                                                          const RenderTheme& theme, qreal x, qreal y,
                                                                          qreal width, int depth) {
  auto result = std::make_unique<BlockLayout>(node.id());
  result->setType(node.type());
  result->setDepth(depth);
  const auto flow = theme.markdownFlow(node);
  const auto key = flow->container(node) ? flow->container(node)->attributes.tag : cssTagForNode(node);
  result->setHtmlContainer(flow->container(node) != nullptr);
  result->setLiteral(node.literal());
  auto style = *theme.elementStyleForNode(node, key);
  const auto used = boxFor(node, theme, key, width);
  x += used.margin.left();
  width = LayoutBox::borderWidth(used, width - used.margin.left() - used.margin.right());
  const auto inset = LayoutBox::insets(used);
  const qreal contentWidth = qMax<qreal>(0, width - inset.left() - inset.right());
  const qreal contentHeight =
      used.heightLength.status == CssLengthStatus::Valid && !used.heightLength.hasPercentage
          ? qMax<qreal>(0, used.heightLength.px - (used.borderBox ? inset.top() + inset.bottom() : 0))
          : -1;
  const qreal zoom = theme.zoomPercent() / 100.0;
  style.layout.scaleLengths(zoom);
  style.box = used;
  qreal sourceHeight = 0;
  if (flow->container(node) && editingHtmlBlockId_ == node.id()) {
    result->setLiteralEditing(true);
    const auto font = theme.textFontForElement(key, &node);
    const LiteralTextLayout::Preedit preedit =
        selectionFocusesNode(selection_, node.id())
            ? LiteralTextLayout::Preedit{selection_.focus.text.textOffset, preeditText_, preeditFormats_,
                                         preeditCursor_}
            : LiteralTextLayout::Preedit{};
    auto source = std::make_shared<LiteralTextLayout>(node.literal(), font, contentWidth,
                                                      containerSourceLineHeight(node, theme, key), true, preedit);
    sourceHeight = source->height();
    result->setLiteralTextLayout(std::move(source));
  }
  std::vector<const MarkdownNode*> nodes;
  std::vector<CssFormattingItem> items;
  for (const auto* child : flow->children(node)) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_))
      continue;
    const auto* childStyle = theme.elementStyleForNode(*child, cssTagForNode(*child));
    if (childStyle && childStyle->layout.display == "none")
      continue;
    nodes.push_back(child);
    items.push_back(formattingItem(*child, theme, contentWidth, depth + 1));
  }
  const auto formatted = solveFormatting(node.id(), style, items, contentWidth, contentHeight,
                                         contentGridInheritance(gridInheritance_.value(&node), inset));
  std::vector<std::unique_ptr<BlockLayout>> children;
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto allocation = formatted.items[i].translated(x + inset.left(), y + inset.top() + sourceHeight);
    children.push_back(buildAllocated(*nodes[i], theme, allocation, formatted.containingWidths[i], depth + 1,
                                      formatted.inheritedGrids[i]));
  }
  qreal closingHeight = 0;
  if (const auto* container = flow->container(node)) {
    auto closing = build(*container->closing, theme, x + inset.left(),
                         y + inset.top() + sourceHeight + formatted.size.height(), contentWidth, depth + 1);
    closingHeight = closing->height();
    children.push_back(std::move(closing));
  }
  const QRectF rect(x, y, width, formatted.size.height() + inset.top() + inset.bottom() + sourceHeight + closingHeight);
  result->setRect(rect);
  result->setFormattingBaselines(formatted.firstBaseline < 0 ? -1
                                                             : formatted.firstBaseline + inset.top() + sourceHeight,
                                 formatted.lastBaseline < 0 ? -1 : formatted.lastBaseline + inset.top() + sourceHeight);
  auto box = LayoutBox::place(key, style, used, rect, theme.textFontForElement(key, &node));
  box.lineHeight = containerSourceLineHeight(node, theme, key);
  result->setCssBoxGeometry(std::move(box));
  result->setChildren(std::move(children));
  return result;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildTocPreview(const MarkdownNode& node, const RenderTheme& theme,
                                                                 qreal x, qreal y, qreal width, int depth) {
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
  const TextFontMetrics fm(font);
  qreal multiplier = theme.lineHeightMultiplierForElement(elementKey, &node);
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

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildContainer(const MarkdownNode& node, const RenderTheme& theme,
                                                                qreal x, qreal y, qreal width, int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);
  const auto flow = theme.markdownFlow(node);
  const auto* htmlContainer = flow->container(node);
  layout->setHtmlContainer(htmlContainer != nullptr);
  if (htmlContainer)
    layout->setLiteral(node.literal());
  layout->setAlertKind(node.alertKind());
  if (node.alertKind() != AlertKind::None) {
    auto plain = *theme.cssElementForNode(node);
    plain.cacheId = 0;
    plain.classes.removeIf([](const auto& name) { return name == "md-alert" || name.startsWith("md-alert-"); });
    const auto ordinary = theme.styleForElement(plain);
    const auto* styled = theme.elementStyleForNode(node, QStringLiteral("blockquote"));
    layout->setAlertThemed(styled && (styled->paint != ordinary.paint ||
                                      styled->box.borderLeftColor != ordinary.box.borderLeftColor ||
                                      styled->box.borderTopColor != ordinary.box.borderTopColor ||
                                      styled->box.borderLeftWidth != ordinary.box.borderLeftWidth ||
                                      styled->box.borderTopWidth != ordinary.box.borderTopWidth));
  }

  const bool quoteBox = node.type() == BlockType::BlockQuote || htmlContainer;
  const QString containerKey = htmlContainer ? htmlContainer->attributes.tag : QStringLiteral("blockquote");
  const ThemeElementBoxStyle qbox = quoteBox ? boxFor(node, theme, containerKey, width) : ThemeElementBoxStyle{};
  if (quoteBox) {
    x += qbox.margin.left();
    const auto availableWidth = qMax<qreal>(1, width - qbox.margin.left() - qbox.margin.right());
    width = LayoutBox::borderWidth(qbox, availableWidth);
    if (qbox.marginLeftAuto)
      x += qMax<qreal>(0, availableWidth - width) / (qbox.marginRightAuto ? 2 : 1);
  }
  const QMarginsF qpad = qbox.padding;
  const QMarginsF qborder = LayoutBox::borders(qbox);
  const qreal childX = x + qborder.left() + qpad.left();
  const qreal childWidth = qMax<qreal>(1.0, width - qborder.left() - qborder.right() - qpad.left() - qpad.right());
  qreal cursorY = y + qborder.top() + qpad.top();
  if (htmlContainer && editingHtmlBlockId_ == node.id()) {
    layout->setLiteralEditing(true);
    const QFont font = theme.textFontForElement(containerKey, &node);
    const LiteralTextLayout::Preedit preedit =
        selectionFocusesNode(selection_, node.id())
            ? LiteralTextLayout::Preedit{selection_.focus.text.textOffset, preeditText_, preeditFormats_,
                                         preeditCursor_}
            : LiteralTextLayout::Preedit{};
    auto source = std::make_shared<LiteralTextLayout>(
        layout->literal(), font, childWidth, containerSourceLineHeight(node, theme, containerKey), true, preedit);
    cursorY += source->height();
    layout->setLiteralTextLayout(std::move(source));
  }
  std::vector<std::unique_ptr<BlockLayout>> children;
  const MarkdownNode* previousChild = nullptr;
  bool omittedOnlyRenderChildren = !node.children().empty();

  const bool firstChildMarginCollapses = qFuzzyIsNull(qborder.top() + qpad.top());
  const bool lastChildMarginCollapses = qFuzzyIsNull(qborder.bottom() + qpad.bottom());
  for (const auto* child : flow->children(node)) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_)) {
      continue;
    }
    omittedOnlyRenderChildren = false;
    if (previousChild) {
      cursorY += spacingBetweenInFlow(*previousChild, *child, theme, false, childWidth);
    } else if (!firstChildMarginCollapses) {
      cursorY += spacingBeforeInFlow(*child, theme, false, childWidth);
    }
    auto childLayout = build(*child, theme, childX, cursorY, childWidth, depth + 1);
    cursorY = childLayout->rect().bottom();
    previousChild = child;
    children.push_back(std::move(childLayout));
  }
  if (htmlContainer) {
    auto closing = build(*htmlContainer->closing, theme, childX, cursorY, childWidth, depth + 1);
    cursorY = closing->rect().bottom();
    children.push_back(std::move(closing));
  }

  qreal height = 0.0;
  if (children.empty()) {
    height = quoteBox && omittedOnlyRenderChildren
                 ? qborder.top() + qpad.top() + qpad.bottom() + qborder.bottom()
                 : qborder.top() + qpad.top() + TextFontMetrics(theme.paragraphFont()).height() + qpad.bottom() +
                       qborder.bottom();
  } else {
    const qreal trailingChildMargin =
        lastChildMarginCollapses || !previousChild ? 0.0 : spacingAfterInFlow(*previousChild, theme, false, childWidth);
    height = cursorY + trailingChildMargin + qpad.bottom() + qborder.bottom() - y;
  }
  if (quoteBox)
    height = LayoutBox::borderHeight(qbox, height - qborder.top() - qpad.top() - qpad.bottom() - qborder.bottom());
  const QRectF flowRect(x, y, width, height);
  layout->setRect(flowRect);
  if (quoteBox) {
    const auto* style = theme.elementStyleForNode(node, containerKey);
    auto box = LayoutBox::place(containerKey, style ? *style : ThemeElementStyle{}, qbox, flowRect,
                                theme.textFontForElement(containerKey, &node));
    box.lineHeight = containerSourceLineHeight(node, theme, containerKey);
    layout->setCssBoxGeometry(std::move(box));
  }
  layout->setChildren(std::move(children));
  return layout;
}

BlockLayoutBuilder::ListMarkerLayout BlockLayoutBuilder::listMarkerLayout(const MarkdownNode& itemNode,
                                                                          const RenderTheme& theme) {
  if (const auto found = listMarkerLayouts_.constFind(itemNode.id()); found != listMarkerLayouts_.cend())
    return found.value();
  const ListMarkerLayout fallback{{BlockLayout::ListMarkerKind::BulletDisc, QString(QChar(0x2022))},
                                  theme.listIndent()};
  const auto* parent = itemNode.parent();
  if (!parent)
    return fallback;
  QVector<NodeId> items;
  bool hasOrderedMarker = false;
  qsizetype index = 0;
  for (const auto& child : parent->children()) {
    if (isVirtualEmptyParagraphNode(*child))
      continue;
    const auto marker = resolveListMarker(*child, theme, index++);
    items.push_back(child->id());
    hasOrderedMarker = hasOrderedMarker || marker.kind == BlockLayout::ListMarkerKind::OrderedText;
    listMarkerLayouts_.insert(child->id(), {marker, theme.listIndent()});
  }
  if (hasOrderedMarker) {
    const TextFontMetrics metrics(theme.paragraphFont());
    qreal widest = 0;
    for (const auto id : items)
      widest = qMax(widest, metrics.horizontalAdvance(listMarkerLayouts_.value(id).marker.text));
    for (const auto id : items) {
      auto& item = listMarkerLayouts_[id];
      if (item.marker.kind == BlockLayout::ListMarkerKind::OrderedText)
        item.contentIndent = qMax(theme.listIndent(), widest + theme.listMarkerGap());
    }
  }
  return listMarkerLayouts_.value(itemNode.id(), fallback);
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildListItem(const MarkdownNode& node, const RenderTheme& theme,
                                                               qreal x, qreal y, qreal width, int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::ListItem);
  layout->setDepth(depth);

  const auto marker = listMarkerLayout(node, theme);
  layout->setListMarkerKind(marker.marker.kind);
  layout->setListMarker(marker.marker.text);
  const qreal contentIndent = marker.contentIndent;
  layout->setListContentIndent(contentIndent);

  const qreal contentX = x + contentIndent;
  const qreal contentWidth = qMax<qreal>(1.0, width - contentIndent);

  auto inlineLayout = std::make_unique<InlineLayout>();
  const QString elementKey = isInsideBlockquote(node) ? QStringLiteral("blockquote p") : QStringLiteral("li");
  InlineLayout::BuildOptions options;
  options.documentPath = documentPath_;
  options.imageResources = imageResources_;
  options.styleNode = primaryParagraph(node) ? primaryParagraph(node) : &node;
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
    options.lineHeightMultiplier = theme.lineHeightMultiplierForElement(elementKey, &node);
    options.alignment = theme.textAlignmentForElement(elementKey, &node);
    options.textTransform = static_cast<TextTransform>(theme.textTransformForElement(elementKey, &node));
    options.textShadow = theme.textShadowForElement(elementKey, &node);
    applyPreedit(options);
    inlineLayout->build(primaryInlinesForListItem(node), listSourceText, theme, contentWidth,
                        theme.textFontForElement(elementKey, &node), options);
  }
  layout->setInlineLayout(std::move(inlineLayout));

  const qreal inlineHeight =
      layout->inlineLayout() ? layout->inlineLayout()->height() : TextFontMetrics(theme.paragraphFont()).height();
  qreal flowBottom = y + inlineHeight;
  std::vector<std::unique_ptr<BlockLayout>> children;

  bool skippedPrimaryParagraph = false;
  const MarkdownNode* previousChild = nullptr;
  for (const auto* child : theme.markdownFlow(node)->children(node)) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_)) {
      continue;
    }
    if (!skippedPrimaryParagraph && child->type() == BlockType::Paragraph) {
      skippedPrimaryParagraph = true;
      previousChild = child;
      continue;
    }
    flowBottom +=
        previousChild ? spacingBetweenInFlow(*previousChild, *child, theme, false, contentWidth) : theme.blockSpacing();
    auto childLayout = build(*child, theme, contentX, flowBottom, contentWidth, depth + 1);
    flowBottom = childLayout->rect().bottom();
    previousChild = child;
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

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildLiteralBlock(const MarkdownNode& node, const RenderTheme& theme,
                                                                   qreal x, qreal y, qreal width, int depth,
                                                                   bool forceHtmlSource) {
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
  const bool htmlPreview = node.type() == BlockType::HtmlBlock && !editingLiteral && !forceHtmlSource;
  const qreal inputX = x, inputWidth = width;
  const QString styleKey = htmlPreview ? QString() : QStringLiteral("pre");
  const auto* resolvedStyle = theme.elementStyleForNode(node, styleKey);
  const auto usedBox = htmlPreview ? ThemeElementBoxStyle{} : boxFor(node, theme, styleKey, width);
  const qreal availableWidth = qMax<qreal>(1, width - usedBox.margin.left() - usedBox.margin.right());
  x += usedBox.margin.left();
  width = LayoutBox::borderWidth(usedBox, availableWidth);
  if (usedBox.marginLeftAuto)
    x += qMax<qreal>(0, availableWidth - width) / (usedBox.marginRightAuto ? 2 : 1);
  const auto padding = LayoutBox::insets(usedBox);
  const QFont codeFont = theme.textFontForElement(styleKey, &node);
  const qreal codeLineHeight = resolvedStyle && resolvedStyle->text.lineHeight > 0
                                   ? codeFont.pointSizeF() * 96.0 / 72.0 * resolvedStyle->text.lineHeight
                                   : TextFontMetrics(codeFont).height();
  layout->setLiteralEditing(editingLiteral);
  const qreal lineNumberGutter = (node.type() == BlockType::CodeFence && showLineNumbers_)
                                     ? codeLineNumberGutterWidth(layout->literal(), codeFont)
                                     : 0.0;
  layout->setLineNumberGutterWidth(lineNumberGutter);
  // Code fences honor markdown/codeBlockWrap; other literal blocks always wrap.
  const bool codeWrap = node.type() == BlockType::CodeFence ? codeBlockWrap_ : true;
  const qreal sourceWidth = qMax<qreal>(1, width - lineNumberGutter - padding.left() - padding.right());
  LiteralTextLayout::Preedit preedit;
  if (selectionFocusesNode(selection_, node.id())) {
    preedit = {selection_.focus.text.textOffset, preeditText_, preeditFormats_, preeditCursor_};
  }
  std::shared_ptr<const LiteralTextLayout> sourceLayout;
  {
    BuildAccumTimer t(literalTextNs_, perfEnabled_);
    sourceLayout = std::make_shared<LiteralTextLayout>(layout->literal(), codeFont, sourceWidth, codeLineHeight,
                                                       codeWrap, std::move(preedit));
  }
  layout->setLiteralTextLayout(sourceLayout);
  // Measure the widest source line so the block knows whether it is horizontally scrollable and
  // the scrollbar thumb ratio. Reserved strip height makes room for the always-on scrollbar.
  qreal reservedStrip = 0.0;
  if (node.type() == BlockType::CodeFence && !codeWrap) {
    const qreal maxLineW = sourceLayout->maximumLineWidth();
    layout->setCodeMaxLineWidth(maxLineW);
    if (codeFenceScroll_ != nullptr) {
      codeFenceScroll_->setContentWidth(node.id(), maxLineW);
    }
    const qreal visibleW = qMax<qreal>(1.0, width - lineNumberGutter - padding.left() - padding.right());
    if (maxLineW > visibleW + 0.5) {
      reservedStrip = BlockLayout::scrollBarStripHeight(theme);
    }
  }
  qreal height = std::ceil(sourceLayout->height() + padding.top() + padding.bottom() + 2) + reservedStrip;
  if (node.type() == BlockType::MathBlock) {
    std::shared_ptr<math::MathLayoutResult> mathLayout;
    {
      BuildAccumTimer t(mathRenderNs_, perfEnabled_);
      mathLayout =
          std::make_shared<math::MathLayoutResult>(mathRenderer_.render(layout->literal(), theme, true, width));
    }
    if (editingLiteral) {
      const qreal markerHeight = qMax<qreal>(14, TextFontMetrics(codeFont).height());
      const qreal previewHeight = mathLayout->valid() ? mathLayout->size.height() : 0;
      height = std::ceil(padding.top() + markerHeight + sourceLayout->height() + markerHeight + padding.bottom() +
                         padding.top() + previewHeight + padding.bottom());
    } else if (mathLayout->valid()) {
      height = std::ceil(mathLayout->size.height() + padding.top() + padding.bottom());
    } else {
      // The inactive parse-error fallback is plain math text. The editable
      // source snapshot above always keeps its resolved code font.
      LiteralTextLayout fallback(layout->literal(), theme.mathFont(), sourceWidth,
                                 TextFontMetrics(theme.mathFont()).height(), true);
      height = std::ceil(fallback.height() + padding.top() + padding.bottom() + 2);
    }
    if (mathLayout->valid()) {
      layout->setMathLayout(std::move(mathLayout));
    }
  }
  // Mermaid diagram (milestone I): always validate while diagrams are enabled.
  // A focused fence uses the debounced async path and keeps painting its source;
  // a Ready scene replaces the source only after focus leaves and show-as-source
  // is off. Error/Unsupported retain the source and add a diagnostic panel.
  if (node.type() == BlockType::CodeFence && node.codeLanguage() == QLatin1String("mermaid") && renderDiagrams_ &&
      mermaidCache_ != nullptr) {
    const bool editingMermaid = selectionFocusesNode(selection_, node.id());
    const bool keepSource = editingMermaid || showMermaidAsSource_;
    const QString source = layout->literal();
    const MermaidRenderKey key = mermaidCache_->makeKey(source);
    const MermaidRenderEntry entry = mermaidSyncMode_ ? mermaidCache_->getSync(key, source)
                                     : editingMermaid ? mermaidCache_->requestDebounced(key, source)
                                                      : mermaidCache_->request(key, source);
    layout->setMermaidState(static_cast<BlockLayout::MermaidState>(
        static_cast<int>(entry.status))); // MermaidRenderStatus ↔ BlockLayout::MermaidState are ordered identically
    if (!keepSource && entry.status == MermaidRenderStatus::Ready && entry.scene) {
      layout->setMermaidViewportCullingEnabled(!mermaidSyncMode_);
      layout->setMermaidScene(entry.scene, entry.naturalSize, entry.metadata);
      const int contentWidth = static_cast<int>(qMax<qreal>(1.0, width - padding.left() - padding.right()));
      const qreal natW = entry.naturalSize.width();
      const qreal scale = natW > 0.0 ? qMin<qreal>(1.0, contentWidth / natW) : 1.0;
      height = std::ceil(entry.naturalSize.height() * scale + padding.top() + padding.bottom());
    } else if (entry.status == MermaidRenderStatus::Error || entry.status == MermaidRenderStatus::Unsupported) {
      layout->setMermaidDiagnostic(entry.diagnostic);
      height += BlockLayout::mermaidDiagnosticFootprint(entry.diagnostic, theme, width);
    }
  }
  if (node.type() == BlockType::HtmlBlock && editingLiteral) {
    BuildAccumTimer t(codeHighlightNs_, perfEnabled_);
    layout->setCodeHighlightSpans(codeHighlighter_.highlight(QStringLiteral("html"), layout->literal()));
  }
  if (node.type() == BlockType::HtmlBlock && !editingLiteral && !forceHtmlSource) {
    const qreal contentWidth = qMax<qreal>(1.0, width - padding.left() - padding.right());
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
      htmlPalette.documentParent = theme.cssElementForNode(node)->parent;
      htmlPalette.documentStyleSheet = theme.documentStyleSheet();
      htmlPalette.fontAliases = theme.fontAliases();
      htmlPalette.cssEnvironment = theme.documentCssEnvironment();
      htmlPalette.cssZoom = theme.zoomPercent() / 100.0;
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
      height = std::ceil(htmlResult->size().height() + padding.top() + padding.bottom());
      layout->setHtmlLayout(std::move(htmlResult));
    } else {
      return buildLiteralBlock(node, theme, inputX, y, inputWidth, depth, true);
    }
  }
  if (forceHtmlSource)
    layout->setCodeHighlightSpans(codeHighlighter_.highlight(QStringLiteral("html"), layout->literal()));
  height = LayoutBox::borderHeight(usedBox, qMax<qreal>(0, height - padding.top() - padding.bottom()));
  layout->setRect(QRectF(x, y, width, height));
  auto box = LayoutBox::place(styleKey, resolvedStyle ? *resolvedStyle : ThemeElementStyle{}, usedBox, layout->rect(),
                              codeFont);
  box.lineHeight = codeLineHeight;
  layout->setCssBoxGeometry(std::move(box));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildTable(const MarkdownNode& node, const RenderTheme& theme, qreal x,
                                                            qreal y, qreal width, int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::Table);
  layout->setDepth(depth);

  const auto* wrapperStyle = theme.elementStyleForNode(node, QStringLiteral(".table-figure"));
  const auto wrapperBox = theme.elementBoxStyle(QStringLiteral(".table-figure"), &node, width);
  const auto wrapperInsets = LayoutBox::insets(wrapperBox);
  const qreal wrapperWidth =
      LayoutBox::borderWidth(wrapperBox, qMax<qreal>(1, width - wrapperBox.margin.left() - wrapperBox.margin.right()));
  const QPointF wrapperOrigin(x + wrapperBox.margin.left(), y);
  x = wrapperOrigin.x() + wrapperInsets.left();
  y += wrapperInsets.top();
  width = qMax<qreal>(1, wrapperWidth - wrapperInsets.left() - wrapperInsets.right());

  const QString tableKey = QStringLiteral("table");
  const auto* tableStyle = theme.elementStyleForNode(node, tableKey);
  const auto tableBox = boxFor(node, theme, tableKey, width);
  const qreal innerTopMargin = qFuzzyIsNull(wrapperInsets.top()) ? 0 : tableBox.margin.top();
  const qreal innerBottomMargin = qFuzzyIsNull(wrapperInsets.bottom()) ? 0 : tableBox.margin.bottom();
  y += innerTopMargin;
  const auto tableInsets = LayoutBox::insets(tableBox);
  const qreal availableWidth = qMax<qreal>(1, width - tableBox.margin.left() - tableBox.margin.right());
  x += tableBox.margin.left();
  width = LayoutBox::borderWidth(tableBox, availableWidth);
  if (tableBox.marginLeftAuto)
    x += qMax<qreal>(0, availableWidth - width) / (tableBox.marginRightAuto ? 2 : 1);
  const qreal contentWidth = qMax<qreal>(1, width - tableInsets.left() - tableInsets.right());

  const int rowCount = static_cast<int>(node.children().size());
  int columnCount = 0;
  for (const auto& row : node.children()) {
    columnCount = qMax(columnCount, static_cast<int>(row->children().size()));
  }

  if (rowCount == 0 || columnCount == 0) {
    layout->setRect(QRectF(x, y, width, TextFontMetrics(theme.paragraphFont()).height()));
    return layout;
  }

  const QVector<qreal> columnWidths = tableColumnWidths(node, theme, contentWidth, breakOnSingleNewline_);

  const QVector<TableAlignment> alignments = node.tableAlignments();
  std::vector<BlockLayout::TableRowLayout> rows;
  qreal cursorY = y + tableInsets.top();
  int rowIndex = 0;

  for (const auto& rowNode : node.children()) {
    std::vector<BlockLayout::TableCellLayout> cells;
    qreal rowHeight = 0;
    qreal cellX = x + tableInsets.left();
    int column = 0;
    for (const auto& cellNode : rowNode->children()) {
      const qreal columnWidth = column < columnWidths.size() ? columnWidths.at(column) : width / columnCount;
      BlockLayout::TableCellLayout cell;
      cell.nodeId = cellNode->id();
      cell.contentSourceStart = sourceContentStartForEditableNode(*cellNode);
      cell.header = rowNode->tableRowIsHeader();
      cell.alternate = rowIndex % 2 == 1;
      const QString key = cell.header ? QStringLiteral("th") : QStringLiteral("td");
      const auto usedBox = theme.elementBoxStyle(key, cellNode.get(), columnWidth);
      const auto padding = LayoutBox::insets(usedBox);
      const auto* cellStyle = theme.elementStyleForNode(*cellNode, key);
      ThemeElementStyle resolved = cellStyle ? *cellStyle : ThemeElementStyle{};
      cell.box = LayoutBox::place(key, resolved, usedBox, {}, theme.textFontForElement(key, cellNode.get()));
      cell.alignment = column < alignments.size() ? alignments.at(column) : TableAlignment::None;
      InlineLayout::BuildOptions options;
      options.styleNode = cellNode.get();
      options.lineHeightMultiplier = theme.lineHeightMultiplierForElement(key, cellNode.get());
      options.baseTextColor =
          theme.textColorForElement(cell.header ? QStringLiteral("th") : QStringLiteral("td"), cellNode.get());
      options.documentPath = documentPath_;
      options.imageResources = imageResources_;
      options.sourceBase =
          sourceContentStartForEditableNode(*cellNode) - cellNode->topLevelBlock()->sourceRange().byteStart;
      if (selection_.focus.text.nodeId == cellNode->id()) {
        options.projectionState = InlineProjectionState::forSelection(selection_, selection_.focus.blockId,
                                                                      sourceContentStartForEditableNode(*cellNode));
      }
      options.isMisspelled = spellMisspelledPredicate();
      options.smartPunct = smartPunctRenderOptions();
      options.breakOnSingleNewline = breakOnSingleNewline_;
      options.renderEmoji = renderEmoji_;
      {
        BuildAccumTimer t(inlineLayoutNs_, perfEnabled_);
        applyPreedit(options); // only the focused cell has projectionState.cursorSourceOffset set
        cell.text.build(
            cellNode->inlines(), sourceTextForEditableNode(*cellNode), theme,
            qMax<qreal>(1.0, columnWidth - padding.left() - padding.right()),
            theme.textFontForElement(cell.header ? QStringLiteral("th") : QStringLiteral("td"), cellNode.get()),
            options);
      }
      rowHeight = qMax(rowHeight, LayoutBox::borderHeight(usedBox, cell.text.height()));
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
      const auto usedBox = theme.elementBoxStyle(QStringLiteral("td"), nullptr, columnWidth);
      const auto padding = LayoutBox::insets(usedBox);
      const auto* style = theme.elementStyle(QStringLiteral("td"));
      cell.box = LayoutBox::place(QStringLiteral("td"), style ? *style : ThemeElementStyle{}, usedBox, {},
                                  theme.textFontForElement(QStringLiteral("td")));
      rowHeight = qMax(rowHeight, TextFontMetrics(cell.box.font).height() + padding.top() + padding.bottom());
      cells.push_back(std::move(cell));
      cellX += columnWidth;
      ++column;
    }
    for (BlockLayout::TableCellLayout& cell : cells) {
      cell.rect.setHeight(rowHeight);
      cell.box = LayoutBox::place(cell.box.hostKey, cell.box.style, cell.box.usedBox, cell.rect, cell.box.font);
    }
    BlockLayout::TableRowLayout row;
    row.nodeId = rowNode->id();
    row.rect = QRectF(x + tableInsets.left(), cursorY, contentWidth, rowHeight);
    const auto* rowStyle = theme.elementStyleForNode(*rowNode, QStringLiteral("tr"));
    row.box = LayoutBox::place(QStringLiteral("tr"), rowStyle ? *rowStyle : ThemeElementStyle{},
                               theme.elementBoxStyle(QStringLiteral("tr"), rowNode.get(), contentWidth), row.rect);
    row.cells = std::move(cells);
    rows.push_back(std::move(row));
    cursorY += rowHeight;
    ++rowIndex;
  }

  const QRectF tableRect(x, y, width, LayoutBox::borderHeight(tableBox, cursorY - y - tableInsets.top()));
  const qreal wrapperHeight =
      LayoutBox::borderHeight(wrapperBox, tableRect.height() + innerTopMargin + innerBottomMargin);
  layout->setRect(QRectF(wrapperOrigin, QSizeF(wrapperWidth, wrapperHeight)));
  layout->setCssBoxGeometry(
      LayoutBox::place(tableKey, tableStyle ? *tableStyle : ThemeElementStyle{}, tableBox, tableRect));
  layout->setOuterCssBox(LayoutBox::place(QStringLiteral(".table-figure"),
                                          wrapperStyle ? *wrapperStyle : ThemeElementStyle{}, wrapperBox,
                                          layout->rect(), theme.paragraphFont()));
  layout->setTableRows(std::move(rows));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildThematicBreak(const MarkdownNode& node, const RenderTheme& theme,
                                                                    qreal x, qreal y, qreal width, int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::ThematicBreak);
  layout->setDepth(depth);
  layout->setRect(QRectF(x, y, width, theme.blockSpacing() * 2.0));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildDefinition(const MarkdownNode& node, const RenderTheme& theme,
                                                                 qreal x, qreal y, qreal width, int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);
  const DefinitionBlock definition = node.definition();
  layout->setDefinition(definition);
  layout->setContentSourceStart(definition.markerRange.isValid() ? definition.markerRange.start
                                                                 : node.sourceRange().byteStart);
  const bool definitionFocused = selection_.isCollapsed() && selection_.focus.blockId == node.id();

  const QFont font = theme.paragraphFont();
  const TextFontMetrics metrics(font);
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
  auto slot = [&](BlockLayout::DefinitionSlotLayout::Field field, const DefinitionFieldRange& sourceRange,
                  const QString& text, const QString& placeholder) {
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
  slot(BlockLayout::DefinitionSlotLayout::Field::Label, definition.labelRange, definition.label,
       QStringLiteral("name"));
  syntax(QStringLiteral("]:"));

  if (definition.kind == DefinitionBlock::Kind::Footnote) {
    syntax(QStringLiteral(" "));
    slot(BlockLayout::DefinitionSlotLayout::Field::Note, definition.noteRange, definition.note,
         QStringLiteral("input description here"));

    // Extract continuation lines for multi-line footnotes
    if (definition.sourceRange.isValid() && definition.noteRange.isValid() &&
        definition.sourceRange.end > definition.noteRange.end && !md().isEmpty()) {
      // Find end of the first line in the source range
      const qsizetype srcStart = definition.sourceRange.start;
      const qsizetype firstLineEnd = md().indexOf(QLatin1Char('\n'), definition.noteRange.end);
      if (firstLineEnd >= 0 && firstLineEnd < definition.sourceRange.end) {
        QString continuation;
        qsizetype pos = firstLineEnd + 1;
        while (pos < definition.sourceRange.end) {
          // Strip leading indentation (up to 4 spaces or 1 tab)
          int indent = 0;
          while (pos < definition.sourceRange.end && indent < 4 && md().at(pos) == QLatin1Char(' ')) {
            ++pos;
            ++indent;
          }
          if (pos < definition.sourceRange.end && indent < 4 && md().at(pos) == QLatin1Char('\t')) {
            ++pos;
          }
          // Read to end of line
          const qsizetype contentStart = pos;
          while (pos < definition.sourceRange.end && md().at(pos) != QLatin1Char('\n') &&
                 md().at(pos) != QLatin1Char('\r')) {
            ++pos;
          }
          if (!continuation.isEmpty()) {
            continuation += QLatin1Char('\n');
          }
          continuation += md().mid(contentStart, pos - contentStart);
          if (pos < definition.sourceRange.end && md().at(pos) == QLatin1Char('\r')) {
            ++pos;
          }
          if (pos < definition.sourceRange.end && md().at(pos) == QLatin1Char('\n')) {
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
    slot(BlockLayout::DefinitionSlotLayout::Field::Destination, definition.destinationRange, definition.destination,
         QStringLiteral("input link url here"));
    if (definition.titleDelimiter != DefinitionBlock::TitleDelimiter::None || definitionFocused) {
      const bool explicitEmptyTitle =
          definition.titleDelimiter != DefinitionBlock::TitleDelimiter::None && definition.title.isEmpty();
      syntax(titleOpeningSyntax());
      slot(BlockLayout::DefinitionSlotLayout::Field::Title, definition.titleRange, definition.title,
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

QString BlockLayoutBuilder::textForListMarker(const MarkdownNode& itemNode, const MarkdownNode& listNode,
                                              qsizetype index) const {
  if (listNode.listKind() == ListKind::Ordered) {
    const ListLineInfo info = authoredMarkerInfo(itemNode);
    if (info.valid && info.ordered) {
      return QStringLiteral("%1%2").arg(info.orderedNumber).arg(info.orderedDelimiter);
    }
    return QStringLiteral("%1.").arg(listNode.listStart() + static_cast<int>(index));
  }
  return QStringLiteral("•");
}

// The item's own marker line, parsed from the live source. Markdown source is the display's
// source of truth (WYSIWYG): a list split mid-way by blank lines still parses as ONE loose list,
// so positional numbering (listStart + index) would display a source-authored "1." as "3." after
// the split. Returns an invalid info when the marker can't be parsed (e.g. a synthesized node),
// letting callers fall back to positional.
ListLineInfo BlockLayoutBuilder::authoredMarkerInfo(const MarkdownNode& itemNode) const {
  const SourceRange range = itemNode.sourceRange();
  if (range.byteStart < 0 || range.byteStart > md().size()) {
    return ListLineInfo{};
  }
  const qsizetype lineStart = lineStartOffset(md(), range.byteStart);
  qsizetype lineEnd = md().indexOf(QLatin1Char('\n'), lineStart);
  if (lineEnd < 0) {
    lineEnd = md().size();
  }
  return listLineInfoFor(md().mid(lineStart, lineEnd - lineStart));
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

BlockLayoutBuilder::ResolvedMarker BlockLayoutBuilder::resolveListMarker(const MarkdownNode& itemNode,
                                                                         const RenderTheme& theme,
                                                                         qsizetype itemIndex) const {
  const MarkdownNode* listNode = itemNode.parent();
  // `li::marker { content: … counter(list-item) … }` — content-driven marker,
  // resolved against the implicit list-item counter (this item's position).
  const auto& contentTokens = theme.decorations().listMarkerContent;
  if (!contentTokens.empty()) {
    const auto value = [this, &itemNode, listNode, itemIndex](const QString& name) -> int {
      if (name == QStringLiteral("list-item")) {
        const ListLineInfo authored = authoredMarkerInfo(itemNode);
        if (listNode && listNode->listKind() == ListKind::Ordered && authored.valid && authored.ordered) {
          return authored.orderedNumber; // authored number, not positional (see authoredMarkerInfo)
        }
        const int start = (listNode && listNode->listKind() == ListKind::Ordered) ? listNode->listStart() : 1;
        return start + int(itemIndex);
      }
      return 0; // named counters are not tracked in the marker path
    };
    const auto chain = [this, &itemNode](const QString& name) -> QVector<int> {
      QVector<int> levels; // outermost first
      if (name != QStringLiteral("list-item")) {
        return levels;
      }
      for (const MarkdownNode* item = &itemNode; item && item->type() == BlockType::ListItem;) {
        const MarkdownNode* list = item->parent();
        if (!list) {
          break;
        }
        int idx = 0;
        for (const auto& s : list->children()) {
          if (s.get() == item) {
            break;
          }
          ++idx;
        }
        const ListLineInfo authored = authoredMarkerInfo(*item);
        if (list->listKind() == ListKind::Ordered && authored.valid && authored.ordered) {
          levels.prepend(authored.orderedNumber);
        } else {
          const int start = (list->listKind() == ListKind::Ordered) ? list->listStart() : 1;
          levels.prepend(start + idx);
        }
        item = list->parent(); // next outer list level (a ListItem, or null at top)
      }
      return levels;
    };
    const auto* origin = theme.cssElementForNode(itemNode);
    const QString text =
        resolveContentTokens(contentTokens, value, chain, [origin](const QString& name) -> std::optional<QString> {
          return origin->attributes.contains(name) ? std::optional<QString>(origin->attributes.value(name))
                                                   : std::nullopt;
        });
    return {text.isEmpty() ? BlockLayout::ListMarkerKind::None : BlockLayout::ListMarkerKind::OrderedText, text};
  }
  QString styleType;
  if (listNode) {
    const bool ordered = listNode->listKind() == ListKind::Ordered;
    styleType = theme.listStyleTypeForItem(ordered);
  }
  if (styleType.isEmpty()) {
    // No CSS list-style-type → legacy depth/kind-based marker.
    if (listNode) {
      return {markerKindForListItem(itemNode), textForListMarker(itemNode, *listNode, itemIndex)};
    }
    return {BlockLayout::ListMarkerKind::BulletDisc, QStringLiteral("•")};
  }
  if (styleType == QStringLiteral("none")) {
    return {BlockLayout::ListMarkerKind::None, QString()};
  }
  if (styleType == QStringLiteral("disc")) {
    return {BlockLayout::ListMarkerKind::BulletDisc, QStringLiteral("•")};
  }
  if (styleType == QStringLiteral("circle")) {
    return {BlockLayout::ListMarkerKind::BulletCircle, QStringLiteral("•")};
  }
  if (styleType == QStringLiteral("square")) {
    return {BlockLayout::ListMarkerKind::BulletSquare, QStringLiteral("•")};
  }
  if (isOrderedListStyle(styleType)) {
    const ListLineInfo authored = authoredMarkerInfo(itemNode);
    int number = int(itemIndex) + 1;
    if (listNode && listNode->listKind() == ListKind::Ordered) {
      number = (authored.valid && authored.ordered) ? authored.orderedNumber : listNode->listStart() + int(itemIndex);
    }
    return {BlockLayout::ListMarkerKind::OrderedText, formatCounterValue(number, styleType) + QStringLiteral(".")};
  }
  if (listNode) {
    return {markerKindForListItem(itemNode), textForListMarker(itemNode, *listNode, itemIndex)};
  }
  return {BlockLayout::ListMarkerKind::BulletDisc, QStringLiteral("•")};
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
                      : (hasResolvedByteRange(range) ? range.byteEnd : sourceOffsetForLineEnd(range.lineEnd));
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

BlockLayoutBuilder::EstimateResult
BlockLayoutBuilder::estimateHeight(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const {
  return heightEstimator_.estimateHeight(node, theme, width, depth);
}

} // namespace muffin
