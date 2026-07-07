#pragma once

#include "document/MarkdownNode.h"
#include "editor/CursorPosition.h"
#include "html/HtmlLayoutResult.h"
#include "math/MathRenderNode.h"
#include "render/CodeHighlight.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QRectF>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace muffin {

class CodeFenceScrollController;

class BlockLayout {
public:
  enum class ListMarkerKind {
    None,
    OrderedText,
    BulletDisc,
    BulletCircle,
    BulletSquare,
  };

  struct TableCellLayout {
    NodeId nodeId;
    QRectF rect;
    InlineLayout text;
    qsizetype contentSourceStart = -1;
    TableAlignment alignment = TableAlignment::None;
    bool header = false;
    bool alternate = false;
  };

  struct TableRowLayout {
    QRectF rect;
    std::vector<TableCellLayout> cells;
  };

  struct DefinitionSlotLayout {
    enum class Field {
      Label,
      Destination,
      Title,
      Note
    };

    Field field = Field::Label;
    QRectF rect;
    QString text;
    QString placeholder;
    qsizetype sourceStart = -1;
    qsizetype sourceEnd = -1;
    bool focused = false;
  };

  struct DefinitionTokenLayout {
    enum class Kind {
      Syntax,
      Slot
    };

    Kind kind = Kind::Syntax;
    DefinitionSlotLayout::Field field = DefinitionSlotLayout::Field::Label;
    QRectF rect;
    QString text;
    QString placeholder;
    qsizetype sourceStart = -1;
    qsizetype sourceEnd = -1;
    bool editable = false;
    bool focused = false;
  };

  // One rendered row of a `[TOC]` block: the heading's title, its level (for
  // indentation), the target heading's NodeId (encoded into a `#toc:` href so the
  // existing Ctrl+click link path navigates), and the row's document-coordinate
  // rect (the click target, shared by paint + hit-test so it never drifts).
  struct TocEntryLayout {
    QRectF rect;
    NodeId target;
    QString title;
    int level = 1;
  };

  struct CssBoxGeometry {
    QString hostKey;
    QRectF flowRect;
    QRectF borderBox;
    QRectF paddingBox;
    QRectF contentBox;
    QPointF inlineTextOrigin;
    QRectF visualOverflow;
    bool valid = false;
  };

  // Interactive state passed into paint so in-block properties (heading text
  // colour, ::after width) animate with the SAME HoverAnimator/FocusAnimator
  // phase the outer glow/bg/scale effects use. hover and focus are orthogonal —
  // a block can be both under the cursor and contain the caret — so each carries
  // its own active flag + phase. Defaults to inactive (base appearance).
  struct BlockPaintState {
    bool hoverActive = false;
    qreal hoverPhase = 0.0;  // 0..1
    bool focusActive = false;
    qreal focusPhase = 0.0;  // 0..1
  };

  explicit BlockLayout(NodeId id = {});

  NodeId nodeId() const;
  BlockType type() const;
  void setType(BlockType type);

  QRectF rect() const;
  void setRect(QRectF rect);
  void setCssBoxGeometry(CssBoxGeometry geometry);
  CssBoxGeometry cssBoxGeometry(const RenderTheme& theme) const;
  QRectF cssBorderBox(const RenderTheme& theme) const;
  QPointF inlineTextOrigin(const RenderTheme& theme) const;
  QRectF visualOverflowRect(const RenderTheme& theme) const;
  void translate(qreal dx, qreal dy);
  void translateY(qreal dy);

  qreal height() const;
  qreal bottom() const;

  void setInlineLayout(std::unique_ptr<InlineLayout> layout);
  InlineLayout* inlineLayout();
  const InlineLayout* inlineLayout() const;

  void setLiteral(QString literal);
  QString literal() const;
  void setCodeLanguage(QString language);
  QString codeLanguage() const;
  void setCodeHighlightSpans(QVector<CodeHighlightSpan> spans);
  const QVector<CodeHighlightSpan>& codeHighlightSpans() const;
  void setMathLayout(std::shared_ptr<math::MathLayoutResult> layout);
  const math::MathLayoutResult* mathLayout() const;
  void setMathDelimiter(MathDelimiter delimiter);
  MathDelimiter mathDelimiter() const;
  void setHtmlLayout(std::shared_ptr<html::HtmlLayoutResult> layout);
  const html::HtmlLayoutResult* htmlLayout() const;
  void setLiteralEditing(bool editing);
  bool literalEditing() const;
  QRectF literalContentRect(const RenderTheme& theme) const;
  // Visual-line queries over a literal block's source (code/math/HTML/front matter). Offsets are
  // LOCAL to the literal (0..literal().size()), matching CursorPosition::textOffset for these
  // blocks. The cursor rect is UN-SCROLLED document space (origin = literalContentRect().topLeft()),
  // so it reflects the real character column even in a horizontally-scrollable code fence.
  int literalVisualLineCount(const RenderTheme& theme) const;
  int literalVisualLineIndexForOffset(qsizetype localOffset, const RenderTheme& theme) const;
  qsizetype literalOffsetAtVisualLineX(int lineIndex, qreal localX, const RenderTheme& theme) const;
  QRectF literalVisualCursorRect(qsizetype localOffset, const RenderTheme& theme) const;
  // Width reserved at the left of a code-fence content rect for line numbers (0 when off).
  void setLineNumberGutterWidth(qreal width);
  qreal lineNumberGutterWidth() const;
  // Pixel width of the longest source line in a code fence (drives horizontal scrollability and the
  // scrollbar thumb ratio). Set by the builder; 0 means unmeasured / not scrollable.
  void setCodeMaxLineWidth(qreal width);
  qreal codeMaxLineWidth() const;
  // Height of the horizontal scrollbar strip drawn at the bottom of a scrollable code fence.
  static qreal scrollBarStripHeight(const RenderTheme& theme);

  void setHeadingLevel(int level);
  int headingLevel() const;

  // Resolved ::before text for headings, e.g. "1. " from `content: counter(h1) ". "`.
  // Empty when the heading has no counter-driven ::before (non-counter themes, or a
  // heading whose ::before is a literal glyph/shape). Painted by DecorationPainter in
  // place of the rule's raw `content` so counter() evaluates to real outline numbers.
  void setHeadingBeforeText(QString text);
  QString headingBeforeText() const;

  void setListMarker(QString marker);
  QString listMarker() const;
  void setListMarkerKind(ListMarkerKind kind);
  ListMarkerKind listMarkerKind() const;
  bool hasListMarker() const;
  // Horizontal gutter reserved for the list marker (the content's left edge sits at
  // rect().left() + this value). For ordered lists it grows with the widest sibling
  // marker so multi-digit numbers never overlap the content; for bullet/task lists it
  // equals theme.listIndent(). Zero for non-list blocks.
  void setListContentIndent(qreal indent);
  qreal listContentIndent() const;
  void setContentSourceStart(qsizetype sourceStart);
  qsizetype contentSourceStart() const;
  void setPlaceholderText(QString text);
  QString placeholderText() const;
  void setDefinition(const DefinitionBlock& definition);
  DefinitionBlock definition() const;
  void setDefinitionSlots(QVector<DefinitionSlotLayout> definitionSlots);
  const QVector<DefinitionSlotLayout>& definitionSlots() const;
  void setDefinitionTokens(QVector<DefinitionTokenLayout> definitionTokens);
  const QVector<DefinitionTokenLayout>& definitionTokens() const;
  QRectF definitionCursorRectForSourceOffset(qsizetype sourceOffset, const RenderTheme& theme) const;
  void setTaskListItem(bool taskListItem, bool checked);
  bool isTaskListItem() const;
  bool taskChecked() const;
  // GitHub-style alert kind for a blockquote rendered as a themed card; None for a plain quote.
  void setAlertKind(AlertKind kind);
  AlertKind alertKind() const;
  // A `[TOC]` paragraph rendered as a generated indented link list of the document's
  // headings. While the caret is NOT in the block it shows the preview (isToc() true);
  // when the caret enters, the builder rebuilds it as a normal paragraph showing the
  // literal `[TOC]` for editing (isToc() false).
  void setIsToc(bool isToc);
  bool isToc() const;
  void setTocEntries(QVector<TocEntryLayout> entries);
  const QVector<TocEntryLayout>& tocEntries() const;
  // Document-coordinate rect of the checkbox drawn for a task-list item (empty
  // when this block is not a task item). Single source of truth shared by the
  // painter and the hit tester so a click target never drifts from the glyph.
  QRectF taskCheckboxRect(const RenderTheme& theme) const;

  void setDepth(int depth);
  int depth() const;

  void setChildren(std::vector<std::unique_ptr<BlockLayout>> children);
  std::vector<std::unique_ptr<BlockLayout>>& children();
  const std::vector<std::unique_ptr<BlockLayout>>& children() const;

  void setTableRows(std::vector<TableRowLayout> rows);
  std::vector<TableRowLayout>& tableRows();
  const std::vector<TableRowLayout>& tableRows() const;
  QRectF tableCellRect(int row, int column) const;

  // NOTE: `hover` is fully specified, not `{}`. Clang (macOS/Xcode) rejects an
  // aggregate-with-NSDMI (BlockPaintState) used in a `= {}` default argument declared within
  // this enclosing class ("default member initializer needed within definition of enclosing
  // class outside of member functions"); MSVC accepts `{}` so this only surfaces on macOS.
  // Explicit values avoid needing the NSDMI here. Keep these four fields synced with
  // BlockPaintState's inactive defaults (hover/focus inactive, phases 0).
  void paint(QPainter& painter, const RenderTheme& theme, qreal scrollY,
             const CodeFenceScrollController* scroll = nullptr,
             BlockPaintState hover = BlockPaintState{false, 0.0, false, 0.0}) const;
  bool intersects(const QRectF& documentViewport) const;
  bool containsNode(NodeId id) const;
  bool containsInteractiveContent(QPointF documentPos, const RenderTheme& theme) const;
  HitTestResult hitTest(QPointF documentPos, const RenderTheme& theme,
                        const CodeFenceScrollController* scroll = nullptr) const;
  QVector<QRectF> selectionRects(const SelectionRange& selection, const RenderTheme& theme) const;
  QVector<QRectF> selectionRectsForOffsets(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const;
  // Selection rects for THIS block's own content within [startOffset, endOffset] only — does NOT
  // descend into children. Use this (not the recursive selectionRectsForOffsets) when painting a
  // pre-flattened block list (e.g. the result of blocksBetween): that list already contains every
  // descendant once, so recursing here would repaint each descendant once per owning ancestor
  // (visible as a darker, double-highlighted band) and would smear the ancestor's offsets onto the
  // descendant's text. Containers (List / BlockQuote / nested List) have no own content and return
  // nothing, exactly as wanted — their content is painted by their own child entries.
  QVector<QRectF> selectionRectsSelfForOffsets(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const;

private:
  void paintSelf(QPainter& painter, const RenderTheme& theme, qreal scrollY, const CodeFenceScrollController* scroll, BlockPaintState hover) const;
  // per-type paint dispatch targets (paintSelf switches over these). viewRect is the
  // scrollY-translated block rect computed once in paintSelf.
  void paintInlineBlock(QPainter& painter, const RenderTheme& theme, QRectF viewRect, qreal scrollY, BlockPaintState hover) const;
  void paintBlockQuote(QPainter& painter, const RenderTheme& theme, QRectF viewRect, qreal scrollY) const;
  void paintMathBlock(QPainter& painter, const RenderTheme& theme, QRectF viewRect, qreal scrollY) const;
  void paintHtmlBlock(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const;
  void paintThematicBreak(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const;
  void paintTable(QPainter& painter, const RenderTheme& theme, qreal scrollY) const;
  HitTestResult hitSelf(QPointF documentPos, const RenderTheme& theme, const CodeFenceScrollController* scroll) const;
  HitTestResult hitTable(QPointF documentPos, const RenderTheme& theme) const;
  QVector<QRectF> selectionRectsSelf(const SelectionRange& selection, const RenderTheme& theme) const;
  QVector<QRectF> literalSelectionRects(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const;
  QRectF mathEditorSourceRect(const RenderTheme& theme) const;
  // (font, width, lineHeight, wrap) used to lay out this block's literal — mirrors the per-type
  // choices in paint/hit-test so the visual-line queries match what is rendered.
  struct LiteralLayoutParams {
    QFont font;
    qreal width = 0.0;
    qreal lineHeight = 0.0;
    bool wrap = true;
  };
  LiteralLayoutParams literalLayoutParams(const RenderTheme& theme) const;
  QRectF mathPreviewContentRect(const RenderTheme& theme) const;
  void paintCodeFence(QPainter& painter, const RenderTheme& theme, QRectF viewRect, const CodeFenceScrollController* scroll) const;
  void paintLiteralSource(QPainter& painter, const RenderTheme& theme, QRectF contentRect, const QVector<CodeHighlightSpan>& spans, bool wrap) const;
  void paintCodeLineNumbers(QPainter& painter, const RenderTheme& theme, const QRectF& codeRect) const;
  void paintCodeFenceScrollBar(QPainter& painter, const RenderTheme& theme, QRectF contentRect, qreal offset, qreal maxLineWidth) const;
  void paintDefinition(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const;
  HitTestResult hitDefinition(QPointF documentPos, const RenderTheme& theme) const;
  void paintToc(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const;
  QVector<QRectF> definitionSelectionRects(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const;

  NodeId id_;
  BlockType type_ = BlockType::Unknown;
  QRectF rect_;
  CssBoxGeometry cssBoxGeometry_;
  std::unique_ptr<InlineLayout> inlineLayout_;
  QString literal_;
  QString codeLanguage_;
  QVector<CodeHighlightSpan> codeHighlightSpans_;
  std::shared_ptr<math::MathLayoutResult> mathLayout_;
  MathDelimiter mathDelimiter_ = MathDelimiter::Dollar;
  std::shared_ptr<html::HtmlLayoutResult> htmlLayout_;
  bool literalEditing_ = false;
  qreal lineNumberGutterWidth_ = 0.0;
  qreal codeMaxLineWidth_ = 0.0;
  int headingLevel_ = 0;
  QString headingBeforeText_;
  QString listMarker_;
  ListMarkerKind listMarkerKind_ = ListMarkerKind::None;
  qreal listContentIndent_ = 0.0;
  qsizetype contentSourceStart_ = -1;
  QString placeholderText_;
  DefinitionBlock definition_;
  QVector<DefinitionSlotLayout> definitionSlots_;
  QVector<DefinitionTokenLayout> definitionTokens_;
  bool taskListItem_ = false;
  bool taskChecked_ = false;
  AlertKind alertKind_ = AlertKind::None;
  bool isToc_ = false;
  QVector<TocEntryLayout> tocEntries_;
  int depth_ = 0;
  std::vector<std::unique_ptr<BlockLayout>> children_;
  std::vector<TableRowLayout> tableRows_;
};

}  // namespace muffin
