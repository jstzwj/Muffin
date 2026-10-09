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
  bool keyboard = false;
  qreal fontSize = 0;
  QTextCharFormat::VerticalAlignment verticalAlignment = QTextCharFormat::AlignNormal;
  QStringList fontFamilies;
  QFont font;
  bool fontSet = false;
  ThemeElementStyle inlineBoxStyle;
  ThemeElementBoxStyle inlineUsedBox;
  quintptr inlineBoxId = 0;
  qreal lineHeight = 0;
};

// Holds a pre-built QTextLayout for a text-containing box.
struct HtmlTextLayout {
  struct TextSourceSpan {
    int start = 0, length = 0, textStart = 0;
  };
  std::vector<TextSourceSpan> sourceSpans;
  struct AtomicInline {
    HtmlBox* box = nullptr;
    int start = 0;
    QSizeF size;
    qreal baseline = 0;
    QMarginsF margin;
    QRectF rect;
  };
  std::vector<AtomicInline> atoms;
  QString text;
  QFont font;
  qreal lineHeight = 0;
  std::unique_ptr<QTextLayout> layout;
  struct LinkSpan {
    int start = 0;
    int length = 0;
    QString href;
  };
  // These ranges address text; QTextLayout formats and links address its buffer,
  // which additionally contains zero-content spacers for inline box edges.
  std::vector<TextFormatSpan> formatSpans;
  std::vector<LayoutBox> inlineBoxes;
  std::vector<QRectF> lineBoxes;
  std::vector<LinkSpan> linkSpans;
  qreal width = 0;
  qreal height = 0;
};

// Measures text using QTextLayout, used as Yoga measurement callback.
class HtmlTextMeasurer {
public:
 using AtomicLayout = std::function<HtmlTextLayout::AtomicInline(HtmlBox&, qreal)>;
 void setAtomicLayout(AtomicLayout callback) { atomicLayout_ = std::move(callback); }
 // Measure a simple text run.
 QSizeF measure(const QString& text, const QFont& font, qreal availableWidth) const;

 // Build a full text layout for later painting.
 // Returns ownership of the layout object.
 std::unique_ptr<HtmlTextLayout> buildLayout(const QString& text, const QFont& font, qreal availableWidth,
                                             Qt::Alignment alignment = Qt::AlignLeft) const;

 // Measure an inline formatting context (block containing mixed inline children).
 // Collects all text from inline children and measures as one unit.
 QSizeF measureInlineContext(const HtmlBox& blockBox, qreal fontSize, qreal availableWidth) const;

 // Build layout for inline context with formatting spans.
 std::unique_ptr<HtmlTextLayout> buildInlineLayout(const HtmlBox& blockBox, qreal fontSize, qreal availableWidth,
                                                   Qt::Alignment alignment = Qt::AlignLeft) const;

 std::unique_ptr<HtmlTextLayout> buildPreLayout(const HtmlBox& preBox, qreal fontSize, qreal availableWidth) const;

 // Default text colour applied to inline runs that specify no colour of their
 // own. Set by the layout engine from the theme palette; invalid (the default)
 // leaves runs uncoloured so the paint layer decides — preserving the
 // inline-HTML path's behaviour.
 void setDefaultTextColor(QColor color);

 // Collect inline text and formatting spans from a box subtree.
 // Public for use by InlineHtmlRenderer.
 void collectInlineTextFromRoot(const HtmlBox& box, QString& outText, std::vector<TextFormatSpan>& outSpans,
                                std::vector<HtmlTextLayout::LinkSpan>& outLinks, int& offset, bool parentBold, bool parentItalic,
                                bool parentMonospace, bool parentKeyboard, HtmlTextDecoration parentDecoration, QColor parentColor,
                                QColor parentBackgroundColor, QTextCharFormat::VerticalAlignment parentVerticalAlignment,
                                QString parentHref, qreal parentFontSize, qreal baseFontSize) const;

private:
 AtomicLayout atomicLayout_;
 QColor defaultTextColor_;
 QString collectPlainText(const HtmlBox& box) const;

 void collectInlineText(const HtmlBox& box, QString& outText, std::vector<TextFormatSpan>& outSpans,
                        std::vector<HtmlTextLayout::LinkSpan>& outLinks, int& offset, bool parentBold, bool parentItalic,
                        bool parentMonospace, bool parentKeyboard, HtmlTextDecoration parentDecoration, QColor parentColor,
                        QColor parentBackgroundColor, QTextCharFormat::VerticalAlignment parentVerticalAlignment, QString parentHref,
                        qreal parentFontSize, qreal baseFontSize, std::vector<HtmlTextLayout::AtomicInline>* atoms = nullptr,
                        qreal availableWidth = -1, const HtmlBox* contextRoot = nullptr,
                        std::vector<HtmlTextLayout::TextSourceSpan>* sources = nullptr) const;
};

}  // namespace muffin::html
