#include "document/DocumentSession.h"
#include "editor/EditorView.h"
#include "render/DocumentLayout.h"
#include "render/ImageDecoder.h"
#include "render/ImageLoader.h"
#include "theme/CssThemeMapper.h"
#include "theme/MarkdownFlow.h"
#include "RenderTestUtils.h"
#include "DocumentVisualFonts.h"

#include <QApplication>
#include <QJsonDocument>
#include <QRegularExpression>

using namespace muffin;
namespace {
void checkText(const InlineLayout& text, QPointF origin, NodeId node, DocumentLayout& layout,
               const RenderTheme& theme) {
  if (text.plainText().isEmpty())
    return;
  // Round-trip visible positions, including CJK, wrapping and formatted runs,
  // through the same geometry used by the editor's mouse and caret paths.
  for (const auto offset : {qsizetype(0), text.plainText().size() / 2}) {
    const auto caret = text.cursorRect(offset);
    const auto hit = layout.hitTest(origin + QPointF(caret.left() + .1, caret.center().y()), theme);
    require(hit.isValid() && hit.textNodeId == node, "painted text hits its original Markdown node");
    require(qAbs(hit.cursorRect.left() - (origin.x() + caret.left())) < 2 &&
                qAbs(hit.cursorRect.top() - (origin.y() + caret.top())) < 2,
            "mouse hit and painted caret agree within two logical pixels");
  }
}
void recordLines(const InlineLayout& text, qsizetype sourceStart, QJsonArray& metrics) {
  QJsonArray starts;
  for (int i = 0; i < text.visualLineCount(); ++i)
    starts.append(qint64(text.sourceOffsetAtVisualLineX(i, text.visualLineRect(i).left())));
  metrics.append(QJsonObject{{"sourceStart", qint64(sourceStart)}, {"lineStarts", starts}});
}
void checkBlock(const BlockLayout& block, DocumentLayout& layout, const RenderTheme& theme, QJsonArray& metrics) {
  if (const auto* text = block.inlineLayout()) {
    checkText(*text, block.inlineTextOrigin(), block.nodeId(), layout, theme);
    recordLines(*text, block.contentSourceStart(), metrics);
  }
  if (block.type() == BlockType::CodeFence && !block.literal().isEmpty()) {
    for (const auto offset : {qsizetype(0), block.literal().size() / 2, block.literal().size() - 1}) {
      const auto caret = block.literalVisualCursorRect(offset, theme);
      const auto hit = layout.hitTest(QPointF(caret.left() + .1, caret.center().y()), theme);
      require(hit.isValid() && hit.blockId == block.nodeId(), "long code block hits its original source node");
      require(qAbs(hit.cursorRect.left() - caret.left()) < 2 && qAbs(hit.cursorRect.top() - caret.top()) < 2,
              "long code block painting and mouse hits share cursor geometry");
    }
  }
  for (const auto& row : block.tableRows())
    for (const auto& cell : row.cells) {
      checkText(cell.text, cell.textOrigin(), cell.nodeId, layout, theme);
      recordLines(cell.text, cell.contentSourceStart, metrics);
    }
  for (const auto& child : block.children())
    checkBlock(*child, layout, theme, metrics);
}

void documents() {
  const QString folder = QStringLiteral(MUFFIN_VISUAL_FIXTURES);
  const auto images = QJsonDocument::fromJson(readFixture(folder + "/images.json").toUtf8()).object();
  for (auto it = images.begin(); it != images.end(); ++it)
    ImageLoader::instance().store(it.key(), image_decoder::decodeFileFallback(folder + '/' + it.value().toString()));
  const qreal dpr = qApp->devicePixelRatio();
  const auto output = qEnvironmentVariable("MUFFIN_VISUAL_OUTPUT");
  if (!output.isEmpty())
    QDir().mkpath(output);
  QJsonArray report;
  for (const QString name : {"readme.zh", "chinese", "table", "code"}) {
    const auto path = folder + '/' + name + ".md";
    const auto source = readFixture(path);
    DocumentSession session;
    session.setMarkdownText(source, false);
    for (int zoom : {100, 125, 150}) {
      RenderTheme theme = RenderTheme::github(zoom);
      theme.updateForViewport(1920, 1080);
      DocumentLayout layout;
      layout.rebuild(session.document(), theme, 1600, {}, path, DocumentLayout::BuildPolicy::Eager);
      require(layout.totalHeight() > 0 && layout.pageWidth() <= 1600, "page fits available editor width");
      QJsonArray lineMetrics;
      for (const auto* block : layout.promotedBlocks())
        checkBlock(*block, layout, theme, lineMetrics);
      const QString id = name + '-' + QString::number(zoom) + '-' + QString::number(qRound(dpr * 100));
      QImage image(QSize(qCeil(1600 * dpr), qCeil(1000 * dpr)), QImage::Format_ARGB32_Premultiplied);
      image.setDevicePixelRatio(dpr);
      image.fill(theme.backgroundColor());
      QPainter painter(&image);
      for (const auto* block : layout.promotedBlocks())
        if (block->rect().top() < 1000)
          block->paint(painter, theme, 0);
      painter.end();
      require(changedPixelCount(image, theme.backgroundColor()) > 200, "document paints visible content");
      if (!output.isEmpty())
        require(image.save(output + '/' + id + ".png"), "write visual acceptance artifact");
      report.append(QJsonObject{{"id", id},
                                {"pageWidth", layout.pageWidth()},
                                {"height", layout.totalHeight()},
                                {"font", theme.paragraphFont().key()},
                                {"dpiScale", dpr},
                                {"zoom", zoom},
                                {"wrapping", lineMetrics}});
    }
  }
  if (!output.isEmpty()) {
    QFile file(output + "/metrics-" + QString::number(qRound(dpr * 100)) + ".json");
    require(file.open(QIODevice::WriteOnly), "open metrics artifact");
    file.write(QJsonDocument(report).toJson());
  }
}

void viewportContract() {
  QWidget host;
  host.resize(1920, 1080);
  EditorView view(&host);
  view.setThemeViewport(&host);
  view.setGeometry(280, 0, 1640, 1000);
  view.setTheme(RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{max-width:800px;padding:20px}@media(min-width:1800px){#write{max-width:1200px}}", "viewport", {})));
  DocumentSession session;
  session.setMarkdownText("# 页面版心\n\n中文内容", false);
  view.setDocument(session.document());
  host.show();
  QApplication::processEvents();
  const auto width = view.theme().pageWidth();
  view.resize(1900, 1000);
  QApplication::processEvents();
  require(view.theme().pageWidth() == width && width > 1000,
          "sidebar width changes available space without changing theme viewport breakpoint");
  view.setContentWidthPx(700);
  require(view.theme().pageWidth() <= 700, "explicit user width overrides theme width");
}
}
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  prepareDocumentVisualFonts();
  QCoreApplication::setOrganizationName("MuffinTest");
  QCoreApplication::setApplicationName("DocumentVisualAcceptance");
  runTest("real document visuals and hit testing", documents);
  runTest("viewport and user width contract", viewportContract);
}
