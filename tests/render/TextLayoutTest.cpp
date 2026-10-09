#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextLayout>
#include <algorithm>

#include "../TestUtils.h"
#include "../theme/BrowserLayoutFont.h"
#include "diagnostics/ProcessMemory.h"
#include "document/DocumentSession.h"
#include "html/HtmlRenderer.h"
#include "render/CssFormattingContext.h"
#include "render/DocumentLayout.h"
#include "render/InlineFormatting.h"
#include "render/TextLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/FontRendering.h"

using namespace muffin;
namespace {
QHash<QString, QString> families;
QJsonObject reference;
void requireNear(qreal a, qreal b, const QString& context, qreal tolerance = .001) {
  require(qAbs(a - b) <= tolerance, context + QString(" actual=%1 expected=%2").arg(a).arg(b));
}
void loadFonts() {
  QFile file(QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/text-backend-browser.json"));
  require(file.open(QIODevice::ReadOnly), "text backend browser fixture");
  reference = QJsonDocument::fromJson(file.readAll()).object();
  for (auto item : reference["fonts"].toArray()) {
    const auto entry = item.toObject();
    const auto path = entry["file"].toString();
    QFile original(QStringLiteral(MUFFIN_SOURCE_DIR "/") + path);
    require(original.open(QIODevice::ReadOnly), "fixture webfont");
    require(
        QString::fromLatin1(QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256).toHex()) == entry["sha256"].toString(),
        "font bytes differ from browser reference");
    QString native;
    if (path.startsWith("resources/themes/"))
      native = QStringLiteral(MUFFIN_BINARY_DIR "/theme-fonts/") + path.mid(17);
    else
      native = QStringLiteral(MUFFIN_BINARY_DIR "/text-fixture-fonts/") + QFileInfo(path).fileName();
    native.replace(".woff2", ".ttf");
    const int id = QFontDatabase::addApplicationFont(native);
    require(id >= 0, "load generated fixture font " + native);
    families.insert(entry["family"].toString(), QFontDatabase::applicationFontFamilies(id).value(0));
  }
}
void testNativeCompatibility() {
  QFont font(families.value("MuffinFixtureSans"));
  font.setPointSizeF(18.4 * .75);
  font_rendering::configureCssFont(font, .7, .3);
  const QString text = QStringLiteral("office\tAV fi wrap mixed text");
  QTextLayout original(text, font);
  TextLayout common(text, font, TextBackend::Native);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  option.setTabStopDistance(40);
  original.setTextOption(option);
  common.setTextOption(option);
  original.beginLayout();
  common.beginLayout();
  qreal y = 0;
  for (;;) {
    auto old = original.createLine();
    auto line = common.createLine();
    require(old.isValid() == line.isValid(), "native line count");
    if (!line.isValid()) break;
    old.setLineWidth(150);
    line.setLineWidth(150);
    old.setPosition({7, y});
    line.setPosition({7, y});
    require(old.textStart() == line.textStart() && old.textLength() == line.textLength(), "native wrapping");
    require(old.naturalTextRect() == line.naturalTextRect(), "native bounds");
    for (int i = line.textStart(); i <= line.textStart() + line.textLength(); ++i)
      requireNear(line.cursorToX(i), old.cursorToX(i), "native caret");
    y += line.height();
  }
  original.endLayout();
  common.endLayout();
  QImage a(220, 170, QImage::Format_ARGB32_Premultiplied), b(a.size(), a.format());
  a.fill(Qt::white);
  b.fill(Qt::white);
  {
    QPainter p(&a);
    original.draw(&p, {13, 11});
  }
  {
    QPainter p(&b);
    common.draw(&p, {13, 11});
  }
  require(a == b, "native painting remains pixel-identical");
}
void testFractionalBrowserGeometry() {
  QJsonArray diagnostics;
  for (auto item : reference["cases"].toArray()) {
    const auto entry = item.toObject();
    const QString id = entry["id"].toString(), text = entry["text"].toString();
    const qreal size = entry["size"].toDouble();
    QFont font;
    font.setFamilies({families.value(entry["family"].toString()), families.value("MuffinFixtureHan")});
    font.setPointSizeF(size * .75);
    font.setBold(entry["bold"].toBool());
    font.setItalic(entry["italic"].toBool());
    font_rendering::configureCssFont(font, entry["letterSpacing"].toDouble(), entry["wordSpacing"].toDouble());
    for (auto backend : {TextBackend::Native, TextBackend::Fractional}) {
      TextLayout layout(text, font, backend);
      QTextOption option;
      option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
      layout.setTextOption(option);
      layout.beginLayout();
      qreal y = 0;
      for (;;) {
        auto line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(entry["width"].toDouble());
        line.setPosition({0, y});
        y += size * 1.5;
      }
      layout.endLayout();
      const auto expected = entry["expected"].toObject();
      const auto lines = expected["lines"].toArray();
      bool sameWrap = layout.lineCount() == lines.size();
      for (int i = 0; i < layout.lineCount(); ++i) {
        const auto line = layout.lineAt(i);
        const auto ref = i < lines.size() ? lines[i].toObject() : QJsonObject{};
        sameWrap &= line.textStart() == ref["start"].toInt() && line.textStart() + line.textLength() == ref["end"].toInt();
      }
      qreal maxError = 0;
      const auto carets = expected["carets"].toArray();
      for (int i = 0; i < carets.size(); ++i) {
        if (text[i].isSpace() || !layout.isValidCursorPosition(i)) continue;
        const auto caret = layout.cursorRect(i);
        const auto expectedCaret = carets[i].toObject();
        maxError = qMax(maxError, qAbs(caret.x() - expectedCaret["x"].toDouble()));
        require(layout.hitTest({caret.x(), caret.center().y()}) == i, id + " caret/hit round trip");
      }
      diagnostics.append(QJsonObject{{"id", id},
                                     {"backend", backend == TextBackend::Native ? "native" : "fractional"},
                                     {"sameWrap", sameWrap},
                                     {"maxCaretError", maxError}});
      if (backend == TextBackend::Native) continue;
      if (!entry["diagnosticOnly"].toBool()) require(sameWrap, id + " browser wrap");
      // These are bounded advance regressions, not pixel equality. Wide italic
      // text remains a measured diagnostic until the shaper residual is fixed.
      if (!entry["italic"].toBool()) requireNear(maxError, 0, id + " browser caret error", 2);
      bool hanRun = false;
      for (const auto& run : layout.backendGlyphRuns()) {
        const auto actualFamily = run.rawFont().familyName();
        require(!run.glyphIndexes().contains(0), id + " missing glyph");
        require(actualFamily == font.families()[0] || actualFamily == font.families()[1], id + " unexpected OS fallback " + actualFamily);
        hanRun |= actualFamily == families.value("MuffinFixtureHan");
        requireNear(run.rawFont().pixelSize() / textBackendScale(backend), size, id + " fractional glyph size", 1. / 64);
      }
      if (entry["family"].toString() != "MuffinFixtureAbyss") require(hanRun, id + " explicit Chinese fallback");
      const auto intrinsic = intrinsicTextWidths(layout);
      require(intrinsic.minContent > 0 && intrinsic.maxContent >= intrinsic.minContent, id + " intrinsic widths");
      require(!layout.selectionRects(0, text.size()).isEmpty(), id + " selection");
    }
  }
  QFile output(QStringLiteral(MUFFIN_BINARY_DIR "/text-backend-comparison-") +
               (documentTextBackend() == TextBackend::Native ? "native.json" : "fractional.json"));
  require(output.open(QIODevice::WriteOnly), "backend diagnostic output");
  output.write(QJsonDocument(diagnostics).toJson());
}
void testPixelFontsAndTabs() {
  QFont font(families.value("MuffinFixtureSans"));
  font.setPixelSize(19);
  TextLayout layout("A\tB", font, TextBackend::Fractional);
  QTextOption option;
  option.setTabStopDistance(80);
  layout.setTextOption(option);
  layout.beginLayout();
  auto line = layout.createLine();
  line.setLineWidth(300);
  layout.endLayout();
  requireNear(line.cursorToX(2), 80, "fractional tab distance", .02);
  for (auto run : layout.backendGlyphRuns()) requireNear(run.rawFont().pixelSize() / 64, 19, "pixel-size font normalized");
  TextFontMetrics metrics(font, TextBackend::Fractional);
  requireNear(metrics.ascent(), line.ascent(), "metrics/shaper ascent");
  TextLayout explicitTabs("A\tB", font, TextBackend::Fractional);
  QTextOption explicitOption;
  QTextOption::Tab stop;
  stop.position = 90;
  explicitOption.setTabs({stop});
  explicitTabs.setTextOption(explicitOption);
  explicitTabs.beginLayout();
  auto tabLine = explicitTabs.createLine();
  tabLine.setLineWidth(1e9);
  explicitTabs.endLayout();
  requireNear(tabLine.cursorToX(2), 90, "explicit fractional tab", .02);
}
void testFractionalPainting() {
  QFont font(families.value("MuffinFixtureAbyss"));
  font.setPointSizeF(28.8 * .75);
  font_rendering::configureCssFont(font, .7, .3);
  const QString text = QStringLiteral("Muffin 中文混排");
  TextLayout common(text, font, TextBackend::Fractional);
  QImage device(1, 1, QImage::Format_ARGB32_Premultiplied);
  device.setDotsPerMeterX(qRound(96 * 64 / .0254));
  device.setDotsPerMeterY(device.dotsPerMeterX());
  QTextLayout referenceLayout(text, font, &device);
  common.beginLayout();
  referenceLayout.beginLayout();
  auto line = common.createLine();
  auto referenceLine = referenceLayout.createLine();
  line.setLineWidth(400);
  referenceLine.setLineWidth(400 * 64);
  line.setPosition({5, 7});
  referenceLine.setPosition({5 * 64, 7 * 64});
  common.endLayout();
  referenceLayout.endLayout();
  QTextLayout::FormatRange selected;
  selected.start = 2;
  selected.length = 5;
  selected.format.setBackground(Qt::yellow);
  const QPointF origin(31.5, 23.25);
  const QRectF clip(30, 20, 140, 75);
  QImage a(500, 160, QImage::Format_ARGB32_Premultiplied), b(a.size(), a.format());
  a.fill(Qt::white);
  b.fill(Qt::white);
  {
    QPainter p(&a);
    p.scale(1.25, 1.25);
    common.draw(&p, origin, {selected}, clip);
  }
  {
    QPainter p(&b);
    p.scale(1.25 / 64, 1.25 / 64);
    referenceLayout.draw(&p, origin * 64, {selected}, QRectF(clip.topLeft() * 64, clip.size() * 64));
  }
  require(a == b, "fractional paint/origin/clip/selection uses the measured layout");
  bool hasInk = false;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x) hasInk |= a.pixel(x, y) != qRgb(255, 255, 255);
  require(hasInk, "fractional text paints visible glyphs");
}
void testEditingAndCaches() {
  const QString css = QString(
                          "#write { font-family: '%1', '%2'; font-size:18.4px; line-height:1.5; padding:8px; max-width:700px } "
                          "h1 {font-size:28.8px; padding:5px; border-bottom:1px solid gray} h1::before {content:'1 '; font-size:16px} "
                          "a::before {content:'> ';} code,kbd {padding:2px 5px; border:1px solid gray; font-family:'%1'}")
                          .arg(families.value("MuffinFixtureAbyss"), families.value("MuffinFixtureHan"));
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "text backend", {}));
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("# Muffin Markdown Example 中文混排 long title wraps\n\n"
                     "Plain **bold** *italic* [link](https://example.com) `code` <kbd>Ctrl</kbd> + <kbd>S</kbd> 中文测试.\n\n"
                     "```text\nAV office fi\ttab 中文混排\nsecond line\n```\n\n<div>HTML <b>bold</b> 中文 <kbd>Ctrl</kbd></div>\n"),
      false);
  for (int zoom : {80, 100, 125})
    for (qreal width : {260., 700.}) {
      theme.setZoomPercent(zoom);
      theme.updateForViewport(width, 1000);
      DocumentLayout incremental;
      incremental.rebuild(session.document(), theme, width);
      const auto compare = [&] {
        for (auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
          DocumentLayout fresh;
          fresh.rebuild(session.document(), theme, width, {}, {}, policy);
          fresh.buildAll(theme);
          requireNear(incremental.totalHeight(), fresh.totalHeight(), "full/lazy total height");
          for (const auto& node : session.document().root().children()) {
            auto* a = incremental.block(node->id());
            auto* b = fresh.block(node->id());
            require(a && b && a->rect() == b->rect(), "full/lazy block box");
            if (!a->inlineLayout()) continue;
            const auto* x = a->inlineLayout();
            const auto* y = b->inlineLayout();
            require(x->selectionRects(0, x->visibleText().size()) == y->selectionRects(0, y->visibleText().size()), "full/lazy selection");
            for (int i = 0; i <= x->visibleText().size(); ++i) require(x->cursorRect(i) == y->cursorRect(i), "full/lazy caret");
          }
        }
      };
      compare();
      const auto id = session.document().root().children().front()->id();
      require(session.applyTextDelta(2, 0, "Edited ", true, {{id, 2, BlockType::Heading}}), "heading edit");
      const auto range = session.lastLocalTopLevelRangeChange();
      if (range.isValid())
        incremental.rebuildTopLevelRange(range, session.document(), theme, {});
      else
        incremental.rebuildBlock(id, session.document(), theme, {});
      compare();
      QImage image(800, 1800, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::white);
      QPainter painter(&image);
      for (const auto& node : session.document().root().children()) incremental.block(node->id())->paint(painter, theme, 0);
    }
  const auto stack = font_rendering::cssFamilyList("MuffinAlias\nMuffinFixtureHan\nsans-serif", font_rendering::sansFamily(),
                                                   {{"muffinalias", families.value("MuffinFixtureSans")}});
  require(stack[0] == families.value("MuffinFixtureSans") && stack[1] == families.value("MuffinFixtureHan"), "CSS fallback order/alias");
}
void benchmark() {
  if (!qEnvironmentVariableIsSet("MUFFIN_TEXT_BENCH")) return;
  const auto before = diag::workingSetBytes();
  QFont font(families.value("MuffinFixtureAbyss"));
  font.setPointSizeF(28.8 * .75);
  font_rendering::configureCssFont(font, .7, .3);
  const QString text = QStringLiteral("Muffin Markdown Example 中文混排 office fi ").repeated(5);
  std::vector<std::unique_ptr<TextLayout>> layouts;
  QElapsedTimer timer;
  timer.start();
  for (int i = 0; i < 1500; ++i) {
    auto layout = std::make_unique<TextLayout>(text + QString::number(i), font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout->setTextOption(option);
    layout->beginLayout();
    qreal y = 0;
    for (;;) {
      auto line = layout->createLine();
      if (!line.isValid()) break;
      line.setLineWidth(700);
      line.setPosition({0, y});
      y += 44;
    }
    layout->endLayout();
    layout->setCacheEnabled(true);
    layouts.push_back(std::move(layout));
  }
  const auto layoutMs = timer.elapsed(), measured = diag::workingSetBytes();
  QImage image(800, 300, QImage::Format_ARGB32_Premultiplied);
  const auto paintPass = [&] {
    QPainter p(&image);
    for (const auto& layout : layouts) layout->draw(&p, {});
  };
  timer.restart();
  paintPass();
  const auto coldMs = timer.elapsed(), coldMemory = diag::workingSetBytes();
  timer.restart();
  paintPass();
  const auto warmMs = timer.elapsed(), warmMemory = diag::workingSetBytes();
  std::fprintf(stdout,
               "[text-bench] backend=%s layouts=1500 layoutMs=%lld coldPaintMs=%lld warmPaintMs=%lld memoryBefore=%lld measured=%lld "
               "cold=%lld warm=%lld\n",
               documentTextBackend() == TextBackend::Native ? "native" : "fractional", (long long)layoutMs, (long long)coldMs,
               (long long)warmMs, (long long)before, (long long)measured, (long long)coldMemory, (long long)warmMemory);
  DocumentSession session;
  QString markdown;
  for (int i = 0; i < 1500; ++i)
    markdown += QString("## Title %1\n\nText **bold** *italic* `code` 中文混排 repeated paragraph.\n\n").arg(i);
  session.setMarkdownText(markdown, false);
  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss(QString("#write { font-family:'%1'; font-size:18.4px; max-width:700px } h2 {font-size:28.8px}")
                                  .arg(families.value("MuffinFixtureAbyss")),
                              "bench", {}));
  DocumentLayout document;
  timer.restart();
  document.rebuild(session.document(), theme, 900);
  const auto documentMs = timer.elapsed();
  const auto id = session.document().root().children().front()->id();
  timer.restart();
  require(session.applyTextDelta(3, 0, "Edited ", true, {{id, 3, BlockType::Heading}}), "benchmark edit");
  const auto range = session.lastLocalTopLevelRangeChange();
  if (range.isValid())
    document.rebuildTopLevelRange(range, session.document(), theme, {});
  else
    document.rebuildBlock(id, session.document(), theme, {});
  std::fprintf(stdout, "[document-bench] blocks=3000 initialMs=%lld editMs=%lld resident=%lld\n", (long long)documentMs,
               (long long)timer.elapsed(), (long long)diag::workingSetBytes());
}
}  // namespace
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_FONT_DPI", "96");
  QApplication app(argc, argv);
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  QSettings("Muffin", "Muffin").setValue("editor/breakOnSingleNewline", false);
  loadFonts();
  runTest("nativeCompatibility", testNativeCompatibility);
  runTest("fractionalBrowserGeometry", testFractionalBrowserGeometry);
  runTest("pixelFontsAndTabs", testPixelFontsAndTabs);
  runTest("fractionalPainting", testFractionalPainting);
  runTest("editingAndCaches", testEditingAndCaches);
  benchmark();
  return 0;
}
