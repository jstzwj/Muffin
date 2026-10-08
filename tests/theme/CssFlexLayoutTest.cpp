#include "document/MarkdownDocument.h"
#include "document/DocumentSession.h"
#include "html/HtmlRenderer.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/CssComputedStyleEngine.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <cmath>

using namespace muffin;
namespace {
int failures = 0;
void require(bool condition, const QString& label) {
  if (!condition) {
    ++failures;
    std::cerr << label.toStdString() << '\n';
  }
}
void near(qreal actual, qreal expected, const QString& label, qreal tolerance = .8) {
  require(std::abs(actual - expected) < tolerance, label + QStringLiteral(" actual=%1 expected=%2").arg(actual).arg(expected));
}
void collect(const html::HtmlBox& box, QPointF parent, QHash<QString, QRectF>& rects) {
  const auto& g = box.geometry();
  const QPointF origin = parent + QPointF(g.left, g.top);
  if (!box.cssId.isEmpty()) rects.insert(box.cssId, QRectF(origin, QSizeF(g.width, g.height)));
  for (const auto& child : box.children()) collect(*child, origin, rects);
}
void browserFixtures() {
  QFile file(QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/flex-layout-browser.json"));
  require(file.open(QIODevice::ReadOnly), "open Flex browser fixture");
  const auto cases = QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray();
  require(cases.size() >= 14, "browser fixture cases");
  html::HtmlRenderer renderer;
  for (const auto& value : cases) {
    const auto c = value.toObject();
    const auto name = c["id"].toString();
    for (const int scale : {1, 2}) {
      auto palette = html::HtmlColorPalette::defaultLight();
      palette.cssZoom = scale;
      const auto result = renderer.render(c["html"].toString(), 12, 800 * scale, {}, palette);
      require(result.valid(), name + " valid");
      if (!result.root()) continue;
      QHash<QString, QRectF> rects;
      collect(*result.root(), {}, rects);
      const auto origin = rects.value("case").topLeft();
      const auto expected = c["expected"].toObject();
      for (auto it = expected.begin(); it != expected.end(); ++it) {
        require(rects.contains(it.key()), name + " contains " + it.key());
        const auto actual = rects.value(it.key()).translated(-origin);
        const auto e = it.value().toObject();
        near(actual.x(), e["x"].toDouble() * scale, name + " " + it.key() + " x");
        // Native Qt and Chrome round font ascents differently at 200% zoom.
        const qreal baselineTolerance = name.startsWith("baseline") ? 1.25 : .8;
        near(actual.y(), e["y"].toDouble() * scale, name + " " + it.key() + " y", baselineTolerance);
        near(actual.width(), e["width"].toDouble() * scale, name + " " + it.key() + " width");
        near(actual.height(), e["height"].toDouble() * scale, name + " " + it.key() + " height", baselineTolerance);
      }
    }
  }
}
void markdownFlex() {
  DocumentSession session;
  session.setMarkdownText("alpha beta gamma delta\n\nsecond paragraph\n\nthird\n", false);
  const auto& document = session.document();
  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{max-width:340px;padding:0;display:flex;gap:10px;flex-wrap:wrap}p{font:16px "
                              "Arial;line-height:20px;margin:0;flex:0 0 100px;min-width:0}p:first-child{order:2}",
                              "flex", {}));
  DocumentLayout eager, lazy;
  eager.rebuild(document, theme, 340, {}, {}, DocumentLayout::BuildPolicy::Eager);
  lazy.rebuild(document, theme, 340, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  const auto& nodes = document.root().children();
  require(nodes.size() == 3, "three Markdown Flex items");
  for (const auto& node : nodes) {
    const auto* a = eager.block(node->id());
    const auto* b = lazy.block(node->id());
    require(a && b, "Markdown Flex item built");
    if (!a || !b) continue;
    require(a->rect() == b->rect(), "eager/lazy Flex rectangles");
    near(a->rect().width(), 100, "Markdown Flex basis");
    const auto cursor = a->inlineLayout()->cursorRect(3).translated(a->inlineTextOrigin());
    const auto hit = eager.hitTest(cursor.center(), theme);
    require(hit.isValid() && hit.textNodeId == node->id() && hit.textOffset == 3, "Markdown Flex caret round trip");
    const auto selection = a->selectionRectsForOffsets(0, 3, theme);
    const auto localSelection = a->inlineLayout()->selectionRects(0, 3);
    require(selection.size() == localSelection.size() && !selection.isEmpty(), "Flex selection retains inline selection fragments");
    for (int i = 0; i < selection.size() && i < localSelection.size(); ++i)
      require(selection[i] == localSelection[i].translated(a->inlineTextOrigin()).adjusted(-1, 0, 1, 0),
              "Flex selection uses the allocated content origin and paint expansion");
    require(eager.blockAt(a->rect().center(), theme) == a, "same-row blockAt uses both axes");
  }
  require(eager.block(nodes[0]->id())->rect().left() > eager.block(nodes[2]->id())->rect().left(),
          "visual order differs from source order");
  require(session.applyTextDelta(0, 0, "a much longer first paragraph that wraps across many lines ", true), "apply real Flex text edit");
  const auto rebuilt = eager.rebuildBlock(document.root().children()[0]->id(), document, theme, {});
  require(rebuilt.rebuilt, "Flex group rebuild after edit");
  lazy.rebuild(document, theme, 340, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  near(eager.totalHeight(), lazy.totalHeight(), "Flex rebuild stable height");
  for (const auto& node : document.root().children())
    require(eager.block(node->id())->rect() == lazy.block(node->id())->rect(), "edited Flex matches fresh layout");
}
void computedFlex() {
  const auto sheet = CssThemeParser::parse(
      "div{flex:3 2 40px;flex:none;flex-flow:column "
      "wrap;flex-flow:row;gap:5px;row-gap:7px;flex-grow:-1;display:flex;display:invalid}p{flex:3 2 40px;flex:initial;gap:4px;gap:-1px}",
      {});
  const CssComputedStyleEngine engine(sheet);
  CssElement div;
  div.tag = "div";
  const auto style = engine.styleFor(div);
  require(
      style.resolvedValue("flex-grow") == "0" && style.resolvedValue("flex-shrink") == "0" && style.resolvedValue("flex-basis") == "auto",
      "flex:none resets all components; invalid negative does not override");
  require(style.resolvedValue("flex-direction") == "row" && style.resolvedValue("flex-wrap") == "nowrap",
          "flex-flow resets omitted wrapping");
  require(style.resolvedValue("row-gap") == "7px" && style.resolvedValue("column-gap") == "5px",
          "gap longhand participates in one cascade");
  require(style.resolvedValue("display") == "flex", "illegal display ignored");
  CssElement p;
  p.tag = "p";
  const auto initial = engine.styleFor(p);
  require(initial.resolvedValue("flex-grow") == "0" && initial.resolvedValue("flex-shrink") == "1" &&
              initial.resolvedValue("flex-basis") == "auto",
          "CSS-wide Flex reset");
  require(initial.resolvedValue("row-gap") == "4px", "invalid shorthand does not erase prior gap");
}
void markdownContainer() {
  DocumentSession session;
  session.setMarkdownText("> alpha beta gamma\n>\n> second paragraph\n\nTail", false);
  auto theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{max-width:340px;padding:0}blockquote{display:flex;gap:10px;width:240px;padding:0;border:0;margin:0}p{"
                              "margin:0;font:16px Arial;line-height:20px}blockquote p{flex:1;min-width:0}",
                              "flex-quote", {}));
  DocumentLayout eager, lazy;
  eager.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Eager);
  lazy.rebuild(session.document(), theme, 340, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  lazy.buildAll(theme);
  const auto id = session.document().root().children()[0]->id();
  const auto* quote = eager.block(id);
  require(quote && quote->children().size() >= 2, "Markdown nested Flex container");
  if (!quote || quote->children().size() < 2) return;
  require(quote->children()[0]->rect().left() < quote->children()[1]->rect().left(), "nested paragraphs share Flex row");
  require(quote->rect() == lazy.block(id)->rect(), "nested Flex eager/lazy convergence");
}
}  // namespace
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  browserFixtures();
  computedFlex();
  markdownFlex();
  markdownContainer();
  return failures ? 1 : 0;
}
