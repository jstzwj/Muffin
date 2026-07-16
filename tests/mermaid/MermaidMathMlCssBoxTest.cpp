#include "math/MathCssBox.h"
#include "math/MathRenderer.h"
#include "math/OpenTypeMathFont.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSizeF>

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
          QStringLiteral("%1: native=%2 browser=%3 tolerance=%4")
              .arg(context).arg(actual).arg(expected).arg(tolerance));
}

void collectTags(const QJsonObject& node, QSet<QString>* tags) {
  tags->insert(node.value(QStringLiteral("tag")).toString());
  for (const QJsonValue& child : node.value(QStringLiteral("children")).toArray())
    collectTags(child.toObject(), tags);
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  try {
    require(argc >= 2, QStringLiteral("MathML CSS box fixture path is required"));
    QFile file(QString::fromLocal8Bit(argv[1]));
    require(file.open(QIODevice::ReadOnly), QStringLiteral("Unable to open MathML CSS box fixture"));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
            QStringLiteral("MathML CSS box Mermaid version drifted"));
    require(root.value(QStringLiteral("fontMode")).toString() ==
                QLatin1String("bundled-noto-stix-two-math-2.13b171"),
            QStringLiteral("MathML CSS box must use the fixed STIX oracle"));
    require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                QLatin1String("191f74a7fba9c51d7c68cde3404ab63725f43a35e571fa69f78af42ccf8d5a8e"),
            QStringLiteral("MathML CSS box fixture changed; regenerate and audit"));

    const math::OpenTypeMathFont& mathFont = math::OpenTypeMathFont::instance();
    require(mathFont.valid(), QStringLiteral("Bundled STIX Two Math failed to load"));
    require(mathFont.familyName() == QLatin1String("STIX Two Math"),
            QStringLiteral("Unexpected strict Math font family"));
    near(mathFont.unitsPerEm(), 1000.0, 0.0, QStringLiteral("STIX units per em"));
    near(mathFont.constants().scriptPercentScaleDown, 0.70, 0.0001,
         QStringLiteral("MATH script scale"));
    near(mathFont.constants().axisHeight, 4.128, 0.001,
         QStringLiteral("MATH axis height"));
    near(mathFont.constants().fractionRuleThickness, 1.088, 0.001,
         QStringLiteral("MATH fraction rule"));
    near(mathFont.constants().radicalDegreeBottomRaisePercent, 0.55, 0.0001,
         QStringLiteral("MATH radical degree raise"));
    const auto italicX = mathFont.mathItalicGlyph(QLatin1Char('x'));
    require(italicX.has_value(), QStringLiteral("STIX mathematical italic x is missing"));
    near(italicX->advance, 8.944, 0.02, QStringLiteral("STIX italic x advance"));
    near(italicX->topAccentAttachment, 5.52, 0.001,
         QStringLiteral("MATH top accent attachment"));
    const auto radicalVariant = mathFont.verticalVariant(
        QString(QChar(0x221A)), 30.0);
    require(radicalVariant.has_value(), QStringLiteral("STIX radical variants are missing"));
    near(radicalVariant->advance, 18.832, 0.02,
         QStringLiteral("MATH radical variant advance"));
    near(radicalVariant->extent, 37.936, 0.001,
         QStringLiteral("MATH radical variant extent"));
    const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
    require(cases.size() == 62, QStringLiteral("MathML CSS box case count regressed"));
    math::MathRenderer renderer;
    QSet<QString> tags;
    QHash<QString, QSizeF> invariantBoxes;
    for (const QJsonValue& value : cases) {
      const QJsonObject fixture = value.toObject();
      const QString id = fixture.value(QStringLiteral("id")).toString();
      const QString tex = fixture.value(QStringLiteral("tex")).toString();
      constexpr qreal kKatexRootFontSize = 16.0 * 1.21;
      const auto layout = renderer.render(tex, kKatexRootFontSize, Qt::black, true);
      require(layout.valid(), id + QStringLiteral(" should produce a native render tree"));
      const math::MathCssBox box = math::layoutMathMlCssBox(layout, kKatexRootFontSize);
      const QJsonObject expected = fixture.value(QStringLiteral("math")).toObject();
      near(box.width, expected.value(QStringLiteral("width")).toDouble(), 0.22,
           id + QStringLiteral(" root width"));
      near(box.height, expected.value(QStringLiteral("height")).toDouble(), 0.22,
           id + QStringLiteral(" root height"));
      const qreal expectedBaseline = fixture.value(QStringLiteral("textBaseline")).toDouble() -
                                     expected.value(QStringLiteral("y")).toDouble();
      near(box.baseline, expectedBaseline, 0.22, id + QStringLiteral(" flex baseline"));
      collectTags(fixture.value(QStringLiteral("tree")).toObject(), &tags);

      if (fixture.value(QStringLiteral("fontSize")).toInt() == 16 &&
          fixture.value(QStringLiteral("dpr")).toDouble() == 1.0) {
        invariantBoxes.insert(id, QSizeF(box.width, box.height));
      } else {
        QString canonical = id;
        canonical.remove(QRegularExpression(QStringLiteral("-(18px|20px|125x|15x|2x)$")));
        require(invariantBoxes.contains(canonical), id + QStringLiteral(" has no 16px/1x oracle"));
        near(box.width, invariantBoxes.value(canonical).width(), 0.001,
             id + QStringLiteral(" CSS width invariance"));
        near(box.height, invariantBoxes.value(canonical).height(), 0.001,
             id + QStringLiteral(" CSS height invariance"));
      }
    }
    for (const QString& required : {QStringLiteral("math"), QStringLiteral("mrow"),
                                    QStringLiteral("mfrac"), QStringLiteral("msup"),
                                    QStringLiteral("msub"), QStringLiteral("msubsup"),
                                    QStringLiteral("msqrt"), QStringLiteral("mroot"),
                                    QStringLiteral("mo"), QStringLiteral("mover"),
                                    QStringLiteral("munderover"),
                                    QStringLiteral("mtable"), QStringLiteral("mtr"),
                                    QStringLiteral("mtd")})
      require(tags.contains(required), QStringLiteral("MathML oracle misses <%1>").arg(required));
    std::cout << "MermaidMathMlCssBoxTest: " << cases.size()
              << " recursive browser boxes passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
