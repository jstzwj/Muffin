#include "math/MathFontMetrics.h"
#include "math/MathMacroExpander.h"
#include "math/MathParseError.h"
#include "math/MathRenderer.h"
#include "math/MathSvgGeometry.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QJsonDocument>

#include <algorithm>
#include <functional>
#include <iostream>

#include "MathTestUtils.h"

using namespace muffin;

namespace {

void testMathMetricsMacrosAndState() {
  require(math::MathFontMetrics::loaded(), QStringLiteral("KaTeX font metrics data should load from resources"));
  require(math::MathFontMetrics::characterMetrics(QStringLiteral("Math-Italic"), QStringLiteral("x")).has_value(),
          QStringLiteral("KaTeX Math-Italic metrics should include x"));
  require(math::MathFontMetrics::characterMetrics(QStringLiteral("Main-Regular"), QStringLiteral("A")).has_value(),
          QStringLiteral("KaTeX Main-Regular metrics should include A"));
  require(math::MathFontMetrics::characterMetrics(QStringLiteral("Math-Italic"), QString(QChar(0x03b1))).has_value(),
          QStringLiteral("KaTeX Math-Italic metrics should include Greek alpha"));
  require(math::MathSvgGeometry::loaded(), QStringLiteral("KaTeX SVG geometry data should load from resources"));
  require(math::MathSvgGeometry::hasPath(QStringLiteral("rightarrow")), QStringLiteral("KaTeX SVG geometry should include rightarrow"));
  require(!math::MathSvgGeometry::painterPath(QStringLiteral("rightarrow"), QRectF(0.0, 0.0, 40.0, 8.0)).isEmpty(),
          QStringLiteral("KaTeX SVG geometry should convert rightarrow path to QPainterPath"));

  {
    math::MathMacroExpander expander;
    require(expander.expand(QStringLiteral("{\\def\\foo{A}\\foo}+\\foo")) == QStringLiteral("{A}+\\foo"),
            QStringLiteral("macro expander should restore local definitions at group end"));
    require(expander.expand(QStringLiteral("{\\global\\def\\bar{B}\\bar}+\\bar")) == QStringLiteral("{B}+B"),
            QStringLiteral("macro expander should preserve global definitions across groups"));
    require(expander.expand(QStringLiteral("\\def\\baz#1#2{#2#1}\\baz{A}{B}")) == QStringLiteral("BA"),
            QStringLiteral("macro expander should substitute primitive macro arguments"));
    require(expander.expand(QStringLiteral("\\let\\copy\\baz\\copy{C}{D}")) == QStringLiteral("DC"),
            QStringLiteral("macro expander should support let aliases"));
    require(expander.expand(QStringLiteral("\\futurelet\\next\\first\\second")) == QStringLiteral("\\first\\second"),
            QStringLiteral("macro expander should keep futurelet following tokens"));
    require(expander.expand(QStringLiteral("\\def\\a{A}\\def\\b{B}\\expandafter\\a\\b")) == QStringLiteral("AB"),
            QStringLiteral("macro expander should support token stack expandafter"));
    require(expander.expand(QStringLiteral("\\def\\a{A}\\noexpand\\a")) == QStringLiteral("\\relax"),
            QStringLiteral("macro expander should support noexpand relax treatment"));
    require(expander.expand(QStringLiteral("\\char`A")) == QStringLiteral("\\text{A}"),
            QStringLiteral("macro expander should lower char primitive to renderable text"));
    require(expander.expand(QStringLiteral("\\pmod{n}+\\iff+\\implies+\\And+\\alef")).contains(QStringLiteral("\\Longleftrightarrow")),
            QStringLiteral("macro expander should include high frequency KaTeX macros"));
  }
  {
    math::MathSettings settings;
    settings.maxExpand = 8;
    bool threw = false;
    try {
      math::MathMacroExpander expander(settings);
      expander.expand(QStringLiteral("\\def\\loop{\\loop}\\loop"));
    } catch (const math::MathParseError& error) {
      threw = true;
      require(error.message().contains(QStringLiteral("Too many expansions")), QStringLiteral("recursive macro should throw maxExpand error"));
      require(error.position() == QStringLiteral("\\def\\loop{\\loop}").size() && error.endPosition() == QStringLiteral("\\def\\loop{\\loop}\\loop").size(),
              QStringLiteral("recursive macro maxExpand error should preserve macro token source range"));
    }
    require(threw, QStringLiteral("macro expander should enforce maxExpand"));
  }

  RenderTheme theme = RenderTheme::github();
  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\n"
      "\\newcommand{\\RR}{R}\\RR+\\newcommand{\\pair}[2]{\\left(#1,#2\\right)}\\pair{a}{b}+\\def\\sq#1{#1^2}\\sq{x}+"
      "{\\def\\local{L}\\local}+\\local+{\\global\\def\\globalMacro{G}\\globalMacro}+\\globalMacro+\\let\\same\\sq\\same{y}+"
      "\\pmod{n}+\\bmod x+\\iff+\\implies+\\impliedby+\\And+\\alef+\\char`A+\\hbox{box}+\\html@mathml{H}{M}+"
      "\\mathbb{R}+\\mathcal{C}+\\mathfrak{g}+\\mathscr{S}+\\boldsymbol{x}+\\rm{roman}+"
      "\\textcolor{red}{\\frac{1}{2}}+\\color{blue} x+y"
      "+\\scriptstyle \\frac{a}{b}+\\normalsize \\phantom{abc}+\\hphantom{wide}+\\vphantom{\\frac{1}{2}}"
      "+\\smash[t]{\\frac{1}{2}}+\\rule[0.2em]{1em}{0.1em}+a\\kern1em b+\\mathrel{\\star}"
      "\n$$");
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("metrics/macro/state math parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 800.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("metrics/macro/state math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          QStringLiteral("metrics/macro/state block should have native layout"));
  require(blockLayout->mathLayout()->size.width() > 40.0, QStringLiteral("metrics/macro/state layout should have measurable width"));
}

void testMathFixtureRenderTreeDumps(const QString& fixturePath) {
  const QJsonArray fixtures = readJsonArrayFixture(fixturePath);
  require(fixtures.size() >= 45, QStringLiteral("math fixture suite should contain at least 45 core formulas"));

  const QString metricsPath = katexMetricsPathForFixture(fixturePath);
  const QMap<QString, QJsonObject> katexMetricsById = readObjectArrayById(metricsPath);
  const QString bboxPath = katexBBoxPathForFixture(fixturePath);
  const QMap<QString, QJsonObject> katexBBoxById = readObjectArrayById(bboxPath);
  const QString glyphsPath = katexGlyphsPathForFixture(fixturePath);
  const QMap<QString, QJsonObject> katexGlyphsById = readObjectArrayById(glyphsPath);
  RenderTheme theme = RenderTheme::github();
  const qreal rootEm = math::MathRenderer::katexRootFontPixelSize(theme);
  QVector<BBoxAuditEntry> bboxAudit;
  const bool auditKatexBBox = qEnvironmentVariableIntValue("MUFFIN_AUDIT_KATEX_BBOX") != 0;
  for (const QJsonValue& value : fixtures) {
    require(value.isObject(), QStringLiteral("math fixture entry should be an object"));
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString tex = fixture.value(QStringLiteral("tex")).toString();
    const bool display = fixture.value(QStringLiteral("display")).toBool();
    require(!id.isEmpty() && !tex.isEmpty(), QStringLiteral("math fixture entry should include id and tex"));

    const math::MathLayoutResult rendered = math::MathRenderer().render(tex, theme, display);
    require(rendered.valid(), QStringLiteral("math fixture %1 should render").arg(id));
    require(rendered.root->width > 0.0 && rendered.root->height >= 0.0 && rendered.root->depth >= 0.0,
            QStringLiteral("math fixture %1 should expose positive root metrics").arg(id));
    require(rendered.size.width() >= rendered.root->width && rendered.size.height() >= rendered.root->height + rendered.root->depth,
            QStringLiteral("math fixture %1 layout size should include root box").arg(id));

    const QJsonObject dump = rendered.root->toJson();
    if (qEnvironmentVariable("MUFFIN_DUMP_MATH_FIXTURE") == id.toUtf8()) {
      qInfo().noquote() << QString::fromUtf8(QJsonDocument(dump).toJson(QJsonDocument::Indented));
    }
    require(dump.value(QStringLiteral("kind")).isString(), QStringLiteral("math fixture %1 JSON dump should include kind").arg(id));
    require(dump.value(QStringLiteral("width")).toDouble() > 0.0, QStringLiteral("math fixture %1 JSON dump should include width").arg(id));
    const QJsonDocument reparsed = QJsonDocument::fromJson(rendered.root->toJsonString().toUtf8());
    require(reparsed.isObject(), QStringLiteral("math fixture %1 JSON dump string should parse").arg(id));

    const QJsonArray mustContain = fixture.value(QStringLiteral("mustContain")).toArray();
    for (const QJsonValue& expected : mustContain) {
      const QString expectedValue = expected.toString();
      require(renderTreeContainsValue(dump, expectedValue),
              QStringLiteral("math fixture %1 render tree should contain %2").arg(id, expectedValue));
    }

    const auto goldenIt = katexMetricsById.constFind(id);
    if (qEnvironmentVariable("MUFFIN_DUMP_KATEX_METRIC_DIFF") == id.toUtf8() && goldenIt != katexMetricsById.constEnd()) {
      QJsonObject nativeMetricsRoot = dump;
      QJsonObject katexHtml;
      if (findRenderTreeText(dump, QStringLiteral("katex-html"), katexHtml)) {
        nativeMetricsRoot = katexHtml;
      }
      dumpBuilderMetricDiffs(id, nativeMetricsRoot, goldenIt.value(), rootEm);
    }
    if (fixture.value(QStringLiteral("compareKatexMetrics")).toBool() && goldenIt != katexMetricsById.constEnd()) {
      const qreal heightTolerance = fixture.value(QStringLiteral("heightToleranceEm")).toDouble(0.35);
      const qreal depthTolerance = fixture.value(QStringLiteral("depthToleranceEm")).toDouble(0.35);
      const qreal shiftTolerance = fixture.value(QStringLiteral("shiftToleranceEm")).toDouble(0.75);
      compareKatexBuilderRootMetrics(id, *rendered.root, dump, goldenIt.value(), rootEm, heightTolerance, depthTolerance, shiftTolerance);
    }

    const auto glyphIt = katexGlyphsById.constFind(id);
    if (qEnvironmentVariable("MUFFIN_DUMP_GLYPH_DIFF") == id.toUtf8() && glyphIt != katexGlyphsById.constEnd()) {
      dumpGlyphDiffs(id, *rendered.root, glyphIt.value(), rootEm);
    }
    if (fixture.value(QStringLiteral("compareKatexGlyphs")).toBool() && glyphIt != katexGlyphsById.constEnd()) {
      const qreal xTolerance = fixture.value(QStringLiteral("glyphXToleranceEm")).toDouble(0.01);
      const qreal widthTolerance = fixture.value(QStringLiteral("glyphWidthToleranceEm")).toDouble(0.02);
      compareKatexGlyphs(id, *rendered.root, glyphIt.value(), rootEm, xTolerance, widthTolerance);
    }

    const auto bboxIt = katexBBoxById.constFind(id);
    if (auditKatexBBox && bboxIt != katexBBoxById.constEnd() && !bboxIt.value().value(QStringLiteral("error")).toBool()) {
      bboxAudit.push_back(makeBBoxAuditEntry(id, *rendered.root, bboxIt.value(), rootEm));
    }
    if (fixture.value(QStringLiteral("compareKatexBBox")).toBool() && bboxIt != katexBBoxById.constEnd()) {
      const qreal widthTolerance = fixture.value(QStringLiteral("bboxWidthToleranceEm")).toDouble(0.75);
      const qreal heightTolerance = fixture.value(QStringLiteral("bboxHeightToleranceEm")).toDouble(0.75);
      compareKatexBrowserBBox(id, *rendered.root, bboxIt.value(), rootEm, widthTolerance, heightTolerance);
    }
    if (fixture.value(QStringLiteral("compareKatexInkBBox")).toBool() && bboxIt != katexBBoxById.constEnd()) {
      const qreal widthTolerance = fixture.value(QStringLiteral("inkBBoxWidthToleranceEm")).toDouble(0.25);
      const qreal heightTolerance = fixture.value(QStringLiteral("inkBBoxHeightToleranceEm")).toDouble(0.25);
      compareKatexInkBBox(id, *rendered.root, bboxIt.value(), rootEm, widthTolerance, heightTolerance);
    }
  }

  if (auditKatexBBox) {
    std::sort(bboxAudit.begin(), bboxAudit.end(), [](const BBoxAuditEntry& left, const BBoxAuditEntry& right) {
      return left.layoutError() > right.layoutError();
    });
    qInfo().noquote() << "KaTeX bbox audit, sorted by layout error:";
    for (const BBoxAuditEntry& entry : bboxAudit) {
      qInfo().noquote()
          << QStringLiteral("%1 layoutErr=%2 inkErr=%3 katexDom=(%4,%5) katexInk=(%6,%7) nativeLayout=(%8,%9) nativeInk=(%10,%11)")
                 .arg(entry.id)
                 .arg(entry.layoutError(), 0, 'f', 4)
                 .arg(entry.inkError(), 0, 'f', 4)
                 .arg(entry.katexWidthEm, 0, 'f', 4)
                 .arg(entry.katexHeightEm, 0, 'f', 4)
                 .arg(entry.katexInkWidthEm, 0, 'f', 4)
                 .arg(entry.katexInkHeightEm, 0, 'f', 4)
                 .arg(entry.nativeLayoutWidthEm, 0, 'f', 4)
                 .arg(entry.nativeLayoutHeightEm, 0, 'f', 4)
                 .arg(entry.nativeInkWidthEm, 0, 'f', 4)
                 .arg(entry.nativeInkHeightEm, 0, 'f', 4);
    }
  }
}

void testOfficialKatexScreenshotterAudit(const QString& fixturePath) {
  const QJsonArray fixtures = readJsonArrayFixture(fixturePath);
  require(fixtures.size() >= 100, QStringLiteral("official KaTeX screenshotter fixture suite should contain broad coverage"));

  const QMap<QString, QJsonObject> katexBBoxById = readObjectArrayById(katexBBoxPathForFixture(fixturePath, QStringLiteral("katex-official")));
  const QMap<QString, QJsonObject> katexGlyphsById = readObjectArrayById(katexGlyphsPathForFixture(fixturePath, QStringLiteral("katex-official")));
  RenderTheme theme = RenderTheme::github();
  math::MathSettings officialSettings;
  officialSettings.trust = false;
  const qreal rootEm = math::MathRenderer::katexRootFontPixelSize(theme);
  QVector<BBoxAuditEntry> bboxAudit;
  QStringList renderFailures;
  QStringList glyphCountMismatches;
  int renderedCount = 0;
  int bboxComparedCount = 0;
  int glyphComparedCount = 0;

  for (const QJsonValue& value : fixtures) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString tex = fixture.value(QStringLiteral("tex")).toString();
    const bool display = fixture.value(QStringLiteral("display")).toBool();
    if (id.isEmpty() || tex.isEmpty()) {
      continue;
    }

    math::MathSettings fixtureSettings = officialSettings;
    const QJsonObject macrosObj = fixture.value(QStringLiteral("macros")).toObject();
    for (const QString& key : macrosObj.keys()) {
      fixtureSettings.macros.insert(key, macrosObj.value(key).toString());
    }

    const math::MathLayoutResult rendered = math::MathRenderer().render(tex, theme, display, fixtureSettings);
    if (!rendered.valid()) {
      renderFailures.push_back(id);
      continue;
    }
    ++renderedCount;

    const QString dumpOfficial = qEnvironmentVariable("MUFFIN_DUMP_OFFICIAL_KATEX_FIXTURE");
    if (dumpOfficial == id) {
      qInfo().noquote() << QStringLiteral("Official native dump for %1:").arg(id);
      qInfo().noquote() << QString::fromUtf8(QJsonDocument(rendered.root->toJson()).toJson(QJsonDocument::Indented));
    }

    const auto bboxIt = katexBBoxById.constFind(id);
    if (bboxIt != katexBBoxById.constEnd() && !bboxIt.value().value(QStringLiteral("error")).toBool()) {
      bboxAudit.push_back(makeBBoxAuditEntry(id, *rendered.root, bboxIt.value(), rootEm));
      ++bboxComparedCount;
    }

    const auto glyphIt = katexGlyphsById.constFind(id);
    if (glyphIt != katexGlyphsById.constEnd() && !glyphIt.value().value(QStringLiteral("error")).toBool()) {
      if (dumpOfficial == id) {
        dumpGlyphDiffs(id, *rendered.root, glyphIt.value(), rootEm);
      }
      const int nativeGlyphs = nativeVisibleGlyphItems(*rendered.root, rootEm).size();
      const int katexGlyphs = katexVisibleGlyphItems(glyphIt.value()).size();
      if (nativeGlyphs != katexGlyphs) {
        glyphCountMismatches.push_back(QStringLiteral("%1 native=%2 katex=%3").arg(id).arg(nativeGlyphs).arg(katexGlyphs));
      }
      ++glyphComparedCount;
    }
  }

  std::sort(bboxAudit.begin(), bboxAudit.end(), [](const BBoxAuditEntry& left, const BBoxAuditEntry& right) {
    return left.layoutError() > right.layoutError();
  });

  qInfo().noquote() << QStringLiteral("Official KaTeX screenshotter audit: fixtures=%1 rendered=%2 renderFailures=%3 bboxCompared=%4 glyphCompared=%5 glyphCountMismatches=%6")
                           .arg(fixtures.size())
                           .arg(renderedCount)
                           .arg(renderFailures.size())
                           .arg(bboxComparedCount)
                           .arg(glyphComparedCount)
                           .arg(glyphCountMismatches.size());
  if (!renderFailures.isEmpty()) {
    qInfo().noquote() << QStringLiteral("Official render failures first 25: %1").arg(renderFailures.mid(0, 25).join(QStringLiteral(", ")));
  }
  if (!glyphCountMismatches.isEmpty()) {
    qInfo().noquote() << QStringLiteral("Official glyph count mismatches (%1): %2").arg(glyphCountMismatches.size()).arg(glyphCountMismatches.join(QStringLiteral("; ")));
  }
  qInfo().noquote() << "Official KaTeX bbox audit top 25 by layout error:";
  const int limit = qMin(25, bboxAudit.size());
  for (int i = 0; i < limit; ++i) {
    const BBoxAuditEntry& entry = bboxAudit.at(i);
    qInfo().noquote()
        << QStringLiteral("%1 layoutErr=%2 inkErr=%3 katexDom=(%4,%5) nativeLayout=(%6,%7) katexInk=(%8,%9) nativeInk=(%10,%11)")
               .arg(entry.id)
               .arg(entry.layoutError(), 0, 'f', 4)
               .arg(entry.inkError(), 0, 'f', 4)
               .arg(entry.katexWidthEm, 0, 'f', 4)
               .arg(entry.katexHeightEm, 0, 'f', 4)
               .arg(entry.nativeLayoutWidthEm, 0, 'f', 4)
               .arg(entry.nativeLayoutHeightEm, 0, 'f', 4)
               .arg(entry.katexInkWidthEm, 0, 'f', 4)
               .arg(entry.katexInkHeightEm, 0, 'f', 4)
               .arg(entry.nativeInkWidthEm, 0, 'f', 4)
               .arg(entry.nativeInkHeightEm, 0, 'f', 4);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  require(argc >= 2, QStringLiteral("Fixture path argument is required"));

  const QFileInfo smokeFixtureInfo(QString::fromLocal8Bit(argv[1]));
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testMathMetricsMacrosAndState);
  runTest("testMathFixtureRenderTreeDumps", [&] {
    testMathFixtureRenderTreeDumps(smokeFixtureInfo.dir().filePath(QStringLiteral("math/core.json")));
  });
  runTest("testOfficialKatexScreenshotterAudit", [&] {
    testOfficialKatexScreenshotterAudit(smokeFixtureInfo.dir().filePath(QStringLiteral("math/katex-official-screenshotter.json")));
  });
#undef RUN_TEST
  return 0;
}
