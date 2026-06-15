#pragma once

#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownTypes.h"
#include "document/SourceRangeUtil.h"
#include "edit/EditTransaction.h"
#include "editor/BlockEditContext.h"
#include "editor/EditorContextHolder.h"

#include <QObject>

namespace muffin {

class MarkdownNode;

class StylizeController final : public QObject, private EditorContextHolder {
  Q_OBJECT

public:
  explicit StylizeController(QObject* parent = nullptr);

  using EditorContextHolder::setContext;

  bool toggleBold();
  bool toggleItalic();
  bool toggleCode();
  bool toggleStrikethrough();
  bool toggleInlineMath();
  bool toggleUnderline();
  bool insertLink();
  bool insertImage();

  // Strip every inline formatting marker from the paragraph holding the cursor
  // (bold/italic/strikethrough/code/underline, and link syntax → label text),
  // leaving content such as inline math and images untouched.
  bool clearFormatting();

signals:
  void unsupportedStyleRequested(QString reason);

private:
  // --- Main dispatch ---
  bool toggleStyle(InlineType type, QString openMarker, QString closeMarker,
                   EditTransaction::Kind kind, QString label);

  // --- Single-block operations ---
  bool toggleCollapsed(const BlockEditContext& context, InlineType type,
                       const QString& openMarker, const QString& closeMarker,
                       EditTransaction::Kind kind, const QString& label);
  bool toggleRange(const BlockEditContext& context,
                   qsizetype localSelStart, qsizetype localSelEnd,
                   InlineType type,
                   const QString& openMarker, const QString& closeMarker,
                   EditTransaction::Kind kind, const QString& label);

  // --- Multi-block operation ---
  bool toggleMultiBlock(InlineType type,
                        const QString& openMarker, const QString& closeMarker,
                        EditTransaction::Kind kind, const QString& label);

  // --- Style detection ---
  bool hasStyleInRange(const InlineProjection& projection, InlineType type,
                       qsizetype localSourceStart, qsizetype localSourceEnd) const;

  // --- InlineNode tree helpers ---
  const InlineNode* findWrappingNode(const QVector<InlineNode>& inlines,
                                     InlineType type,
                                     qsizetype contentBase,
                                     qsizetype localSourceOffset) const;

  struct MarkerSpan {
    qsizetype localStart = 0;
    qsizetype localEnd = 0;
  };

  QVector<MarkerSpan> collectOverlappingMarkers(
      const QVector<InlineNode>& inlines, InlineType targetType,
      qsizetype contentBase,
      qsizetype localSelStart, qsizetype localSelEnd) const;

  void collectOverlappingMarkersRecursive(
      const QVector<InlineNode>& inlines, InlineType targetType,
      qsizetype contentBase,
      qsizetype localSelStart, qsizetype localSelEnd,
      QVector<MarkerSpan>& markers) const;

  // Collect the delimiter-syntax source spans of every styling node (the source
  // region lying outside each node's content range) so they can be stripped in
  // one pass. Content (math, images, label text) is left in place.
  void collectClearFormattingSpans(const QVector<InlineNode>& inlines,
                                   qsizetype contentBase,
                                   QVector<MarkerSpan>& spans) const;

  // --- Word range helper (for collapsed cursor → word-wrap) ---
  bool findWordRange(const BlockEditContext& context,
                     qsizetype& localSourceStart,
                     qsizetype& localSourceEnd) const;

  // --- Underline toggle (HTML <u> tags, separate from Markdown markers) ---
  bool toggleUnderlineCollapsed(const BlockEditContext& context);
  bool toggleUnderlineRange(const BlockEditContext& context,
                            qsizetype localSelStart, qsizetype localSelEnd);
  bool hasUnderlineInRange(const QString& contentText,
                           qsizetype localSourceStart,
                           qsizetype localSourceEnd) const;
  QVector<MarkerSpan> collectHtmlUnderlineMarkers(
      const QString& contentText,
      qsizetype localSelStart, qsizetype localSelEnd) const;

  // --- Content builder ---
  struct ToggledContent {
    QString text;
    qsizetype adjustedSelStart = 0;
    qsizetype adjustedSelEnd = 0;
  };

  ToggledContent buildToggledContent(
      const QString& contentText,
      const QVector<MarkerSpan>& markers,
      qsizetype localSelStart, qsizetype localSelEnd,
      bool allHasStyle,
      const QString& openMarker, const QString& closeMarker) const;

  // --- Delta application (unchanged) ---
  bool applyStyleDelta(
      EditTransaction::Kind kind,
      const QString& label,
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      qsizetype nextAnchorSourceOffset,
      qsizetype nextFocusSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {});
  CursorPosition cursorForSourceOffset(qsizetype sourceOffset) const;
};

}  // namespace muffin
