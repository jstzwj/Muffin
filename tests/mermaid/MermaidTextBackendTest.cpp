#include "../TestUtils.h"
#include "document/DocumentSession.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "render/DocumentLayout.h"
#include "render/InlineLayout.h"
#include "render/TextLayout.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <cmath>

using namespace muffin;
using namespace muffin::mermaid;
using namespace muffin::mermaid::flowchart;
namespace mermaid_editor = muffin::mermaid::editor;
using mermaid_editor::MermaidRenderCache;

namespace {
QRect inkBounds(const QImage& image) {
  QRect bounds;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      if (qAlpha(image.pixel(x, y)) > 32) bounds = bounds.united(QRect(x, y, 1, 1));
  return bounds;
}

void glyphCoordinates() {
  for (auto backend : {TextBackend::Native, TextBackend::Fractional}) {
    QFont font;
    MermaidFontRegistry::configureFont(font, MermaidFontRegistry::cssFamilyStack());
    setTextPixelSize(font, 28.8, backend);
    font.setHintingPreference(QFont::PreferNoHinting);
    const QString text = QString::fromUtf8("office AV 中文 fi مرحبا");
    TextLayout layout(text, font, backend);
    layout.beginLayout();
    auto line = layout.createLine();
    line.setLineWidth(900);
    line.setPosition({7, 9});
    layout.endLayout();
    const auto runs = line.glyphRuns();
    require(!runs.isEmpty(), "logical glyph runs available");
    QSet<QString> families;
    QImage fromLayout(950, 120, QImage::Format_ARGB32_Premultiplied), fromGlyphs(fromLayout.size(), fromLayout.format());
    fromLayout.fill(Qt::transparent);
    fromGlyphs.fill(Qt::transparent);
    {
      QPainter painter(&fromLayout);
      layout.draw(&painter, {13, 11});
    }
    {
      QPainter painter(&fromGlyphs);
      for (const auto& run : runs) {
        require(run.stringIndexes().size() == run.glyphIndexes().size(), "glyph source indices preserved");
        require(qAbs(run.rawFont().pixelSize() - textFontPixelSize(font)) < .02, "raw font exposes logical fractional size");
        require(run.boundingRect().right() < 950, "glyph bounds never expose shaping-device coordinates");
        families.insert(run.rawFont().familyName());
        for (int i = 0; i < run.glyphIndexes().size(); ++i) {
          require(run.glyphIndexes()[i] != 0, "pinned Latin/CJK/Arabic fallback contains every glyph");
          require(run.stringIndexes()[i] < text.size(), "glyph source index in range");
          require(qAbs(run.positions()[i].y()) < 120, "glyph baseline uses logical pixels");
        }
        painter.drawGlyphRun({13, 11}, run);
      }
    }
    require(families.contains("Noto Sans") && families.contains("Noto Sans CJK SC"), "pinned mixed-script font fallback");
    const auto a = inkBounds(fromLayout), b = inkBounds(fromGlyphs);
    require(!a.isEmpty() && !b.isEmpty(), "both glyph and layout painters produce text");
    require(qAbs(a.left() - b.left()) <= 1 && qAbs(a.right() - b.right()) <= 1 &&
                qAbs(a.top() - b.top()) <= 1 && qAbs(a.bottom() - b.bottom()) <= 1,
            "prepared glyph painting agrees with shared layout extent");
    if (backend == TextBackend::Native) require(fromLayout == fromGlyphs, "native glyph painting stays pixel-identical");
  }
}

void flowLabels() {
  const auto family = MermaidFontRegistry::cssFamilyStack();
  for (qreal size : {16., 18.4, 28.8}) {
    for (qreal width : {110., 260.}) {
      auto label = parseFlowLabel(QString::fromUtf8("A long **bold** and *italic* 中文标签 with office AV fi and more words"), "markdown");
      label.underline = true;
      label.letterSpacingPx = .3;
      label = wrapFlowLabel(label, family, size, width);
      const auto measured = layoutFlowLabel(label, family, size, size * 1.5);
      require(measured.lines.size() > 1 && measured.size.height() > size, "long labels wrap at allocated width");
      qreal totalHeight = 0;
      for (const auto& line : measured.lines) {
        require(line.width > 0 && line.width < width + size, "wrapped line has logical width");
        require(line.baseline > 0 && line.baseline <= line.height, "baseline is inside line box");
        totalHeight += line.blockHeight;
        for (const auto& run : line.runs) {
          require(!run.preparedGlyphs.isEmpty(), "paint consumes full-line prepared glyphs");
          require(qAbs(run.preparedGlyphs.rawFont().pixelSize() - textFontPixelSize(makeFlowLabelFont(family, size))) < .02,
                  "FlowLabel retains the common backend size");
        }
      }
      require(qAbs(totalHeight - measured.size.height()) < .001, "layout height sums the same line boxes used by painting");
      for (qreal zoom : {.8, 1., 1.25, 2.}) {
        QImage image(qCeil((width + 60) * zoom), qCeil((measured.size.height() + 60) * zoom), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
          QPainter painter(&image);
          painter.scale(zoom, zoom);
          paintFlowLabel(painter, label, {30, 30, width, measured.size.height()}, family, size, size * 1.5, Qt::black,
                         false, FlowLabelAlign::Left);
        }
        const QRect ink = inkBounds(image);
        require(!ink.isEmpty(), "zoomed label paints");
        require(ink.left() > 15 * zoom && ink.right() < (width + 45) * zoom && ink.top() > 15 * zoom &&
                    ink.bottom() < (measured.size.height() + 45) * zoom,
                "scaled prepared glyphs stay inside the allocated label envelope");
      }
    }
    auto math = parseFlowLabel(QString::fromUtf8("中文 $$\\frac{x}{2}$$ tail"), "markdown");
    require(prepareFlowLabelMath(math, size) > 0, "specialized math backend prepares content");
    const auto measured = layoutFlowLabel(math, family, size, size * 1.5);
    require(std::isfinite(measured.size.width()) && measured.size.width() > 0 && measured.size.height() > 0,
            "math outer layout shares finite logical coordinates");
  }
}

QStringList diagramSources() {
  return {
      QString::fromUtf8("flowchart LR\nA[\"A long 中文标签 with office and italic words\"]-->B[Done]"),
      QString::fromUtf8("gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\nsection 中文计划\nA long task label :a, 2024-01-01, 3d"),
      QString::fromUtf8("C4Context\nPerson(user, \"中文用户\", \"A long description\")\nSystem(app, \"Muffin\")\nRel(user, app, \"Uses\")"),
      QString::fromUtf8("journey\ntitle 中文旅程\nsection Work\nA long label with many words: 5: Me"),
      QString::fromUtf8("venn-beta\nset A[中文标签]: 10"),
      QString::fromUtf8("mindmap\n  root((中文主题))\n    A long child label\n    Another child"),
  };
}

QImage paintDocument(DocumentLayout& layout, const RenderTheme& theme, int width) {
  QImage image(width, qCeil(layout.totalHeight()) + 1, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  for (const auto* block : layout.promotedBlocks()) block->paint(painter, theme, 0);
  return image;
}

void cacheAndDocument() {
  MermaidRenderCache cache;
  for (const auto& body : diagramSources()) {
    const QString source = QStringLiteral("%%{init:{\"fontFamily\":\"Noto Sans\",\"themeVariables\":{\"fontFamily\":\"Noto Sans\",\"fontSize\":\"18.4px\"}}}%%\n") + body;
    const auto key = MermaidRenderCache::makeKey(source);
    const auto entry = cache.getSync(key, source);
    require(entry.status == mermaid_editor::MermaidRenderStatus::Ready && entry.scene, "each migrated family builds a scene: " + body);
    require(entry.scene == cache.getSync(key, source).scene, "immutable scene cache reuses text measurements");
    require(entry.naturalSize.width() > 0 && entry.naturalSize.height() > 0, "scene has finite positive extent");
    DocumentSession session;
    session.setMarkdownText("before\n\n```mermaid\n" + source + "\n```\n\nafter", false);
    for (int zoom : {80, 125, 200}) {
      RenderTheme theme = RenderTheme::defaultTheme();
      theme.setZoomPercent(zoom);
      for (int width : {300, 740}) {
        DocumentLayout eager, lazy;
        for (auto* layout : {&eager, &lazy}) {
          layout->setMermaidRenderCache(&cache);
          layout->setMermaidSyncMode(true);
          layout->rebuild(session.document(), theme, width, {}, {}, layout == &eager ? DocumentLayout::BuildPolicy::Eager
                                                                                  : DocumentLayout::BuildPolicy::Lazy);
          layout->buildAll(theme);
        }
        require(qAbs(eager.totalHeight() - lazy.totalHeight()) < .001, "full/lazy Mermaid document height agrees");
        for (auto* actual : eager.promotedBlocks()) {
          const auto* expected = lazy.block(actual->nodeId());
          require(expected && expected->rect() == actual->rect(), "full/lazy allocated block geometry agrees");
          if (const auto* text = actual->inlineLayout()) {
            require(expected->inlineLayout() && text->cursorRectForSourceOffset(2) == expected->inlineLayout()->cursorRectForSourceOffset(2),
                    "neighboring text caret agrees");
            require(text->selectionRectsForSourceOffsets(0, 3) == expected->inlineLayout()->selectionRectsForSourceOffsets(0, 3),
                    "neighboring text selection agrees");
          }
        }
        // Prime FreeType's first-use glyph raster cache before comparing pixels.
        paintDocument(eager, theme, width);
        paintDocument(lazy, theme, width);
        require(paintDocument(eager, theme, width) == paintDocument(lazy, theme, width), "full/lazy diagram painting agrees at each zoom and width");
      }
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_FONT_DPI", "96");
  QApplication app(argc, argv);
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  QCoreApplication::setOrganizationName("Muffin");
  QCoreApplication::setApplicationName("MermaidTextBackendTest");
  MermaidFontRegistry::ensureLoaded();
  runTest("glyphCoordinates", glyphCoordinates);
  runTest("flowLabels", flowLabels);
  runTest("cacheAndDocument", cacheAndDocument);
  return 0;
}
