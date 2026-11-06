#include "export/NativeExporter.h"

#include "render/DocumentLayout.h"
#include "document/MarkdownDocument.h"

#include <QImage>
#include <QPainter>
#include <QPrinter>
#include <QDate>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

namespace muffin {

bool exportNativePdf(const QString& path, const MarkdownDocument& document, RenderTheme theme,
                     const QString& sourcePath, const PdfExportOptions& options,
                     mermaid::editor::MermaidRenderCache* cache) {
  QPrinter printer;
  printer.setResolution(96);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  if (!printer.setPageLayout(options.pageLayout))
    return false;
  const QRectF page = printer.pageRect(QPrinter::DevicePixel);
  if (page.isEmpty())
    return false;
  theme.updateForViewport(page.width(), page.height());
  QString title = QFileInfo(sourcePath).completeBaseName();
  for (const auto& heading : document.outline()) {
    if (heading.level == 1 || title.isEmpty()) {
      title = heading.title;
      break;
    }
  }
  printer.setDocName(title);
  printer.setCreator(QStringLiteral("Muffin"));
  QFont decorationFont = theme.paragraphFont();
  decorationFont.setPixelSize(11);
  const QFontMetricsF metrics(decorationFont, &printer);
  const qreal bandHeight = std::ceil(metrics.height()) + 12;
  const qreal headerHeight = options.header.isEmpty() ? 0 : bandHeight;
  const qreal footerHeight = options.footer.isEmpty() && !options.pageNumbers ? 0 : bandHeight;
  const qreal contentHeight = page.height() - headerHeight - footerHeight;
  if (contentHeight < 1)
    return false;
  DocumentLayout layout;
  layout.setMermaidRenderCache(cache);
  layout.setMermaidSyncMode(true);
  layout.rebuild(document, theme, page.width(), sourcePath);
  QVector<QPair<qreal, qreal>> rows;
  const auto addRow = [&](QRectF rect) {
    if (rect.height() > 0 && rect.height() < contentHeight)
      rows.append({rect.top(), rect.bottom()});
  };
  const auto collect = [&](const auto& self, const BlockLayout& block) -> void {
    if (const auto* text = block.inlineLayout()) {
      for (int row = 0; row < text->visualLineCount(); ++row)
        addRow(text->allocatedLineRect(row).united(text->visualLineRect(row)).translated(block.inlineTextOrigin()));
    }
    if (block.type() == BlockType::CodeFence || block.literalEditing()) {
      const auto origin = block.literalContentRect(theme).topLeft();
      for (const auto& line : block.literalTextLayout(theme).lines())
        addRow(line.rect.translated(origin));
    }
    for (const auto& row : block.tableRows())
      addRow(row.rect);
    for (const auto& child : block.children())
      self(self, *child);
  };
  for (const auto* block : layout.promotedBlocks())
    collect(collect, *block);
  std::sort(rows.begin(), rows.end());
  QVector<QPair<qreal, qreal>> bands;
  for (const auto& row : rows) {
    if (!bands.isEmpty() && row.first < bands.last().second - .001)
      bands.last().second = qMax(bands.last().second, row.second);
    else
      bands.append(row);
  }
  const auto pageEnd = [&](qreal top) {
    const qreal nominal = top + contentHeight;
    const auto it =
        std::lower_bound(bands.cbegin(), bands.cend(), nominal, [](const auto& row, qreal y) { return row.first < y; });
    if (it != bands.cbegin()) {
      const auto& previous = *std::prev(it);
      if (previous.second > nominal && previous.first > top + .001)
        return previous.first;
    }
    return nominal;
  };
  struct Page {
    qreal top = 0;
    qreal end = 0;
    QVector<const BlockLayout*> blocks;
  };
  QVector<Page> pages{Page{0, pageEnd(0), {}}};
  // Plan pagination before painting so ${pageCount} is accurate even when a
  // heading starts a page or one code/table block spans several pages.
  for (const auto* block : layout.promotedBlocks()) {
    const auto rect = block->rect();
    if (!std::isfinite(rect.bottom()))
      return false;
    if (rect.top() >= pages.last().end ||
        (options.breakOnHeading && block->headingLevel() == 1 && !pages.last().blocks.isEmpty()))
      pages.append(Page{rect.top(), pageEnd(rect.top()), {}});
    for (;;) {
      pages.last().blocks.append(block);
      if (rect.bottom() <= pages.last().end)
        break;
      const qreal next = pages.last().end;
      pages.append(Page{next, pageEnd(next), {}});
    }
  }
  QPainter painter(&printer);
  if (!painter.isActive())
    return false;
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  const QHash<QString, QString> variables{{"title", title},
                                          {"author", options.author},
                                          {"fileName", QFileInfo(sourcePath).fileName()},
                                          {"date", QDate::currentDate().toString(Qt::ISODate)},
                                          {"pageCount", QString::number(pages.size())}};
  const auto expand = [&](const QString& text, qsizetype pageIndex) {
    // Expand each placeholder once; a title containing '${pageNo}' remains
    // literal content rather than being interpreted as another template.
    static const QRegularExpression placeholder(QStringLiteral("\\$\\{([A-Za-z]+)\\}"));
    QString result;
    qsizetype position = 0;
    auto matches = placeholder.globalMatch(text);
    while (matches.hasNext()) {
      const auto match = matches.next();
      result += text.mid(position, match.capturedStart() - position);
      const auto key = match.captured(1);
      result += key == "pageNo" ? QString::number(pageIndex + 1) : variables.value(key, match.captured());
      position = match.capturedEnd();
    }
    return result + text.mid(position);
  };
  for (qsizetype i = 0; i < pages.size(); ++i) {
    if (i && !printer.newPage())
      return false;
    // QPrinter's default origin is already the printable area's top left.
    painter.fillRect(QRectF(0, 0, page.width(), page.height()), theme.backgroundColor());
    painter.save();
    painter.translate(0, headerHeight);
    painter.setClipRect(QRectF(0, 0, page.width(), pages[i].end - pages[i].top));
    for (const auto* block : pages[i].blocks)
      block->paint(painter, theme, pages[i].top);
    painter.restore();
    painter.save();
    painter.setClipRect(QRectF(0, 0, page.width(), page.height()));
    painter.setFont(decorationFont);
    painter.setPen(theme.textColor());
    const auto draw = [&](const QString& text, qreal top, qreal width, Qt::Alignment alignment) {
      painter.drawText(QRectF(0, top, width, metrics.height()), alignment | Qt::AlignVCenter,
                       metrics.elidedText(text, Qt::ElideRight, qMax<qreal>(0, width)));
    };
    if (headerHeight)
      draw(expand(options.header, i), 0, page.width(), Qt::AlignLeft);
    if (footerHeight) {
      const qreal top = page.height() - metrics.height();
      const auto counter = QStringLiteral("%1 / %2").arg(i + 1).arg(pages.size());
      const qreal counterWidth = options.pageNumbers ? metrics.horizontalAdvance(counter) + 16 : 0;
      draw(expand(options.footer, i), top, page.width() - counterWidth, Qt::AlignLeft);
      if (options.pageNumbers)
        draw(counter, top, page.width(), Qt::AlignRight);
    }
    painter.restore();
  }
  return painter.end();
}

bool exportNativeImage(const QString& path, const MarkdownDocument& document, RenderTheme theme,
                       const QString& sourcePath, const ImageExportOptions& options,
                       mermaid::editor::MermaidRenderCache* cache) {
  theme.updateForViewport(options.width);
  DocumentLayout layout;
  layout.setMermaidRenderCache(cache);
  layout.setMermaidSyncMode(true);
  layout.rebuild(document, theme, options.width, sourcePath);
  const qreal height = qMax(1.0, std::ceil(layout.totalHeight()));
  const qreal pixelWidth = options.width * options.scale;
  const qreal pixelHeight = height * options.scale;
  if (!std::isfinite(pixelHeight) || pixelWidth < 1 || pixelHeight > 32767 || pixelWidth * pixelHeight > 64000000)
    return false;
  QImage image(QSize(int(pixelWidth), int(pixelHeight)), QImage::Format_ARGB32_Premultiplied);
  if (image.isNull())
    return false;
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(options.scale, options.scale);
  for (const auto* block : layout.promotedBlocks())
    block->paint(painter, theme, 0);
  painter.end();
  return image.save(path, "PNG");
}

} // namespace muffin
