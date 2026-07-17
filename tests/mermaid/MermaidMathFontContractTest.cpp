#include "math/OpenTypeMathFont.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace muffin;

namespace {

void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

void near(qreal actual, qreal expected, qreal tolerance, const QString& context) {
  require(std::abs(actual - expected) <= tolerance,
          QStringLiteral("%1: actual=%2 expected=%3 tolerance=%4")
              .arg(context).arg(actual).arg(expected).arg(tolerance));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  try {
    const math::OpenTypeMathFont& font = math::OpenTypeMathFont::instance();
    require(font.valid(), QStringLiteral("Bundled STIX Two Math failed to load"));

    QFile fontFile(QStringLiteral(":/katex/fonts/STIXTwoMath-Regular.otf"));
    require(fontFile.open(QIODevice::ReadOnly),
            QStringLiteral("Bundled STIX Two Math resource is missing"));
    const QByteArray bytes = fontFile.readAll();
    require(bytes.size() == 838652, QStringLiteral("Bundled STIX font size drifted"));
    require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
                QByteArrayLiteral("3a5f3f26f40d5698b3c62dd085d48d6663696a3f80825aab8b553d5097518e8c"),
            QStringLiteral("Bundled STIX font identity drifted"));

    require(font.familyName() == QLatin1String("STIX Two Math"),
            QStringLiteral("Strict oracle resolved an unexpected font family"));
    near(font.unitsPerEm(), 1000.0, 0.0, QStringLiteral("units per em"));
    near(font.constants().scriptPercentScaleDown, 0.70, 0.0001,
         QStringLiteral("script scale"));
    near(font.constants().scriptScriptPercentScaleDown, 0.55, 0.0001,
         QStringLiteral("scriptscript scale"));
    near(font.constants().axisHeight, 4.128, 0.001, QStringLiteral("axis height"));
    near(font.constants().fractionRuleThickness, 1.088, 0.001,
         QStringLiteral("fraction rule"));
    near(font.constants().superscriptShiftUpCramped, 4.032, 0.001,
         QStringLiteral("cramped superscript shift"));
    near(font.constants().superscriptBottomMaxWithSubscript, 6.08, 0.001,
         QStringLiteral("superscript bottom with subscript"));

    const auto italicX = font.mathItalicGlyph(QLatin1Char('x'));
    require(italicX.has_value(), QStringLiteral("Mathematical italic x is missing"));
    near(italicX->advance, 8.944, 0.02, QStringLiteral("italic x advance"));
    near(italicX->italicCorrection, 0.16, 0.001,
         QStringLiteral("italic x correction"));
    near(italicX->topAccentAttachment, 5.52, 0.001,
         QStringLiteral("italic x accent attachment"));

    const auto radical = font.verticalVariant(QString(QChar(0x221A)), 30.0);
    require(radical.has_value(), QStringLiteral("Radical variants are missing"));
    near(radical->advance, 18.832, 0.02, QStringLiteral("radical advance"));
    near(radical->extent, 37.936, 0.001, QStringLiteral("radical extent"));
    const auto assembledBrace = font.verticalAssembly(QStringLiteral("{"), 42.0);
    require(assembledBrace.has_value(), QStringLiteral("Brace assembly is missing"));
    near(assembledBrace->advance, 9.6, 0.02, QStringLiteral("brace assembly width"));
    near(assembledBrace->extent, 71.984, 0.001,
         QStringLiteral("brace minimum assembly extent"));
    const auto stretchedBrace = font.verticalAssembly(QStringLiteral("{"), 100.0);
    require(stretchedBrace.has_value(), QStringLiteral("Stretchable brace assembly is missing"));
    near(stretchedBrace->extent, 100.0, 0.001,
         QStringLiteral("brace extender assembly extent"));
    const auto braceParts = font.verticalAssemblyParts(QStringLiteral("{"), 100.0);
    require(braceParts.has_value(), QStringLiteral("Brace paint assembly is missing"));
    require(braceParts->parts.size() == 7,
            QStringLiteral("Brace assembly operation sequence drifted"));
    require(braceParts->parts.front().offset == 0.0 &&
                !braceParts->parts.front().extender &&
                !braceParts->parts.back().extender,
            QStringLiteral("Brace terminal assembly parts drifted"));
    int extenderCount = 0;
    qreal previousOffset = -1.0;
    for (qsizetype i = 0; i < braceParts->parts.size(); ++i) {
      const auto& part = braceParts->parts.at(i);
      if (part.extender) ++extenderCount;
      require(part.offset > previousOffset && part.fullAdvance > 0.0,
              QStringLiteral("Brace assembly offsets are not monotonic"));
      require(!font.glyphPath(part.glyphIndex).isEmpty(),
              QStringLiteral("Brace assembly glyph outline is missing"));
      previousOffset = part.offset;
      if (i + 1 < braceParts->parts.size()) {
        near(braceParts->parts.at(i + 1).offset,
             part.offset + part.fullAdvance - part.connectorOverlap,
             0.0001, QStringLiteral("Brace connector continuity"));
      }
    }
    require(extenderCount == 4,
            QStringLiteral("Brace extender consumption order drifted"));
    near(braceParts->extent, 100.0, 0.001,
         QStringLiteral("Brace paint operation extent"));
    near(braceParts->parts.back().offset + braceParts->parts.back().fullAdvance,
         braceParts->extent, 0.0001,
         QStringLiteral("Brace operation terminal extent"));
    const auto underBraceParts = font.horizontalAssemblyParts(
        QString(QChar(0x23DF)), 80.0);
    require(underBraceParts.has_value(),
            QStringLiteral("Horizontal underbrace assembly is missing"));
    const auto shortUnderBrace = font.horizontalVariant(
        QString(QChar(0x23DF)), 35.75);
    require(shortUnderBrace.has_value(),
            QStringLiteral("Horizontal underbrace variants are missing"));
    near(shortUnderBrace->extent, 41.616, 0.001,
         QStringLiteral("Horizontal underbrace fixed variant extent"));
    near(shortUnderBrace->advance, 3.952, 0.001,
         QStringLiteral("Horizontal underbrace fixed variant height"));
    const auto cssUnderBrace = font.horizontalAssemblyParts(
        QString(QChar(0x23DF)), 35.75 / font.constants().scriptPercentScaleDown);
    require(cssUnderBrace.has_value(),
            QStringLiteral("Script-style underbrace assembly is missing"));
    near(cssUnderBrace->extent * font.constants().scriptPercentScaleDown,
         35.75, 0.001,
         QStringLiteral("Script-style underbrace CSS extent"));
    require(underBraceParts->parts.size() == 7,
            QStringLiteral("Horizontal underbrace part sequence drifted"));
    require(std::count_if(underBraceParts->parts.cbegin(),
                          underBraceParts->parts.cend(),
                          [](const auto& part) { return part.extender; }) == 4,
            QStringLiteral("Horizontal underbrace extender sequence drifted"));
    near(underBraceParts->extent, 80.0, 0.001,
         QStringLiteral("Horizontal underbrace assembly extent"));
    const auto underArrowParts = font.horizontalAssemblyParts(
        QString(QChar(0x2194)), 80.0);
    require(underArrowParts.has_value(),
            QStringLiteral("Horizontal arrow assembly is missing"));
    require(underArrowParts->parts.size() == 11,
            QStringLiteral("Horizontal arrow part sequence drifted"));
    require(std::count_if(underArrowParts->parts.cbegin(),
                          underArrowParts->parts.cend(),
                          [](const auto& part) { return part.extender; }) == 9,
            QStringLiteral("Horizontal arrow extender sequence drifted"));
    near(underArrowParts->extent, 80.0, 0.001,
         QStringLiteral("Horizontal arrow assembly extent"));
    const auto assembledParen = font.verticalAssembly(QStringLiteral("("), 50.0);
    require(assembledParen.has_value(), QStringLiteral("Parenthesis assembly is missing"));
    near(assembledParen->extent, 52.768, 0.001,
         QStringLiteral("parenthesis minimum assembly extent"));
    near(font.constants().stackTopDisplayStyleShiftUp, 12.48, 0.001,
         QStringLiteral("display stack top shift"));
    near(font.constants().stackBottomDisplayStyleShiftDown, 11.04, 0.001,
         QStringLiteral("display stack bottom shift"));
    near(font.constants().stackDisplayStyleGapMin, 4.8, 0.001,
         QStringLiteral("display stack gap"));
    near(font.constants().overbarVerticalGap, 2.8, 0.001,
         QStringLiteral("overbar vertical gap"));
    near(font.constants().overbarRuleThickness, 1.088, 0.001,
         QStringLiteral("overbar rule"));
    near(font.constants().underbarVerticalGap, 2.8, 0.001,
         QStringLiteral("underbar vertical gap"));
    near(font.constants().underbarExtraDescender, 1.088, 0.001,
         QStringLiteral("underbar extra descender"));

    std::cout << "MermaidMathFontContractTest: bundled STIX/OpenType MATH contract passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
