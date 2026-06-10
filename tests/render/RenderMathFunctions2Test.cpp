#include "document/MarkdownDocument.h"
#include "math/MathBuilder.h"
#include "math/MathFunctionRegistry.h"
#include "math/MathParseError.h"
#include "math/MathParser.h"
#include "math/MathRenderer.h"
#include "math/MathSymbols.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QPainter>

#include <functional>
#include <iostream>

#include "MathTestUtils.h"

using namespace muffin;

namespace {

void testRemainingKatexFunctionsPart2() {
  {
    const QStringList symbolCommands{
        QStringLiteral("\\zeta"), QStringLiteral("\\Xi"), QStringLiteral("\\oplus"), QStringLiteral("\\supseteq"),
        QStringLiteral("\\approx"), QStringLiteral("\\Longleftrightarrow"), QStringLiteral("\\aleph"), QStringLiteral("\\clubsuit"),
        QStringLiteral("\\nless"), QStringLiteral("\\Finv"), QStringLiteral("\\dashrightarrow")};
    for (const QString& command : symbolCommands) {
      const math::MathSymbolInfo symbol = math::lookupSymbol(command);
      require(symbol.known, QStringLiteral("symbol table should include %1").arg(command));
    }
  }
  {
    math::MathParser parser(QStringLiteral("\\html@mathml{H}{M}+\\hbox{box}+\\rm roman"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    bool sawHtmlBranch = false, sawHbox = false, sawRmDeclaration = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::Group && node.label == QStringLiteral("\\html@mathml") && !node.body.isEmpty()) sawHtmlBranch = true;
      if (node.type == math::MathNodeType::Group && node.label == QStringLiteral("\\hbox")) sawHbox = true;
      if (node.type == math::MathNodeType::Text && node.label == QStringLiteral("\\rm") && !node.body.isEmpty()) sawRmDeclaration = true;
    }
    require(sawHtmlBranch && sawHbox && sawRmDeclaration,
            QStringLiteral("htmlmathml hbox and old font declarations should preserve KaTeX-like parse semantics"));
  }
  {
    math::MathParser parser(QStringLiteral("\\begin{matrix}\\text{\\underline{text}}\\end{matrix}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    require(nodes.size() == 1 && nodes.first().type == math::MathNodeType::Array,
            QStringLiteral("text command inside matrix should not leak the environment terminator"));

    math::MathParser colorParser(QStringLiteral("\\begin{matrix}a\\\\\\color{green}{b}\\\\c\\end{matrix}"));
    const QVector<math::MathParseNode> colorNodes = colorParser.parse();
    require(colorNodes.size() == 1 && colorNodes.first().type == math::MathNodeType::Array &&
                colorNodes.first().rows.size() == 3,
            QStringLiteral("color declarations should stop at array row and environment boundaries"));
    std::unique_ptr<math::MathRenderNode> colorTree =
        math::MathBuilder(math::MathOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), math::MathSettings())).buildExpression(colorNodes);
    require(colorTree != nullptr && !renderTreeContainsValue(colorTree->toJson(), QStringLiteral("\\end")),
            QStringLiteral("color declarations should not leak environment delimiters into render tree"));
  }
  {
    const QStringList stretchyAccentLabels = {
        QStringLiteral("\\overrightarrow"), QStringLiteral("\\overleftarrow"), QStringLiteral("\\Overrightarrow"),
        QStringLiteral("\\overleftrightarrow"), QStringLiteral("\\overgroup"), QStringLiteral("\\overlinesegment"),
        QStringLiteral("\\overleftharpoon"), QStringLiteral("\\overrightharpoon")};
    for (const QString& label : stretchyAccentLabels) {
      math::MathParser parser(QStringLiteral("%1{AB}").arg(label));
      const QVector<math::MathParseNode> nodes = parser.parse();
      require(nodes.size() == 1 && nodes.first().type == math::MathNodeType::Accent,
              QStringLiteral("%1 should parse as a KaTeX stretchy accent").arg(label));
    }
  }

  bool threw = false;
  math::MathSettings strictSettings;
  strictSettings.throwOnError = true;
  try {
    math::MathParser parser(QStringLiteral("a+\\definitelyUnsupported"), strictSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.tokenText() == QStringLiteral("\\definitelyUnsupported"), QStringLiteral("unsupported command error should keep token text"));
  }
  require(threw, QStringLiteral("throwOnError should raise MathParseError for unsupported commands"));

  math::MathSettings strictHtmlSettings;
  strictHtmlSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\htmlClass{note}{q}"), strictHtmlSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("htmlExtension")), QStringLiteral("strict html extension should report category"));
  }
  require(threw, QStringLiteral("strict error should throw on nonstrict html extension"));

  math::MathSettings strictUnknownSettings;
  strictUnknownSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\notInRegistryYet"), strictUnknownSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("unknownSymbol")), QStringLiteral("unknown command should report unknownSymbol"));
  }
  require(threw, QStringLiteral("strict unknown command should throw unknownSymbol"));

  math::MathSettings strictUnicodeSettings;
  strictUnicodeSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\xe4\xb8\xad"), strictUnicodeSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("unicodeTextInMathMode")),
            QStringLiteral("strict unicode text in math should report category"));
  }
  require(threw, QStringLiteral("strict unicode text in math mode should throw"));

  math::MathSettings strictAccentSettings;
  strictAccentSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\'{e}"), strictAccentSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("mathVsTextAccents")),
            QStringLiteral("text accent in math should report mathVsTextAccents"));
  }
  require(threw, QStringLiteral("strict text accent in math should throw"));

  math::MathSettings strictUnitSettings;
  strictUnitSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\mkern1em"), strictUnitSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("mathVsTextUnits")), QStringLiteral("mkern em should report mathVsTextUnits"));
  }
  require(threw, QStringLiteral("strict mkern with non-mu units should throw"));

  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\kern1mu"), strictUnitSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("mathVsTextUnits")), QStringLiteral("kern mu should report mathVsTextUnits"));
  }
  require(threw, QStringLiteral("strict kern with mu units should throw"));

  math::MathSettings untrustedSettings;
  untrustedSettings.throwOnError = true;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\href{https://example.com}{h}"), untrustedSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("not trusted")), QStringLiteral("untrusted href should throw trust error"));
  }
  require(threw, QStringLiteral("untrusted href should be rejected when throwOnError is enabled"));

  math::MathSettings trustedSettings;
  trustedSettings.trustHandler = [](const math::MathTrustContext& context) {
    return context.command == QStringLiteral("\\href") && context.protocol == QStringLiteral("https");
  };
  math::MathParser trustedParser(QStringLiteral("\\href{https://example.com}{h}"), trustedSettings);
  const QVector<math::MathParseNode> trustedTree = trustedParser.parse();
  require(!trustedTree.isEmpty() && trustedTree.first().type == math::MathNodeType::Href,
          QStringLiteral("trusted href should parse to href node"));

  bool sawHtmlDataContext = false;
  math::MathSettings htmlDataTrustSettings;
  htmlDataTrustSettings.trustHandler = [&sawHtmlDataContext](const math::MathTrustContext& context) {
    sawHtmlDataContext = context.command == QStringLiteral("\\htmlData") &&
                         context.attributes.value(QStringLiteral("data-x")).trimmed() == QStringLiteral("1") &&
                         context.attribute == QStringLiteral("data-x") &&
                         context.value.trimmed() == QStringLiteral("1");
    return sawHtmlDataContext;
  };
  math::MathParser htmlDataParser(QStringLiteral("\\htmlData{x=1,y=two}{q}"), htmlDataTrustSettings);
  const QVector<math::MathParseNode> htmlDataTree = htmlDataParser.parse();
  require(sawHtmlDataContext && !htmlDataTree.isEmpty() && htmlDataTree.first().type == math::MathNodeType::Html,
          QStringLiteral("htmlData trust context should expose data attributes"));

  math::MathSettings htmlDataErrorSettings;
  htmlDataErrorSettings.throwOnError = true;
  htmlDataErrorSettings.trust = true;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\\htmlData{broken}{q}"), htmlDataErrorSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("missing equals sign")), QStringLiteral("htmlData missing equals should throw"));
  }
  require(threw, QStringLiteral("htmlData missing equals should match KaTeX error behavior"));

  RenderTheme theme = RenderTheme::github();
  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\n"
      "\\mathchoice{D}{T}{S}{SS}+\\mathllap{L}x+\\mathrlap{R}y+\\mathclap{C}z"
      "+\\raisebox{0.35em}{a}+\\vcenter{\\frac{a}{b}}"
      "+x_i^2+x^{a}_{b}+\\sqrt[3]{\\frac{a}{b}}+\\genfrac{[}{]}{0.08em}{0}{a}{b}+\\boxed{z}+\\textbf{B}+\\mathit{x}+\\mathtt{code}"
      "+\\@binrel{=}{x}+\\@binrel{+}{y}+\\overset{a}{=}+\\boldsymbol{=}+\\zeta+\\Xi+\\oplus+\\supseteq+\\approx+\\Longleftrightarrow+\\clubsuit"
      "+\\cancel{x}+\\bcancel{y}+\\xcancel{z}+\\sout{text}+\\fbox{box}+\\colorbox{yellow}{cb}+\\fcolorbox{red}{yellow}{fcb}+\\phase{V}+\\angl{n}"
      "+\\href{https://example.com}{h}+\\url{https://example.com}+\\htmlClass{note}{q}"
      "+\\includegraphics[width=1em,height=0.8em,totalheight=1em,alt=img]{missing-file.png}"
      "+\\verb|a b|+\\verb*|c d|+\\tag{1}"
      "\n$$");
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("remaining KaTeX function families parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 900.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("remaining function math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          QStringLiteral("remaining function block should have native layout"));
  require(blockLayout->mathLayout()->size.width() > 80.0, QStringLiteral("remaining function layout should have measurable width"));

  QImage image(QSize(880, qCeil(blockLayout->height()) + 50), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  blockLayout->paint(painter, theme, blockLayout->rect().top() - 12.0);
  painter.end();
  require(changedPixelCount(image, theme.backgroundColor()) > 160, QStringLiteral("remaining function paint should draw visible pixels"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testRemainingKatexFunctionsPart2);
#undef RUN_TEST
  return 0;
}
