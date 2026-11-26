#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QWidget>
#include <cstdio>

#include "../TestUtils.h"
#include "document/DocumentSession.h"
#include "diagnostics/ThemePerformance.h"
#include "document/MarkdownNode.h"
#include "render/CssPaintAnimation.h"
#include "render/DecorationPainter.h"
#include "render/DocumentLayout.h"
#include "render/GeneratedContent.h"
#include "render/InlineLayout.h"
#include "render/LayoutBox.h"
#include "render/Blur.h"
#include <cmath>
#include "theme/CssThemeMapper.h"
#include "theme/NodeCssElement.h"
using namespace muffin;
namespace {
void pixels(const QJsonObject& root) {
  int failures = 0;
  for (const auto& entry : root["effects"].toArray()) {
    const auto c = entry.toObject();
    const auto dpr = c["dpr"].toDouble();
    CssElement element;
    element.tag = "div";
    element.id = "box";
    const auto sheet = CssThemeParser::parse(
        "#box{font-size:16px;box-sizing:border-box;padding:4px;border:2px solid #789abc;background:#a03c78;" +
            c["css"].toString() + "}",
        {});
    const auto style = CssThemeMapper::projectComputedStyle("#box", CssComputedStyleEngine(sheet).styleFor(element));
    QFont font;
    font.setPointSizeF(12);
    const auto box = LayoutBox::place("#box", style, style.box, {16, 16, 64, 48}, font);
    QImage image(qRound(96 * dpr), qRound(80 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
      QPainter painter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      if (c["id"].toString().startsWith("backdrop")) {
        painter.fillRect(QRectF(0, 0, 48, 80), QColor("#3678a0"));
        painter.fillRect(QRectF(48, 0, 48, 80), QColor("#ac5268"));
      }
      paintCssElement(
          painter, box.borderBox, box.visualOverflow, style.paint, 1, nullptr,
          [&](QPainter& target, const auto& neutral) {
            auto unfiltered = style;
            unfiltered.paint = neutral;
            paintLayoutBox(target, box, {}, &unfiltered);
            target.fillRect(QRectF(30, 26, 24, 20), QColor("#2c74a8"));
          },
          &box);
    }
    const auto expected = QByteArray::fromBase64(c["pixels"].toString().toLatin1());
    const auto stable = QByteArray::fromBase64(c["stable"].toString().toLatin1());
    int at = 0, sample = 0, differing = 0, maximum = 0;
    for (int y = 3; y < image.height() - 2; y += 4)
      for (int x = 3; x < image.width() - 2; x += 4) {
        const auto actual = image.pixelColor(x, y);
        const int channels[]{actual.red(), actual.green(), actual.blue(), actual.alpha()};
        const int expectedAlpha = static_cast<unsigned char>(expected[at + 3]);
        int difference = 0;
        for (int channel = 0; channel < 4; ++channel) {
          require(at < expected.size(), "browser effect sample size");
          const int value = static_cast<unsigned char>(expected[at++]);
          const int delta =
              channel == 3
                  ? qAbs(channels[channel] - value)
                  : qAbs(qRound(channels[channel] * actual.alpha() / 255.) - qRound(value * expectedAlpha / 255.));
          difference = qMax(difference, delta);
        }
        if (stable[sample++] && difference > 6) {
          ++differing;
          maximum = qMax(maximum, difference);
        }
      }
    require(at == expected.size(), "all browser effect samples compared");
    if (differing) {
      std::fprintf(stderr, "effect %s DPR %.2f: %d differing, max %d\n", qPrintable(c["id"].toString()), dpr, differing,
                   maximum);
      image.save("effects-" + c["id"].toString() + "-" + QString::number(dpr) + ".png");
      ++failures;
    }
  }
  require(failures == 0, "mask/filter/group pixels agree with Chromium interiors");
}
void positions(const QJsonObject& root) {
  for (const auto& entry : root["positions"].toArray()) {
    const auto c = entry.toObject();
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
        "h4::before{content:'';position:absolute;" + c["css"].toString() + "}", "position", {}));
    CssElement origin;
    origin.tag = "h4";
    const auto rule = theme.pseudoForElement(origin, "before");
    require(rule.has_value(), "positioned pseudo exists");
    QFont font;
    font.setPointSizeF(12);
    const auto value = generatedContentStyle(theme, *rule, font, {220, 120});
    require(value.has_value(), "project generated used dimensions");
    const auto rect = positionGeneratedContent(*value, {0, 0, 220, 120}, {0, 0, value->width, value->height}, 1);
    const auto expected = c["rect"].toArray();
    const qreal actual[]{rect.x(), rect.y(), rect.width(), rect.height()};
    for (int i = 0; i < 4; ++i)
      require(qAbs(actual[i] - expected[i].toDouble()) < .02,
              "browser positioned rect " + c["id"].toString() + " axis " + QString::number(i) + " got " +
                  QString::number(actual[i]) + " expected " + QString::number(expected[i].toDouble()));
  }
}
void sparseShadows() {
  for (const qreal dpr : {1., 1.25, 2.}) {
    QImage reference;
    quint64 referenceAllocation = 0;
    for (const qreal distance : {12345., 1000001.}) {
      ThemeElementPaintStyle style;
      style.filters = *parseCssFilters(
          QString("drop-shadow(%1px 0 #386eaa) drop-shadow(3px 2px 1px red) invert(.2)").arg(distance, 0, 'f', 0));
      const QRectF box(16 - distance, 16, 64, 48);
      QImage image(qRound(96 * dpr), qRound(80 * dpr), QImage::Format_ARGB32_Premultiplied);
      image.setDevicePixelRatio(dpr);
      image.fill(Qt::transparent);
      resetCssFilterRasterStats();
      {
        QPainter painter(&image);
        paintCssElement(painter, box, box, style, 1, nullptr, [&](QPainter& target, const auto&) {
          target.fillRect(box, QColor("#a03c78"));
          target.fillRect(box.adjusted(3, 3, -3, -3), Qt::transparent);
          target.fillRect(QRectF(box.left() + 8, box.top() + 8, 1, 30), Qt::white);
        });
      }
      const auto stats = cssFilterRasterStats();
      require(stats.largestSurfacePixels < quint64(128 * 112 * dpr * dpr),
              "far shadow surfaces track visible demand, not offset distance");
      require(image.pixelColor(qRound(32 * dpr), qRound(32 * dpr)).alpha() > 0,
              "offscreen source reaches visible shadow");
      if (reference.isNull()) {
        reference = image;
        referenceAllocation = stats.allocatedPixels;
      } else {
        if (image != reference) {
          image.save("sparse-actual.png");
          reference.save("sparse-reference.png");
        }
        require(image == reference, "million-pixel shadow offset retains identical native-DPR pixels");
        require(stats.allocatedPixels == referenceAllocation, "transparent distance adds no surface allocation");
      }
    }
    // Compare a tiled ordered filter chain with the existing dense whole-surface
    // implementation. Cross-tile overlap and blur halos must not create seams.
    QImage original(qRound(1100 * dpr), qRound(130 * dpr), QImage::Format_ARGB32_Premultiplied);
    original.setDevicePixelRatio(dpr);
    original.fill(Qt::transparent);
    {
      QPainter painter(&original);
      painter.fillRect(QRectF(480, 32, 110, 46), QColor(80, 140, 210, 160));
    }
    const auto operations = *parseCssFilters("drop-shadow(8px 4px 2px #d04080) invert(.3) blur(1px)");
    ThemeElementPaintStyle style;
    style.filters = operations;
    auto dense = original.copy();
    applyElementFilter(dense, style);
    auto tiled = original.copy();
    tiled.fill(Qt::transparent);
    {
      QPainter painter(&tiled);
      paintSparseCssFilter(painter, {0, 0, 1100, 130}, style, 1,
                           [&](QPainter& target) { target.drawImage(QPointF(), original); });
    }
    require(tiled == dense, "ordered sparse filter matches dense compositing across 512-pixel seams");
  }
}
void widgetDeviceClip() {
  class EffectWidget final : public QWidget {
    void paintEvent(QPaintEvent*) override {
      QPainter painter(this);
      painter.fillRect(rect(), Qt::white);
      const QRectF box(width() - 20, height() - 20, 16, 16);
      ThemeElementPaintStyle style;
      style.opacity = .5;
      paintCssElement(painter, box, box, style, 1, nullptr,
                      [&](QPainter& target, const auto&) { target.fillRect(box, Qt::red); });
    }
  } widget;
  widget.setAttribute(Qt::WA_DontShowOnScreen);
  widget.resize(200, 140);
  widget.show();
  QApplication::processEvents();
  const auto image = widget.grab().toImage();
  const auto color = image.pixelColor(qRound((widget.width() - 12) * image.devicePixelRatioF()),
                                      qRound((widget.height() - 12) * image.devicePixelRatioF()));
  require(color.red() == 255 && qAbs(color.green() - 128) <= 1 && qAbs(color.blue() - 128) <= 1,
          "widget implicit clip retains right/bottom effects at native window DPR");
}

void tallPageShadows() {
  for (const qreal top : {4., -10000.}) {
    QImage reference;
    for (const qreal height : {20000., 1000000.}) {
      QImage image(120, 90, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      {
        QPainter painter(&image);
        DecorationPainter::paintBoxShadow(painter, {10, top, 90, height}, 6, QColor(0, 0, 0, 160), 2, 3, 8, 1);
      }
      if (reference.isNull())
        reference = image;
      else
        require(image == reference, "page shadow pixels depend on visible geometry, not offscreen document height");
    }
  }
}

void counters(const QJsonObject& root) {
  for (const auto& entry : root["counters"].toArray()) {
    const auto c = entry.toObject();
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(c["css"].toString(), "counter", {}));
    DocumentSession session;
    session.setMarkdownText(c["markdown"].toString(), false);
    theme.resolveDocumentCounters(session.document());
    QVector<const CssElement*> targets;
    std::function<void(const MarkdownNode&)> collect = [&](const MarkdownNode& node) {
      if (c["inline"].toBool()) {
        std::function<void(const QVector<InlineNode>&)> inlines = [&](const auto& nodes) {
          for (const auto& inlineNode : nodes) {
            const auto* element = theme.cssInlineElement(node, inlineNode.sourceStart());
            if (element && (element->tag == "strong" || element->tag == "em") && !targets.contains(element))
              targets.append(element);
            inlines(inlineNode.children());
          }
        };
        inlines(node.inlines());
      } else if (node.type() == BlockType::Paragraph ||
                 (node.type() == BlockType::BlockQuote && c["id"] == "pseudo-order"))
        targets.append(theme.cssElementForNode(node));
      for (const auto& child : node.children())
        collect(*child);
    };
    collect(session.document().root());
    const auto expected = c["before"].toArray();
    require(targets.size() == expected.size(), "counter semantic witness count " + c["id"].toString());
    for (int i = 0; i < targets.size(); ++i) {
      if (expected[i].isNull())
        continue;
      const auto rule = theme.pseudoForElement(*targets[i], "before");
      require(rule && rule->content == expected[i].toString(), "counter witness " + c["id"].toString() + " #" +
                                                                   QString::number(i) + " got " +
                                                                   (rule ? rule->content : "<missing>"));
    }
    const auto after = c["after"].toObject();
    for (auto it = after.begin(); it != after.end(); ++it) {
      const int index = it.key().front().unicode() - 'a';
      const auto rule = theme.pseudoForElement(*targets[index], "after");
      require(rule && rule->content == it.value().toString(), "after is evaluated after descendants");
    }
    // New document generation must not retain old element identities/counter values.
    theme.invalidateDocumentStyles();
    theme.resolveDocumentCounters(session.document());
    if (!expected[0].isNull())
      require(theme.pseudoForNode(*session.document().root().children().front(), "before").has_value() ||
                  c["inline"].toBool(),
              "counter generation rebuilt");
  }
}
void capabilities() {
  for (const auto& query : QStringList{"(mask:linear-gradient(black,transparent) center/20px 10px no-repeat)",
                                       "(mask-mode:luminance,alpha)", "(mask-composite:exclude)",
                                       "(counter-set:Chapter 3)", "(filter:drop-shadow(1px 2px red) invert(.4))"})
    require(CssThemeSheet::supportsMatches(query), "implemented effect capability " + query);
  for (const auto& query :
       QStringList{"(mask-mode:invalid)", "(mask-composite:source-in)", "(mask-size:-2px)", "(filter:drop-shadow(1px))",
                   "(filter:url(#svg))", "(counter-reset:reversed(n))"})
    require(!CssThemeSheet::supportsMatches(query), "unsupported effect capability " + query);
  const auto sheet =
      CssThemeParser::parse("p{filter:invert(.5);filter:invalid(1);mask-mode:alpha;mask-mode:invalid}", {});
  CssElement p;
  p.tag = "p";
  const auto style = CssThemeMapper::projectComputedStyle("p", CssComputedStyleEngine(sheet).styleFor(p));
  require(style.paint.filters.size() == 1 && style.paint.filters[0].kind == CssFilterOperation::Kind::Invert,
          "invalid filter keeps previous cascade fallback");
}

void editedCounters() {
  for (bool lazy : {false, true}) {
    DocumentSession session;
    session.setMarkdownText("Alpha\n\nBeta\n\nGamma", false);
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
        "#write{counter-reset:n}p{counter-increment:n}p::before{content:counter(n) ':'}", "edit-counters", {}));
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 600, {}, {},
                   lazy ? DocumentLayout::BuildPolicy::Lazy : DocumentLayout::BuildPolicy::Eager);
    layout.buildAll(theme);
    require(session.applyTextEdit(0, 0, "Extra\n\n") != muffin::TextEditResult::Rejected,
            "insert preceding counter element");
    const auto range = session.lastLocalTopLevelRangeChange();
    require(range.isValid(), "counter insertion is a local structural change");
    require(layout.rebuildTopLevelRange(range, session.document(), theme, {}).rebuilt,
            "incremental counter range rebuild");
    layout.buildAll(theme);
    int value = 0;
    for (const auto& node : session.document().root().children()) {
      const auto* block = layout.block(node->id());
      require(block && block->inlineLayout(), "counter block promoted");
      require(block->inlineLayout()->displayText().contains(QString::number(++value) + ":"),
              "unchanged suffix gets newly resolved counter text");
    }
  }
}

void whitespaceMaskAtom() {
  DocumentSession session;
  session.setMarkdownText("**A**", false);
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{padding:0;margin:0}p{margin:0}strong::before{content:' ';display:inline-block;width:22px;height:12px;"
      "background:red;mask-image:linear-gradient(black,black)}",
      "mask-space", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 300);
  const auto* block = layout.block(session.document().root().children().front()->id());
  const auto rects = block->inlineLayout()->generatedPseudoRects();
  require(rects.size() == 1 && qAbs(rects[0].width() - 22) < .01 && qAbs(rects[0].height() - 12) < .01,
          "whitespace mask inline-block keeps its authored dimensions instead of a space advance");
  QImage image(300, 100, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  block->paint(painter, theme, 0);
  painter.end();
  require(image.pixelColor((rects[0].center() + block->inlineTextOrigin()).toPoint()) == QColor(Qt::red),
          "the empty inline-block paints through the common mask path");
}
// Independent copy of the uncompressed mask/convolution used before the
// optimization. Keep raster clipping and phase rules identical; no shared
// compact-mask helper or tolerance can hide a changed blur profile.
void fullCanvasBoxShadow(QPainter& painter, QRectF rect, qreal radius, QColor color, QPointF offset, qreal blur,
                         qreal spread) {
  auto core = rect.translated(offset).adjusted(-spread, -spread, spread, spread);
  if (core.isEmpty())
    return;
  radius = qMax<qreal>(0, radius + spread);
  if (blur <= 0) {
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(core, radius, radius);
    painter.restore();
    return;
  }
  const int kernel = qMax(1, qRound(blur / 2));
  const qreal halo = 3 * kernel + 2;
  const auto snap = [](qreal value) { return std::round(value * 256) / 256; };
  core = QRectF(QPointF(snap(core.left()), snap(core.top())), QPointF(snap(core.right()), snap(core.bottom())));
  auto bounds = core.adjusted(-halo, -halo, halo, halo)
                    .intersected(cssPaintDeviceBounds(painter).adjusted(-halo, -halo, halo, halo));
  if (painter.hasClipping())
    bounds = bounds.intersected(painter.clipBoundingRect().adjusted(-halo, -halo, halo, halo));
  const auto imageBounds = bounds.toAlignedRect();
  if (imageBounds.isEmpty())
    return;
  const qreal guard = radius * 2 + 4;
  const auto local =
      core.translated(-imageBounds.topLeft())
          .intersected(QRectF(-guard, -guard, imageBounds.width() + 2 * guard, imageBounds.height() + 2 * guard));
  QImage shadow(imageBounds.size(), QImage::Format_ARGB32_Premultiplied);
  shadow.fill(Qt::transparent);
  {
    QPainter mask(&shadow);
    mask.setRenderHint(QPainter::Antialiasing);
    mask.setPen(Qt::NoPen);
    mask.setBrush(color);
    mask.drawRoundedRect(local, radius, radius);
  }
  boxBlur(shadow, kernel);
  painter.drawImage(imageBounds.topLeft(), shadow);
}

void compactShadowMatchesFullCanvas() {
  const auto render = [](bool reference, QRectF rect, qreal radius, qreal blur, qreal spread, QColor color, qreal dpr,
                         int mode) {
    QImage image(QSize(460, 320) * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(QColor(20, 45, 60, 70));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (mode == 1) {
      painter.translate(7.25, -13.5);
      painter.setClipRect(QRectF(45.75, 26.25, 210.5, 180.75));
    }
    if (mode == 2) {
      painter.translate(5, 12);
      painter.scale(.9, 1.1);
      painter.rotate(7);
      painter.setOpacity(.6);
    }
    const QPointF offset(3.25, -2.5);
    if (reference)
      fullCanvasBoxShadow(painter, rect, radius, color, offset, blur, spread);
    else
      DecorationPainter::paintBoxShadow(painter, rect, radius, color, offset.x(), offset.y(), blur, spread);
    return image;
  };
  const QVector<QRectF> rects{{30.25, 25.5, 340.75, 220.25},
                              {20.5, -2000.25, 390.25, 6000},
                              {-80.75, 40.25, 700.5, 200.75},
                              {60.25, 50.75, 22.5, 90.25},
                              {40.25, 90.75, 310.5, 18.25}};
  for (qreal dpr : {1., 1.5, 2.})
    for (int mode : {0, 1, 2})
      for (int i = 0; i < rects.size(); ++i)
        for (const auto values : {QPointF(0, 5), QPointF(9.75, 11), QPointF(180, 3), QPointF(5.5, 0)}) {
          const qreal spread = i % 2 ? -1.25 : 2.5;
          const QColor color = i % 2 ? QColor(30, 120, 210, 75) : QColor(100, 20, 130, 163);
          const auto expected = render(true, rects[i], values.x(), values.y(), spread, color, dpr, mode);
          const auto actual = render(false, rects[i], values.x(), values.y(), spread, color, dpr, mode);
          require(actual == expected,
                  QString("compact shadow equals full canvas: dpr=%1 mode=%2 shape=%3 radius=%4 blur=%5")
                      .arg(dpr)
                      .arg(mode)
                      .arg(i)
                      .arg(values.x())
                      .arg(values.y()));
          require(render(false, rects[i], values.x(), values.y(), spread, color, dpr, mode) == actual,
                  "cached shadow retains exact convolution pixels");
        }
  DecorationPainter::resetBoxShadowRasterStats();
  render(false, {10.5, -5000.25, 401.25, 10000}, 8, 7, .75, QColor(23, 67, 118, 93), 1, 0);
  const auto work = DecorationPainter::boxShadowRasterStats();
  require(work.misses == 1 && work.maskPixels * 20 < work.outputPixels && work.cacheBytes <= 16 * 1024 * 1024,
          "large constant interiors avoid full-canvas convolution while final cache stays bounded");
}

void shadowRasterReuse() {
  const auto draw = [](QPointF offset, QColor color, qreal radius, QRectF clip = {}) {
    QImage image(300, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(offset);
    if (!clip.isEmpty())
      painter.setClipRect(clip);
    DecorationPainter::paintBoxShadow(painter, {70.25, 60.5, 89, 43}, radius, color, 2, 3, 11, 1);
    return image;
  };
  const auto cold = draw({}, QColor("#6a317fa0"), 7);
  enableThemePerformance(true);
  resetThemePerformance();
  const auto warm = draw({}, QColor("#6a317fa0"), 7);
  const auto shifted = draw({7, 11}, QColor("#6a317fa0"), 7);
  const auto samples = themePerformanceSamples();
  enableThemePerformance(false);
  require(cold == warm, "cached shadow must preserve all cold raster pixels");
  require(cold.copy(0, 0, 290, 180) == shifted.copy(7, 11, 290, 180),
          "integer translations preserve exact shadow coverage");
  require(samples[size_t(ThemePerfStage::Shadow)].work == 0,
          "same shadow geometry must reuse its raster across integer translations");
  require(draw({}, Qt::red, 7) != warm && draw({}, QColor("#6a317fa0"), 2) != warm,
          "color and radius changes cannot use the old shadow mask");
  const auto clipped = draw({}, QColor("#6a317fa0"), 7, {75, 55, 25, 80});
  require(clipped != warm && clipped == draw({}, QColor("#6a317fa0"), 7, {75, 55, 25, 80}),
          "clipped shadow cache preserves the visible edge rather than a different viewport");
  const auto tall = [](qreal height) {
    QImage image(300, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    DecorationPainter::paintBoxShadow(painter, {20.25, 20.5, 250, height}, 9, QColor("#59312f77"), 1, 2, 13, 1);
    return image;
  };
  const auto firstPage = tall(100000);
  enableThemePerformance(true);
  resetThemePerformance();
  const auto largerPage = tall(200000);
  const auto tallWork = themePerformanceSamples()[size_t(ThemePerfStage::Shadow)].work;
  enableThemePerformance(false);
  require(firstPage == largerPage && tallWork == 0,
          "distant lazy page-height changes preserve the visible mask and cached raster");
}
} // namespace
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  require(argc > 1, "browser effects fixture argument");
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), "read fixed browser effects");
  const auto root = QJsonDocument::fromJson(file.readAll()).object();
  runTest("browser effect pixels", [&] { pixels(root); });
  runTest("browser positioned boxes", [&] { positions(root); });
  runTest("browser counter witnesses", [&] { counters(root); });
  runTest("effect capabilities", capabilities);
  runTest("sparse far shadows", sparseShadows);
  runTest("tall page shadows", tallPageShadows);
  runTest("widget device clip", widgetDeviceClip);
  runTest("edited counter cache invalidation", editedCounters);
  runTest("whitespace mask inline-block", whitespaceMaskAtom);
  runTest("shadow raster reuse", shadowRasterReuse);
  runTest("compact shadow equals full canvas", compactShadowMatchesFullCanvas);
  return 0;
}
