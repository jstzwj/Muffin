#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "../render/RenderTestUtils.h"
#include "BrowserLayoutFont.h"
#include "document/DocumentSession.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"

using namespace muffin;
namespace {
void requireNear(qreal actual, qreal expected, const QString& message, qreal tolerance = 1.2) {
  require(qAbs(actual - expected) <= tolerance, message + QString(" actual=%1 expected=%2").arg(actual).arg(expected));
}
void boxNear(const QRectF& actual, QJsonArray expected, const QString& message) {
  requireNear(actual.x(), expected[0].toDouble(), message + " x");
  requireNear(actual.y(), expected[1].toDouble(), message + " y");
  requireNear(actual.width(), expected[2].toDouble(), message + " width");
  requireNear(actual.height(), expected[3].toDouble(), message + " height");
}
void testRealThemeBrowserGeometry() {
  const auto reference =
      QJsonDocument::fromJson(readFixture(QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/real-theme-browser.json")).toUtf8())
          .object();
  const auto family = browserLayoutFont(reference).value("muffinfixturesans");
  const auto sources = reference["sources"].toObject();
  for (auto it = sources.begin(); it != sources.end(); ++it) {
    auto text = readFixture(QStringLiteral(MUFFIN_SOURCE_DIR "/") + it.key());
    text.replace("\r\n", "\n");
    require(QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex()) == it.value().toString(),
            "real theme CSS changed; regenerate the browser geometry reference");
  }
  for (const auto value : reference["cases"].toArray()) {
    const auto entry = value.toObject();
    const auto context = entry["id"].toString();
    auto css = readFixture(QStringLiteral(MUFFIN_SOURCE_DIR "/resources/themes/") + entry["theme"].toString() + ".css") + '\n' +
               reference["fixedFontCss"].toString();
    css.replace("MuffinFixtureSans", family);
    auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, context, QStringLiteral(MUFFIN_SOURCE_DIR "/resources/themes")));
    const auto width = entry["width"].toDouble();
    theme.updateForViewport(width, 1000);
    DocumentSession session;
    session.setMarkdownText(reference["markdown"].toString(), false);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, width);
    const auto expected = entry["expected"].toObject();
    const auto expectedBlocks = expected["blocks"].toArray();
    const auto& nodes = session.document().root().children();
    require(nodes.size() >= size_t(expectedBlocks.size()), context + " block count");
    const auto page = expected["page"].toArray();
    requireNear(layout.pageRect(theme, 0).x(), page[0].toDouble(), context + " page x");
    requireNear(layout.pageRect(theme, 0).width(), page[2].toDouble(), context + " page width");
    for (int i = 0; i < expectedBlocks.size(); ++i) {
      const auto* block = layout.block(nodes[i]->id());
      const auto expectedBlock = expectedBlocks[i].toObject();
      boxNear(block->cssBorderBox(), expectedBlock["border"].toArray(), context + QString(" block %1").arg(i));
      const auto* text = block->inlineLayout();
      require(text, context + " inline text");
      requireNear(text->firstLineBaselineY() + block->inlineTextOrigin().y(), expectedBlock["firstBaseline"].toDouble(),
                  context + " first baseline");
      requireNear(text->lastLineBaselineY() + block->inlineTextOrigin().y(), expectedBlock["lastBaseline"].toDouble(),
                  context + " last baseline");
      const auto chars = expectedBlock["characters"].toArray();
      require(text->visibleText().size() == chars.size(), context + " visible text size");
      for (int j = 0; j < chars.size(); ++j) {
        if (text->visibleText()[j].isSpace()) continue;
        const auto caret = text->cursorRect(j);
        const auto character = chars[j].toObject();
        requireNear(caret.x() + block->inlineTextOrigin().x(), character["x"].toDouble(), context + " character x");
        // A DOM Range encloses that character's font, while an editor caret
        // uses the whole line box. Compare their shared baseline separately.
        require(text->hitTestTextOffset({caret.left(), caret.center().y()}) == j, context + " click/caret round trip");
      }
      const auto components = expectedBlock["components"].toArray();
      require(text->inlineBoxes().size() == components.size(), context + " component count");
      for (int j = 0; j < components.size(); ++j)
        boxNear(text->inlineBoxes()[j].borderBox.translated(block->inlineTextOrigin()), components[j].toObject()["border"].toArray(),
                context + " component box");
    }
    const auto compare = [&] {
      for (const auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
        DocumentLayout fresh;
        fresh.rebuild(session.document(), theme, width, {}, {}, policy);
        fresh.buildAll(theme);
        for (const auto& node : session.document().root().children()) {
          const auto* a = layout.block(node->id());
          const auto* b = fresh.block(node->id());
          require(a && b && a->rect() == b->rect(), context + " full/lazy/incremental box");
          if (!a->inlineLayout()) continue;
          const auto* x = a->inlineLayout();
          const auto* y = b->inlineLayout();
          require(x->selectionRects(0, x->visibleText().size()) == y->selectionRects(0, y->visibleText().size()),
                  context + " selection consistency");
          for (int i = 0; i <= x->visibleText().size(); ++i) require(x->cursorRect(i) == y->cursorRect(i), context + " caret consistency");
        }
      }
    };
    compare();
    const auto id = nodes[0]->id();
    require(session.applyTextDelta(2, 0, "Edited ", true, {{id, 2, BlockType::Heading}}), context + " edit");
    const auto range = session.lastLocalTopLevelRangeChange();
    if (range.isValid())
      layout.rebuildTopLevelRange(range, session.document(), theme, {});
    else
      layout.rebuildBlock(id, session.document(), theme, {});
    compare();
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
  QCoreApplication::setOrganizationName("MuffinTests");
  QCoreApplication::setApplicationName("RealThemeBrowserGeometry");
  QSettings().setValue("markdown/breakOnSingleNewline", false);
  runTest("testRealThemeBrowserGeometry", testRealThemeBrowserGeometry);
  return 0;
}
