#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "render/InlineLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QString>

#include "RenderTestUtils.h"
#include "../theme/BrowserLayoutFont.h"

using namespace muffin;

namespace {

// Natural (unwrapped) width of the first paragraph under a theme.
qreal paragraphNaturalWidth(const QString& css, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("t"), QString()));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(p != nullptr, QStringLiteral("fixture should contain a paragraph"));
  const BlockLayout* block = layout.block(p->id());
  require(block != nullptr, QStringLiteral("paragraph block should be promoted"));
  const InlineLayout* inlineLayout = block->inlineLayout();
  require(inlineLayout != nullptr, QStringLiteral("paragraph should have an inline layout"));
  return inlineLayout->size().width();
}

// Phase 3c: a link ::before icon must reserve real inline flow (Phase 3c-1), not
// just paint into the left margin. A paragraph whose only content is a short
// link stays on one line, so its natural width grows by ~the icon advance when
// the icon theme is applied. Relative (same link text / offscreen font), so the
// assertion is robust to the offscreen font-metric differences that make
// absolute geometry unreliable.
void testLinkBeforeIconReservesFlow() {
  const QString markdown = QStringLiteral("[ab](https://example.com)\n");  // short → one line
  const QString base = QStringLiteral(
      "#write { color:#000000; } a { color:#0000ff; }");
  const QString icon = QStringLiteral(
      "#write { color:#000000; } a { color:#0000ff; }"
      "#write a::before { content:''; background-color:#0000ff;"
      " width:16px; height:16px; margin-right:6px;"
      " -webkit-mask:url(\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'>"
      "<path d='M0 0L10 10'/></svg>\") center/contain; }");
  const qreal baseWidth = paragraphNaturalWidth(base, markdown);
  const qreal iconWidth = paragraphNaturalWidth(icon, markdown);
  // The icon's reserved flow is a DELTA, not an absolute width. The ::before is fixed
  // at width:16px + margin-right:6px = 22 CSS pixels, which advances the line by the
  // same amount on every platform (the "ab" text width cancels: it appears in both
  // baseWidth and iconWidth). The absolute widths themselves vary with the offscreen
  // font (Windows >20px, macOS ~19px), so an absolute floor on baseWidth flakes —
  // assert the cross-platform delta instead. See offscreen-test-harness-broken-font-metrics.
  const qreal reserved = iconWidth - baseWidth;
  require(baseWidth > 1.0,
          QStringLiteral("baseline link paragraph should render (width=%1)").arg(baseWidth));
  require(reserved > 15.0 && reserved < 30.0,
          QStringLiteral("a::before icon must reserve ~22px (16w+6margin) of flow (base=%1 icon=%2 delta=%3)")
              .arg(baseWidth).arg(iconWidth).arg(reserved));
}

void testBrowserGeneratedLinksAndEditing() {
  const auto reference = QJsonDocument::fromJson(readFixture(
      QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/live-pseudos-browser.json")).toUtf8()).object();
  const auto family = browserLayoutFont(reference).value("muffinfixturesans");
  for (const auto value : reference["cases"].toArray()) {
    const auto entry = value.toObject();
    if (entry["id"].toString().startsWith("absolute-")) continue;
    auto css = entry["css"].toString(); css.replace("MuffinFixtureSans", family);
    for (int zoom : {100, 125, 200}) {
      const auto context = entry["id"].toString() + QString(" at %1%").arg(zoom);
      const qreal scale = zoom / 100., width = entry["width"].toDouble() * scale;
      const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, context, {}), zoom);
      DocumentSession session; session.setMarkdownText(entry["markdown"].toString() + '\n', false);
      DocumentLayout layout; layout.rebuild(session.document(), theme, width);
      const auto id = session.document().root().children()[0]->id();
      const auto* text = layout.block(id)->inlineLayout();
      const auto expected = entry["expected"].toObject();
      require(qAbs(text->height() - expected["height"].toDouble() * scale) < 1.1,
          context + QString(" browser height actual=%1 expected=%2").arg(text->height()).arg(expected["height"].toDouble() * scale));
      const auto chars = expected["characters"].toArray();
      require(text->visibleText().size() == chars.size(), context + " generated text is excluded from the editable text");
      for (int i = 0; i < chars.size(); ++i) {
        if (text->visibleText()[i].isSpace()) continue;
        const auto caret = text->cursorRect(i); const auto character = chars[i].toObject();
        require(qAbs(caret.x() - character["x"].toDouble() * scale) < 1.2,
            context + QString(" character %1 x actual=%2 expected=%3").arg(i).arg(caret.x()).arg(character["x"].toDouble() * scale));
        require(qAbs(caret.y() - character["y"].toDouble() * scale) < 1.2,
            context + QString(" character %1 y actual=%2 expected=%3").arg(i).arg(caret.y()).arg(character["y"].toDouble() * scale));
        require(text->hitTestTextOffset({caret.left(), caret.center().y()}) == i, context + " click/caret round trip");
      }
      for (const auto& rect : text->generatedPseudoRects()) {
        require(!text->linkHrefAtLocalPos(rect.center()).isEmpty(), context + " generated link content keeps its link target");
        const auto anchor = text->hitTestTextOffset(rect.center());
        require(text->hitTestCursorRect(rect.center()) == text->cursorRect(anchor), context + " generated content snaps to its own link boundary");
      }
      const auto compare = [&] {
        const auto* current = layout.block(id);
        for (const auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
          DocumentLayout fresh; fresh.rebuild(session.document(), theme, width, {}, {}, policy); fresh.buildAll(theme);
          const auto* other = fresh.block(id);
          require(current->rect() == other->rect(), context + " eager/lazy/incremental box");
          const auto* a = current->inlineLayout(); const auto* b = other->inlineLayout();
          require(a->selectionRects(0, a->visibleText().size()) == b->selectionRects(0, b->visibleText().size()), context + " selection consistency");
          for (int i = 0; i <= a->visibleText().size(); ++i) require(a->cursorRect(i) == b->cursorRect(i), context + " caret consistency");
          const auto raster = [&](const BlockLayout* block) {
            QImage image(qCeil(width + 20), qCeil(block->height() + 20), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
            QPainter painter(&image); painter.translate(-block->rect().topLeft()); block->paint(painter, theme, 0, nullptr); return image;
          };
          require(raster(current) == raster(other), context + " pixel consistency");
        }
      };
      compare();
      require(session.applyTextDelta(0, 0, "Edited ", true, {{id, 0, BlockType::Paragraph}}), context + " edit");
      const auto range = session.lastLocalTopLevelRangeChange();
      if (range.isValid()) layout.rebuildTopLevelRange(range, session.document(), theme, {});
      else layout.rebuildBlock(id, session.document(), theme, {});
      compare();
    }
  }
}

void testStandaloneLinkUsesTheSharedCascade() {
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{font-size:16px}a[href$='one.test']::before{content:'prefix'}", "standalone-link", {}));
  const auto measured = [&](const QString& href) {
    const auto markdown = QString("[link](%1)").arg(href);
    DocumentSession session;
    session.setMarkdownText(markdown + '\n', false);
    const auto& nodes = session.document().root().children()[0]->inlines();
    InlineLayout text;
    text.build(nodes, markdown, theme, 500, theme.paragraphFont(), {});
    require(text.visibleText() == "link", "temporary generated content is not editable");
    return text.intrinsicWidths().second;
  };
  const auto one = measured("https://one.test"), two = measured("https://two.test");
  require(one > two + 10, "standalone links use actual attributes through the common computed engine");
  require(measured("https://one.test") == one && measured("https://two.test") == two,
      "temporary semantic elements do not alias cached pseudo snapshots");
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testLinkBeforeIconReservesFlow);
  RUN_TEST(testBrowserGeneratedLinksAndEditing);
  RUN_TEST(testStandaloneLinkUsesTheSharedCascade);
#undef RUN_TEST
  return 0;
}
