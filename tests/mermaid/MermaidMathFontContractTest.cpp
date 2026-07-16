#include "math/OpenTypeMathFont.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>

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
    const auto assembledBrace = font.verticalVariant(QStringLiteral("{"), 100.0, true);
    require(assembledBrace.has_value(), QStringLiteral("Brace assembly is missing"));
    near(assembledBrace->advance, 9.6, 0.02, QStringLiteral("brace assembly width"));

    std::cout << "MermaidMathFontContractTest: bundled STIX/OpenType MATH contract passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
