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
    for (auto backend : {TextBackend::Native, TextBackend::Fractional}) {
      font_rendering::configureCssFont(font, entry["letterSpacing"].toDouble(), entry["wordSpacing"].toDouble(), backend);
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
      require(sameWrap, id + " browser wrap");
      // Includes real/synthetic bold and italic, wide and narrow lines. Qt's
      // Han-adjacent space classification still accounts for up to .3px here.
      requireNear(maxError, 0, id + " browser caret error", .4);
      bool hanRun = false;
      for (const auto& run : layout.backendGlyphRuns()) {
        const auto actualFamily = run.rawFont().familyName();
        require(!run.glyphIndexes().contains(0), id + " missing glyph");
        require(actualFamily == font.families()[0] || actualFamily == font.families()[1], id + " unexpected OS fallback " + actualFamily);
        hanRun |= actualFamily == families.value("MuffinFixtureHan");
        requireNear(run.rawFont().pixelSize() / textBackendScale(backend), size, id + " fractional glyph size", 1. / 64);
      }
      if (entry["family"].toString() != "MuffinFixtureAbyss" && text.contains(QStringLiteral("中文")))
        require(hanRun, id + " explicit Chinese fallback");
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
  auto deviceFont = font;
  deviceFont.setWordSpacing(font.wordSpacing() * 64);
  QTextLayout referenceLayout(text, deviceFont, &device);
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
void testSpacingConversion() {
  const QString text = QStringLiteral("AV office fi words");
  for (auto backend : {TextBackend::Native, TextBackend::Fractional}) {
    QFont base(families.value("MuffinFixtureSans"));
    setTextPixelSize(base, 28.8, backend);
    setTextLetterSpacing(base, -.7, backend);
    const auto make = [&](const QFont& font, const QList<TextLayout::FormatRange>& formats = {}) {
      auto layout = std::make_unique<TextLayout>(text, font, backend);
      layout->setFormats(formats);
      // A second update must not multiply already converted values again.
      layout->setFormats(layout->formats());
      layout->beginLayout();
      auto line = layout->createLine();
      line.setLineWidth(1000);
      layout->endLayout();
      return layout;
    };
    const auto plain = make(base);
    for (qreal spacing : {-2.25, .3, 2.25}) {
      auto spacedFont = base;
      spacedFont.setWordSpacing(spacing);
      auto spaced = make(spacedFont);
      requireNear(spaced->lineAt(0).horizontalAdvance() - plain->lineAt(0).horizontalAdvance(), 3 * spacedFont.wordSpacing(),
                  "word spacing uses logical pixels", .002);
      requireNear(TextFontMetrics(spacedFont, backend).horizontalAdvance(text), spaced->lineAt(0).horizontalAdvance(),
                  "word spacing metrics and shaping agree", .002);
      TextLayout::FormatRange range;
      range.start = 0;
      range.length = text.size();
      range.format.setFontWordSpacing(spacedFont.wordSpacing());
      auto formatted = make(base, {range});
      requireNear(formatted->lineAt(0).horizontalAdvance(), spaced->lineAt(0).horizontalAdvance(), "format word spacing conversion", .002);
      requireNear(formatted->formats()[0].format.fontWordSpacing(), spacedFont.wordSpacing(), "public format stays logical");
      for (int i = 0; i <= text.size(); ++i)
        requireNear(formatted->cursorRect(i).x(), spaced->cursorRect(i).x(), "formatted spacing caret", .002);
    }
    if (backend == TextBackend::Fractional) {
      QFont expected(families.value("MuffinFixtureSans"));
      setTextPixelSize(expected, 28.8, backend);
      expected.setWordSpacing(2.25);
      TextLayout::FormatRange range;
      range.start = 0;
      range.length = text.size();
      range.format.setProperty(QTextFormat::FontPixelSize, 28.8);
      range.format.setFontWordSpacing(2.25);
      auto formatted = make(QFont(families.value("MuffinFixtureSans")), {range});
      auto direct = make(expected);
      requireNear(formatted->lineAt(0).horizontalAdvance(), direct->lineAt(0).horizontalAdvance(), "fractional pixel format size", .002);

      // Qt treats spaces in Han shaping items as inter-character
      // opportunities and therefore skips QFont::wordSpacing.  CSS still
      // applies word-spacing at a Chinese/Latin boundary, so the shared
      // TextLayout must retain the extra advance and caret position there.
      const QString cjkText = QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87 \xE6\xB7\xB7\xE6\x8E\x92");
      QFont cjkPlain(families.value("MuffinFixtureSans"));
      setTextPixelSize(cjkPlain, 18.4, backend);
      QFont cjkSpaced = cjkPlain;
      cjkSpaced.setWordSpacing(2.25);
      const auto makeSingle = [&](const QFont& font) {
        auto result = std::make_unique<TextLayout>(cjkText, font, backend);
        result->beginLayout();
        auto line = result->createLine();
        line.setLineWidth(1000);
        result->endLayout();
        return result;
      };
      const auto cjkBase = makeSingle(cjkPlain);
      const auto cjkWithSpacing = makeSingle(cjkSpaced);
      requireNear(cjkWithSpacing->lineAt(0).horizontalAdvance() -
                      cjkBase->lineAt(0).horizontalAdvance(),
                  cjkSpaced.wordSpacing(), "CJK boundary word spacing", .002);
      requireNear(cjkWithSpacing->cursorRect(3).x() - cjkBase->cursorRect(3).x(),
                  cjkSpaced.wordSpacing(), "CJK boundary spacing caret", .002);
    }
  }
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
void testSharedDocumentFallbackStack() {
  const auto tail = font_rendering::documentFallbackTail();
  const auto resolvedFamilies = font_rendering::cssFamilyList(
      QStringLiteral("MuffinFixtureSans, sans-serif"), tail);
  require(!resolvedFamilies.isEmpty() &&
              resolvedFamilies.front().compare(QStringLiteral("MuffinFixtureSans"),
                                       Qt::CaseInsensitive) == 0,
          "shared CSS fallback keeps the declared Latin family first");
  require(resolvedFamilies.contains(QStringLiteral("Noto Sans CJK SC"), Qt::CaseInsensitive),
          "shared CSS fallback registers the bundled CJK face");

  QFont font;
  font.setFamilies(resolvedFamilies);
  setTextPixelSize(font, 18.4, TextBackend::Fractional);
  TextLayout layout(QString::fromUtf8("中文混排"), font,
                    TextBackend::Fractional);
  layout.beginLayout();
  auto line = layout.createLine();
  line.setLineWidth(300);
  layout.endLayout();
  bool cjkFallback = false;
  for (const auto& run : layout.backendGlyphRuns()) {
    const auto family = run.rawFont().familyName();
    cjkFallback |= family.contains(QStringLiteral("Noto Sans CJK SC"),
                                   Qt::CaseInsensitive);
  }
  require(cjkFallback,
          "shared fractional layout selects the deterministic CJK fallback");
}
void testThemeTypography() {
  const auto read = [](const QString& path) {
    QFile file(QStringLiteral(MUFFIN_SOURCE_DIR "/") + path);
    require(file.open(QIODevice::ReadOnly), "theme typography fixture " + path);
    auto text = QString::fromUtf8(file.readAll());
    return text.replace("\r\n", "\n");
  };
  const auto fixture = QJsonDocument::fromJson(read("tests/fixtures/theme/theme-typography-browser.json").toUtf8()).object();
  const auto sources = fixture["sources"].toObject();
  for (auto it = sources.begin(); it != sources.end(); ++it)
    require(QString::fromLatin1(QCryptographicHash::hash(read(it.key()).toUtf8(), QCryptographicHash::Sha256).toHex()) == it.value().toString(),
            "theme typography source changed; regenerate browser reference: " + it.key());
  for (const auto value : fixture["cases"].toArray()) {
    const auto entry = value.toObject();
    const auto context = entry["id"].toString();
    auto css = read(entry["cssFile"].toString());
    for (auto it = families.begin(); it != families.end(); ++it) css.replace(it.key(), it.value());
    auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, context, {}));
    qreal width = entry["width"].toDouble();
    theme.updateForViewport(width, 1000);
    DocumentSession session;
    session.setMarkdownText(fixture["markdown"].toString(), false);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, width);
    // The browser retains fractional sizes. Native rendering is checked for
    // compatibility and editing consistency separately, not falsely certified
    // against a browser with different used font sizes.
    if (documentTextBackend() == TextBackend::Fractional) {
      const auto expected = entry["expected"].toObject();
      const auto page = expected["page"].toArray();
      requireNear(layout.pageOuterLeft(), page[0].toDouble(), context + " page left", .1);
      requireNear(layout.pageOuterWidth(), page[2].toDouble(), context + " page width", .1);
      const auto box = [&](QRectF actual, QJsonArray expectedRect, const QString& label) {
        requireNear(actual.x(), expectedRect[0].toDouble(), context + label + " x", 1.2);
        requireNear(actual.y(), expectedRect[1].toDouble(), context + label + " y", 1.2);
        requireNear(actual.width(), expectedRect[2].toDouble(), context + label + " width", 1.2);
        requireNear(actual.height(), expectedRect[3].toDouble(), context + label + " height", 1.2);
      };
      const auto blocks = expected["blocks"].toArray();
      for (int i = 0; i < blocks.size(); ++i) {
        const auto* block = layout.block(session.document().root().children()[i]->id());
        const auto expectedBlock = blocks[i].toObject();
        box(block->cssBorderBox(), expectedBlock["border"].toArray(), QString(" block %1").arg(i));
        const auto* text = block->inlineLayout();
        require(text, context + " inline layout");
        requireNear(text->firstLineBaselineY() + block->inlineTextOrigin().y(), expectedBlock["firstBaseline"].toDouble(), context + " first baseline", 1.2);
        requireNear(text->lastLineBaselineY() + block->inlineTextOrigin().y(), expectedBlock["lastBaseline"].toDouble(), context + " last baseline", 1.2);
        const auto chars = expectedBlock["characters"].toArray();
        require(chars.size() == text->visibleText().size(), context + " character count");
        for (int j = 0; j < chars.size(); ++j) {
          if (text->visibleText()[j].isSpace()) continue;
          const auto caret = text->cursorRect(j);
          requireNear(caret.x() + block->inlineTextOrigin().x(), chars[j].toObject()["x"].toDouble(), context + " character x", 1.2);
          require(text->hitTestTextOffset({caret.x(), caret.center().y()}) == j, context + " caret/click");
        }
        const auto components = expectedBlock["components"].toArray();
        // Heading inline boxes also contain generated counter text. DOM
        // querySelectorAll returns only the authored code/keyboard elements.
        if (session.document().root().children()[i]->type() == BlockType::Paragraph)
          require(components.size() == text->inlineBoxes().size(), context + " authored component count");
        for (int j = 0; j < components.size(); ++j)
          box(text->inlineBoxes()[j].borderBox.translated(block->inlineTextOrigin()), components[j].toObject()["border"].toArray(), " component");
      }
    }
    QString phase = "initial";
    const auto consistent = [&] {
      for (auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
        DocumentLayout fresh;
        fresh.rebuild(session.document(), theme, width, {}, {}, policy);
        fresh.buildAll(theme);
        requireNear(layout.totalHeight(), fresh.totalHeight(), context + " total height");
        for (const auto& node : session.document().root().children()) {
          const auto* a = layout.block(node->id());
          const auto* b = fresh.block(node->id());
          require(a && b, context + " materialized block");
          const auto rectangle = [](QRectF r) { return QString("(%1,%2 %3x%4)").arg(r.x(), 0, 'g', 16).arg(r.y(), 0, 'g', 16).arg(r.width(), 0, 'g', 16).arg(r.height(), 0, 'g', 16); };
          require(a->rect() == b->rect(), context + " " + phase + " full/lazy/incremental box " + rectangle(a->rect()) + " vs " + rectangle(b->rect()));
          if (!a->inlineLayout()) continue;
          const auto* x = a->inlineLayout();
          const auto* y = b->inlineLayout();
          require(x->selectionRects(0, x->visibleText().size()) == y->selectionRects(0, y->visibleText().size()), context + " selection");
          for (int j = 0; j <= x->visibleText().size(); ++j) require(x->cursorRect(j) == y->cursorRect(j), context + " caret");
        }
      }
    };
    consistent();
    const auto id = session.document().root().children().front()->id();
    require(session.applyTextDelta(2, 0, "Edited ", true, {{id, 2, BlockType::Heading}}), context + " edit");
    const auto range = session.lastLocalTopLevelRangeChange();
    if (range.isValid()) layout.rebuildTopLevelRange(range, session.document(), theme, {});
    else layout.rebuildBlock(id, session.document(), theme, {});
    phase = "edit";
    consistent();
    width += 37;
    theme.updateForViewport(width, 1000);
    if (!layout.relayoutForViewportWidth(theme, width)) layout.rebuild(session.document(), theme, width);
    phase = "resize";
    consistent();
    theme.setZoomPercent(125);
    theme.updateForViewport(width, 1000);
    if (!layout.relayoutForViewportWidth(theme, width)) layout.rebuild(session.document(), theme, width);
    phase = "zoom";
    consistent();
  }
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
  std::vector<qint64> editTimes;
  for (int i = 0; i < 12; ++i) {
    timer.restart();
    require(session.applyTextDelta(3, 0, "x", true, {{id, 3, BlockType::Heading}}), "repeated benchmark edit");
    const auto changed = session.lastLocalTopLevelRangeChange();
    if (changed.isValid()) document.rebuildTopLevelRange(changed, session.document(), theme, {});
    else document.rebuildBlock(id, session.document(), theme, {});
    editTimes.push_back(timer.elapsed());
  }
  std::sort(editTimes.begin(), editTimes.end());
  DocumentLayout lazy;
  lazy.rebuild(session.document(), theme, 900, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.visibleBlocks({0, 0, 900, 900}, theme);
  const auto resizePass = [&] {
    for (int i = 0; i < 24; ++i) {
      const qreal width = 640 + (i % 7) * 55;
      theme.updateForViewport(width, 1000);
      if (!lazy.relayoutForViewportWidth(theme, width)) lazy.rebuild(session.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
      lazy.visibleBlocks({0, 0, width, 900}, theme);
    }
  };
  timer.restart();
  resizePass();
  const auto firstResizeMs = timer.elapsed(), firstResizeMemory = diag::workingSetBytes();
  timer.restart();
  resizePass();
  std::fprintf(stdout,
      "[document-interaction-bench] edits=12 editMedianMs=%lld editMaxMs=%lld resizeFrames=24 firstResizeMs=%lld secondResizeMs=%lld "
      "firstResizeMemory=%lld secondResizeMemory=%lld promoted=%lld totalBlocks=%lld\n",
      (long long)editTimes[editTimes.size() / 2], (long long)editTimes.back(), (long long)firstResizeMs, (long long)timer.elapsed(),
      (long long)firstResizeMemory, (long long)diag::workingSetBytes(), (long long)lazy.promotedBlocks().size(), (long long)lazy.slotCount());
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
  runTest("spacingConversion", testSpacingConversion);
  runTest("editingAndCaches", testEditingAndCaches);
  runTest("sharedDocumentFallbackStack", testSharedDocumentFallbackStack);
  runTest("themeTypography", testThemeTypography);
  benchmark();
  return 0;
}
