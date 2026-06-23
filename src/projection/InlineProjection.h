#pragma once

#include "document/InlineNode.h"
#include "document/TextSelection.h"
#include "html/HtmlTextMeasurer.h"

#include <QString>
#include <QtGlobal>
#include <QVector>

#include <vector>

namespace muffin {

enum class InlineProjectionBias {
  Backward,
  Forward
};

enum class InlineSpanKind {
  Text,
  OpenMarker,
  CloseMarker,
  EmptyContentSlot,
  HiddenSyntax,
  Atom,
  HtmlContent
};

// Length (in QChars) of a complete <br>/<br/>/<br /> tag starting at content[offset]
// (case-insensitive), or 0 if no tag starts there. A complete tag requires its
// closing '>', so a half-typed "<br" returns 0. Shared by the render path
// (InlineProjection) and the table-cell edit bridge (TableCellSourceEdit) so both
// recognize every br spelling consistently.
int brTagLengthAt(const QString& content, qsizetype offset);
// True when text is exactly a standalone <br>/<br/>/<br /> tag.
bool isStandaloneBrTag(const QString& text);

struct LinkRange {
  qsizetype displayStart = 0;
  qsizetype displayEnd = 0;
  QString href;
};

struct HtmlInlineFormatData {
  std::vector<html::TextFormatSpan> formatSpans;
  std::vector<html::HtmlTextLayout::LinkSpan> links;
  qsizetype displayStart = 0;
};

struct InlineProjectionSpan {
  InlineType type = InlineType::Unknown;
  InlineSpanKind kind = InlineSpanKind::Text;
  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  qsizetype contentSourceStart = 0;
  qsizetype contentSourceEnd = 0;
  qsizetype displayStart = 0;
  qsizetype displayEnd = 0;
  qsizetype visibleStart = 0;
  qsizetype visibleEnd = 0;
  bool editable = true;
  bool bold = false;
  bool italic = false;
  bool strike = false;
  bool underline = false;
  bool link = false;          // Wrapping attribute ORTHOGONAL to `type`: this span is part of a link →
                              // link color/underline applied in InlineLayout. Decoupled from `type` so a
                              // link composes with any inner node (image/code/math/emphasis…) instead of
                              // overwriting it — this is what makes an image-link [![alt](img)](url) render
                              // as a clickable image, and keeps `[`code`](url)` rendered as code.
  bool highlight = false;     // ==text==: yellow wash propagated onto content spans
  bool subscript = false;     // ~text~: lowered baseline propagated onto content spans
  bool superscript = false;   // ^text^: raised baseline propagated onto content spans
  bool folded = false;        // Render-level smart-punct token (e.g. "--"->en-dash): source range is
                              // wider than the single display glyph, so edits/delete act on the whole token.
  QString href;  // Non-empty for Image and Link Atom spans
};

// What makes an inline's markdown markers (e.g. `**`, `$`, `~~`) appear in the projection.
// Reveal follows the ACTIVE caret — the selection's focus: a marker shows when the focus sits inside
// the inline. The selection's EXTENT never reveals markers, so dragging across many inlines doesn't
// fan out a cascade of revealed syntax (only the inline the focus is in is revealed). `revealMarkdownMarkers`
// is the global override (syntax-view / tests).
struct InlineProjectionState {
  qsizetype cursorSourceOffset = -1;
  qsizetype cursorVisibleOffset = -1;
  bool revealMarkdownMarkers = false;

  bool shouldRevealSourceRange(qsizetype sourceStart, qsizetype sourceEnd) const;
  bool shouldRevealVisibleRange(qsizetype visibleStart, qsizetype visibleEnd) const;

  static InlineProjectionState forCursor(
      const CursorPosition& cursor,
      NodeId blockId,
      qsizetype contentSourceStart);
  static InlineProjectionState forSelection(
      const SelectionRange& selection,
      NodeId blockId,
      qsizetype contentSourceStart);
};

// Display-only smart punctuation (SmartyPants) for the render path: ASCII quotes/dashes/ellipsis
// are shown as their Unicode forms without touching the Markdown source. Mirrors the input-level
// conversion (InputController) so the two paths stay consistent.
struct SmartPunctRenderOptions {
  bool convertQuotes = false;
  bool convertDashes = false;
  bool convertEllipsis = false;
  int doubleQuoteStyle = 0;  // 0 = curly, 1 = straight (mirrors markdown/doubleQuoteStyle)
  int singleQuoteStyle = 0;  // 0 = curly, 1 = straight
};

class InlineProjection {
public:
  InlineProjection() = default;
  InlineProjection(const QVector<InlineNode>& inlines, QString sourceText, InlineProjectionState state = {}, qsizetype sourceBase = -1,
                   qreal baseFontSize = 16.0, qsizetype pendingPrefixLength = 0,
                   SmartPunctRenderOptions smartPunct = {}, bool breakOnSingleNewline = false);

  bool isValid() const;
  QString sourceText() const;
  QString displayText() const;
  QString visibleText() const;
  const QVector<InlineProjectionSpan>& spans() const;
  // Render-level folded-token queries (Convert on Rendering). `offset` is source-relative to this
  // projection's sourceText (block-content-relative). For deletion: when the caret sits at a folded
  // token's boundary in `direction`, returns the whole token's source range so backspace/delete
  // removes it atomically. Interior: when `offset` lies strictly inside a folded token, returns its
  // range so cursor movement jumps to the boundary instead of stopping mid-token.
  bool foldedTokenForDeletion(qsizetype offset, int direction, qsizetype& start, qsizetype& end) const;
  bool foldedSpanInterior(qsizetype offset, qsizetype& start, qsizetype& end) const;
  const QVector<HtmlInlineFormatData>& htmlFormatData() const;
  QString linkHrefAtDisplayOffset(qsizetype displayOffset) const;

  bool sourceOffsetForVisibleOffset(qsizetype visibleOffset, qsizetype& sourceOffset) const;
  bool visibleOffsetForSourceOffset(qsizetype sourceOffset, qsizetype& visibleOffset) const;
  bool sourceOffsetForDisplayOffset(qsizetype displayOffset, qsizetype& sourceOffset) const;
  bool sourceOffsetForDisplayOffset(qsizetype displayOffset, InlineProjectionBias bias, qsizetype& sourceOffset) const;
  bool displayOffsetForSourceOffset(qsizetype sourceOffset, qsizetype& displayOffset) const;
  bool displayOffsetForSourceOffset(qsizetype sourceOffset, InlineProjectionBias bias, qsizetype& displayOffset) const;

  static QString plainTextForInlines(const QVector<InlineNode>& inlines, bool breakOnSingleNewline = false);
  static QString markdownForInlines(const QVector<InlineNode>& inlines);
  static bool isPlainInlineSource(const QVector<InlineNode>& inlines, const QString& sourceText, qsizetype sourceBase = -1);

private:
  struct BuildState {
    const QString* sourceText = nullptr;
    qsizetype sourceBase = -1;
    InlineProjectionState projectionState;
    qsizetype displayOffset = 0;
    qsizetype visibleOffset = 0;
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool underline = false;
    bool highlight = false;
    bool subscript = false;
    bool superscript = false;
    qreal baseFontSize = 16.0;
    SmartPunctRenderOptions smartPunct;
    // markdown/breakOnSingleNewline (default on): render a single '\n' soft break as a line break
    // instead of joining it into one paragraph line (CommonMark).
    bool breakOnSingleNewline = false;
    QString displayText;
    QString visibleText;
    QVector<InlineProjectionSpan> spans;
    QVector<LinkRange> linkRanges;
  };

  static QString markerForInline(const InlineNode& node);
  static QString markdownForInline(const InlineNode& node);
  static QString plainTextForInline(const InlineNode& node, bool breakOnSingleNewline = false);
  static void appendTextSpan(BuildState& state, InlineType type, InlineSpanKind kind, qsizetype sourceStart, qsizetype sourceEnd,
                             QString displayText, bool visible, bool editable = true);
  static void appendTextSpan(BuildState& state, InlineType type, InlineSpanKind kind, qsizetype sourceStart, qsizetype sourceEnd,
                             qsizetype contentSourceStart, qsizetype contentSourceEnd, QString displayText, bool visible,
                             bool editable = true);
  // Emits a <br> tag as a gray OpenMarker (the markup) + a zero-width-source line break.
  // Centralized so every HtmlInline render path — top-level appendInlines and appendInline
  // (for <br> nested inside paired inline HTML like <b>..<br>..</b>) — agrees with
  // plainTextForInline/flattenPlainText, which decode <br> to '\n'.
  static void appendBrLineBreak(BuildState& state, qsizetype sourceStart, qsizetype sourceEnd, const QString& tagText);
  // Emits one or more Text spans for a decoded run, applying render-level smart punctuation
  // (Convert on Rendering). Folded tokens (--/---/...) become their own span with folded=true so
  // the N:1 source/display mapping stays exact (no per-span linear drift); quotes are 1:1.
  static void appendSmartPunctTextSpans(BuildState& state, qsizetype sourceStart, qsizetype sourceEnd, const QString& decoded);
  static bool appendHtmlImageAtom(BuildState& state, const QString& tagText, qsizetype sourceStart, qsizetype sourceEnd,
                                  qsizetype contentSourceStart, qsizetype contentSourceEnd);
  static void appendInlines(BuildState& state, const QVector<InlineNode>& inlines, qsizetype sourceStart, qsizetype sourceEnd,
                            QVector<HtmlInlineFormatData>& htmlFormatData);
  static void appendInline(BuildState& state, const InlineNode& node, qsizetype sourceStart, qsizetype sourceEnd,
                           QVector<HtmlInlineFormatData>& htmlFormatData);
  static int tryAppendHtmlInlineGroup(BuildState& state, const QVector<InlineNode>& inlines, int index,
                                      qsizetype sourceStart, qsizetype sourceEnd, qsizetype& searchFrom,
                                      QVector<HtmlInlineFormatData>& htmlFormatData);
  static void appendHtmlInlineContent(BuildState& state, const QVector<InlineNode>& inlines,
                                      int startIndex, int endIndex, qsizetype openEnd, qsizetype closeNodeStart,
                                      QVector<HtmlInlineFormatData>& htmlFormatData);
  static qsizetype findMarkdown(const QString& sourceText, const QString& markdown, qsizetype searchFrom, qsizetype searchEnd);
  bool offsetInSource(qsizetype sourceOffset) const;

  QString sourceText_;
  QString displayText_;
  QString visibleText_;
  QVector<InlineProjectionSpan> spans_;
  QVector<LinkRange> linkRanges_;
  QVector<HtmlInlineFormatData> htmlFormatData_;
  bool valid_ = false;
};

}  // namespace muffin
