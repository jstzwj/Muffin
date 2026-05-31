#include "render/MarkdownRenderWidget.h"

#include "document/InlineNode.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"

#include <QFrame>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

#include <cmath>

namespace muffin {
namespace {

constexpr int kContentWidth = 800;
constexpr int kHorizontalInset = 64;

QString escape(QString value) {
  return value.toHtmlEscaped();
}

QString paragraphCss(int zoomPercent) {
  const double fontSize = 16.0 * zoomPercent / 100.0;
  return QStringLiteral(
             "font-family:'Microsoft YaHei UI','Segoe UI',sans-serif;"
             "font-size:%1px;"
             "color:#202124;"
             "line-height:155%;"
             "margin-top:0;"
             "margin-bottom:12px;")
      .arg(fontSize);
}

QString headingCss(int level, int zoomPercent) {
  const double base = 16.0 * zoomPercent / 100.0;
  const double sizes[] = {2.15, 1.7, 1.35, 1.15, 1.0, 0.95};
  const double fontSize = base * sizes[qBound(0, level - 1, 5)];
  return QStringLiteral(
             "font-family:'Microsoft YaHei UI','Segoe UI',sans-serif;"
             "font-size:%1px;"
             "font-weight:700;"
             "line-height:125%;"
             "color:#202124;"
             "margin-top:%2px;"
             "margin-bottom:%3px;")
      .arg(fontSize)
      .arg(level == 1 ? 10 : 18)
      .arg(level <= 2 ? 12 : 8);
}

QString monoFontCss() {
  return QStringLiteral("Consolas,'Cascadia Mono','Microsoft YaHei UI Mono',monospace");
}

QString blockTextCss(int zoomPercent) {
  return QStringLiteral(
             "font-family:%1;"
             "font-size:%2px;"
             "line-height:145%;"
             "color:#24292f;"
             "background:#f6f8fa;"
             "border:1px solid #e5e7eb;"
             "padding:10px 12px;")
      .arg(monoFontCss())
      .arg(14.0 * zoomPercent / 100.0);
}

class RichTextBlock final : public QTextBrowser {
public:
  explicit RichTextBlock(QString html, QWidget* parent = nullptr) : QTextBrowser(parent) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setOpenExternalLinks(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral("QTextBrowser { background:#ffffff; border:0; padding:0; margin:0; }"));
    document()->setDocumentMargin(0);
    setHtml(std::move(html));
    updateDocumentHeight();
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QTextBrowser::resizeEvent(event);
    updateDocumentHeight();
  }

private:
  void updateDocumentHeight() {
    const int textWidth = qMax(1, viewport()->width());
    document()->setTextWidth(textWidth);
    setFixedHeight(qMax(1, static_cast<int>(std::ceil(document()->size().height())) + 2));
  }
};

}  // namespace

MarkdownRenderWidget::MarkdownRenderWidget(QWidget* parent) : QWidget(parent) {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  scrollArea_ = new QScrollArea(this);
  scrollArea_->setFrameShape(QFrame::NoFrame);
  scrollArea_->setWidgetResizable(true);
  scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  rootLayout->addWidget(scrollArea_);

  canvas_ = new QWidget(scrollArea_);
  auto* canvasLayout = new QVBoxLayout(canvas_);
  canvasLayout->setContentsMargins(0, 38, 0, 56);
  canvasLayout->setSpacing(0);

  body_ = new QWidget(canvas_);
  bodyLayout_ = new QVBoxLayout(body_);
  bodyLayout_->setContentsMargins(0, 0, 0, 0);
  bodyLayout_->setSpacing(0);

  canvasLayout->addWidget(body_, 0, Qt::AlignHCenter | Qt::AlignTop);
  canvasLayout->addStretch(1);

  scrollArea_->setWidget(canvas_);
  setStyleSheet(QStringLiteral(
      "MarkdownRenderWidget, QScrollArea, QWidget { background:#ffffff; }"
      "QScrollArea { border:0; }"
      "QScrollBar:vertical {"
      "  background:#ffffff;"
      "  width:10px;"
      "  margin:0;"
      "}"
      "QScrollBar::handle:vertical {"
      "  background:#b8b8b8;"
      "  min-height:42px;"
      "  border-radius:4px;"
      "  margin:2px;"
      "}"
      "QScrollBar::handle:vertical:hover { background:#8f8f8f; }"
      "QScrollBar::add-line:vertical,"
      "QScrollBar::sub-line:vertical {"
      "  height:0;"
      "  border:0;"
      "  background:transparent;"
      "}"
      "QScrollBar::add-page:vertical,"
      "QScrollBar::sub-page:vertical { background:transparent; }"
      "QScrollBar:horizontal {"
      "  background:#ffffff;"
      "  height:10px;"
      "  margin:0;"
      "}"
      "QScrollBar::handle:horizontal {"
      "  background:#b8b8b8;"
      "  min-width:42px;"
      "  border-radius:4px;"
      "  margin:2px;"
      "}"
      "QScrollBar::handle:horizontal:hover { background:#8f8f8f; }"
      "QScrollBar::add-line:horizontal,"
      "QScrollBar::sub-line:horizontal {"
      "  width:0;"
      "  border:0;"
      "  background:transparent;"
      "}"
      "QScrollBar::add-page:horizontal,"
      "QScrollBar::sub-page:horizontal { background:transparent; }"));
  updateBodyWidth();
}

void MarkdownRenderWidget::setDocument(const MarkdownDocument& document) {
  rebuild(document.root());
}

void MarkdownRenderWidget::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
}

void MarkdownRenderWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  updateBodyWidth();
}

void MarkdownRenderWidget::rebuild(const MarkdownNode& root) {
  clearBody();
  for (const auto& child : root.children()) {
    addBlock(*child);
  }
  bodyLayout_->addStretch(1);
}

void MarkdownRenderWidget::clearBody() {
  while (QLayoutItem* item = bodyLayout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
}

void MarkdownRenderWidget::updateBodyWidth() {
  if (!body_) {
    return;
  }
  const int availableWidth = qMax(0, width() - kHorizontalInset * 2);
  const int targetWidth = qMin(kContentWidth, availableWidth > 0 ? availableWidth : kContentWidth);
  body_->setFixedWidth(targetWidth);
}

void MarkdownRenderWidget::addBlock(const MarkdownNode& node, int depth) {
  switch (node.type()) {
    case BlockType::CodeFence:
      addCodeBlock(node);
      break;
    case BlockType::MathBlock:
      addMathBlock(node);
      break;
    case BlockType::ThematicBreak:
      addHorizontalRule();
      break;
    case BlockType::Document:
      for (const auto& child : node.children()) {
        addBlock(*child, depth);
      }
      break;
    default:
      addRichBlock(renderBlockHtml(node, depth));
      break;
  }
}

void MarkdownRenderWidget::addRichBlock(const QString& html, const QString& css) {
  if (html.trimmed().isEmpty()) {
    return;
  }
  auto* block = new RichTextBlock(html, body_);
  if (!css.isEmpty()) {
    block->setStyleSheet(css);
  }
  bodyLayout_->addWidget(block);
}

void MarkdownRenderWidget::addCodeBlock(const MarkdownNode& node) {
  auto* label = new QLabel(body_);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(false);
  label->setMargin(0);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setStyleSheet(blockTextCss(zoomPercent_));
  label->setText(node.literal().trimmed());
  bodyLayout_->addSpacing(4);
  bodyLayout_->addWidget(label);
  bodyLayout_->addSpacing(16);
}

void MarkdownRenderWidget::addMathBlock(const MarkdownNode& node) {
  auto* label = new QLabel(body_);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setMargin(0);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setStyleSheet(QStringLiteral(
                           "font-family:Cambria Math,'Times New Roman',serif;"
                           "font-size:%1px;"
                           "line-height:150%;"
                           "color:#24292f;"
                           "background:#fbfbfb;"
                           "border-left:3px solid #d0d7de;"
                           "padding:10px 14px;")
                           .arg(17.0 * zoomPercent_ / 100.0));
  label->setText(node.literal().trimmed());
  bodyLayout_->addSpacing(4);
  bodyLayout_->addWidget(label);
  bodyLayout_->addSpacing(16);
}

void MarkdownRenderWidget::addHorizontalRule() {
  auto* line = new QFrame(body_);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Plain);
  line->setStyleSheet(QStringLiteral("color:#e5e5e5; background:#e5e5e5;"));
  bodyLayout_->addSpacing(14);
  bodyLayout_->addWidget(line);
  bodyLayout_->addSpacing(14);
}

QString MarkdownRenderWidget::renderBlockHtml(const MarkdownNode& node, int depth) const {
  switch (node.type()) {
    case BlockType::Paragraph:
      return QStringLiteral("<p style=\"%1\">%2</p>").arg(paragraphCss(zoomPercent_), renderInlines(node.inlines()));
    case BlockType::Heading:
      return QStringLiteral("<h%1 style=\"%2\">%3</h%1>")
          .arg(qBound(1, node.headingLevel(), 6))
          .arg(headingCss(node.headingLevel(), zoomPercent_))
          .arg(renderInlines(node.inlines()));
    case BlockType::CodeFence:
    case BlockType::MathBlock:
      return QString();
    case BlockType::BlockQuote:
      return QStringLiteral(
                 "<blockquote style=\"%1 border-left:4px solid #d0d7de;"
                 "color:#57606a;margin-top:4px;margin-bottom:14px;padding-left:14px;\">%2</blockquote>")
          .arg(paragraphCss(zoomPercent_), renderChildrenHtml(node, depth + 1));
    case BlockType::List: {
      const QString tag = node.listKind() == ListKind::Ordered ? QStringLiteral("ol") : QStringLiteral("ul");
      return QStringLiteral("<%1 style=\"%2 margin-top:0;margin-bottom:12px;padding-left:%3px;\">%4</%1>")
          .arg(tag, paragraphCss(zoomPercent_))
          .arg(24 + depth * 14)
          .arg(renderChildrenHtml(node, depth + 1));
    }
    case BlockType::ListItem:
      return renderListItemHtml(node, depth);
    case BlockType::Table:
      return QStringLiteral(
                 "<table cellspacing=\"0\" cellpadding=\"0\" style=\"border-collapse:collapse;width:100%;"
                 "font-family:'Microsoft YaHei UI','Segoe UI',sans-serif;font-size:%1px;"
                 "line-height:1.45;margin:8px 0 18px 0;\">%2</table>")
          .arg(15.0 * zoomPercent_ / 100.0)
          .arg(renderChildrenHtml(node, depth));
    case BlockType::TableRow:
      return QStringLiteral("<tr>%1</tr>").arg(renderChildrenHtml(node, depth));
    case BlockType::TableCell: {
      const QString tag = node.tableRowIsHeader() ? QStringLiteral("th") : QStringLiteral("td");
      return QStringLiteral(
                 "<%1 style=\"border:1px solid #d0d7de;padding:6px 10px;text-align:left;"
                 "font-weight:%2;\">%3</%1>")
          .arg(tag, node.tableRowIsHeader() ? QStringLiteral("700") : QStringLiteral("400"), renderInlines(node.inlines()));
    }
    case BlockType::HtmlBlock:
      return QStringLiteral("<pre style=\"%1 white-space:pre-wrap;\">%2</pre>").arg(paragraphCss(zoomPercent_), escape(node.literal()));
    default:
      if (!node.inlines().isEmpty()) {
        return QStringLiteral("<p style=\"%1\">%2</p>").arg(paragraphCss(zoomPercent_), renderInlines(node.inlines()));
      }
      return renderChildrenHtml(node, depth);
  }
}

QString MarkdownRenderWidget::renderChildrenHtml(const MarkdownNode& node, int depth) const {
  QString html;
  for (const auto& child : node.children()) {
    const QString childHtml = renderBlockHtml(*child, depth);
    if (!childHtml.isEmpty()) {
      html += childHtml;
    }
  }
  return html;
}

QString MarkdownRenderWidget::renderListItemHtml(const MarkdownNode& node, int depth) const {
  QString html = QStringLiteral("<li style=\"margin-top:0;margin-bottom:4px;\">");
  bool first = true;

  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      const QString content = renderInlines(child->inlines());
      html += first ? content : QStringLiteral("<p style=\"%1 margin-bottom:4px;\">%2</p>").arg(paragraphCss(zoomPercent_), content);
    } else {
      const QString childHtml = renderBlockHtml(*child, depth + 1);
      if (!childHtml.isEmpty()) {
        html += childHtml;
      }
    }
    first = false;
  }

  if (first && !node.inlines().isEmpty()) {
    html += renderInlines(node.inlines());
  }
  html += QStringLiteral("</li>");
  return html;
}

QString MarkdownRenderWidget::renderInlines(const QVector<InlineNode>& inlines) const {
  QString html;
  for (const InlineNode& node : inlines) {
    html += renderInline(node);
  }
  return html;
}

QString MarkdownRenderWidget::renderInline(const InlineNode& node) const {
  switch (node.type()) {
    case InlineType::Text:
      return escape(node.text());
    case InlineType::SoftBreak:
      return QStringLiteral(" ");
    case InlineType::LineBreak:
      return QStringLiteral("<br/>");
    case InlineType::Code:
      return QStringLiteral("<code style=\"font-family:%1;background:#f6f8fa;"
                            "border:1px solid #e5e7eb;padding:1px 4px;\">%2</code>")
          .arg(monoFontCss())
          .arg(escape(node.text()));
    case InlineType::Emphasis:
      return QStringLiteral("<em>%1</em>").arg(renderInlines(node.children()));
    case InlineType::Strong:
      return QStringLiteral("<strong>%1</strong>").arg(renderInlines(node.children()));
    case InlineType::Strikethrough:
      return QStringLiteral("<s>%1</s>").arg(renderInlines(node.children()));
    case InlineType::Link:
      return QStringLiteral("<a href=\"%1\" style=\"color:#2f6fbd;text-decoration:none;\">%2</a>")
          .arg(escape(node.href()), renderInlines(node.children()));
    case InlineType::Image:
      return QStringLiteral("<span style=\"color:#57606a;\">[%1]</span>").arg(escape(node.alt()));
    case InlineType::InlineMath:
      return QStringLiteral("<span style=\"font-family:Cambria Math,'Times New Roman',serif;\">%1</span>")
          .arg(escape(node.text()));
    case InlineType::HtmlInline:
      return escape(node.text());
    default:
      return renderInlines(node.children());
  }
}

}  // namespace muffin
