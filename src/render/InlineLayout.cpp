#include "render/InlineLayout.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>

namespace muffin {
namespace {

QString escaped(QString value) {
  return value.toHtmlEscaped();
}

QString flattenPlainText(const QVector<InlineNode>& inlines) {
  QString text;
  for (const InlineNode& node : inlines) {
    switch (node.type()) {
      case InlineType::Text:
      case InlineType::Code:
      case InlineType::InlineMath:
      case InlineType::HtmlInline:
        text += node.text();
        break;
      case InlineType::SoftBreak:
        text += QLatin1Char(' ');
        break;
      case InlineType::LineBreak:
        text += QLatin1Char('\n');
        break;
      case InlineType::Image:
        text += node.alt();
        break;
      default:
        text += flattenPlainText(node.children());
        break;
    }
  }
  return text;
}

}  // namespace

void InlineLayout::build(
    const QVector<InlineNode>& inlines,
    const RenderTheme& theme,
    qreal width,
    const QFont& baseFont) {
  plainText_ = flattenPlainText(inlines);
  html_ = renderInlines(inlines, theme);
  document_ = std::make_unique<QTextDocument>();
  document_->setDefaultFont(baseFont);
  document_->setDocumentMargin(0);
  document_->setTextWidth(qMax<qreal>(1.0, width));
  document_->setHtml(QStringLiteral(
                         "<html><head><style>"
                         "body { margin:0; color:%1; }"
                         "p { margin:0; line-height:1.45; }"
                         "a { color:%2; text-decoration:none; }"
                         "code { font-family:'Cascadia Mono','Consolas',monospace; background:%3;"
                         " border:1px solid %4; padding:1px 4px; }"
                         ".math { font-family:'Cambria Math','Times New Roman',serif; }"
                         "</style></head><body><p>%5</p></body></html>")
                         .arg(
                             cssColor(theme.textColor()),
                             cssColor(theme.linkColor()),
                             cssColor(theme.codeBackgroundColor()),
                             cssColor(theme.codeBorderColor()),
                             html_));
  size_ = document_->size();
}

QSizeF InlineLayout::size() const {
  return size_;
}

qreal InlineLayout::height() const {
  return size_.height();
}

void InlineLayout::paint(QPainter& painter, QPointF origin) const {
  if (!document_) {
    return;
  }

  painter.save();
  painter.translate(origin);
  QAbstractTextDocumentLayout::PaintContext context;
  document_->documentLayout()->draw(&painter, context);
  painter.restore();
}

QString InlineLayout::plainText() const {
  return plainText_;
}

QString InlineLayout::html() const {
  return html_;
}

QString InlineLayout::renderInlines(const QVector<InlineNode>& inlines, const RenderTheme& theme) const {
  QString html;
  for (const InlineNode& node : inlines) {
    html += renderInline(node, theme);
  }
  return html;
}

QString InlineLayout::renderInline(const InlineNode& node, const RenderTheme& theme) const {
  switch (node.type()) {
    case InlineType::Text:
      return escaped(node.text());
    case InlineType::SoftBreak:
      return QStringLiteral(" ");
    case InlineType::LineBreak:
      return QStringLiteral("<br/>");
    case InlineType::Code:
      return QStringLiteral("<code>%1</code>").arg(escaped(node.text()));
    case InlineType::Emphasis:
      return QStringLiteral("<em>%1</em>").arg(renderInlines(node.children(), theme));
    case InlineType::Strong:
      return QStringLiteral("<strong>%1</strong>").arg(renderInlines(node.children(), theme));
    case InlineType::Strikethrough:
      return QStringLiteral("<s>%1</s>").arg(renderInlines(node.children(), theme));
    case InlineType::Link:
      return QStringLiteral("<a href=\"%1\">%2</a>").arg(escaped(node.href()), renderInlines(node.children(), theme));
    case InlineType::Image:
      return QStringLiteral("<span style=\"color:%1;\">[%2]</span>").arg(cssColor(theme.mutedTextColor()), escaped(node.alt()));
    case InlineType::InlineMath:
      return QStringLiteral("<span class=\"math\">%1</span>").arg(escaped(node.text()));
    case InlineType::HtmlInline:
      return escaped(node.text());
    default:
      return renderInlines(node.children(), theme);
  }
}

QString InlineLayout::cssColor(const QColor& color) const {
  return color.name(QColor::HexRgb);
}

}  // namespace muffin
