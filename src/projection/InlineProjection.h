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
  QString href;  // Non-empty for Image and Link Atom spans
};

struct InlineProjectionState {
  qsizetype cursorSourceOffset = -1;
  qsizetype cursorVisibleOffset = -1;
  qsizetype selectionSourceStart = -1;
  qsizetype selectionSourceEnd = -1;
  qsizetype selectionVisibleStart = -1;
  qsizetype selectionVisibleEnd = -1;
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

class InlineProjection {
public:
  InlineProjection() = default;
  InlineProjection(const QVector<InlineNode>& inlines, QString sourceText, InlineProjectionState state = {}, qsizetype sourceBase = -1,
                   qreal baseFontSize = 16.0, qsizetype headingPrefixLength = 0, qsizetype pendingPrefixLength = 0);

  bool isValid() const;
  QString sourceText() const;
  QString displayText() const;
  QString visibleText() const;
  const QVector<InlineProjectionSpan>& spans() const;
  const QVector<HtmlInlineFormatData>& htmlFormatData() const;
  QString linkHrefAtDisplayOffset(qsizetype displayOffset) const;

  bool sourceOffsetForVisibleOffset(qsizetype visibleOffset, qsizetype& sourceOffset) const;
  bool visibleOffsetForSourceOffset(qsizetype sourceOffset, qsizetype& visibleOffset) const;
  bool sourceOffsetForDisplayOffset(qsizetype displayOffset, qsizetype& sourceOffset) const;
  bool sourceOffsetForDisplayOffset(qsizetype displayOffset, InlineProjectionBias bias, qsizetype& sourceOffset) const;
  bool displayOffsetForSourceOffset(qsizetype sourceOffset, qsizetype& displayOffset) const;
  bool displayOffsetForSourceOffset(qsizetype sourceOffset, InlineProjectionBias bias, qsizetype& displayOffset) const;

  static QString plainTextForInlines(const QVector<InlineNode>& inlines);
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
    qreal baseFontSize = 16.0;
    QString displayText;
    QString visibleText;
    QVector<InlineProjectionSpan> spans;
    QVector<LinkRange> linkRanges;
  };

  static QString markerForInline(const InlineNode& node);
  static QString markdownForInline(const InlineNode& node);
  static QString plainTextForInline(const InlineNode& node);
  static void appendTextSpan(BuildState& state, InlineType type, InlineSpanKind kind, qsizetype sourceStart, qsizetype sourceEnd,
                             QString displayText, bool visible, bool editable = true);
  static void appendTextSpan(BuildState& state, InlineType type, InlineSpanKind kind, qsizetype sourceStart, qsizetype sourceEnd,
                             qsizetype contentSourceStart, qsizetype contentSourceEnd, QString displayText, bool visible,
                             bool editable = true);
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
