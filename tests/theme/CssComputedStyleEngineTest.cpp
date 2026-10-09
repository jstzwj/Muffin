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
  require(s.resolvedValue(QStringLiteral("border-color")) == QStringLiteral("#00f3ff"),
          QStringLiteral("var() fallback should resolve with inherited custom properties"));
}

void testSelectorMatching() {
  CssElement write; write.id = QStringLiteral("write");
  CssElement inlineCode; inlineCode.tag = QStringLiteral("code"); inlineCode.parent = &write;
  CssElement fence; fence.tag = QStringLiteral("code"); fence.classes = {QStringLiteral("md-fencescode")}; fence.parent = &write;
  CssElement pre; pre.tag = QStringLiteral("pre"); pre.classes = {QStringLiteral("md-fences")}; pre.parent = &write;
  CssElement table; table.tag = QStringLiteral("table"); table.parent = &write;
  CssElement tbody; tbody.tag = QStringLiteral("tbody"); tbody.parent = &table;
  CssElement tr; tr.tag = QStringLiteral("tr"); tr.childIndex = 1; tr.parent = &tbody;

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

void testNestedBlockquoteParagraphAndMarker() {
  CssElement body; body.tag = QStringLiteral("body");
  CssElement write; write.id = QStringLiteral("write"); write.parent = &body;
  CssElement p; p.tag = QStringLiteral("p"); p.parent = &write;
  CssElement blockquote; blockquote.tag = QStringLiteral("blockquote"); blockquote.parent = &write;
  CssElement quoteP; quoteP.tag = QStringLiteral("p"); quoteP.parent = &blockquote;
  CssElement ul; ul.tag = QStringLiteral("ul"); ul.parent = &write;
  CssElement li; li.tag = QStringLiteral("li"); li.parent = &ul;
  CssElement marker; marker.tag = QStringLiteral("li"); marker.pseudoElement = QStringLiteral("marker"); marker.parent = &li;
  const QString css = QStringLiteral(
      ":root { --quote:#7aeaf0; --marker:#3db8bf; }"
      "#write p { color:#222222; }"
      "#write blockquote { --quote:#089ba3; }"
      "#write blockquote p { color:var(--quote); }"
      "#write li::marker { color:var(--marker); }");
  require(styleFor(css, p).resolvedValue(QStringLiteral("color")) == QStringLiteral("#222222"),
          QStringLiteral("normal paragraph keeps #write p colour"));
  require(styleFor(css, quoteP).resolvedValue(QStringLiteral("color")) == QStringLiteral("#089ba3"),
          QStringLiteral("blockquote p should resolve inherited blockquote custom property"));
  require(styleFor(css, marker).resolvedValue(QStringLiteral("color")) == QStringLiteral("#3db8bf"),
          QStringLiteral("li::marker should match as a pseudo element under li"));
}

void testUnsupportedStructuralPseudosDoNotLeak() {
  CssElement write; write.id = QStringLiteral("write");
  CssElement p; p.tag = QStringLiteral("p"); p.parent = &write;
  const QString css = QStringLiteral(
      "#write { color:#111111; text-align:left; }"
      "#write p { color:#222222; }"
      "#write p:has(img) { text-align:center; color:#ff00ff; }");
  const CssComputedStyle s = styleFor(css, p);
  require(s.resolvedValue(QStringLiteral("color")) == QStringLiteral("#222222"),
          QStringLiteral("p:has(img) colour must not leak into every prototype p"));
  require(s.resolvedValue(QStringLiteral("text-align")) == QStringLiteral("left"),
          QStringLiteral("p:has(img) text-align must not center every prototype p"));
}

void testHoverStateQuery() {
  CssElement write; write.id = QStringLiteral("write");
  CssElement h2; h2.tag = QStringLiteral("h2"); h2.parent = &write;
  const QString css = QStringLiteral(
      "#write h2 { color:#111111; }"
      "#write h2:hover { color:#3db8bf; box-shadow:0 0 16px #3db8bf; }");
  const CssThemeSheet sheet = CssThemeParser::parse(css, QString());
  CssComputedStyleEngine engine(sheet);
  require(engine.styleFor(h2).resolvedValue(QStringLiteral("color")) == QStringLiteral("#111111"),
          QStringLiteral("static h2 should ignore hover colour"));
  CssElementState hover; hover.hover = true;
  const CssComputedStyle hovered = engine.styleFor(h2, hover);
  require(hovered.resolvedValue(QStringLiteral("color")) == QStringLiteral("#3db8bf"),
          QStringLiteral("hover query should apply h2:hover colour"));
  require(hovered.resolvedValue(QStringLiteral("box-shadow")).contains(QStringLiteral("16px")),
          QStringLiteral("hover query should expose h2:hover shadow"));
}

void testTyporaEditorOnlyClassDropped() {
  CssElement body; body.tag = QStringLiteral("body");
  CssElement write; write.id = QStringLiteral("write"); write.parent = &body;
  CssElement pre; pre.tag = QStringLiteral("pre"); pre.parent = &write;
  // pixyll-style editor-only hacks (md-meta-block / ty-*) must never leak into a
  // rendered element, even when the selector is structural and routes through this
  // computed-style engine (not just the flat mapper). See theme/TyporaEditorOnly.h.
  const QString css = QStringLiteral(
      "pre { background:#111111; }"
      "pre.md-meta-block { background:#ff0000; padding-top:2000px; }"
      ".ty-search-panel { background:#00ff00; }");
  const CssComputedStyle s = styleFor(css, pre);
  require(s.resolvedValue(QStringLiteral("background")) == QStringLiteral("#111111"),
          QStringLiteral("pre.md-meta-block editor-only hack must not leak into rendered pre"));
  require(!s.hasProperty(QStringLiteral("padding-top")),
          QStringLiteral("editor-only padding-top must not reach the element"));
}

void testRootSelectorAppliesToHtml() {
  CssElement html; html.tag = QStringLiteral("html");
  CssElement body; body.tag = QStringLiteral("body"); body.parent = &html;
  // :root element declarations (not just :root variables) must reach the root element.
  const QString css = QStringLiteral(
      ":root { color:#123456; font-size:16px; }"
      "body { color:#999999; }");
  const CssComputedStyle rootStyle = styleFor(css, html);
  require(rootStyle.resolvedValue(QStringLiteral("color")) == QStringLiteral("#123456"),
          QStringLiteral(":root element declarations should apply to the html root element"));
  require(rootStyle.resolvedValue(QStringLiteral("font-size")) == QStringLiteral("16px"),
          QStringLiteral(":root font-size should reach the root element"));
  require(styleFor(css, body).resolvedValue(QStringLiteral("color")) == QStringLiteral("#999999"),
          QStringLiteral(":root must not leak into body — it matches only the root"));
}

void testAttributeSelectorsAndInlineCascade() {
  CssElement svg; svg.tag = QStringLiteral("svg"); svg.id = QStringLiteral("diagram");
  CssElement group; group.tag = QStringLiteral("g"); group.classes = {QStringLiteral("node")};
  group.attributes.insert(QStringLiteral("data-id"), QStringLiteral("Alpha-42"));
  group.attributes.insert(QStringLiteral("data-look"), QStringLiteral("neo"));
  group.parent = &svg;
  CssElement rect; rect.tag = QStringLiteral("rect"); rect.classes = {QStringLiteral("basic")};
  rect.parent = &group;

  const CssThemeSheet sheet = CssThemeParser::parse(QStringLiteral(
      "[data-id] rect { fill:#111; }"
      "[data-id^='Alpha'][data-id$='42'] > rect { fill:#222; }"
      "[data-look='NEO' i] rect { stroke:#333 !important; color:#444; }"), {});
  const CssComputedStyleEngine engine(sheet);
  CssComputedStyle style = engine.styleFor(rect);
  require(style.resolvedValue(QStringLiteral("fill")) == QStringLiteral("#222"),
          QStringLiteral("attribute existence/prefix/suffix selectors should match SVG attributes"));
  require(style.resolvedValue(QStringLiteral("stroke")) == QStringLiteral("#333"),
          QStringLiteral("case-insensitive attribute selector should match"));

  const auto inlineNormal = CssThemeParser::parseDeclarations(
      QStringLiteral("fill:#abc; stroke:#def;"));
  style = engine.styleFor(rect, CssElementState{}, inlineNormal);
  require(style.resolvedValue(QStringLiteral("fill")) == QStringLiteral("#abc"),
          QStringLiteral("inline normal should beat normal stylesheet rules"));
  require(style.resolvedValue(QStringLiteral("stroke")) == QStringLiteral("#333"),
          QStringLiteral("stylesheet !important should beat inline normal"));

  const auto inlineImportant = CssThemeParser::parseDeclarations(
      QStringLiteral("stroke:#fed !important"));
  style = engine.styleFor(rect, CssElementState{}, inlineImportant);
  require(style.resolvedValue(QStringLiteral("stroke")) == QStringLiteral("#fed"),
          QStringLiteral("inline !important should beat stylesheet !important"));
}

void testCssValueValidityAndShorthandReset() {
  CssElement p;
  p.tag = QStringLiteral("p");
  auto check = [&](const QString& css) { return styleFor(css, p); };
  require(check(QStringLiteral("p{color:red;color:madeup}")).resolvedValue(QStringLiteral("color")) == QStringLiteral("red"),
          QStringLiteral("invalid later color must not replace a valid declaration"));
  require(check(QStringLiteral("p{background-color:red;background:blue}")).resolvedValue(QStringLiteral("background-color")) ==
              QStringLiteral("blue"),
          QStringLiteral("background shorthand must reset and replace background-color"));
  require(check(QStringLiteral("p{font-size:0em}")).fontSizePx == 0.0, QStringLiteral("zero font-size is a valid computed value"));
  require(check(QStringLiteral("p{padding-left:7px;padding-left:-10px}")).resolvedValue(QStringLiteral("padding-left")) ==
              QStringLiteral("7px"),
          QStringLiteral("negative padding declaration must be invalid"));
  require(check("p{background:red;background:wrong}").resolvedValue("background-color") == QStringLiteral("red"),
          "invalid background shorthand must not erase a valid color");
  require(check("p{border:2px solid red;border:3px 4px blue}").resolvedValue("border-left-width") == QStringLiteral("2px"),
          "duplicate shorthand components must invalidate the whole border declaration");
  require(check("p{background-color:red;background:linear-gradient(blue,green)}").resolvedValue("background-color") ==
              QStringLiteral("transparent"),
          "image background shorthand resets the omitted color");
  require(check("p{font-weight:bold;font:16px serif}").resolvedValue("font-weight") == QStringLiteral("normal"),
          "font shorthand resets omitted font components");
  require(!check(QStringLiteral("p{border-left-width:2px}")).hasProperty(QStringLiteral("border-left-style")),
          QStringLiteral("border width alone must retain the initial none style"));
}

void testCustomPropertyCaseAndComputedInheritance() {
  CssElement html;
  html.tag = QStringLiteral("html");
  CssElement parent;
  parent.tag = QStringLiteral("div");
  parent.parent = &html;
  CssElement child;
  child.tag = QStringLiteral("p");
  child.parent = &parent;
  const auto sheet =
      CssThemeParser::parse(QStringLiteral("html{--Tone:red;--Alias:var(--Tone)}div{--Tone:blue}p{--tone:green;color:var(--Alias)}"), {});
  const CssComputedStyle s = CssComputedStyleEngine(sheet).styleFor(child);
  require(s.resolvedValue(QStringLiteral("color")) == QStringLiteral("red"),
          QStringLiteral("inherited custom aliases must keep the parent's computed value"));
  require(s.rawValue(QStringLiteral("--Tone")) == QStringLiteral("blue") && s.rawValue(QStringLiteral("--tone")) == QStringLiteral("green"),
          QStringLiteral("custom property names must preserve case"));
}

void testComputedInvalidityAndCycles() {
  CssElement html;
  html.tag = "html";
  CssElement p;
  p.tag = "p";
  p.parent = &html;
  const auto s = styleFor("html{color:blue}p{color:red;color:var(--missing);margin-left:7px;margin:var(--missing)}", p);
  require(s.resolvedValue("color") == QStringLiteral("blue"), "invalid at computed time must unset, not resurrect an earlier declaration");
  require(s.resolvedValue("margin-left") == QStringLiteral("0px"), "invalid variable shorthand must reset every component");
  require(styleFor("p{--a:var(--b);--b:var(--a,red);color:var(--a,green)}", p).resolvedValue("color") == QStringLiteral("green"),
          "cyclic variables must invalidate the cycle before applying a consumer fallback");
  require(styleFor("html{--a:red}p{--a:initial;color:var(--a,blue)}", p).resolvedValue("color") == QStringLiteral("blue"),
          "custom initial must suppress the inherited custom property");
  require(CssThemeParser::resolveVars("'var(--x)'", {{"--x", "red"}}) == QStringLiteral("'var(--x)'"),
          "var tokens inside string literals must remain literal");
  require(styleFor("p{--a:'var(--a)';content:var(--a)}", p).resolvedValue("content") == QStringLiteral("'var(--a)'"),
          "quoted var text must not create a custom-property dependency cycle");
  const auto empty = styleFor("p{--empty:;padding:var(--empty,5px)}", p);
  require(empty.customProperties().contains("--empty") && empty.resolvedValue("padding-left") == QStringLiteral("0px"),
          "an empty custom value is valid and must not trigger var fallback");
}

void testDeferredLengthsAndDimensionalMath() {
  CssElement p;
  p.tag = "p";
  const auto s = styleFor("p{font-size:20px;margin-left:calc(10% + 2em);padding-top:5%;padding-right:calc(2px - 4px)}", p);
  const auto margin = s.length("margin-left");
  require(margin.status == CssLengthStatus::Valid && margin.hasPercentage && margin.px == 40 && margin.fraction == .1,
          "computed length must retain percentage instead of using viewport width");
  require(margin.used(600) == 100 && margin.used(300) == 70, "same computed value must resolve against different containing blocks");
  require(s.length("padding-top").used(600) == 30, "vertical padding percentages use containing width");
  CssElement parent;
  parent.tag = "div";
  p.parent = &parent;
  const auto inherited = styleFor("div{font-size:20px;margin-left:2em;letter-spacing:1em}p{font-size:10px;margin-left:inherit}", p);
  require(inherited.length("margin-left").px == 40 && inherited.length("letter-spacing").px == 20,
          "inherited lengths must freeze font-relative units in the defining parent");
  require(parseCssLengthPercentage(u"0em", {}).status == CssLengthStatus::Valid, "zero is valid in every length unit");
  require(parseCssLengthPercentage(u"", {}).status == CssLengthStatus::Missing, "missing length has a separate state");
  for (const auto& value : {u"3", u"4bogus", u"calc(2px * 3px)", u"calc(2px / 0)", u"calc(2px + 1)", u"calc(2px+1px)"})
    require(parseCssLengthPercentage(value, {}).status == CssLengthStatus::Invalid, "invalid length/dimensional math must be rejected");
  require(parseCssLengthPercentage(u"calc(0px * 2)", {}).status == CssLengthStatus::Valid,
          "calc result zero must not be treated as a parse failure");
  require(styleFor("p{font-size:22px;font-size:wrong;padding:8px;padding:1px -2px}", p).fontSizePx == 22,
          "invalid font declaration keeps earlier value");
  require(styleFor("p{padding:8px;padding:1px -2px}", p).resolvedValue("padding-top") == QStringLiteral("8px"),
          "invalid shorthand is rejected atomically");
}

void testFunctionalSelectorsAndSpecificity() {
  CssElement root;
  root.tag = "div";
  root.classes = {"scope"};
  CssElement p;
  p.tag = "p";
  p.classes = {"Note"};
  p.parent = &root;
  require(styleFor("p{color:red}:where(#x,.Note){color:blue}", p).resolvedValue("color") == QStringLiteral("red"),
          ":where contributes zero specificity");
  require(styleFor(".Note{color:red}:is(#absent,div.scope > p){color:blue}", p).resolvedValue("color") == QStringLiteral("blue"),
          ":is matches complex alternatives and takes the maximum argument specificity");
  require(styleFor("p{color:red}p:not(.other,.scope > .Note){color:blue}", p).resolvedValue("color") == QStringLiteral("red"),
          ":not must evaluate selector lists and complex selectors");
  require(styleFor("p{color:red}p:is(:unsupported,.Note):not(.other){color:blue}", p).resolvedValue("color") == QStringLiteral("blue"),
          ":is uses a forgiving selector list");
  require(!styleFor(".note{padding:2px}", p).hasProperty("padding-left"), "class selectors are case sensitive");
  const auto sheet = CssThemeParser::parse("p:is(:nth-child(2),.x){color:red}", {});
  require(CssComputedStyleEngine(sheet).selectorFeatures().hasStructuralRules,
          "structural features inside functional selectors must activate live matching");
}

void testCacheLifetimeAndGeneration() {
  const auto sheet = CssThemeParser::parse("p{color:red}h1{color:blue}p:hover{color:green}", {});
  CssComputedStyleEngine engine(sheet);
  CssElement element;
  element.tag = "p";
  require(engine.styleFor(element).resolvedValue("color") == QStringLiteral("red"), "initial uncached query");
  element.tag = "h1";
  require(engine.styleFor(element).resolvedValue("color") == QStringLiteral("blue"), "temporary elements must not be cached by address");
  element.tag = "p";
  element.cacheId = 123;
  require(engine.styleFor(element).resolvedValue("color") == QStringLiteral("red"), "snapshot cache query");
  CssElementState hover;
  hover.hover = true;
  require(engine.styleFor(element, hover).resolvedValue("color") == QStringLiteral("green"), "cache keys distinguish interaction states");
  const auto generation = engine.generation();
  element.tag = "h1";
  engine.clearCache();
  require(engine.generation() > generation && engine.styleFor(element).resolvedValue("color") == QStringLiteral("blue"),
          "new generation invalidates previous computed styles");
}

void testEnvironmentSnapshots() {
  const auto sheet = CssThemeParser::parse(
      ":root{--size:5vw}p{width:calc(var(--size) + 10px)}"
      "@media(min-width:800px){p:nth-child(2){color:red}}",
      {});
  CssEnvironment environment;
  environment.viewportWidth = 640;
  environment.viewportHeight = 480;
  CssComputedStyleEngine engine(sheet, environment);
  CssElement p;
  p.tag = "p";
  p.cacheId = 7;
  p.childIndex = 1;
  require(qAbs(engine.styleFor(p).length("width").px - 42) < .01, "initial viewport-unit value");
  environment.viewportWidth = 720;
  require(engine.sameActiveRules(environment), "same media bucket reuses selector compilation");
  const auto resized = engine.withEnvironment(environment);
  require(resized->generation() > engine.generation(), "environment copy starts a fresh value generation");
  require(qAbs(resized->styleFor(p).length("width").px - 46) < .01, "custom-property viewport lengths recompute");
  require(qAbs(engine.styleFor(p).length("width").px - 42) < .01, "old environment and cached snapshot remain immutable");
  const std::vector<CssDeclaration> inlineStyle{{"height", "10vh", false}};
  environment.viewportHeight = 600;
  const auto taller = resized->withEnvironment(environment);
  require(qAbs(taller->styleFor(p, {}, inlineStyle).length("height").px - 60) < .01, "inline viewport units use the new environment");
  environment.viewportWidth = 900;
  require(!engine.sameActiveRules(environment), "media activation changes the rule set");
  const auto wide = taller->withEnvironment(environment);
  require(wide->styleFor(p).resolvedValue("color") == "red" && wide->selectorFeatures().hasStructuralRules,
          "newly active structural rules compile and invalidate values");
  require(!engine.selectorFeatures().hasStructuralRules && !engine.styleFor(p).hasProperty("color"),
          "new media state does not mutate the original engine");
}

void testModernMediaConditions() {
  CssEnvironment env;
  env.viewportWidth = 800;
  env.viewportHeight = 800;
  for (const auto& query :
       {"screen and (600px <= width < 1000px)", "(width >= 800px)", "(hover:hover) and (pointer:fine)", "(orientation:portrait)",
        "((width < 600px) or (width >= 800px))", "(prefers-reduced-motion:no-preference)", "not screen and (min-width:1000px)"})
    require(CssThemeSheet::mediaMatches(query, env), "supported media condition should match desktop environment");
  for (const auto& query : {"(width > 800px)", "(prefers-reduced-motion:reduce)", "(unknown:yes)", "(min-width:2dpi)"})
    require(!CssThemeSheet::mediaMatches(query, env), "inactive/unknown/invalid-unit media condition must not leak");
}

void testPseudoOriginSelectorsAndStateInheritance() {
  const auto sheet = CssThemeParser::parse(
      "#write>a:is(.chosen):not(.excluded)::before{content:'yes';font-size:50%}"
      "a{font-size:20px;color:red}a:hover{color:blue;--label:'hover'}"
      "a:hover::after{content:var(--label)}", {});
  CssComputedStyleEngine engine(sheet);
  CssElement parent; parent.tag = "div"; parent.id = "write";
  CssElement origin; origin.tag = "a"; origin.classes = {"chosen"}; origin.parent = &parent;
  CssElement pseudo = origin; pseudo.pseudoElement = "before"; pseudo.originatingElement = &origin;
  auto style = engine.styleFor(pseudo);
  require(style.resolvedValue("content") == "'yes'" && style.fontSizePx == 10,
          "functional selectors inspect the pseudo's origin, whose parent remains the selector parent");
  CssElementState state; state.hover = true;
  require(engine.styleFor(pseudo, state).resolvedValue("color") == "blue", "pseudo inherits the active host state");
  pseudo.pseudoElement = "after";
  require(engine.styleFor(pseudo, state).resolvedValue("content") == "'hover'", "host-state custom properties inherit before pseudo computation");
  origin.classes << "excluded"; pseudo.classes = origin.classes; pseudo.pseudoElement = "before";
  require(!engine.styleFor(pseudo).hasProperty("content"), "not() excludes the originating element");
}

void testGeneratedPseudoCandidatesAcrossMedia() {
  const auto sheet = CssThemeParser::parse(
      "#write::before{content:''}h4:hover::after{content:'!'}"
      "@media(min-width:800px){:is(a,h2)::before{content:'+'}}", {});
  CssEnvironment environment; environment.viewportWidth = 600;
  CssComputedStyleEngine engine(sheet, environment);
  CssElement paragraph; paragraph.tag = "p";
  CssElement write; write.tag = "div"; write.id = "write";
  CssElement heading; heading.tag = "h4";
  require(!engine.mayGeneratePseudo(paragraph, "before") && !engine.mayGeneratePseudo(paragraph, "after"),
      "unrelated paragraphs need no generated-style snapshots");
  require(engine.mayGeneratePseudo(write, "before") && engine.mayGeneratePseudo(heading, "after"),
      "id-qualified and inactive interactive selectors stay candidates");
  environment.viewportWidth = 900;
  const auto wide = engine.withEnvironment(environment);
  require(wide->mayGeneratePseudo(paragraph, "before"), "functional subjects remain conservative after media activation");
  require(!engine.mayGeneratePseudo(paragraph, "before"), "media snapshots keep independent candidate indexes");
  CssElement link; link.tag = "a";
  CssElement pseudo = link; pseudo.originatingElement = &link; pseudo.pseudoElement = "before";
  require(wide->mayGeneratePseudo(link, "before") && wide->styleFor(pseudo).resolvedValue("content") == "'+'",
      "candidate filtering preserves the shared functional-selector cascade");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testCascadeOrder);
  RUN_TEST(testInheritanceAndCustomProperties);
  RUN_TEST(testSelectorMatching);
  RUN_TEST(testFilteringAndPseudoIsolation);
  RUN_TEST(testNestedBlockquoteParagraphAndMarker);
  RUN_TEST(testUnsupportedStructuralPseudosDoNotLeak);
  RUN_TEST(testHoverStateQuery);
  RUN_TEST(testTyporaEditorOnlyClassDropped);
  RUN_TEST(testRootSelectorAppliesToHtml);
  RUN_TEST(testAttributeSelectorsAndInlineCascade);
  RUN_TEST(testCssValueValidityAndShorthandReset);
  RUN_TEST(testCustomPropertyCaseAndComputedInheritance);
  RUN_TEST(testComputedInvalidityAndCycles);
  RUN_TEST(testDeferredLengthsAndDimensionalMath);
  RUN_TEST(testFunctionalSelectorsAndSpecificity);
  RUN_TEST(testCacheLifetimeAndGeneration);
  RUN_TEST(testEnvironmentSnapshots);
  RUN_TEST(testModernMediaConditions);
  RUN_TEST(testPseudoOriginSelectorsAndStateInheritance);
  RUN_TEST(testGeneratedPseudoCandidatesAcrossMedia);
#undef RUN_TEST
  return 0;
}
