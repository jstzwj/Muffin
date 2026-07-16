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

void collectNodes(const QJsonObject& node, QStringView tag,
                  QVector<QJsonObject>* nodes) {
  if (node.value(QStringLiteral("tag")).toString() == tag) nodes->push_back(node);
  for (const QJsonValue& child : node.value(QStringLiteral("children")).toArray())
    collectNodes(child.toObject(), tag, nodes);
}

const math::MathRenderNode* findArray(const math::MathRenderNode* node) {
  if (!node) return nullptr;
  if (node->semanticKind == math::MathSemanticKind::Array) return node;
  for (const auto& child : node->children)
    if (const auto* array = findArray(child.get())) return array;
  return nullptr;
}

const math::MathRenderNode* findSemantic(const math::MathRenderNode* node,
                                         math::MathSemanticKind kind) {
  if (!node) return nullptr;
  if (node->semanticKind == kind) return node;
  for (const auto& child : node->children)
    if (const auto* semantic = findSemantic(child.get(), kind)) return semantic;
  return nullptr;
}

const math::MathRenderNode* findOperator(const math::MathRenderNode* node,
                                         math::MathOperatorKind kind) {
  if (!node) return nullptr;
  if (node->operatorKind == kind) return node;
  for (const auto& child : node->children)
    if (const auto* op = findOperator(child.get(), kind)) return op;
  return nullptr;
}

const math::MathRenderNode* findAccent(const math::MathRenderNode* node,
                                       math::MathAccentKind kind) {
  if (!node) return nullptr;
  if (node->accentKind == kind) return node;
  for (const auto& child : node->children)
    if (const auto* accent = findAccent(child.get(), kind)) return accent;
  return nullptr;
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
                QLatin1String("a5264ed84a6484b7dbfabca5c4ff0e46ae52b7be83b0d8c4e7373fe7820cdb1e"),
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
    require(cases.size() == 84, QStringLiteral("MathML CSS box case count regressed"));
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
      if (id == QLatin1String("cases-piecewise") ||
          id == QLatin1String("aligned-equations")) {
        const math::MathRenderNode* array = findArray(layout.root.get());
        require(array, id + QStringLiteral(" must preserve its Array semantic node"));
        require(array->arrayEnvironment == (id == QLatin1String("cases-piecewise")
                    ? QLatin1String("cases") : QLatin1String("aligned")),
                id + QStringLiteral(" array environment was lost"));
        require(array->arrayColumnSeparation == (id == QLatin1String("cases-piecewise")
                    ? QLatin1String("") : QLatin1String("align")),
                id + QStringLiteral(" column separation was lost"));
        if (id == QLatin1String("cases-piecewise")) {
          near(array->arrayStretch, 1.2, 0.0001, id + QStringLiteral(" array stretch"));
          require(array->arrayLeftDelimiter == QLatin1String("\\lbrace") &&
                      array->arrayRightDelimiter == QLatin1String("."),
                  id + QStringLiteral(" delimiters were lost"));
        }
      }
      if (id == QLatin1String("product-limits") || id == QLatin1String("limit-below")) {
        const auto* op = findOperator(layout.root.get(), math::MathOperatorKind::Limits);
        require(op, id + QStringLiteral(" must preserve its limits operator container"));
        require(!op->operatorText.isEmpty(), id + QStringLiteral(" operator identity was lost"));
      }
      if (id == QLatin1String("operator-name")) {
        const auto* op = findOperator(layout.root.get(), math::MathOperatorKind::Named);
        require(op && op->operatorText == QLatin1String("\\operatorname"),
                id + QStringLiteral(" named operator identity was lost"));
      }
      if (id == QLatin1String("accent-overline")) {
        const auto* accent = findAccent(layout.root.get(), math::MathAccentKind::Overline);
        require(accent && accent->accentLabel == QLatin1String("\\overline"),
                id + QStringLiteral(" overline semantic was lost"));
      }
      if (id == QLatin1String("accent-widehat")) {
        const auto* accent = findAccent(layout.root.get(), math::MathAccentKind::Over);
        require(accent && accent->accentLabel == QLatin1String("\\widehat"),
                id + QStringLiteral(" stretchy accent semantic was lost"));
      }
      if (id == QLatin1String("binomial")) {
        const auto* fraction = findSemantic(
            layout.root.get(), math::MathSemanticKind::Fraction);
        require(fraction && !fraction->fractionHasBarLine,
                id + QStringLiteral(" must preserve its zero-thickness fraction"));
      }
      if (id.startsWith(QLatin1String("genfrac-")) ||
          id.endsWith(QLatin1String("-fraction")) ||
          id.endsWith(QLatin1String("-binomial"))) {
        const auto* fraction = findSemantic(
            layout.root.get(), math::MathSemanticKind::Fraction);
        require(fraction, id + QStringLiteral(" must preserve fraction semantics"));
        if (id == QLatin1String("genfrac-display-rule")) {
          near(fraction->fractionLineThicknessEm, 0.1, 0.0001,
               id + QStringLiteral(" line thickness"));
          require(fraction->fractionStyleSize == 0,
                  id + QStringLiteral(" display style was lost"));
        }
        if (id == QLatin1String("genfrac-text-stack")) {
          require(!fraction->fractionHasBarLine && fraction->fractionStyleSize == 1,
                  id + QStringLiteral(" text stack semantics were lost"));
        }
      }
      if (id == QLatin1String("accent-under-arrow")) {
        const auto* accent = findAccent(layout.root.get(), math::MathAccentKind::Under);
        require(accent && accent->accentCharacter == QString(QChar(0x2194)),
                id + QStringLiteral(" MathML operator character was lost"));
      }
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

      const QJsonObject tree = fixture.value(QStringLiteral("tree")).toObject();
      if (id == QLatin1String("genfrac-display-rule")) {
        QVector<QJsonObject> styles;
        QVector<QJsonObject> fractions;
        collectNodes(tree, u"mstyle", &styles);
        collectNodes(tree, u"mfrac", &fractions);
        require(styles.size() == 1 && fractions.size() == 1,
                id + QStringLiteral(" DOM nesting drifted"));
        require(styles.front().value(QStringLiteral("attributes")).toObject()
                    .value(QStringLiteral("displaystyle")).toString() == QLatin1String("true"),
                id + QStringLiteral(" displaystyle attribute drifted"));
        near(fractions.front().value(QStringLiteral("height")).toDouble(), 34.297, 0.001,
             id + QStringLiteral(" mfrac height"));
      }
      if (id == QLatin1String("accent-underbrace")) {
        QVector<QJsonObject> unders;
        collectNodes(tree, u"munder", &unders);
        require(unders.size() == 2, id + QStringLiteral(" nested munder drifted"));
        near(unders.front().value(QStringLiteral("height")).toDouble(), 32.75, 0.001,
             id + QStringLiteral(" annotated munder height"));
        near(unders.back().value(QStringLiteral("height")).toDouble(), 22.875, 0.001,
             id + QStringLiteral(" brace munder height"));
      }
      if (id == QLatin1String("tall-delimiter-assembly")) {
        QVector<QJsonObject> tables;
        QVector<QJsonObject> operators;
        collectNodes(tree, u"mtable", &tables);
        collectNodes(tree, u"mo", &operators);
        require(tables.size() == 1 && !operators.isEmpty(),
                id + QStringLiteral(" assembly DOM drifted"));
        near(tables.front().value(QStringLiteral("height")).toDouble(), 105.375, 0.001,
             id + QStringLiteral(" mtable height"));
        near(operators.front().value(QStringLiteral("height")).toDouble(), 105.969, 0.001,
             id + QStringLiteral(" assembled fence height"));
      }
      if (id == QLatin1String("nested-mathml-structure")) {
        QVector<QJsonObject> fractions;
        QVector<QJsonObject> unders;
        collectNodes(tree, u"mfrac", &fractions);
        collectNodes(tree, u"munder", &unders);
        require(fractions.size() == 2 && unders.size() == 2,
                id + QStringLiteral(" recursive DOM structure drifted"));
        near(fractions.front().value(QStringLiteral("height")).toDouble(), 61.594, 0.001,
             id + QStringLiteral(" outer mfrac height"));
        near(fractions.back().value(QStringLiteral("height")).toDouble(), 19.672, 0.001,
             id + QStringLiteral(" inner mfrac height"));
      }

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
