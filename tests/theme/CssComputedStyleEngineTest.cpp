#include "theme/CssComputedStyleEngine.h"
#include "theme/CssThemeParser.h"

#include <QCoreApplication>
#include <QString>

#include "../TestUtils.h"

using namespace muffin;

namespace {

CssComputedStyle styleFor(const QString& css, const CssElement& element) {
  const CssThemeSheet sheet = CssThemeParser::parse(css, QString());
  return CssComputedStyleEngine(sheet).styleFor(element);
}

void testCascadeOrder() {
  CssElement body; body.tag = QStringLiteral("body");
  CssElement write; write.id = QStringLiteral("write"); write.parent = &body;
  CssElement h2; h2.tag = QStringLiteral("h2"); h2.classes = {QStringLiteral("md-heading")}; h2.parent = &write;

  CssComputedStyle s = styleFor(QStringLiteral(
      "h2 { color: #111; }"
      "#write h2 { color: #222; }"
      "h2.md-heading { color: #333; }"), h2);
  require(s.resolvedValue(QStringLiteral("color")) == QStringLiteral("#222"),
          QStringLiteral("id-descendant specificity should beat class/tag selectors"));

  s = styleFor(QStringLiteral("h2 { color: #111 !important; } #write h2 { color: #222; }"), h2);
  require(s.resolvedValue(QStringLiteral("color")) == QStringLiteral("#111"),
          QStringLiteral("!important should beat higher specificity non-important rule"));

  s = styleFor(QStringLiteral("h2 { color: #111; } h2 { color: #222; }"), h2);
  require(s.resolvedValue(QStringLiteral("color")) == QStringLiteral("#222"),
          QStringLiteral("later source order should win ties"));
}

void testInheritanceAndCustomProperties() {
  CssElement body; body.tag = QStringLiteral("body");
  CssElement write; write.id = QStringLiteral("write"); write.parent = &body;
  CssElement p; p.tag = QStringLiteral("p"); p.parent = &write;
  const CssComputedStyle s = styleFor(QStringLiteral(
      ":root { --accent: #00f3ff; --deep: var(--accent); }"
      "body { color: #d6deeb; font-family: Body; line-height: 1.5; }"
      "#write { --accent: #ff00ff; line-height: 2.25; }"
      "#write p { color: var(--accent); border-color: var(--missing, var(--deep)); }"), p);
  require(s.resolvedValue(QStringLiteral("line-height")) == QStringLiteral("2.25"),
          QStringLiteral("#write line-height should inherit to paragraph"));
  require(s.resolvedValue(QStringLiteral("font-family")) == QStringLiteral("Body"),
          QStringLiteral("font-family should inherit from body"));
  require(s.resolvedValue(QStringLiteral("color")) == QStringLiteral("#ff00ff"),
          QStringLiteral("descendant custom property override should resolve var()"));
  require(s.resolvedValue(QStringLiteral("border-color")) == QStringLiteral("#ff00ff"),
          QStringLiteral("var() fallback should resolve with inherited custom properties"));
}

void testSelectorMatching() {
  CssElement write; write.id = QStringLiteral("write");
  CssElement inlineCode; inlineCode.tag = QStringLiteral("code"); inlineCode.parent = &write;
  CssElement fence; fence.tag = QStringLiteral("code"); fence.classes = {QStringLiteral("md-fencescode")}; fence.parent = &write;
  CssElement pre; pre.tag = QStringLiteral("pre"); pre.classes = {QStringLiteral("md-fences")}; pre.parent = &write;
  CssElement table; table.tag = QStringLiteral("table"); table.parent = &write;
  CssElement tbody; tbody.tag = QStringLiteral("tbody"); tbody.parent = &table;
  CssElement tr; tr.tag = QStringLiteral("tr"); tr.nthEven = true; tr.parent = &tbody;

  const QString css = QStringLiteral(
      "#write code:not(.md-fencescode) { background: #123456; }"
      ".md-fences { background: #222222; }"
      "tbody tr:nth-child(even) { background: #eeeeee; }");
  require(styleFor(css, inlineCode).resolvedValue(QStringLiteral("background")) == QStringLiteral("#123456"),
          QStringLiteral("code:not(.md-fencescode) should match inline code"));
  require(!styleFor(css, fence).hasProperty(QStringLiteral("background")),
          QStringLiteral("code:not(.md-fencescode) should not match fenced code class"));
  require(styleFor(css, pre).resolvedValue(QStringLiteral("background")) == QStringLiteral("#222222"),
          QStringLiteral(".md-fences should match fenced pre target"));
  require(styleFor(css, tr).resolvedValue(QStringLiteral("background")) == QStringLiteral("#eeeeee"),
          QStringLiteral("tbody tr:nth-child(even) should match alternate row target"));
}

void testFilteringAndPseudoIsolation() {
  CssElement body; body.tag = QStringLiteral("body");
  CssElement write; write.id = QStringLiteral("write"); write.parent = &body;
  CssElement code; code.tag = QStringLiteral("code"); code.parent = &write;
  CssElement writeBefore; writeBefore.id = QStringLiteral("write"); writeBefore.pseudoElement = QStringLiteral("before"); writeBefore.parent = &body;
  const QString css = QStringLiteral(
      "#write { background: #111111; color: #dddddd; }"
      ".typora-export #write { background: #ff00ff; }"
      "#write::before { background: #00ff00; }"
      "code { background: #222222; }"
      "code:hover, code.md-focus { background: #00f3ff; }");
  require(styleFor(css, write).resolvedValue(QStringLiteral("background")) == QStringLiteral("#111111"),
          QStringLiteral("export selector and #write::before must not override live #write background"));
  require(styleFor(css, code).resolvedValue(QStringLiteral("background")) == QStringLiteral("#222222"),
          QStringLiteral("hover/.md-focus rules must not leak into static code background"));
  require(styleFor(css, writeBefore).resolvedValue(QStringLiteral("background")) == QStringLiteral("#00ff00"),
          QStringLiteral("#write::before should still be matchable as a pseudo target"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testCascadeOrder);
  RUN_TEST(testInheritanceAndCustomProperties);
  RUN_TEST(testSelectorMatching);
  RUN_TEST(testFilteringAndPseudoIsolation);
#undef RUN_TEST
  return 0;
}
