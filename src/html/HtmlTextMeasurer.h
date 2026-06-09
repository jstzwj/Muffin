#pragma once

#include "html/HtmlBox.h"

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTextLayout>

#include <memory>
#include <vector>

namespace muffin::html {

struct TextFormatSpan {
  int start;
  int length;
  bool bold = false;
  bool italic = false;
  HtmlTextDecoration decoration = HtmlTextDecoration::None;
  QColor color;
  QColor backgroundColor;
  bool monospace = false;
  qreal fontSize = 0;
  QTextCharFormat::VerticalAlignment verticalAlignment = QTextCharFormat::AlignNormal;
};

// Holds a pre-built QTextLayout for a text-containing box.
struct HtmlTextLayout {
  QString text;
  QFont font;
  qreal lineHeight = 0;
  std::unique_ptr<QTextLayout> layout;
  struct LinkSpan {
    int start = 0;
    int length = 0;
    QString href;
  };
  std::vector<LinkSpan> linkSpans;
  qreal width = 0;
  qreal height = 0;
};

// Measures text using QTextLayout, used as Yoga measurement callback.
class HtmlTextMeasurer {
public:
  // Measure a simple text run.
  QSizeF measure(const QString& text, const QFont& font, qreal availableWidth) const;

  // Build a full text layout for later painting.
  // Returns ownership of the layout object.
  std::unique_ptr<HtmlTextLayout> buildLayout(
      const QString& text,
      const QFont& font,
      qreal availableWidth,
      Qt::Alignment alignment = Qt::AlignLeft) const;

  // Measure an inline formatting context (block containing mixed inline children).
  // Collects all text from inline children and measures as one unit.
  QSizeF measureInlineContext(
      const HtmlBox& blockBox,
      qreal fontSize,
      qreal availableWidth) const;

  // Build layout for inline context with formatting spans.
  std::unique_ptr<HtmlTextLayout> buildInlineLayout(
      const HtmlBox& blockBox,
      qreal fontSize,
      qreal availableWidth,
      Qt::Alignment alignment = Qt::AlignLeft) const;

  std::unique_ptr<HtmlTextLayout> buildPreLayout(
      const HtmlBox& preBox,
      qreal fontSize,
      qreal availableWidth) const;

  // Collect inline text and formatting spans from a box subtree.
  // Public for use by InlineHtmlRenderer.
  void collectInlineTextFromRoot(
      const HtmlBox& box,
      QString& outText,
      std::vector<TextFormatSpan>& outSpans,
      std::vector<HtmlTextLayout::LinkSpan>& outLinks,
      int& offset,
      bool parentBold,
      bool parentItalic,
      bool parentMonospace,
      HtmlTextDecoration parentDecoration,
      QColor parentColor,
      QColor parentBackgroundColor,
      QTextCharFormat::VerticalAlignment parentVerticalAlignment,
      QString parentHref,
      qreal parentFontSize,
      qreal baseFontSize) const;

private:
  QString collectPlainText(const HtmlBox& box) const;

  void collectInlineText(
      const HtmlBox& box,
      QString& outText,
      std::vector<TextFormatSpan>& outSpans,
      std::vector<HtmlTextLayout::LinkSpan>& outLinks,
      int& offset,
      bool parentBold,
      bool parentItalic,
      bool parentMonospace,
      HtmlTextDecoration parentDecoration,
      QColor parentColor,
      QColor parentBackgroundColor,
      QTextCharFormat::VerticalAlignment parentVerticalAlignment,
      QString parentHref,
      qreal parentFontSize,
      qreal baseFontSize) const;
};

}  // namespace muffin::html
