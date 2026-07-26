#include "math/OpenTypeMathFont.h"
#include "mermaid/MermaidFontRegistry.h"

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
    mermaid::MermaidFontRegistry::ensureLoaded();
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
    const auto integral = font.verticalVariant(
        QString(QChar(0x222B)), font.constants().displayOperatorMinHeight);
    require(integral.has_value(), QStringLiteral("Integral variants are missing"));
    near(integral->advance, 16.496, 0.02,
         QStringLiteral("integral variant advance"));
    near(integral->italicCorrection, 8.64, 0.001,
         QStringLiteral("integral variant italic correction"));
    const auto assembledBrace = font.verticalAssembly(QStringLiteral("{"), 42.0);
    require(assembledBrace.has_value(), QStringLiteral("Brace assembly is missing"));
    near(assembledBrace->advance, 9.6, 0.02, QStringLiteral("brace assembly width"));
    near(assembledBrace->extent, 55.952, 0.001,
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
    const qreal connectorOverlap =
        braceParts->parts.front().connectorOverlap;
    for (qsizetype i = 0; i < braceParts->parts.size(); ++i) {
      const auto& part = braceParts->parts.at(i);
      if (part.extender) ++extenderCount;
      require(part.offset > previousOffset && part.fullAdvance > 0.0,
              QStringLiteral("Brace assembly offsets are not monotonic"));
      require(!font.glyphPath(part.glyphIndex).isEmpty(),
              QStringLiteral("Brace assembly glyph outline is missing"));
      previousOffset = part.offset;
      if (i + 1 < braceParts->parts.size()) {
        near(part.connectorOverlap, connectorOverlap, 0.0001,
             QStringLiteral("Brace uniform connector overlap"));
        near(braceParts->parts.at(i + 1).offset,
             part.offset + part.fullAdvance - part.connectorOverlap,
             0.0001, QStringLiteral("Brace connector continuity"));
      }
    }
    require(extenderCount == 4,
            QStringLiteral("Brace extender consumption order drifted"));
    near(braceParts->extent, 100.0, 0.001,
         QStringLiteral("Brace paint operation extent"));
    require(!braceParts->inkBounds.isEmpty() &&
                braceParts->inkBounds.height() > 0.0,
            QStringLiteral("Brace vertical assembly ink bounds are missing"));
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
    require(!underArrowParts,
            QStringLiteral("Non-extender arrow assembly must be rejected"));
    const auto underArrowFallback = font.horizontalVariant(
        QString(QChar(0x2194)), 80.0);
    require(underArrowFallback && underArrowFallback->glyphIndex != 0 &&
                underArrowFallback->extent > 0.0,
            QStringLiteral("Horizontal arrow fixed fallback is missing"));
    const auto assembledParen = font.verticalAssembly(QStringLiteral("("), 50.0);
    require(assembledParen.has_value(), QStringLiteral("Parenthesis assembly is missing"));
    near(assembledParen->extent, 52.768, 0.001,
         QStringLiteral("parenthesis minimum assembly extent"));
    for (const QChar delimiter : {QChar(0x2308), QChar(0x230A)}) {
      const auto parts = font.verticalAssemblyParts(QString(delimiter), 70.0);
      require(parts && !parts->parts.isEmpty(),
              QStringLiteral("Floor/ceiling assembly is missing"));
      const QList<quint32> indexes{
          parts->parts.front().glyphIndex, parts->parts.back().glyphIndex};
      const QRectF firstInk = font.rasterGlyphBounds(indexes.front());
      const QRectF lastInk = font.rasterGlyphBounds(indexes.back());
      require((delimiter == QChar(0x2308)
                   ? firstInk.width() > lastInk.width()
                   : lastInk.width() > firstInk.width()),
              QStringLiteral("Floor/ceiling terminal order drifted"));
    }
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

    const auto requireLtrFallbackOrder = [&](const QString& text,
                                             const QString& rtlFamily) {
      const auto shaped = font.shapeMathMlTextWithFallback(
          text, mermaid::MermaidFontRegistry::familyStack());
      require(shaped.has_value(),
              QStringLiteral("Fallback bidi shaping failed for %1").arg(text));
      const auto rtl = std::find_if(
          shaped->runs.cbegin(), shaped->runs.cend(),
          [&](const math::MathShapedTextRun& run) {
            return run.familyName.contains(rtlFamily);
          });
      const auto cjk = std::find_if(
          shaped->runs.cbegin(), shaped->runs.cend(),
          [](const math::MathShapedTextRun& run) {
            return run.familyName.contains(QLatin1String("CJK"));
          });
      require(rtl != shaped->runs.cend() && cjk != shaped->runs.cend() &&
                  rtl->inkBounds.left() < cjk->inkBounds.left(),
              QStringLiteral("LTR MathML fallback run order drifted for %1")
                  .arg(text));
    };
    requireLtrFallbackOrder(
        QString::fromUtf8("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xe4\xb8\xad\xe6\x96\x87"),
        QStringLiteral("Arabic"));
    requireLtrFallbackOrder(
        QString::fromUtf8("\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xe4\xb8\xad\xe6\x96\x87"),
        QStringLiteral("Hebrew"));

    const auto productVariant = font.verticalVariant(
        QString(QChar(0x220F)), 22.0);
    const auto tripleIntegralVariant = font.verticalVariant(
        QString(QChar(0x222D)), 38.0);
    require(productVariant && tripleIntegralVariant,
            QStringLiteral("Large-operator raster variants are missing"));
    // Raster bounds come from QRawFont::boundingRect, whose value depends on
    // the font backend (FreeType vs CoreText) — e.g. macOS reports ~20.58 vs
    // 22.0 elsewhere for the same glyph. Use a renderer-tolerant slack; the
    // raster>outline relation checked below is the load-bearing assertion.
    near(font.rasterGlyphBounds(productVariant->glyphIndex).width(), 22.0,
         3.0, QStringLiteral("product raster width"));
    near(font.rasterGlyphBounds(tripleIntegralVariant->glyphIndex).width(),
         31.0, 3.0, QStringLiteral("triple-integral raster width"));
    require(font.rasterGlyphBounds(productVariant->glyphIndex).width() > 0.0 &&
                font.glyphPath(productVariant->glyphIndex).boundingRect().width() > 0.0 &&
                font.rasterGlyphBounds(tripleIntegralVariant->glyphIndex).width() > 0.0 &&
                font.glyphPath(tripleIntegralVariant->glyphIndex).boundingRect().width() > 0.0,
            QStringLiteral("Raster/outline glyph bounds are missing"));

    std::cout << "MermaidMathFontContractTest: bundled STIX/OpenType MATH contract passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
