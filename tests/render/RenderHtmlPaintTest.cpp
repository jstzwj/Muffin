#include "html/HtmlBoxBuilder.h"
#include "html/HtmlLayoutEngine.h"
#include "html/HtmlParser.h"
#include "html/HtmlRenderer.h"
#include "html/HtmlStyleResolver.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// ---- HTML helpers (local) ----

const html::HtmlBox* firstChildWithTag(const html::HtmlBox& box, html::HtmlTag tag) {
  for (const auto& child : box.children()) {
    if (child->tag() == tag) {
      return child.get();
    }
    if (const html::HtmlBox* nested = firstChildWithTag(*child, tag)) {
      return nested;
    }
  }
  return nullptr;
}

void collectChildrenWithTag(const html::HtmlBox& box, html::HtmlTag tag, QVector<const html::HtmlBox*>& out) {
  for (const auto& child : box.children()) {
    if (child->tag() == tag) {
      out.push_back(child.get());
    }
    collectChildrenWithTag(*child, tag, out);
  }
}

QRectF absoluteHtmlBoxRect(const html::HtmlBox& box) {
  QPointF origin;
  const html::HtmlBox* current = &box;
  while (current) {
    origin += QPointF(current->geometry().left, current->geometry().top);
    current = current->parent();
  }
  return QRectF(origin, QSizeF(box.geometry().width, box.geometry().height));
}

// ---- test functions ----

void testHtmlTableAndListLayoutContract() {
  html::HtmlDocument document;
  require(document.parse(QStringLiteral(
              "<ul><li>First item</li><li>Second item</li></ul>"
              "<ol><li>Alpha</li><li>Beta</li></ol>"
              "<table><caption>Language Comparison</caption><thead><tr><th>Name</th><th>Value</th></tr></thead>"
              "<tbody><tr><td>One</td><td>Two</td></tr></tbody></table>")),
          QStringLiteral("html table/list fixture should parse"));

  html::HtmlBoxBuilder builder;
  auto root = builder.build(document);
  require(root != nullptr, QStringLiteral("html table/list fixture should build box tree"));

  html::HtmlStyleResolver resolver;
  resolver.resolve(*root, 16.0);

  QVector<const html::HtmlBox*> listItems;
  collectChildrenWithTag(*root, html::HtmlTag::ListItem, listItems);
  require(listItems.size() == 4, QStringLiteral("html table/list fixture should contain four list items"));
  require(listItems[0]->listMarker() == QStringLiteral("\u2022"), QStringLiteral("unordered list item should use bullet marker"));
  require(listItems[2]->listMarker() == QStringLiteral("1."), QStringLiteral("ordered list item should start at 1"));
  require(listItems[3]->listMarker() == QStringLiteral("2."), QStringLiteral("ordered list item should increment marker"));

  std::vector<std::unique_ptr<html::HtmlTextLayout>> textLayouts;
  html::HtmlLayoutEngine engine;
  engine.layout(*root, 320.0, 16.0, textLayouts);

  QVector<const html::HtmlBox*> rows;
  collectChildrenWithTag(*root, html::HtmlTag::TableRow, rows);
  QVector<const html::HtmlBox*> cells;
  collectChildrenWithTag(*root, html::HtmlTag::TableCell, cells);
  QVector<const html::HtmlBox*> headers;
  collectChildrenWithTag(*root, html::HtmlTag::TableHeader, headers);
  const html::HtmlBox* table = firstChildWithTag(*root, html::HtmlTag::Table);
  const html::HtmlBox* caption = firstChildWithTag(*root, html::HtmlTag::Caption);
  require(rows.size() == 2, QStringLiteral("html table should contain two rows"));
  require(cells.size() == 2 && headers.size() == 2, QStringLiteral("html table should contain expected cells"));
  require(table != nullptr && caption != nullptr, QStringLiteral("html table fixture should include table caption"));

  const QRectF tableRect = absoluteHtmlBoxRect(*table);
  const QRectF captionRect = absoluteHtmlBoxRect(*caption);
  require(tableRect.width() < 320.0,
          QStringLiteral("auto-width html table should use intrinsic width instead of filling the block"));
  require(qAbs(captionRect.center().x() - tableRect.center().x()) < 1.0,
          QStringLiteral("html table caption should be centered over the table"));
  require(qAbs(captionRect.width() - tableRect.width()) < 1.0,
          QStringLiteral("html table caption width should match table width"));

  const html::HtmlBox* firstHeader = headers[0];
  const html::HtmlBox* secondHeader = headers[1];
  const QRectF firstHeaderRect = absoluteHtmlBoxRect(*firstHeader);
  const QRectF secondHeaderRect = absoluteHtmlBoxRect(*secondHeader);
  require(firstHeaderRect.width() > 0 && firstHeaderRect.height() > 0,
          QStringLiteral("html table header should receive non-zero geometry"));
  require(secondHeaderRect.left() > firstHeaderRect.left(),
          QStringLiteral("html table cells in a row should be laid out horizontally"));
  require(qAbs(secondHeaderRect.top() - firstHeaderRect.top()) < 1.0,
          QStringLiteral("html table cells in a row should share a row top"));

  const html::HtmlBox* firstBodyCell = cells[0];
  const html::HtmlBox* secondBodyCell = cells[1];
  const QRectF firstBodyCellRect = absoluteHtmlBoxRect(*firstBodyCell);
  const QRectF secondBodyCellRect = absoluteHtmlBoxRect(*secondBodyCell);
  require(firstBodyCellRect.top() > firstHeaderRect.top(),
          QStringLiteral("html table body row should appear below header row"));
  require(qAbs(firstBodyCellRect.left() - firstHeaderRect.left()) < 1.0 &&
              qAbs(secondBodyCellRect.left() - secondHeaderRect.left()) < 1.0,
          QStringLiteral("html table columns should align across row groups"));

  html::HtmlLayoutResult result;
  result.setRoot(std::move(root));
  result.setTextLayouts(std::move(textLayouts));
  result.setSize(QSizeF(340.0, 260.0));

  QImage output(380, 300, QImage::Format_ARGB32_Premultiplied);
  output.fill(Qt::white);
  QPainter painter(&output);
  result.paint(painter, QPointF(24, 12));
  painter.end();

  require(changedPixelCount(output, Qt::white) > 300,
          QStringLiteral("html table/list fixture should paint visible table/list pixels"));
}

void testHtmlPreLayoutContract() {
  html::HtmlDocument document;
  require(document.parse(QStringLiteral(
              "<pre>\nvoid hello() {\n    printf(&quot;Hello, HTML pre!\\n&quot;);\n}</pre>"
              "<pre style=\"white-space: pre-wrap\">abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz</pre>")),
          QStringLiteral("html pre fixture should parse"));

  html::HtmlBoxBuilder builder;
  auto root = builder.build(document);
  require(root != nullptr, QStringLiteral("html pre fixture should build box tree"));

  html::HtmlStyleResolver resolver;
  resolver.resolve(*root, 16.0);

  QVector<const html::HtmlBox*> preBlocks;
  collectChildrenWithTag(*root, html::HtmlTag::Pre, preBlocks);
  require(preBlocks.size() == 2, QStringLiteral("html pre fixture should contain two pre blocks"));
  require(preBlocks[0]->style().whiteSpace == html::HtmlWhiteSpace::Pre,
          QStringLiteral("pre default should use pre white-space mode"));
  require(preBlocks[1]->style().whiteSpace == html::HtmlWhiteSpace::PreWrap,
          QStringLiteral("inline white-space: pre-wrap should resolve distinctly"));
  require(preBlocks[0]->style().backgroundColor.isValid(), QStringLiteral("pre should get default background"));
  require(preBlocks[0]->style().padding.left() > 0 && preBlocks[0]->style().padding.top() > 0,
          QStringLiteral("pre should get default padding"));
  require(preBlocks[0]->style().lineHeight > 1.0, QStringLiteral("pre should get stable default line-height"));

  std::vector<std::unique_ptr<html::HtmlTextLayout>> textLayouts;
  html::HtmlLayoutEngine engine;
  engine.layout(*root, 140.0, 16.0, textLayouts);

  require(preBlocks[0]->ownsTextLayout() && preBlocks[1]->ownsTextLayout(),
          QStringLiteral("pre blocks should own dedicated text layouts"));
  require(textLayouts.size() == 2, QStringLiteral("html pre fixture should create two text layouts"));

  const auto& noWrapLayout = textLayouts.at(static_cast<size_t>(preBlocks[0]->textLayoutIndex()));
  const auto& wrapLayout = textLayouts.at(static_cast<size_t>(preBlocks[1]->textLayoutIndex()));
  require(noWrapLayout != nullptr && noWrapLayout->layout != nullptr,
          QStringLiteral("pre no-wrap text layout should exist"));
  require(wrapLayout != nullptr && wrapLayout->layout != nullptr,
          QStringLiteral("pre-wrap text layout should exist"));
  require(!noWrapLayout->text.startsWith(QLatin1Char('\n')),
          QStringLiteral("pre layout should strip the initial source newline"));
  require(noWrapLayout->text.startsWith(QStringLiteral("void hello()")),
          QStringLiteral("pre layout should preserve the first code line after newline normalization"));
  require(noWrapLayout->text.contains(QStringLiteral("    printf")),
          QStringLiteral("pre layout should preserve indentation"));
  require(noWrapLayout->layout->lineCount() == 3,
          QStringLiteral("pre no-wrap layout should preserve explicit source lines"));
  require(wrapLayout->layout->lineCount() > 1,
          QStringLiteral("pre-wrap layout should wrap long text within available width"));

  const QRectF noWrapRect = absoluteHtmlBoxRect(*preBlocks[0]);
  require(noWrapRect.height() > noWrapLayout->height,
          QStringLiteral("pre block geometry should include padding around text layout"));
}

void testHtmlImagePaintContract() {
  html::HtmlDocument document;
  require(document.parse(QStringLiteral("<div><img src=\"missing-local-image.png\" alt=\"Missing\"></div>")),
          QStringLiteral("html image fixture should parse"));

  html::HtmlBoxBuilder builder;
  auto root = builder.build(document);
  require(root != nullptr, QStringLiteral("html image fixture should build box tree"));

  html::HtmlStyleResolver resolver;
  resolver.resolve(*root, 16.0);

  std::vector<std::unique_ptr<html::HtmlTextLayout>> textLayouts;
  html::HtmlLayoutEngine engine;
  engine.layout(*root, 220.0, 16.0, textLayouts);

  const html::HtmlBox* image = firstChildWithTag(*root, html::HtmlTag::Image);
  require(image != nullptr, QStringLiteral("html image fixture should contain image box"));
  require(image->geometry().width > 0 && image->geometry().height > 0,
          QStringLiteral("html image should receive non-zero layout size"));

  html::HtmlLayoutResult result;
  result.setRoot(std::move(root));
  result.setTextLayouts(std::move(textLayouts));
  result.setSize(QSizeF(220.0, 140.0));

  QImage output(260, 180, QImage::Format_ARGB32_Premultiplied);
  output.fill(Qt::white);
  QPainter painter(&output);
  result.paint(painter, QPointF(10, 10));
  painter.end();

  require(changedPixelCount(output, Qt::white) > 100,
          QStringLiteral("html missing image placeholder should paint visible pixels"));
}

void testHtmlLinkWrappedSvgImagesPaintContract() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("html svg badge temp dir should be valid"));

  const QString firstPath = dir.filePath(QStringLiteral("first.svg"));
  const QString secondPath = dir.filePath(QStringLiteral("second.svg"));
  {
    QFile file(firstPath);
    require(file.open(QIODevice::WriteOnly | QIODevice::Text), QStringLiteral("first svg should open"));
    file.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"82\" height=\"20\">"
               "<rect width=\"82\" height=\"20\" fill=\"#0a7cff\"/>"
               "<text x=\"6\" y=\"14\" fill=\"#ffffff\" font-size=\"12\">MIT</text>"
               "</svg>");
  }
  {
    QFile file(secondPath);
    require(file.open(QIODevice::WriteOnly | QIODevice::Text), QStringLiteral("second svg should open"));
    file.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" height=\"20\">"
               "<rect width=\"120\" height=\"20\" fill=\"#6f42c1\"/>"
               "<text x=\"6\" y=\"14\" fill=\"#ffffff\" font-size=\"12\">Platform</text>"
               "</svg>");
  }

  html::HtmlRenderer renderer;
  html::HtmlLayoutResult result = renderer.render(
      QStringLiteral("<div><a href=\"LICENSE\"><img src=\"first.svg\" alt=\"License\">"
                     "<img src=\"second.svg\" alt=\"Platform\"></a></div>"),
      16.0,
      320.0,
      dir.path());
  require(result.valid(), QStringLiteral("html linked svg images fixture should render"));

  const html::HtmlBox* root = result.root();
  require(root != nullptr, QStringLiteral("html linked svg result should have root"));
  QVector<const html::HtmlBox*> images;
  collectChildrenWithTag(*root, html::HtmlTag::Image, images);
  require(images.size() == 2, QStringLiteral("html linked svg fixture should keep both image boxes"));
  require(images[0]->geometry().width >= 80.0 && images[0]->geometry().height >= 19.0,
          QStringLiteral("first linked svg image should receive natural dimensions"));
  require(images[1]->geometry().width >= 118.0 && images[1]->geometry().height >= 19.0,
          QStringLiteral("second linked svg image should receive natural dimensions"));

  QImage output(360, 80, QImage::Format_ARGB32_Premultiplied);
  output.fill(Qt::white);
  QPainter painter(&output);
  result.paint(painter, QPointF(10, 10));
  painter.end();

  const QRect ink = imageInkBounds(output, Qt::white);
  require(!ink.isNull(), QStringLiteral("linked svg images should paint visible pixels"));
  require(ink.width() >= 180 && ink.height() >= 18,
          QStringLiteral("linked svg images should paint both badges"));
}

void testHtmlLinkAndRelativeImageHitContract() {
  html::HtmlRenderer renderer;
  html::HtmlLayoutResult linkResult =
      renderer.render(QStringLiteral("<div><a href=\"https://example.test/path\">Open link</a></div>"), 16.0, 240.0);
  require(linkResult.valid(), QStringLiteral("html link fixture should render"));
  const html::HtmlLayoutResult::HitResult linkHit = linkResult.hitTest(QPointF(4.0, 8.0));
  require(linkHit.linkHref == QStringLiteral("https://example.test/path"),
          QStringLiteral("html link hit-test should return href"));

  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("html relative image temp dir should be valid"));
  const QString relativeName = QStringLiteral("asset.png");
  const QString imagePath = dir.filePath(relativeName);
  QImage image(64, 32, QImage::Format_ARGB32_Premultiplied);
  image.fill(QColor(80, 120, 200));
  require(image.save(imagePath), QStringLiteral("html relative image fixture should save image"));

  html::HtmlLayoutResult imageResult =
      renderer.render(QStringLiteral("<div><img src=\"asset.png\" width=\"32\" alt=\"asset\"></div>"), 16.0, 240.0, dir.path());
  require(imageResult.valid(), QStringLiteral("html relative image fixture should render"));
  require(imageResult.size().height() >= 16.0 && imageResult.size().height() <= 40.0,
          QStringLiteral("html image width attr should preserve natural aspect ratio"));
  const html::HtmlLayoutResult::HitResult imageHit = imageResult.hitTest(QPointF(4.0, 4.0));
  require(QFileInfo(imageHit.imageSrc).absoluteFilePath() == QFileInfo(imagePath).absoluteFilePath(),
          QStringLiteral("html image hit-test should return resolved local path"));
}

void testHtmlWrappedTextLinePositionsContract() {
  html::HtmlDocument document;
  require(document.parse(QStringLiteral(
              "<div style=\"text-align: justify; font-size: 16px;\">"
              "Antidisestablishmentarianism Antidisestablishmentarianism Antidisestablishmentarianism "
              "Antidisestablishmentarianism Antidisestablishmentarianism"
              "</div>")),
          QStringLiteral("html wrapped text fixture should parse"));

  html::HtmlBoxBuilder builder;
  auto root = builder.build(document);
  require(root != nullptr, QStringLiteral("html wrapped text fixture should build box tree"));

  html::HtmlStyleResolver resolver;
  resolver.resolve(*root, 16.0);

  std::vector<std::unique_ptr<html::HtmlTextLayout>> textLayouts;
  html::HtmlLayoutEngine engine;
  engine.layout(*root, 48.0, 16.0, textLayouts);

  require(textLayouts.size() == 1, QStringLiteral("html wrapped text fixture should create one text layout"));
  const html::HtmlTextLayout* textLayout = textLayouts.front().get();
  require(textLayout != nullptr && textLayout->layout != nullptr, QStringLiteral("html wrapped text layout should exist"));
  require(textLayout->layout->lineCount() > 1, QStringLiteral("html wrapped text should create multiple lines"));

  qreal previousY = textLayout->layout->lineAt(0).position().y();
  for (int i = 1; i < textLayout->layout->lineCount(); ++i) {
    const qreal y = textLayout->layout->lineAt(i).position().y();
    require(y > previousY, QStringLiteral("html wrapped text line positions must be vertically ordered"));
    previousY = y;
  }
  require(textLayout->height > textLayout->layout->lineAt(0).height(),
          QStringLiteral("html wrapped text height should include all lines"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testHtmlTableAndListLayoutContract);
  RUN_TEST(testHtmlPreLayoutContract);
  RUN_TEST(testHtmlImagePaintContract);
  RUN_TEST(testHtmlLinkWrappedSvgImagesPaintContract);
  RUN_TEST(testHtmlLinkAndRelativeImageHitContract);
  RUN_TEST(testHtmlWrappedTextLinePositionsContract);
#undef RUN_TEST
  return 0;
}
