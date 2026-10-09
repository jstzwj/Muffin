#include "BrowserLayoutFont.h"
#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "html/HtmlRenderer.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "render/GradientPainter.h"
#include "render/InlineLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/CssValueParser.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QtMath>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const QString& context) {
  if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", qPrintable(context)); }
}
void near(qreal actual, qreal expected, qreal tolerance, const QString& context) {
  check(qAbs(actual - expected) <= tolerance, context + QString(" actual=%1 expected=%2").arg(actual).arg(expected));
}
const muffin::html::HtmlBox* paragraph(const muffin::html::HtmlBox& box) {
  if (box.tag() == muffin::html::HtmlTag::Paragraph) return &box;
  for (const auto& child : box.children()) if (const auto* found = paragraph(*child)) return found;
  return nullptr;
}
void saveLayout(const muffin::DocumentSession& document, const muffin::RenderTheme& theme, const muffin::DocumentLayout& layout,
                  qreal width, const QString& id) {
  const auto directory = qEnvironmentVariable("MUFFIN_THEME_AUDIT_DIR");
  if (directory.isEmpty()) return;
  QDir().mkpath(directory);
  QImage image(qCeil(width), qCeil(layout.totalHeight() + 40), QImage::Format_ARGB32_Premultiplied);
  const auto background = theme.viewportBackgroundColor();
  image.fill(background.isValid() ? background : QColor(Qt::white));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  for (const auto& node : document.document().root().children()) layout.block(node->id())->paint(painter, theme, 0, nullptr);
  painter.end();
  check(image.save(directory + '/' + id + "-native.png"), id + " audit raster");
}
void compareLazy(const muffin::DocumentSession& document, const muffin::RenderTheme& theme, const muffin::DocumentLayout& eager,
                   qreal width, const QString& id) {
  muffin::DocumentLayout lazy;
  lazy.rebuild(document.document(), theme, width, {}, {}, muffin::DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  for (const auto& node : document.document().root().children())
    check(lazy.block(node->id())->rect() == eager.block(node->id())->rect(), id + " eager/lazy box");
  near(lazy.totalHeight(), eager.totalHeight(), .001, id + " eager/lazy height");
}
void headingFixtures(const QJsonObject& reference, const QString& family) {
  const auto width = reference["viewport"].toObject()["width"].toDouble();
  for (const auto& value : reference["headings"].toArray()) {
    const auto entry = value.toObject();
    const auto id = entry["id"].toString();
    auto css = entry["css"].toString(); css.replace("MuffinFixtureSans", family);
    auto theme = muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss(css, id, {}));
    theme.updateForViewport(width, 1600);
    QString markdown;
    for (const auto& node : entry["nodes"].toArray()) {
      const auto tag = node.toObject()["tag"].toString();
      markdown += (tag.startsWith('h') ? QString(tag.mid(1).toInt(), '#') + ' ' : QString()) + node.toObject()["text"].toString() + "\n\n";
    }
    muffin::DocumentSession document;
    document.setMarkdownText(markdown.trimmed(), false);
    muffin::DocumentLayout layout;
    layout.rebuild(document.document(), theme, width);
    const auto& nodes = document.document().root().children();
    const auto expected = entry["expected"].toArray();
    check(nodes.size() == size_t(expected.size()), id + " block count");
    if (nodes.size() != size_t(expected.size())) continue;
    const qreal origin = layout.block(nodes[0]->id())->cssBoxGeometry().borderBox.top();
    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto e = expected[int(i)].toObject();
      const auto& box = layout.block(nodes[i]->id())->cssBoxGeometry();
      const auto context = id + QString(" block %1").arg(i);
      near(box.borderBox.top() - origin, e["top"].toDouble(), .6, context + " top");
      near(box.borderBox.height(), e["height"].toDouble(), .2, context + " height");
      near(box.borderBox.width(), e["width"].toDouble(), .2, context + " width");
      near(box.font.pointSizeF() * 96 / 72, e["fontSize"].toDouble(), .02, context + " font");
      near(box.usedBox.margin.top(), e["marginTop"].toDouble(), .05, context + " margin-top");
      near(box.usedBox.margin.bottom(), e["marginBottom"].toDouble(), .05, context + " margin-bottom");
    }
    compareLazy(document, theme, layout, width, id);
    saveLayout(document, theme, layout, width, id);
  }
}
void inlineFixtures(const QJsonObject& reference, const QString& family) {
  for (const auto& value : reference["inlines"].toArray()) {
    const auto entry = value.toObject(), expected = entry["expected"].toObject();
    const auto id = entry["id"].toString();
    auto css = entry["css"].toString(); css.replace("MuffinFixtureSans", family);
    const auto theme = muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss(css, id, {}));
    muffin::DocumentSession document;
    document.setMarkdownText(entry["markdown"].toString(), false);
    const qreal width = entry["width"].toDouble();
    muffin::DocumentLayout layout;
    layout.rebuild(document.document(), theme, width);
    const auto* block = layout.block(document.document().root().children()[0]->id());
    const auto* text = block->inlineLayout();
    check(text != nullptr, id + " native text");
    if (!text) continue;
    near(text->height(), expected["height"].toDouble(), .25, id + " browser line height");
    const auto boxes = expected["boxes"].toArray();
    check(text->inlineBoxes().size() == boxes.size(), id + " keyboard fragments");
    for (qsizetype i = 0; i < qMin(text->inlineBoxes().size(), boxes.size()); ++i) {
      const auto& box = text->inlineBoxes()[i];
      const auto expectedBox = boxes[i].toObject();
      near(box.borderBox.left(), expectedBox["left"].toDouble(), .8, id + " keyboard left");
      near(box.borderBox.width(), expectedBox["width"].toDouble(), .8, id + " keyboard width");
      check(box.borderBox.left() >= -.01 && box.borderBox.right() <= width + .01, id + " keyboard inside wrapping width");
    }
    if (id.startsWith("keyboard-wrap")) {
      check(text->visibleText() == "abcdefgh S, next.", id + " generated edges do not enter copied text");
      for (qsizetype i = 0; i < text->visibleText().size(); ++i) {
        if (!text->visibleText()[i].isLetter()) continue;
        const auto cursor = text->cursorRect(i);
        check(text->hitTestTextOffset(QPointF(cursor.left(), cursor.center().y())) == i, id + " wrapped cursor/click roundtrip");
      }
      check(!text->selectionRects(0, text->visibleText().size()).isEmpty(), id + " wrapped selection");
    }
    if (id == "negative-leading") {
      check(block->visualOverflowRect().contains(text->visualTextBounds().translated(block->cssBoxGeometry().inlineTextOrigin)),
            id + " repaint covers glyphs beyond the CSS line box");
      for (const int offset : {2, 8}) {
        const auto cursor = text->cursorRect(offset);
        check(text->hitTestTextOffset(QPointF(cursor.left(), cursor.center().y())) == offset, id + " cursor/click roundtrip");
      }
      check(text->selectionRects(0, text->visibleText().size()).size() == 2, id + " selection covers two lines");
    }
    muffin::html::HtmlColorPalette palette = muffin::html::HtmlColorPalette::defaultLight();
    palette.documentStyleSheet = theme.documentStyleSheet();
    palette.fontAliases = theme.fontAliases();
    muffin::html::HtmlRenderer renderer;
    const auto html = renderer.render("<p>" + entry["html"].toString() + "</p>", 12, width, {}, palette);
    check(html.valid() && html.root(), id + " HTML layout");
    if (html.root()) {
      const auto* p = paragraph(*html.root());
      check(p != nullptr, id + " HTML paragraph");
      if (p) near(p->geometry().height, expected["height"].toDouble(), .25, id + " shared HTML line height");
    }
    compareLazy(document, theme, layout, width, id);
    saveLayout(document, theme, layout, width, id);
  }
}
void gradientFixtures(const QJsonObject& reference) {
  for (const auto& value : reference["gradients"].toArray()) {
    const auto entry = value.toObject(); const auto id = entry["id"].toString();
    muffin::CssLengthContext context;
    if (entry.contains("fontSize")) context.emPx = entry["fontSize"].toDouble();
    const auto spec = muffin::parseGradientSpec(entry["css"].toString(), {}, context);
    check(muffin::GradientPainter::isGradient(spec), id + " parsed gradient");
    const QImage expected = QImage::fromData(QByteArray::fromBase64(entry["png"].toString().toLatin1()));
    QImage actual(entry["width"].toInt(), entry["height"].toInt(), QImage::Format_ARGB32_Premultiplied);
    actual.fill(Qt::transparent);
    QPainter painter(&actual);
    painter.fillRect(actual.rect(), muffin::GradientPainter::makeBrush(spec, actual.rect(), entry["zoom"].toDouble(1)));
    painter.end();
    check(!expected.isNull() && expected.size() == actual.size(), id + " browser raster");
    for (const auto& sample : entry["samples"].toArray()) {
      const auto point = sample.toArray(); const int x = point[0].toInt(), y = point[1].toInt();
      const auto a = actual.pixelColor(x, y), e = expected.pixelColor(x, y);
      check(qAbs(a.red()-e.red()) <= 5 && qAbs(a.green()-e.green()) <= 5 && qAbs(a.blue()-e.blue()) <= 5 && qAbs(a.alpha()-e.alpha()) <= 5,
            id + QString(" pixel %1,%2 actual=%3 expected=%4").arg(x).arg(y).arg(a.name(QColor::HexArgb),e.name(QColor::HexArgb)));
    }
    if (id == "abyss-dot") {
      int visible = 0;
      for (int y=0; y<actual.height(); ++y) for (int x=0; x<actual.width(); ++x) if (actual.pixelColor(x,y).alpha() > 127) ++visible;
      check(visible == 4, "Abyss texture contains a 1px-radius dot, without a diffuse halo");
    }
  }
}
void wrappedMathBelongsToItsLine() {
  const auto theme = muffin::RenderTheme::fromDefinition(muffin::CssThemeMapper::fromCss(
      "#write{padding:0;margin:0;max-width:none}p{margin:0;font-size:16px;line-height:24px}", "wrapped-math", {}));
  muffin::DocumentSession document;
  document.setMarkdownText("abcdefgh $E=mc^2$.", false);
  for (const qreal width : {80., 100., 120.}) {
    muffin::DocumentLayout layout;
    layout.rebuild(document.document(), theme, width);
    const auto* text = layout.block(document.document().root().children()[0]->id())->inlineLayout();
    const auto rects = text->mathAtomRects({});
    check(rects.size() == 1, "one wrapped formula");
    if (!rects.isEmpty()) {
      check(rects[0].left() >= -.01 && rects[0].right() <= width + .01, "wrapped formula belongs to the next line");
      check(rects[0].top() >= -.01 && rects[0].bottom() <= text->height() + .01, "formula uses allocated baseline extents");
    }
    compareLazy(document, theme, layout, width, "wrapped-math");
  }
}
}  // namespace
int main(int argc, char** argv) {
  qputenv("QT_FONT_DPI", "96");
  QApplication app(argc, argv);
  QFile file(QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/typography-browser.json"));
  if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot open typography browser reference");
  const auto reference = QJsonDocument::fromJson(file.readAll()).object();
  const auto aliases = browserLayoutFont(reference);
  const auto family = aliases.value(reference["font"].toObject()["family"].toString().toLower());
  headingFixtures(reference, family);
  inlineFixtures(reference, family);
  gradientFixtures(reference);
  wrappedMathBelongsToItsLine();
  return failures ? 1 : 0;
}
