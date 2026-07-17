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
                QLatin1String("47c3e447d384606073d8df70acc3addd0d4187e9b1524ccb1b9bd63cf5b9329f"),
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
    require(cases.size() == 85, QStringLiteral("MathML CSS box case count regressed"));
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
        const auto accentBox = math::layoutMathMlAccentBox(
            layout, kKatexRootFontSize);
        require(accentBox && !accentBox->over &&
                    accentBox->character == QString(QChar(0x2194)),
                id + QStringLiteral(" native accent box is missing"));
        near(accentBox->body.y(), 0.0, 0.02, id + QStringLiteral(" body y"));
        near(accentBox->body.height(), 14.0, 0.02,
             id + QStringLiteral(" body height"));
        near(accentBox->accent.y(), 14.0, 0.02,
             id + QStringLiteral(" arrow y"));
        near(accentBox->accent.height(), 7.0, 0.02,
             id + QStringLiteral(" arrow height"));
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
      if (id == QLatin1String("fraction-nested")) {
        QVector<QJsonObject> fractions;
        collectNodes(tree, u"mfrac", &fractions);
        require(fractions.size() == 2,
                id + QStringLiteral(" browser nesting drifted"));
        const auto operations = math::layoutMathMlFractionOperations(
            layout, kKatexRootFontSize);
        require(operations.has_value() && operations->children.size() == 1 &&
                    operations->children.front().children.isEmpty(),
                id + QStringLiteral(" must produce a two-level operation tree"));
        const auto compareFraction = [&](const math::MathCssFractionOperation& op,
                                         const QJsonObject& browser,
                                         QStringView name) {
          near(op.box.fraction.x(), browser.value(QStringLiteral("x")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" x"));
          near(op.box.fraction.y(), browser.value(QStringLiteral("y")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" y"));
          near(op.box.fraction.width(),
               browser.value(QStringLiteral("width")).toDouble(), 0.22,
               id + QLatin1Char(' ') + name + QStringLiteral(" width"));
          near(op.box.fraction.height(),
               browser.value(QStringLiteral("height")).toDouble(), 0.22,
               id + QLatin1Char(' ') + name + QStringLiteral(" height"));
        };
        compareFraction(*operations, fractions.at(0), u"outer fraction");
        compareFraction(operations->children.front(), fractions.at(1),
                        u"inner fraction");
        const QJsonArray innerChildren = fractions.at(1)
            .value(QStringLiteral("children")).toArray();
        require(innerChildren.size() == 2,
                id + QStringLiteral(" inner fraction children drifted"));
        const auto compareChild = [&](QRectF actual, const QJsonObject& browser,
                                      QStringView name) {
          near(actual.x(), browser.value(QStringLiteral("x")).toDouble(), 0.22,
               id + QLatin1Char(' ') + name + QStringLiteral(" x"));
          near(actual.y(), browser.value(QStringLiteral("y")).toDouble(), 0.22,
               id + QLatin1Char(' ') + name + QStringLiteral(" y"));
          near(actual.width(), browser.value(QStringLiteral("width")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" width"));
          near(actual.height(),
               browser.value(QStringLiteral("height")).toDouble(), 0.22,
               id + QLatin1Char(' ') + name + QStringLiteral(" height"));
        };
        compareChild(operations->children.front().box.numerator,
                     innerChildren.at(0).toObject(), u"inner numerator");
        compareChild(operations->children.front().box.denominator,
                     innerChildren.at(1).toObject(), u"inner denominator");
        require(operations->box.hasRule &&
                    operations->children.front().box.hasRule,
                id + QStringLiteral(" must own both rule operations"));
      }
      if (id == QLatin1String("fraction-sup")) {
        QVector<QJsonObject> scripts;
        collectNodes(tree, u"msubsup", &scripts);
        require(scripts.size() == 2,
                id + QStringLiteral(" browser script nesting drifted"));
        const auto operations = math::layoutMathMlFractionOperations(
            layout, kKatexRootFontSize);
        require(operations.has_value() && operations->scripts.size() == 2,
                id + QStringLiteral(" must expose two script operations"));
        for (qsizetype index = 0; index < scripts.size(); ++index) {
          const auto& actual = operations->scripts.at(index);
          const QJsonObject browser = scripts.at(index);
          const QJsonArray children = browser.value(
              QStringLiteral("children")).toArray();
          require(children.size() == 3,
                  id + QStringLiteral(" script children drifted"));
          const auto compare = [&](QRectF rect, const QJsonObject& expected,
                                   QStringView component) {
            near(rect.x(), expected.value(QStringLiteral("x")).toDouble(), 0.22,
                 id + QLatin1Char(' ') + component + QStringLiteral(" x"));
            near(rect.y(), expected.value(QStringLiteral("y")).toDouble(), 0.22,
                 id + QLatin1Char(' ') + component + QStringLiteral(" y"));
            near(rect.width(), expected.value(QStringLiteral("width")).toDouble(),
                 0.22, id + QLatin1Char(' ') + component + QStringLiteral(" width"));
            near(rect.height(),
                 expected.value(QStringLiteral("height")).toDouble(), 0.22,
                 id + QLatin1Char(' ') + component + QStringLiteral(" height"));
          };
          compare(actual.container, browser, u"script container");
          compare(actual.base, children.at(0).toObject(), u"script base");
          compare(actual.subscript, children.at(1).toObject(), u"subscript");
          compare(actual.superscript, children.at(2).toObject(), u"superscript");
        }
      }
      if (id == QLatin1String("fraction-radical")) {
        QVector<QJsonObject> radicals;
        collectNodes(tree, u"msqrt", &radicals);
        require(radicals.size() == 2,
                id + QStringLiteral(" browser radical nesting drifted"));
        const auto operations = math::layoutMathMlFractionOperations(
            layout, kKatexRootFontSize);
        require(operations.has_value() && operations->radicals.size() == 2,
                id + QStringLiteral(" must expose two radical operations"));
        for (qsizetype index = 0; index < radicals.size(); ++index) {
          const auto& actual = operations->radicals.at(index);
          const QJsonObject browser = radicals.at(index);
          const QJsonArray children = browser.value(
              QStringLiteral("children")).toArray();
          require(children.size() == 1,
                  id + QStringLiteral(" radical body drifted"));
          const auto compare = [&](QRectF rect, const QJsonObject& expected,
                                   QStringView component) {
            near(rect.x(), expected.value(QStringLiteral("x")).toDouble(), 0.22,
                 id + QLatin1Char(' ') + component + QStringLiteral(" x"));
            near(rect.y(), expected.value(QStringLiteral("y")).toDouble(), 0.22,
                 id + QLatin1Char(' ') + component + QStringLiteral(" y"));
            near(rect.width(), expected.value(QStringLiteral("width")).toDouble(),
                 0.22, id + QLatin1Char(' ') + component + QStringLiteral(" width"));
            near(rect.height(), expected.value(QStringLiteral("height")).toDouble(),
                 0.22, id + QLatin1Char(' ') + component + QStringLiteral(" height"));
          };
          compare(actual.container, browser, u"radical container");
          compare(actual.body, children.at(0).toObject(), u"radical body");
          require(actual.glyphIndex != 0 && !actual.glyph.isEmpty() &&
                      !actual.rule.isEmpty(),
                  id + QStringLiteral(" radical paint operations are incomplete"));
        }
      }
      if (id == QLatin1String("genfrac-display-rule") ||
          id == QLatin1String("genfrac-text-stack") ||
          id == QLatin1String("display-fraction") ||
          id == QLatin1String("text-fraction")) {
        QVector<QJsonObject> fractions;
        collectNodes(tree, u"mfrac", &fractions);
        require(fractions.size() == 1,
                id + QStringLiteral(" must expose one browser mfrac"));
        const QJsonObject browserFraction = fractions.front();
        const QJsonArray browserChildren = browserFraction.value(
            QStringLiteral("children")).toArray();
        require(browserChildren.size() == 2,
                id + QStringLiteral(" mfrac children drifted"));
        const auto fractionBox = math::layoutMathMlFractionBox(
            layout, kKatexRootFontSize);
        require(fractionBox.has_value(),
                id + QStringLiteral(" native fraction paint operations are missing"));
        const auto compareRect = [&](QRectF actual, const QJsonObject& browser,
                                     QStringView name) {
          near(actual.x(), browser.value(QStringLiteral("x")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" x"));
          near(actual.y(), browser.value(QStringLiteral("y")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" y"));
          near(actual.width(), browser.value(QStringLiteral("width")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" width"));
          near(actual.height(), browser.value(QStringLiteral("height")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" height"));
        };
        compareRect(fractionBox->fraction, browserFraction, u"fraction");
        compareRect(fractionBox->numerator, browserChildren.at(0).toObject(),
                    u"numerator");
        compareRect(fractionBox->denominator, browserChildren.at(1).toObject(),
                    u"denominator");
        if (id == QLatin1String("genfrac-text-stack")) {
          require(!fractionBox->hasRule && fractionBox->rule.isEmpty(),
                  id + QStringLiteral(" must remain a rule-free stack"));
        } else {
          require(fractionBox->hasRule && !fractionBox->rule.isEmpty(),
                  id + QStringLiteral(" must expose a native rule operation"));
          near(fractionBox->rule.left(), fractionBox->fraction.left() + 1.0,
               0.001, id + QStringLiteral(" rule left padding"));
          near(fractionBox->rule.right(), fractionBox->fraction.right() - 1.0,
               0.001, id + QStringLiteral(" rule right padding"));
          const auto operations = math::layoutMathMlFractionOperations(
              layout, kKatexRootFontSize);
          require(operations.has_value(),
                  id + QStringLiteral(" rule operation tree is missing"));
          near(fractionBox->rule.center().y(),
               fractionBox->fraction.top() + operations->lineAscent -
                   mathFont.constants().axisHeight,
               0.001, id + QStringLiteral(" rule axis"));
        }
        if (id == QLatin1String("genfrac-display-rule"))
          near(fractionBox->rule.height(), 1.6, 0.001,
               id + QStringLiteral(" explicit 1pt rule"));
      }
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
        const auto accentBox = math::layoutMathMlAccentBox(
            layout, kKatexRootFontSize);
        require(accentBox && !accentBox->over &&
                    accentBox->character == QString(QChar(0x23DF)),
                id + QStringLiteral(" native accent box is missing"));
        near(accentBox->fontScale, 0.7, 0.0001,
             id + QStringLiteral(" script style scale"));
        near(accentBox->body.y(), 0.0, 0.02, id + QStringLiteral(" body y"));
        near(accentBox->body.height(), 14.0, 0.02,
             id + QStringLiteral(" body height"));
        near(accentBox->accent.y(), 16.797, 0.02,
             id + QStringLiteral(" brace y"));
        near(accentBox->accent.height(), 5.0, 0.02,
             id + QStringLiteral(" brace height"));
        near(accentBox->annotation.y(), 25.672, 0.02,
             id + QStringLiteral(" annotation y"));
      }
      if (id == QLatin1String("accent-overbrace")) {
        const auto accentBox = math::layoutMathMlAccentBox(
            layout, kKatexRootFontSize);
        require(accentBox && accentBox->over &&
                    accentBox->character == QString(QChar(0x23DE)),
                id + QStringLiteral(" native accent box is missing"));
        near(accentBox->annotation.y(), 0.0, 0.02,
             id + QStringLiteral(" annotation y"));
        near(accentBox->accent.y(), 10.953, 0.02,
             id + QStringLiteral(" brace y"));
        near(accentBox->accent.height(), 5.0, 0.02,
             id + QStringLiteral(" brace height"));
        near(accentBox->body.y(), 18.75, 0.02,
             id + QStringLiteral(" body y"));
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
