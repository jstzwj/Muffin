#include "document/DocumentSession.h"
#include "projection/MarkdownHtmlSerializer.h"
#include "render/DocumentLayout.h"
#include "render/DecorationPainter.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QRawFont>
#include <QtEndian>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QtMath>
#include <cstdio>

using namespace muffin;
namespace {
int failures = 0;
void check(bool ok, const QString& message) {
  if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); }
}
QJsonArray rect(QRectF r) { return {r.x(), r.y(), r.width(), r.height()}; }
QImage paint(const DocumentLayout& layout, const RenderTheme& theme, int width, bool focus = false) {
  QImage image(width, qCeil(layout.totalHeight() + 20), QImage::Format_ARGB32_Premultiplied);
  image.fill(theme.viewportBackgroundColor().isValid() ? theme.viewportBackgroundColor() : theme.backgroundColor());
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  const auto page = layout.pageRect(theme, 1000);
  DecorationPainter::paintBoxShadow(painter, page, theme.pageBorderRadius(), theme.pageShadowColor(),
      theme.pageShadowOffsetX(), theme.pageShadowOffsetY(), theme.pageShadowBlur(), theme.pageShadowSpread());
  painter.setBrush(theme.pageBackgroundColor());
  painter.setPen(theme.pageBorderColor().isValid() && theme.pageBorderWidth() > 0
      ? QPen(theme.pageBorderColor(), theme.pageBorderWidth()) : QPen(Qt::NoPen));
  painter.drawRoundedRect(page, theme.pageBorderRadius(), theme.pageBorderRadius());
  painter.save(); painter.setClipRect(page);
  DecorationPainter::paintWriteTexture(painter, theme, page);
  painter.restore();
  BlockLayout::BlockPaintState state;
  state.focusActive = focus;
  state.focusPhase = focus ? 1 : 0;
  for (const auto* block : layout.promotedBlocks()) block->paint(painter, theme, 0, nullptr, state);
  return image;
}
void sameLayout(const DocumentLayout& actual, const DocumentLayout& expected, const RenderTheme& theme, int width,
                const QString& id) {
  check(qAbs(actual.totalHeight() - expected.totalHeight()) < .01, id + " full/lazy height");
  for (const auto* block : actual.promotedBlocks()) {
    const auto* other = expected.block(block->nodeId());
    check(other && block->rect() == other->rect(), id + " full/lazy block geometry");
    const auto* text = block->inlineLayout();
    if (!other || !text || !other->inlineLayout()) continue;
    for (qsizetype i = 0; i < text->visibleText().size(); ++i) {
      if (!text->visibleText()[i].isLetter()) continue;
      const auto caret = text->cursorRect(i);
      check(caret == other->inlineLayout()->cursorRect(i), id + " full/lazy caret");
      const auto hit = text->hitTestTextOffset(QPointF(caret.left(), caret.center().y()));
      // Formula source characters deliberately snap to either atomic edge.
      check(hit == i || text->cursorRect(hit) == caret, id + QString(" click/caret offset %1 -> %2").arg(i).arg(hit));
    }
    check(text->selectionRects(0, text->visibleText().size()) ==
              other->inlineLayout()->selectionRects(0, other->inlineLayout()->visibleText().size()), id + " full/lazy selection");
  }
  // Warm both Qt font rasterization paths before comparing pixels.
  paint(actual, theme, width); paint(expected, theme, width);
  const auto a = paint(actual, theme, width), b = paint(expected, theme, width);
  if (a != b) {
    const auto output = qEnvironmentVariable("MUFFIN_THEME_AUDIT_DIR", QDir::currentPath());
    a.save(output + '/' + id + "-mismatch-native.png");
    b.save(output + '/' + id + "-mismatch-fresh.png");
    const auto ar = actual.pageRect(theme, 1000), br = expected.pageRect(theme, 1000);
    std::fprintf(stderr, "Page %.17g %.17g %.17g %.17g / %.17g %.17g %.17g %.17g; images %dx%d / %dx%d\n",
        ar.x(), ar.y(), ar.width(), ar.height(), br.x(), br.y(), br.width(), br.height(), a.width(), a.height(), b.width(), b.height());
    for (const auto* block : actual.promotedBlocks()) {
      const auto* other = expected.block(block->nodeId());
      if (other && block->inlineTextOrigin() != other->inlineTextOrigin()) {
        const auto x = block->inlineTextOrigin(), y = other->inlineTextOrigin();
        std::fprintf(stderr, "Text origins %.17g %.17g / %.17g %.17g\n", x.x(), x.y(), y.x(), y.y());
      }
    }
  }
  check(a == b, id + " full/lazy pixels");
}
void fresh(const DocumentSession& document, const DocumentLayout& actual, const RenderTheme& theme, int width, const QString& id) {
  for (const auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
    DocumentLayout other;
    other.rebuild(document.document(), theme, width, {}, {}, policy);
    other.buildAll(theme);
    sameLayout(actual, other, theme, width, id);
  }
}
QJsonArray geometry(const DocumentSession& document, const DocumentLayout& layout, const RenderTheme& theme) {
  QJsonArray result;
  for (const auto& node : document.document().root().children()) {
    const auto* block = layout.block(node->id());
    if (!block) continue;
    const auto& box = block->cssBoxGeometry();
    QJsonArray runs;
    if (const auto* text = block->inlineLayout())
      for (const auto& run : text->debugTextFormats(theme, box.font)) {
        if (run.format.foreground().color().alpha() == 0) continue;
        const auto font = run.format.font();
        runs.append(QJsonObject{{"start", run.start}, {"length", run.length}, {"family", font.families().join(", ")},
                                {"size", font.pointSizeF() * 96 / 72}, {"weight", int(font.weight())}, {"letterSpacing", font.letterSpacing()}});
      }
    const auto rawFont = QRawFont::fromFont(box.font);
    const auto os2 = rawFont.fontTable("OS/2");
    const QJsonObject metrics{{"requestedPx", box.font.pointSizeF() * 96 / 72},
        {"resolvedPx", rawFont.pixelSize()}, {"requestedWeight", int(box.font.weight())},
        {"faceWeight", os2.size() >= 6 ? int(qFromBigEndian<quint16>(os2.constData()+4)) : -1},
        {"layoutLetterSpacing", box.font.letterSpacing()}, {"layoutWordSpacing", box.font.wordSpacing()},
        {"advance", QFontMetricsF(box.font).horizontalAdvance(block->inlineLayout() ? block->inlineLayout()->visibleText() : QString())}};
    result.append(QJsonObject{{"type", int(node->type())}, {"headingLevel", node->headingLevel()}, {"rect", rect(block->rect())},
                             {"content", rect(box.contentBox)}, {"fontFamily", box.font.families().join(", ")},
                             {"resolvedFont", QFontInfo(box.font).family()}, {"fontMetrics", metrics}, {"fontSize", box.font.pointSizeF() * 96 / 72},
                             {"text", block->inlineLayout() ? block->inlineLayout()->visibleText() : QString()},
                             {"lines", block->inlineLayout() ? block->inlineLayout()->visualLineCount() : 0}, {"runs", runs},
                             {"editorEmpty", node->type() == BlockType::Paragraph && node->sourceRange().byteStart == node->sourceRange().byteEnd}});
  }
  return result;
}
void write(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write " + path);
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_FONT_DPI", "96");
  QApplication app(argc, argv);
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  QCoreApplication::setOrganizationName("MuffinTests");
  QCoreApplication::setApplicationName("RealThemeRegression");
  QSettings().setValue("markdown/breakOnSingleNewline", false);
  QFile example(QStringLiteral(MUFFIN_SOURCE_DIR "/example.md"));
  check(example.open(QIODevice::ReadOnly), "open example.md");
  auto source = QString::fromUtf8(example.readAll());
  source.replace("\r\n", "\n");
  // The same source excerpt as the four screenshots, including mixed fonts,
  // math, code/kbd, all heading levels and nested quotes.
  source = source.left(source.indexOf("## Lists"));
  QJsonArray themes{QJsonObject{{"id", "newsprint"}, {"css", QStringLiteral(MUFFIN_SOURCE_DIR "/resources/themes/newsprint.css")}},
                    QJsonObject{{"id", "night"}, {"css", QStringLiteral(MUFFIN_SOURCE_DIR "/resources/themes/night.css")}}};
  const QString manifestPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : qEnvironmentVariable("MUFFIN_THEME_AUDIT_MANIFEST");
  if (!manifestPath.isEmpty()) {
    QFile manifest(manifestPath);
    check(manifest.open(QIODevice::ReadOnly), "open external theme manifest");
    themes = QJsonDocument::fromJson(manifest.readAll()).array();
    check(!themes.isEmpty(), "external theme manifest is not empty");
  }
  const QString output = qEnvironmentVariable("MUFFIN_THEME_AUDIT_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  QJsonArray cases;
  for (const auto& value : themes) {
    const auto entry = value.toObject();
    const auto id = entry["id"].toString(), css = entry["css"].toString();
    check(QFile::exists(css), id + " original theme exists");
    const auto definition = ThemeDefinition::fromCss(css, id);
    for (int fontSize : {16, 18}) {
      auto theme = RenderTheme::fromDefinition(definition);
      theme.setFontSizePx(fontSize);
      DocumentSession document;
      document.setMarkdownText(source, false);
      DocumentLayout layout;
      int previousWidth = 0;
      for (int width : {960, 720, 1440}) {
        const QString caseId = id + QString("-%1-%2").arg(fontSize).arg(width);
        const bool changed = theme.updateForViewport(width, 1000);
        if (!previousWidth || changed || !layout.relayoutForViewportWidth(theme, width))
          layout.rebuild(document.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
        layout.buildAll(theme);
        fresh(document, layout, theme, width, caseId);
        if (!output.isEmpty()) {
          paint(layout, theme, width).save(output + '/' + caseId + "-native.png");
          paint(layout, theme, width, true).save(output + '/' + caseId + "-focus-native.png");
          cases.append(QJsonObject{{"id", caseId}, {"theme", id}, {"css", css}, {"fontSize", fontSize}, {"width", width},
                                   {"height", 1000}, {"viewportColor", theme.viewportBackgroundColor().name(QColor::HexRgb)},
                                   {"textColor", theme.textColor().name(QColor::HexRgb)}, {"blocks", geometry(document, layout, theme)}});
        }
        previousWidth = width;
      }
      // Actual parse/edit/incremental rebuild, then deletion restores the source.
      auto* node = document.document().root().children()[1].get();
      const auto nodeId = node->id();
      const auto start = node->sourceRange().byteStart;
      for (bool insert : {true, false}) {
        check(document.applyTextDelta(start, insert ? 0 : 14, insert ? QString("Inserted text ") : QString(), true,
                                      {{nodeId, start, BlockType::Paragraph}}), id + " typing/deletion");
        const auto range = document.lastLocalTopLevelRangeChange();
        if (range.isValid()) layout.rebuildTopLevelRange(range, document.document(), theme, {});
        else layout.rebuildBlock(nodeId, document.document(), theme, {});
        fresh(document, layout, theme, previousWidth, id + " edited");
      }
      check(document.markdownText().toString() == source, id + " deletion restores source");
    }
  }
  if (!output.isEmpty()) {
    DocumentSession document;
    document.setMarkdownText(source, false);
    MarkdownHtmlOptions options;
    options.breakOnSingleNewline = false;
    write(output + "/document.html", MarkdownHtmlSerializer::serializeTree(document.document().root(), options).toUtf8());
    write(output + "/native.json", QJsonDocument(QJsonObject{{"platform", QGuiApplication::platformName()},
                                                            {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex())},
                                                            {"cases", cases}}).toJson());
  }
  std::fprintf(stderr, "Real theme geometry/editing regression: %d failures\n", failures);
  return failures ? 1 : 0;
}
