#include "document/DocumentSession.h"
#include "render/DocumentLayout.h"
#include "render/InlineLayout.h"
#include "render/CssPaintAnimation.h"
#include "theme/CssThemeMapper.h"
#include "theme/NodeCssElement.h"
#include "BrowserLayoutFont.h"
#include "../render/RenderTestUtils.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>

using namespace muffin;
namespace {
QString normalizedDisplay(QString text) {
  text.remove(QChar(0x200b));
  text.remove(QChar(0x2060));
  text.remove(QChar(0xe001));
  return text;
}
void testIndentedSemanticStyles() {
  for (const QString& newline : {QStringLiteral("\n"), QStringLiteral("\r\n")}) {
    for (int indent = 0; indent <= 3; ++indent) {
      const QString spaces(indent, QLatin1Char(' '));
      DocumentSession session;
      session.setMarkdownText("prefix" + newline + newline + spaces + "**A *B***x **C**" + newline, false);
      auto theme =
          RenderTheme::fromDefinition(CssThemeMapper::fromCss("#write{padding:0}p{margin:0}strong{color:red}"
                                                              "strong::before{content:'['}strong::after{content:']'}"
                                                              "em::before{content:'{'}em::after{content:'}'}",
                                                              "indented-semantic-styles", {}));
      DocumentLayout layout;
      layout.rebuild(session.document(), theme, 700);
      const auto& node = *session.document().root().children()[1];
      const auto* text = layout.block(node.id())->inlineLayout();
      require(normalizedDisplay(text->displayText()).remove('\r') == spaces + "[A {B}]x [C]",
              QStringLiteral("indent %1: nested CSS styles use the same signed coordinates as projection: %2")
                  .arg(indent)
                  .arg(normalizedDisplay(text->displayText())));
      require(QString(text->visibleText()).remove('\r') == spaces + "A Bx C",
              "generated CSS content is excluded from editable text");
      require(text->stylesMatch(theme, node), "style invalidation uses the same origin as initial style lookup");
      const auto caret = text->cursorRect(indent + 2);
      require(text->hitTestTextOffset({caret.left(), caret.center().y()}) == indent + 2,
              "nested styled text retains hit-test coordinates with indentation");
    }
  }
}
void testBrowserSemantics() {
  const auto reference =
      QJsonDocument::fromJson(
          readFixture(QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/inline-semantics-browser.json")).toUtf8())
          .object();
  const auto family = browserLayoutFont(reference).value("muffinfixturesans");
  QFile icons(QStringLiteral(MUFFIN_SOURCE_DIR "/src/theme/MarkdownAlertIcon.h"));
  require(
      icons.open(QIODevice::ReadOnly) &&
          QString::fromLatin1(
              QCryptographicHash::hash(icons.readAll().replace("\r\n", "\n"), QCryptographicHash::Sha256).toHex()) ==
              reference["iconSourceSha256"].toString(),
      "semantic icon source changes require regenerating the browser reference");
  for (const auto entryValue : reference["cases"].toArray())
    for (int zoom : {100, 125, 200}) {
      const auto entry = entryValue.toObject();
      const auto context = entry["id"].toString() + QString(" at %1%").arg(zoom);
      auto css = entry["css"].toString();
      css.replace("MuffinFixtureSans", family);
      const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, context, {}), zoom);
      DocumentSession session;
      const auto source = entry["markdown"].toString() + '\n';
      session.setMarkdownText(source, false);
      DocumentLayout layout;
      const qreal scale = zoom / 100.;
      layout.rebuild(session.document(), theme, entry["width"].toDouble() * scale);
      const auto* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
      const auto* text = layout.block(p->id())->inlineLayout();
      const auto expected = entry["expected"].toObject();
      require(normalizedDisplay(text->displayText()) == expected["display"].toString(),
              context + " nested pseudo order actual=" + normalizedDisplay(text->displayText()) +
                  " expected=" + expected["display"].toString());
      require(qAbs(text->height() - expected["height"].toDouble() * scale) < 1.2, context + " browser line height");
      const auto characters = expected["characters"].toArray();
      int index = 0;
      for (int offset = 0; offset < text->visibleText().size(); ++offset) {
        if (text->visibleText()[offset] == '\n')
          continue;
        require(index < characters.size(), context + " editable text has no generated characters");
        const auto character = characters[index++].toObject();
        require(QString(text->visibleText()[offset]) == character["text"].toString(),
                context + " editable text sequence");
        if (text->visibleText()[offset].isSpace())
          continue;
        const auto caret = text->cursorRect(offset);
        require(qAbs(caret.x() - character["x"].toDouble() * scale) < 1.2 &&
                    qAbs(caret.y() - character["y"].toDouble() * scale) < 1.2,
                context + QString(" character %1 actual=(%2,%3) expected=(%4,%5)")
                              .arg(offset)
                              .arg(caret.x())
                              .arg(caret.y())
                              .arg(character["x"].toDouble() * scale)
                              .arg(character["y"].toDouble() * scale));
        require(text->hitTestTextOffset({caret.left(), caret.center().y()}) == offset,
                context + " click/caret round trip");
      }
      require(index == characters.size(), context + " all browser text is represented");
      for (const auto& rect : text->generatedPseudoRects()) {
        const auto anchor = text->hitTestTextOffset(rect.center());
        require(text->hitTestCursorRect(rect.center()) == text->cursorRect(anchor),
                context + " generated content clicks snap to an editable boundary");
        if (entry["id"].toString() == "nested-link")
          require(text->linkHrefAtLocalPos(rect.center()) == "https://example.com",
                  context + " nested pseudos inherit the link target");
      }
      if (entry["alert"].toBool()) {
        const auto images = theme.cssInlineImages(*p);
        require(images.size() == 1 && theme.computedStyleForElement(*images[0].element)
                                              .resolvedValue("fill")
                                              .compare("currentColor", Qt::CaseInsensitive) == 0,
                context + " icon is a real theme-addressable SVG child");
        const auto icon = expected["icon"].toObject();
        bool found = false;
        for (const auto& rect : text->generatedPseudoRects())
          found = found || (qAbs(rect.x() - icon["x"].toDouble() * scale) < 1.2 &&
                            qAbs(rect.y() - icon["y"].toDouble() * scale) < 1.2 &&
                            qAbs(rect.width() - icon["width"].toDouble() * scale) < .1 &&
                            qAbs(rect.height() - icon["height"].toDouble() * scale) < .1);
        require(found, context + " browser SVG geometry");
        SelectionRange selection;
        selection.focus.blockId = p->id();
        selection.focus.text.nodeId = p->id();
        selection.focus.text.sourceOffset = source.indexOf("[!") + 2;
        selection.focus.text.textOffset = 2;
        selection.anchor = selection.focus;
        layout.rebuild(session.document(), theme, entry["width"].toDouble() * scale, selection);
        require(layout.block(p->id())->inlineLayout()->visibleText().startsWith("[!"),
                context + " entering the title reveals its original marker");
      }
      require(session.document().markdownText().mid(0, source.size()) == source,
              context + " generated content never modifies Markdown");
    }
}
void testElementStateAndInvalidation() {
  DocumentSession session;
  session.setMarkdownText("**first** **second**\n", false);
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{padding:0;font-size:16px}p{margin:0}strong::before{content:'';display:inline-block;width:12px;height:12px;background:red;transition:background-color 100ms linear}"
      "strong:hover::before{background:blue}span:nth-of-type(2)>strong::after{content:'!'}",
      "interactive", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 500);
  const auto* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
  const auto* text = layout.block(p->id())->inlineLayout();
  require(text->stylesMatch(theme, *p), "all ancestor pseudo snapshots match before an edit");
  const auto rects = text->generatedPseudoRects();
  require(rects.size() == 3, "two generated shapes and the structural suffix exist");
  QVector<QRectF> shapes;
  for (const auto& rect : rects)
    if (qAbs(rect.width() - 12) < .1)
      shapes.push_back(rect);
  std::sort(shapes.begin(), shapes.end(), [](const auto& a, const auto& b) { return a.x() < b.x(); });
  require(shapes.size() == 2, "each strong has its own shape");
  CssPaintContext state;
  state.timeMs = 0;
  const auto paint = [&] {
    QImage image(520, 80, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    text->paint(painter, {}, 0, 0, &state, "test");
    painter.end();
    return image;
  };
  paint();
  state.pointer = shapes[0].center();
  paint();
  state.timeMs = 50;
  auto mid = paint();
  const auto a = mid.pixelColor(shapes[0].center().toPoint()), b = mid.pixelColor(shapes[1].center().toPoint());
  require(a.red() > 120 && a.red() < 136 && a.blue() > 120 && a.blue() < 136 && b == QColor(Qt::red),
          "hover over a generated shape animates only its own origin");
  state.timeMs = 100;
  require(paint().pixelColor(shapes[0].center().toPoint()) == QColor(Qt::blue),
          "pseudo transition reaches its final colour");
  // Structural siblings can change an unchanged block's generated content.
  session.setMarkdownText("**first** *new* **second**\n", false);
  theme.invalidateDocumentStyles();
  const auto* updated = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(!text->stylesMatch(theme, *updated),
          "changed semantic ancestor paths invalidate generated content snapshots");
}
void testAlertSvgPainting() {
  for (const auto kind :
       {QString("NOTE"), QString("TIP"), QString("IMPORTANT"), QString("WARNING"), QString("CAUTION")}) {
    DocumentSession session;
    session.setMarkdownText("> [!" + kind + "]\n> Body.\n", false);
    const auto* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
    const QString base =
        "#write{padding:0;font-size:16px;color:#123456}blockquote,p{padding:0;margin:0}"
        ".md-alert-text{color:#123456;transition:color 100ms linear}.md-alert-text:hover{color:#abcdef}";
    for (const auto extra : {QString(), QString(".md-alert-text svg path{fill:#ff0000}"),
                             QString(".md-alert-text{opacity:.5}"), QString(".md-alert-text svg{display:none}")}) {
      auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(base + extra, "svg-paint", {}));
      DocumentLayout layout;
      layout.rebuild(session.document(), theme, 500);
      const auto* text = layout.block(p->id())->inlineLayout();
      const auto rects = text->generatedPseudoRects();
      if (extra.contains("display:none")) {
        require(rects.isEmpty(), "SVG display:none removes both pixels and advance");
        continue;
      }
      require(rects.size() == 1, "one semantic icon per alert");
      CssPaintContext state;
      state.timeMs = 0;
      const auto paint = [&] {
        QImage image(500, 80, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        text->paint(painter, {}, 0, 0, &state, "svg");
        painter.end();
        return image;
      };
      const auto image = paint();
      const auto expected = extra.contains("ff0000")    ? QColor(Qt::red)
                            : extra.contains("opacity") ? QColor(136, 153, 170)
                                                        : QColor("#123456");
      QPoint sample(-1, -1);
      for (int y = qCeil(rects[0].top()); y < qFloor(rects[0].bottom()); ++y)
        for (int x = qCeil(rects[0].left()); x < qFloor(rects[0].right()); ++x) {
          const auto color = image.pixelColor(x, y);
          if (qAbs(color.red() - expected.red()) <= 1 && qAbs(color.green() - expected.green()) <= 1 &&
              qAbs(color.blue() - expected.blue()) <= 1)
            sample = {x, y};
        }
      require(sample.x() >= 0, kind + " semantic SVG uses theme path fill and ancestor opacity");
      if (extra.isEmpty()) {
        state.pointer = rects[0].center();
        paint();
        state.timeMs = 100;
        require(paint().pixelColor(sample) == QColor("#abcdef"),
                kind + " icon inherits its hovered title's animated currentColor");
      }
    }
  }
}
void testTextPseudoState() {
  DocumentSession session;
  session.setMarkdownText("**one** **two**\n", false);
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write{padding:0;font-size:16px}p{margin:0}strong::before{content:'[';color:red;background:lime;transition:color 100ms linear,background-color 100ms linear}"
      "strong:hover::before{color:blue;background:yellow}",
      "text-state", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 500);
  const auto* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
  const auto* text = layout.block(p->id())->inlineLayout();
  auto rects = text->generatedPseudoRects();
  std::sort(rects.begin(), rects.end(), [](const auto& a, const auto& b) { return a.x() < b.x(); });
  require(rects.size() == 2, "each strong gets a generated text prefix");
  CssPaintContext state;
  state.timeMs = 0;
  const auto paint = [&] {
    QImage image(500, 80, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    text->paint(painter, {}, 0, 0, &state, "text");
    painter.end();
    return image;
  };
  paint();
  state.pointer = rects[0].center();
  paint();
  state.timeMs = 100;
  const auto image = paint();
  bool blue = false, yellow = false, red = false, green = false;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const auto color = image.pixelColor(x, y);
      if (rects[0].contains(QPointF(x, y))) {
        blue = blue || (color.blue() > 150 && color.red() < 100);
        yellow = yellow || color == QColor(Qt::yellow);
      }
      if (rects[1].contains(QPointF(x, y))) {
        red = red || (color.red() > 150 && color.green() < 100);
        green = green || color == QColor(Qt::green) || color == QColor("lime");
      }
    }
  require(blue && yellow && red && green, "generated text foreground and background share element-local transitions");
}
}
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  runTest("indented semantic styles", testIndentedSemanticStyles);
  runTest("browser inline semantics", testBrowserSemantics);
  runTest("element state and invalidation", testElementStateAndInvalidation);
  runTest("alert SVG painting", testAlertSvgPainting);
  runTest("text pseudo state", testTextPseudoState);
  return 0;
}
