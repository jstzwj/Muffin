#include "mermaid/math/MathMlCssLayout.h"
#include "mermaid/math/MathMlCssPainter.h"
#include "math/MathRenderer.h"
#include "math/OpenTypeMathFont.h"

#include <QFile>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QImage>
#include <QRegularExpression>
#include <QPainter>
#include <QSet>
#include <QSizeF>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace muffin;

namespace muffin::math {

// Test-only convenience. Production callers consume the structured result and
// must not erase failure details behind an optional-returning API.
static std::optional<MathCssPaintOperation> checkedMathMlPaintOperations(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize = 16.0) {
  auto build = buildMathMlPaintOperations(
      layout, renderFontPixelSize, cssRootFontPixelSize);
  if (build.failure)
    throw MathMlPaintError(std::move(*build.failure));
  return std::move(build.operation);
}

}  // namespace muffin::math

namespace {

void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

void near(qreal actual, qreal expected, qreal tolerance, const QString& context) {
  require(std::abs(actual - expected) <= tolerance,
          QStringLiteral("%1: native=%2 browser=%3 tolerance=%4")
              .arg(context).arg(actual).arg(expected).arg(tolerance));
}

QJsonValue semanticPaintOperationJson(const QJsonValue& value) {
  if (value.isArray()) {
    QJsonArray result;
    for (const QJsonValue& element : value.toArray())
      result.push_back(semanticPaintOperationJson(element));
    return result;
  }
  if (!value.isObject()) return value;
  static const QSet<QString> rasterOnlyKeys = {
      QStringLiteral("baselineOrigin"),
      QStringLiteral("inkBounds"),
      QStringLiteral("paintOffset"),
  };
  QJsonObject result;
  const QJsonObject object = value.toObject();
  for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
    if (!rasterOnlyKeys.contains(it.key()))
      result.insert(it.key(), semanticPaintOperationJson(it.value()));
  }
  return result;
}

void collectTags(const QJsonObject& node, QSet<QString>* tags) {
  tags->insert(node.value(QStringLiteral("tag")).toString());
  for (const QJsonValue& child : node.value(QStringLiteral("children")).toArray())
    collectTags(child.toObject(), tags);
}

QRectF primitiveInkBounds(
    const QVector<math::MathMlPaintPrimitive>& primitives,
    QSizeF canvasSize) {
  constexpr int padding = 4;
  QImage image(qCeil(canvasSize.width()) + 2 * padding,
               qCeil(canvasSize.height()) + 2 * padding,
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.translate(padding, padding);
  math::paintMathMlPrimitives(painter, primitives, Qt::white);
  painter.end();
  int left = image.width(), top = image.height(), right = -1, bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (qAlpha(image.pixel(x, y)) == 0) continue;
      left = std::min(left, x); top = std::min(top, y);
      right = std::max(right, x); bottom = std::max(bottom, y);
    }
  }
  return right < left ? QRectF{} : QRectF(left - padding, top - padding,
                                          right - left + 1,
                                          bottom - top + 1);
}

void collectNodes(const QJsonObject& node, QStringView tag,
                  QVector<QJsonObject>* nodes) {
  if (node.value(QStringLiteral("tag")).toString() == tag) nodes->push_back(node);
  for (const QJsonValue& child : node.value(QStringLiteral("children")).toArray())
    collectNodes(child.toObject(), tag, nodes);
}

const math::MathCssFractionPaint* fractionPaint(
    const math::MathCssPaintOperation& operation) {
  return std::get_if<math::MathCssFractionPaint>(&operation.payload);
}

QVector<const math::MathCssPaintOperation*> childrenOfKind(
    const math::MathCssPaintOperation& operation,
    math::MathSemanticKind kind) {
  QVector<const math::MathCssPaintOperation*> result;
  for (const auto& child : operation.children)
    if (child.semanticKind() == kind) result.push_back(&child);
  return result;
}

int semanticNodeCount(const math::MathRenderNode* node) {
  if (!node) return 0;
  int result = node->semanticKind == math::MathSemanticKind::Fraction ||
                       node->semanticKind == math::MathSemanticKind::Radical ||
                       node->semanticKind == math::MathSemanticKind::SupSub ||
                       node->semanticKind == math::MathSemanticKind::Array
                   ? 1
                   : 0;
  for (const auto& child : node->children)
    result += semanticNodeCount(child.get());
  return result;
}

int paintOperationCount(const math::MathCssPaintOperation& operation) {
  int result = 1;
  for (const auto& child : operation.children)
    result += paintOperationCount(child);
  return result;
}

bool hasOperationPath(const math::MathCssPaintOperation& operation,
                      std::initializer_list<math::MathSemanticKind> path) {
  if (path.size() == 0 || operation.semanticKind() != *path.begin())
    return false;
  auto next = path.begin();
  ++next;
  const math::MathCssPaintOperation* current = &operation;
  for (; next != path.end(); ++next) {
    const auto child = std::find_if(
        current->children.cbegin(), current->children.cend(),
        [&](const math::MathCssPaintOperation& candidate) {
          return candidate.semanticKind() == *next;
        });
    if (child == current->children.cend()) return false;
    current = &*child;
  }
  return true;
}

bool hasPaintKindPath(const math::MathCssPaintOperation& operation,
                      std::initializer_list<math::MathCssPaintKind> path) {
  if (path.size() == 0 || operation.kind() != *path.begin()) return false;
  auto next = path.begin();
  ++next;
  const math::MathCssPaintOperation* current = &operation;
  for (; next != path.end(); ++next) {
    const auto child = std::find_if(
        current->children.cbegin(), current->children.cend(),
        [&](const math::MathCssPaintOperation& candidate) {
          return candidate.kind() == *next;
        });
    if (child == current->children.cend()) return false;
    current = &*child;
  }
  return true;
}

QString paintKindTree(const math::MathCssPaintOperation& operation) {
  QString result = QString::number(static_cast<int>(operation.kind()));
  if (!operation.children.isEmpty()) {
    QStringList children;
    for (const auto& child : operation.children)
      children.push_back(paintKindTree(child));
    result += QLatin1Char('(') + children.join(QLatin1Char(',')) +
              QLatin1Char(')');
  }
  return result;
}

bool hasVisibleSymbol(const math::MathRenderNode* node) {
  if (!node || node->phantom) return false;
  if ((node->kind == math::MathRenderKind::Symbol ||
       node->kind == math::MathRenderKind::Error) &&
      !node->text.isEmpty() && node->width > 0.0)
    return true;
  for (const auto& child : node->children)
    if (hasVisibleSymbol(child.get())) return true;
  return false;
}

bool regionHasExplicitOwner(
    QRectF region, const math::MathRenderNode* node,
    const QVector<math::MathCssGlyphRunOperation>& runs,
    const QVector<math::MathCssPaintOperation>& children) {
  if (region.isEmpty() || !hasVisibleSymbol(node) || !runs.isEmpty())
    return true;
  return std::any_of(
      children.cbegin(), children.cend(),
      [&](const math::MathCssPaintOperation& child) {
        return child.container().intersects(region);
      });
}

bool hasExplicitRegionOwnership(
    const math::MathCssPaintOperation& operation) {
  bool owned = true;
  if (const auto* row =
          std::get_if<math::MathCssRowOperation>(&operation.payload)) {
    owned = regionHasExplicitOwner(
        row->container, row->node, row->glyphRuns, operation.children);
  } else if (const auto* fraction =
          std::get_if<math::MathCssFractionPaint>(&operation.payload)) {
    owned = regionHasExplicitOwner(
                fraction->box.numerator, fraction->numeratorNode,
                fraction->numeratorGlyphRuns, operation.children) &&
            regionHasExplicitOwner(
                fraction->box.denominator, fraction->denominatorNode,
                fraction->denominatorGlyphRuns, operation.children);
  } else if (const auto* leftRight =
                 std::get_if<math::MathCssLeftRightOperation>(
                     &operation.payload)) {
    for (const auto& region : leftRight->bodyRegions)
      owned = owned && regionHasExplicitOwner(
                           region.box, region.node, region.glyphRuns,
                           operation.children);
  } else if (const auto* array =
                 std::get_if<math::MathCssArrayOperation>(
                     &operation.payload)) {
    for (const auto& cell : array->cells)
      owned = owned && regionHasExplicitOwner(
                           cell.content, cell.contentNode, cell.glyphRuns,
                           operation.children);
  } else if (const auto* accent =
                 std::get_if<math::MathCssAccentOperation>(
                     &operation.payload)) {
    owned = regionHasExplicitOwner(
                accent->box.body, accent->bodyNode,
                accent->bodyGlyphRuns, operation.children) &&
            regionHasExplicitOwner(
                accent->annotationContent, accent->annotationNode,
                accent->annotationGlyphRuns, operation.children);
  } else if (const auto* script =
                 std::get_if<math::MathCssScriptOperation>(
                     &operation.payload)) {
    owned = (script->largeOperatorGlyph.has_value() ||
             regionHasExplicitOwner(
                 script->base, script->baseNode,
                 script->baseGlyphRuns, operation.children)) &&
            regionHasExplicitOwner(
                script->superscript, script->superscriptNode,
                script->superscriptGlyphRuns, operation.children) &&
            regionHasExplicitOwner(
                script->subscript, script->subscriptNode,
                script->subscriptGlyphRuns, operation.children);
  } else if (const auto* radical =
                 std::get_if<math::MathCssRadicalOperation>(
                     &operation.payload)) {
    owned = regionHasExplicitOwner(
        radical->body, radical->bodyNode, radical->bodyGlyphRuns,
        operation.children);
  }
  if (!owned) return false;
  for (const auto& child : operation.children)
    if (!hasExplicitRegionOwnership(child)) return false;
  return true;
}

void collectGlyphRuns(
    const math::MathCssPaintOperation& operation,
    QVector<const math::MathCssGlyphRunOperation*>* result) {
  const auto append = [&](const QVector<math::MathCssGlyphRunOperation>& runs) {
    for (const auto& run : runs) result->push_back(&run);
  };
  if (const auto* group =
          std::get_if<math::MathCssGlyphRunGroupOperation>(&operation.payload)) {
    append(group->runs);
  } else if (const auto* row =
                 std::get_if<math::MathCssRowOperation>(&operation.payload)) {
    append(row->glyphRuns);
  } else if (const auto* leftRight =
                 std::get_if<math::MathCssLeftRightOperation>(&operation.payload)) {
    for (const auto& region : leftRight->bodyRegions) append(region.glyphRuns);
  } else if (const auto* middle =
                 std::get_if<math::MathCssMiddlePaintOperation>(&operation.payload)) {
    result->push_back(&middle->glyphRun);
  } else if (const auto* fraction =
                 std::get_if<math::MathCssFractionPaint>(&operation.payload)) {
    append(fraction->numeratorGlyphRuns);
    append(fraction->denominatorGlyphRuns);
  } else if (const auto* script =
                 std::get_if<math::MathCssScriptOperation>(&operation.payload)) {
    append(script->baseGlyphRuns);
    append(script->superscriptGlyphRuns);
    append(script->subscriptGlyphRuns);
  } else if (const auto* radical =
                 std::get_if<math::MathCssRadicalOperation>(&operation.payload)) {
    append(radical->bodyGlyphRuns);
  } else if (const auto* array =
                 std::get_if<math::MathCssArrayOperation>(&operation.payload)) {
    for (const auto& cell : array->cells) append(cell.glyphRuns);
  } else if (const auto* accent =
                 std::get_if<math::MathCssAccentOperation>(&operation.payload)) {
    append(accent->bodyGlyphRuns);
    append(accent->annotationGlyphRuns);
  }
  for (const auto& child : operation.children) collectGlyphRuns(child, result);
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
                QLatin1String("3152a3125c65dabacf789a4d11c0d6727b5d6a0bcf218b65f38bedce49c986e3"),
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
    require(cases.size() == 167, QStringLiteral("MathML CSS box case count regressed"));
    math::MathRenderer renderer;
    QSet<QString> tags;
    QHash<QString, QSizeF> invariantBoxes;
    QJsonArray paintOperationGolden;
    QJsonArray paintFailureGolden;
    constexpr qreal kKatexRootFontSize = 16.0 * 1.21;
    for (const QJsonValue& value : cases) {
      const QJsonObject fixture = value.toObject();
      const QString id = fixture.value(QStringLiteral("id")).toString();
      const QString tex = fixture.value(QStringLiteral("tex")).toString();
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
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operation && operation->kind() == math::MathCssPaintKind::Accent,
                id + QStringLiteral(" accent operation is missing"));
        const auto* paint = std::get_if<math::MathCssAccentOperation>(
            &operation->payload);
        require(paint && paint->bodyGlyphRuns.size() == 3 &&
                    paint->annotationGlyphRuns.isEmpty() &&
                    paint->glyph.scalePolicy ==
                        math::MathCssHorizontalScalePolicy::PreserveVariantScale,
                id + QStringLiteral(" glyph-run operation coverage drifted"));
      }
      const math::MathCssBox box = math::layoutMathMlCssBox(layout, kKatexRootFontSize);
      const QJsonObject expected = fixture.value(QStringLiteral("math")).toObject();
      near(box.width, expected.value(QStringLiteral("width")).toDouble(), 0.22,
           id + QStringLiteral(" root width"));
      const qreal rootHeightTolerance = 0.22;
      near(box.height, expected.value(QStringLiteral("height")).toDouble(),
           rootHeightTolerance,
           id + QStringLiteral(" root height"));
      const qreal expectedBaseline = fixture.value(QStringLiteral("textBaseline")).toDouble() -
                                     expected.value(QStringLiteral("y")).toDouble();
      near(box.baseline, expectedBaseline, 0.22,
           id + QStringLiteral(" flex baseline"));
      collectTags(fixture.value(QStringLiteral("tree")).toObject(), &tags);

      const QJsonObject tree = fixture.value(QStringLiteral("tree")).toObject();
      const bool extendedAccent =
          id == QLatin1String("accent-double-right-arrow") ||
          id == QLatin1String("accent-left-harpoon") ||
          id == QLatin1String("accent-right-harpoon") ||
          id == QLatin1String("accent-overgroup") ||
          id == QLatin1String("accent-overlinesegment-upstream-text");
      if (extendedAccent) {
        QVector<QJsonObject> operators;
        collectNodes(tree, u"mo", &operators);
        require(operators.size() == 2,
                id + QStringLiteral(" MathML operator structure drifted"));
        const QJsonObject browserAccent = operators.back();
        const auto accentBox = math::layoutMathMlAccentBox(
            layout, kKatexRootFontSize);
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* paint = operation
            ? std::get_if<math::MathCssAccentOperation>(&operation->payload)
            : nullptr;
        require(accentBox && paint,
                id + QStringLiteral(" accent operation is missing"));
        near(accentBox->accent.x(),
             browserAccent.value(QStringLiteral("x")).toDouble(), 0.22,
             id + QStringLiteral(" operator x"));
        near(accentBox->accent.y(),
             browserAccent.value(QStringLiteral("y")).toDouble(), 0.22,
             id + QStringLiteral(" operator y"));
        near(accentBox->accent.width(),
             browserAccent.value(QStringLiteral("width")).toDouble(), 0.22,
             id + QStringLiteral(" operator width"));
        near(accentBox->accent.height(),
             browserAccent.value(QStringLiteral("height")).toDouble(), 0.22,
             id + QStringLiteral(" operator height"));
        require(paint->box.character ==
                    browserAccent.value(QStringLiteral("text")).toString(),
                id + QStringLiteral(" MathML character drifted"));
        if (id == QLatin1String("accent-overlinesegment-upstream-text")) {
          require(paint->glyph.kind ==
                      math::MathCssHorizontalGlyphKind::ShapedText &&
                      paint->glyph.text == QLatin1String("undefined") &&
                      paint->glyph.textGlyphIndexes.size() == 9,
                  id + QStringLiteral(" upstream text operation drifted"));
        } else {
          require(paint->glyph.kind !=
                      math::MathCssHorizontalGlyphKind::ShapedText,
                  id + QStringLiteral(" must use a MATH glyph operation"));
        }
      }
      const bool basicAccent =
          id == QLatin1String("accent-hat") ||
          id == QLatin1String("accent-vector") ||
          id == QLatin1String("accent-overline") ||
          id == QLatin1String("accent-underline");
      if (basicAccent) {
        QVector<QJsonObject> operators;
        collectNodes(tree, u"mo", &operators);
        require(!operators.isEmpty(),
                id + QStringLiteral(" browser accent operator is missing"));
        const QJsonObject browserAccent = operators.back();
        const auto accentBox = math::layoutMathMlAccentBox(
            layout, kKatexRootFontSize);
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* paint = operation
            ? std::get_if<math::MathCssAccentOperation>(&operation->payload)
            : nullptr;
        require(accentBox && paint &&
                    paint->glyph.kind ==
                        math::MathCssHorizontalGlyphKind::FixedVariant &&
                    paint->glyph.fixedGlyphIndex != 0,
                id + QStringLiteral(" fixed accent operation is missing"));
        near(accentBox->accent.x(),
             browserAccent.value(QStringLiteral("x")).toDouble(), 0.22,
             id + QStringLiteral(" basic operator x"));
        near(accentBox->accent.y(),
             browserAccent.value(QStringLiteral("y")).toDouble(), 0.22,
             id + QStringLiteral(" basic operator y"));
        near(accentBox->accent.width(),
             browserAccent.value(QStringLiteral("width")).toDouble(), 0.22,
             id + QStringLiteral(" basic operator width"));
        near(accentBox->accent.height(),
             browserAccent.value(QStringLiteral("height")).toDouble(), 0.22,
             id + QStringLiteral(" basic operator height"));
        require(paint->box.character ==
                    browserAccent.value(QStringLiteral("text")).toString(),
                id + QStringLiteral(" basic operator character drifted"));
      }
      if (id == QLatin1String("relations")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        QVector<const math::MathCssGlyphRunOperation*> runs;
        if (operation) collectGlyphRuns(*operation, &runs);
        const auto overlay = std::find_if(
            runs.cbegin(), runs.cend(),
            [](const math::MathCssGlyphRunOperation* run) {
              return run && run->text == QString(QChar(0xE020));
            });
        require(operation && overlay != runs.cend() &&
                    (*overlay)->rawFont.isValid() &&
                    (*overlay)->rawFont.familyName() ==
                        QLatin1String("KaTeX_Main") &&
                    (*overlay)->glyphIndexes.size() == 1 &&
                    (*overlay)->glyphIndexes.front() != 0,
                id + QStringLiteral(" private overlay glyph operation is missing"));
      }
      if (id == QLatin1String("delimiter-row") ||
          id == QLatin1String("left-right-nullable-plain") ||
          id == QLatin1String("left-right-middle") ||
          id == QLatin1String("left-right-multiple-middle") ||
          id == QLatin1String("left-right-nested-plain")) {
        QVector<QJsonObject> operators;
        collectNodes(tree, u"mo", &operators);
        QVector<QJsonObject> fences;
        std::copy_if(operators.cbegin(), operators.cend(),
                     std::back_inserter(fences), [](const QJsonObject& node) {
          return node.value(QStringLiteral("attributes")).toObject()
                     .value(QStringLiteral("fence")).toString() ==
                 QLatin1String("true");
        });
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* leftRight = operation
            ? std::get_if<math::MathCssLeftRightOperation>(
                  &operation->payload)
            : nullptr;
        require(leftRight && !fences.isEmpty(),
                id + QStringLiteral(" LeftRight operation is missing"));
        const auto compareFence = [&](QRectF actual,
                                      const QJsonObject& browser,
                                      QStringView component) {
          near(actual.x(), browser.value(QStringLiteral("x")).toDouble(),
               0.22, id + QLatin1Char(' ') + component +
                         QStringLiteral(" x"));
          near(actual.y(), browser.value(QStringLiteral("y")).toDouble(),
               0.22, id + QLatin1Char(' ') + component +
                         QStringLiteral(" y"));
          near(actual.width(),
               browser.value(QStringLiteral("width")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component +
                   QStringLiteral(" width"));
          near(actual.height(),
               browser.value(QStringLiteral("height")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component +
                   QStringLiteral(" height"));
        };
        if (id == QLatin1String("left-right-nullable-plain")) {
          require(!leftRight->leftDelimiterGlyph &&
                      leftRight->rightDelimiterGlyph && fences.size() == 1,
                  id + QStringLiteral(" nullable fence ownership drifted"));
          compareFence(leftRight->rightDelimiter, fences.back(),
                       u"right fence");
        } else {
          require(leftRight->leftDelimiterGlyph &&
                      leftRight->rightDelimiterGlyph,
                  id + QStringLiteral(" paired fence ownership drifted"));
          compareFence(leftRight->leftDelimiter, fences.front(),
                       u"left fence");
          compareFence(leftRight->rightDelimiter, fences.back(),
                       u"right fence");
        }
        if (id == QLatin1String("left-right-middle") ||
            id == QLatin1String("left-right-multiple-middle")) {
          const qsizetype expectedMiddles =
              id == QLatin1String("left-right-middle") ? 1 : 2;
          require(leftRight->middleDelimiters.size() == expectedMiddles &&
                      fences.size() == expectedMiddles + 2,
                  id + QStringLiteral(" middle delimiter count drifted"));
          for (qsizetype middle = 0; middle < expectedMiddles; ++middle) {
            require(leftRight->middleDelimiters.at(middle).glyph.has_value(),
                    id + QStringLiteral(" middle glyph operation is missing"));
            compareFence(leftRight->middleDelimiters.at(middle).box,
                         fences.at(middle + 1),
                         QStringLiteral("middle fence %1").arg(middle));
          }
        }
        if (id == QLatin1String("left-right-nested-plain")) {
          const auto nested = std::find_if(
              operation->children.cbegin(), operation->children.cend(),
              [](const math::MathCssPaintOperation& child) {
                return child.kind() == math::MathCssPaintKind::LeftRight;
              });
          const auto* nestedLeftRight = nested == operation->children.cend()
              ? nullptr : std::get_if<math::MathCssLeftRightOperation>(
                              &nested->payload);
          require(nestedLeftRight && fences.size() == 4,
                  id + QStringLiteral(" nested LeftRight operation drifted"));
          compareFence(nestedLeftRight->leftDelimiter, fences.at(1),
                       u"nested left fence");
          compareFence(nestedLeftRight->rightDelimiter, fences.at(2),
                       u"nested right fence");
        }
      }
      if (id == QLatin1String("left-right-middle-fraction") ||
          id == QLatin1String("left-right-middle-radical") ||
          id == QLatin1String("left-right-middle-script") ||
          id == QLatin1String("left-right-middle-array")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const math::MathCssPaintKind expectedRoot =
            id == QLatin1String("left-right-middle-fraction")
                ? math::MathCssPaintKind::Fraction
                : id == QLatin1String("left-right-middle-radical")
                    ? math::MathCssPaintKind::Radical
                    : id == QLatin1String("left-right-middle-script")
                        ? math::MathCssPaintKind::SupSub
                        : math::MathCssPaintKind::Array;
        require(operation && operation->kind() == expectedRoot,
                id + QStringLiteral(" semantic operation root drifted"));
        const math::MathCssPaintOperation* middleOperation = nullptr;
        std::function<void(const math::MathCssPaintOperation&)> findMiddle =
            [&](const math::MathCssPaintOperation& candidate) {
          if (candidate.kind() == math::MathCssPaintKind::MiddleDelimiter) {
            require(!middleOperation,
                    id + QStringLiteral(" owns duplicate middle operations"));
            middleOperation = &candidate;
          }
          for (const auto& child : candidate.children) findMiddle(child);
        };
        findMiddle(*operation);
        const auto* middle = middleOperation
            ? std::get_if<math::MathCssMiddlePaintOperation>(
                  &middleOperation->payload)
            : nullptr;
        require(middle && middle->character == QLatin1String("|") &&
                    middle->glyphRun.glyphIndexes.size() == 1 &&
                    middle->glyph &&
                    middle->glyph->kind ==
                        math::MathCssVerticalGlyphKind::FixedVariant,
                id + QStringLiteral(" recursive middle operation is missing"));

        QVector<QJsonObject> operators;
        collectNodes(tree, u"mo", &operators);
        QVector<QJsonObject> fences;
        std::copy_if(operators.cbegin(), operators.cend(),
                     std::back_inserter(fences), [](const QJsonObject& node) {
          return node.value(QStringLiteral("attributes")).toObject()
                     .value(QStringLiteral("fence")).toString() ==
                 QLatin1String("true");
        });
        require(fences.size() == 3,
                id + QStringLiteral(" browser middle fence count drifted"));
        const auto compareOwnedFence = [&](QRectF actual,
                                           const QJsonObject& browser,
                                           QStringView component) {
          near(actual.x(), browser.value(QStringLiteral("x")).toDouble(),
               0.22, id + QLatin1Char(' ') + component + QStringLiteral(" x"));
          near(actual.y(), browser.value(QStringLiteral("y")).toDouble(),
               0.22, id + QLatin1Char(' ') + component + QStringLiteral(" y"));
          near(actual.width(),
               browser.value(QStringLiteral("width")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" width"));
          near(actual.height(),
               browser.value(QStringLiteral("height")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" height"));
        };
        const math::MathCssFencePair* ownedFences = nullptr;
        if (const auto* radical =
                std::get_if<math::MathCssRadicalOperation>(&operation->payload))
          ownedFences = radical->fences ? &*radical->fences : nullptr;
        if (const auto* script =
                std::get_if<math::MathCssScriptOperation>(&operation->payload))
          ownedFences = script->fences ? &*script->fences : nullptr;
        if (id == QLatin1String("left-right-middle-radical") ||
            id == QLatin1String("left-right-middle-script")) {
          require(ownedFences && ownedFences->leftGlyph &&
                      ownedFences->rightGlyph &&
                      ownedFences->leftGlyph->scalePolicy ==
                          math::MathCssVerticalScalePolicy::FitTargetExtent &&
                      ownedFences->rightGlyph->scalePolicy ==
                          math::MathCssVerticalScalePolicy::FitTargetExtent &&
                      ownedFences->leftCharacter == QLatin1String("(") &&
                      ownedFences->rightCharacter == QLatin1String(")"),
                  id + QStringLiteral(" semantic root fence ownership drifted"));
          compareOwnedFence(ownedFences->left, fences.front(),
                            u"owned left fence");
          compareOwnedFence(ownedFences->right, fences.back(),
                            u"owned right fence");
        }
        const QJsonObject browserMiddle = fences.at(1);
        near(middle->container.x(),
             browserMiddle.value(QStringLiteral("x")).toDouble(), 0.22,
             id + QStringLiteral(" middle x"));
        near(middle->container.y(),
             browserMiddle.value(QStringLiteral("y")).toDouble(), 0.22,
             id + QStringLiteral(" middle y"));
        near(middle->container.width(),
             browserMiddle.value(QStringLiteral("width")).toDouble(), 0.22,
             id + QStringLiteral(" middle width"));
        near(middle->container.height(),
             browserMiddle.value(QStringLiteral("height")).toDouble(), 0.22,
             id + QStringLiteral(" middle height"));
        const qreal expectedSpacing =
            id == QLatin1String("left-right-middle-script") ? 0.56 : 0.8;
        near(middle->container.x() - middle->allocation.x(),
             expectedSpacing, 0.03,
             id + QStringLiteral(" middle lspace"));
        near(middle->allocation.right() - middle->container.right(),
             expectedSpacing, 0.03,
             id + QStringLiteral(" middle rspace"));
      }
      if (id == QLatin1String("matrix-2x2") ||
          id == QLatin1String("matrix-3x3") ||
          id == QLatin1String("matrix-fractions") ||
          id == QLatin1String("aligned-equations") ||
          id == QLatin1String("cases-piecewise") ||
          id == QLatin1String("tall-delimiter-assembly") ||
          id == QLatin1String("tall-paren-assembly") ||
          id == QLatin1String("tall-bracket-assembly") ||
          id == QLatin1String("tall-bar-assembly") ||
          id == QLatin1String("nested-delimiter-array") ||
          id == QLatin1String("tall-double-bar") ||
          id == QLatin1String("tall-floor") ||
          id == QLatin1String("tall-ceil") ||
          id == QLatin1String("tall-angle") ||
          id == QLatin1String("nullable-delimiter")) {
        QVector<QJsonObject> tables;
        QVector<QJsonObject> rows;
        QVector<QJsonObject> cells;
        QVector<QJsonObject> contents;
        collectNodes(tree, u"mtable", &tables);
        collectNodes(tree, u"mtr", &rows);
        collectNodes(tree, u"mtd", &cells);
        collectNodes(tree, u"mstyle", &contents);
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operations
            ? std::get_if<math::MathCssArrayOperation>(&operations->payload)
            : nullptr;
        require(array && tables.size() == 1 &&
                    rows.size() == array->rows.size() &&
                    cells.size() == array->cells.size() &&
                    contents.size() == array->cells.size(),
                id + QStringLiteral(" array operation structure drifted"));
        const auto compare = [&](QRectF actual, const QJsonObject& browser,
                                 QStringView component) {
          near(actual.x(), browser.value(QStringLiteral("x")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" x"));
          near(actual.y(), browser.value(QStringLiteral("y")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" y"));
          near(actual.width(), browser.value(QStringLiteral("width")).toDouble(),
               0.22, id + QLatin1Char(' ') + component + QStringLiteral(" width"));
          near(actual.height(), browser.value(QStringLiteral("height")).toDouble(),
               0.22, id + QLatin1Char(' ') + component + QStringLiteral(" height"));
        };
        compare(array->table, tables.front(), u"table");
        for (qsizetype index = 0; index < rows.size(); ++index)
          compare(array->rows.at(index), rows.at(index), u"row");
        for (qsizetype index = 0; index < cells.size(); ++index) {
          compare(array->cells.at(index).box, cells.at(index), u"cell");
          compare(array->cells.at(index).content, contents.at(index),
                  u"cell content");
        }
        if (id == QLatin1String("matrix-2x2")) {
          require(std::all_of(
                      array->cells.cbegin(), array->cells.cend(),
                      [](const math::MathCssArrayCell& cell) {
                        return !cell.glyphRuns.isEmpty();
                      }),
                  id + QStringLiteral(" cell glyph-run oracle is incomplete"));
        }
        if (id == QLatin1String("matrix-fractions"))
          require(paintOperationCount(*operations) ==
                      semanticNodeCount(layout.root.get()),
                  id + QStringLiteral(" array cell operation ownership drifted: %1/%2")
                           .arg(paintOperationCount(*operations))
                           .arg(semanticNodeCount(layout.root.get())));
      }
      if (id == QLatin1String("fraction-nested")) {
        QVector<QJsonObject> fractions;
        collectNodes(tree, u"mfrac", &fractions);
        require(fractions.size() == 2,
                id + QStringLiteral(" browser nesting drifted"));
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operations.has_value() && operations->children.size() == 1 &&
                    operations->children.front().children.isEmpty(),
                id + QStringLiteral(" must produce a two-level operation tree"));
        const auto compareFraction = [&](const math::MathCssPaintOperation& op,
                                         const QJsonObject& browser,
                                         QStringView name) {
          const auto* fraction = fractionPaint(op);
          require(fraction, id + QStringLiteral(" operation kind drifted"));
          near(fraction->box.fraction.x(), browser.value(QStringLiteral("x")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" x"));
          near(fraction->box.fraction.y(), browser.value(QStringLiteral("y")).toDouble(),
               0.22, id + QLatin1Char(' ') + name + QStringLiteral(" y"));
          near(fraction->box.fraction.width(),
               browser.value(QStringLiteral("width")).toDouble(), 0.22,
               id + QLatin1Char(' ') + name + QStringLiteral(" width"));
          near(fraction->box.fraction.height(),
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
        const auto* outer = fractionPaint(*operations);
        const auto* inner = fractionPaint(operations->children.front());
        require(outer && inner, id + QStringLiteral(" fraction payload missing"));
        compareChild(inner->box.numerator,
                     innerChildren.at(0).toObject(), u"inner numerator");
        compareChild(inner->box.denominator,
                     innerChildren.at(1).toObject(), u"inner denominator");
        require(outer->box.hasRule && inner->box.hasRule,
                id + QStringLiteral(" must own both rule operations"));
        require(!inner->numeratorGlyphRuns.isEmpty() &&
                    !inner->denominatorGlyphRuns.isEmpty(),
                id + QStringLiteral(" inner fraction glyph-run oracle is incomplete"));
        near(inner->numeratorGlyphRuns.front().fontScale, 0.7, 0.0001,
             id + QStringLiteral(" nested fraction script style"));
      }
      if (id == QLatin1String("fraction-sup")) {
        QVector<QJsonObject> scripts;
        collectNodes(tree, u"msubsup", &scripts);
        require(scripts.size() == 2,
                id + QStringLiteral(" browser script nesting drifted"));
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto scriptOperations = operations
            ? childrenOfKind(*operations, math::MathSemanticKind::SupSub)
            : QVector<const math::MathCssPaintOperation*>{};
        require(operations.has_value() && scriptOperations.size() == 2,
                id + QStringLiteral(" must expose two script operations"));
        for (qsizetype index = 0; index < scripts.size(); ++index) {
          const auto* actual = std::get_if<math::MathCssScriptOperation>(
              &scriptOperations.at(index)->payload);
          require(actual, id + QStringLiteral(" script payload missing"));
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
          compare(actual->container, browser, u"script container");
          compare(actual->base, children.at(0).toObject(), u"script base");
          compare(actual->subscript, children.at(1).toObject(), u"subscript");
          compare(actual->superscript, children.at(2).toObject(), u"superscript");
        }
        require(std::any_of(
                    scriptOperations.cbegin(), scriptOperations.cend(),
                    [](const math::MathCssPaintOperation* operation) {
                      const auto* script =
                          std::get_if<math::MathCssScriptOperation>(
                              &operation->payload);
                      return script && (!script->baseGlyphRuns.isEmpty() ||
                          !script->superscriptGlyphRuns.isEmpty() ||
                          !script->subscriptGlyphRuns.isEmpty());
                    }),
                id + QStringLiteral(" script glyph-run oracle is incomplete"));
      }
      if (id == QLatin1String("sum-limits") ||
          id == QLatin1String("product-limits") ||
          id == QLatin1String("root-mixed-sum-limits") ||
          id == QLatin1String("root-limits-fraction") ||
          id == QLatin1String("root-mixed-product-limits") ||
          id == QLatin1String("root-mixed-coproduct-limits")) {
        QVector<QJsonObject> limits;
        collectNodes(tree, u"munderover", &limits);
        require(limits.size() == 1,
                id + QStringLiteral(" browser limits stack drifted"));
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto scriptOperations = operations
            ? childrenOfKind(*operations, math::MathSemanticKind::SupSub)
            : QVector<const math::MathCssPaintOperation*>{};
        require(!scriptOperations.isEmpty(),
                id + QStringLiteral(" limits operation is missing"));
        const auto* actual = std::get_if<math::MathCssScriptOperation>(
            &scriptOperations.front()->payload);
        require(actual && actual->limits && actual->largeOperatorGlyph,
                id + QStringLiteral(" large operator glyph is missing"));
        require(actual->largeOperatorGlyph->kind ==
                    math::MathCssVerticalGlyphKind::FixedVariant &&
                    actual->largeOperatorGlyph->parts.isEmpty(),
                id + QStringLiteral(" rounded fixed operator became an assembly"));
        const QJsonObject browser = limits.front();
        const QJsonArray children = browser.value(
            QStringLiteral("children")).toArray();
        require(children.size() == 3,
                id + QStringLiteral(" limits children drifted"));
        const auto compare = [&](QRectF rect, const QJsonObject& expected,
                                 QStringView component) {
          near(rect.x(), expected.value(QStringLiteral("x")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" x"));
          near(rect.y(), expected.value(QStringLiteral("y")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" y"));
          near(rect.width(), expected.value(QStringLiteral("width")).toDouble(),
               0.22, id + QLatin1Char(' ') + component +
                         QStringLiteral(" width"));
          near(rect.height(), expected.value(QStringLiteral("height")).toDouble(),
               0.22, id + QLatin1Char(' ') + component +
                         QStringLiteral(" height"));
        };
        compare(actual->container, browser, u"limits container");
        compare(actual->base, children.at(0).toObject(), u"operator");
        compare(actual->subscript, children.at(1).toObject(), u"lower limit");
        compare(actual->superscript, children.at(2).toObject(), u"upper limit");
      }
      if (id == QLatin1String("root-mixed-integral-scripts")) {
        QVector<QJsonObject> scripts;
        collectNodes(tree, u"msubsup", &scripts);
        require(scripts.size() == 1,
                id + QStringLiteral(" browser integral script drifted"));
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto scriptOperations = operations
            ? childrenOfKind(*operations, math::MathSemanticKind::SupSub)
            : QVector<const math::MathCssPaintOperation*>{};
        require(scriptOperations.size() == 1,
                id + QStringLiteral(" integral operation is missing"));
        const auto* actual = std::get_if<math::MathCssScriptOperation>(
            &scriptOperations.front()->payload);
        require(actual && !actual->limits && actual->largeOperatorGlyph,
                id + QStringLiteral(" integral large glyph is missing"));
        require(actual->largeOperatorGlyph->kind ==
                    math::MathCssVerticalGlyphKind::FixedVariant &&
                    actual->largeOperatorGlyph->parts.isEmpty(),
                id + QStringLiteral(" rounded integral became an assembly"));
        const QJsonObject browser = scripts.front();
        const QJsonArray children = browser.value(
            QStringLiteral("children")).toArray();
        require(children.size() == 3,
                id + QStringLiteral(" integral children drifted"));
        const auto compare = [&](QRectF rect, const QJsonObject& expected,
                                 QStringView component) {
          near(rect.x(), expected.value(QStringLiteral("x")).toDouble(), 0.45,
               id + QLatin1Char(' ') + component + QStringLiteral(" x"));
          near(rect.y(), expected.value(QStringLiteral("y")).toDouble(), 0.45,
               id + QLatin1Char(' ') + component + QStringLiteral(" y"));
          near(rect.width(), expected.value(QStringLiteral("width")).toDouble(),
               0.45, id + QLatin1Char(' ') + component +
                         QStringLiteral(" width"));
          near(rect.height(), expected.value(QStringLiteral("height")).toDouble(),
               0.45, id + QLatin1Char(' ') + component +
                         QStringLiteral(" height"));
        };
        compare(actual->container, browser, u"integral container");
        compare(actual->base, children.at(0).toObject(), u"integral operator");
        compare(actual->subscript, children.at(1).toObject(), u"integral subscript");
        compare(actual->superscript, children.at(2).toObject(), u"integral superscript");
      }
      if (id == QLatin1String("root-mixed-product-limits") ||
          id == QLatin1String("root-mixed-coproduct-limits") ||
          id == QLatin1String("root-mixed-triple-integral")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operation.has_value(),
                id + QStringLiteral(" primitive operation is missing"));
        const auto primitives = math::collectMathMlPaintPrimitives(*operation);
        QSet<QString> paths;
        QHash<int, int> roleCounts;
        for (const auto& primitive : primitives) {
          require(!primitive.operationPath.isEmpty() &&
                      !paths.contains(primitive.operationPath),
                  id + QStringLiteral(" primitive path is missing or duplicated: ") +
                      primitive.operationPath);
          paths.insert(primitive.operationPath);
          ++roleCounts[static_cast<int>(primitive.role)];
        }
        const bool limits = id != QLatin1String("root-mixed-triple-integral");
        require(roleCounts.value(static_cast<int>(
                    math::MathMlPaintPrimitiveRole::Row)) == 4 &&
                    roleCounts.value(static_cast<int>(
                    math::MathMlPaintPrimitiveRole::LargeOperator)) == 1 &&
                    roleCounts.value(static_cast<int>(
                    math::MathMlPaintPrimitiveRole::ScriptSubscript)) ==
                        (limits ? 3 : 1) &&
                    roleCounts.value(static_cast<int>(
                    math::MathMlPaintPrimitiveRole::ScriptSuperscript)) ==
                        (limits ? 1 : 0),
                id + QStringLiteral(" primitive role coverage drifted"));
      }
      if (id == QLatin1String("root-mixed-double-integral") ||
          id == QLatin1String("root-mixed-triple-integral")) {
        QVector<QJsonObject> scripts;
        collectNodes(tree, u"msub", &scripts);
        require(scripts.size() == 1,
                id + QStringLiteral(" browser multi-integral drifted"));
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto scriptOperations = operations
            ? childrenOfKind(*operations, math::MathSemanticKind::SupSub)
            : QVector<const math::MathCssPaintOperation*>{};
        require(scriptOperations.size() == 1,
                id + QStringLiteral(" multi-integral operation is missing"));
        const auto* actual = std::get_if<math::MathCssScriptOperation>(
            &scriptOperations.front()->payload);
        require(actual && !actual->limits && actual->largeOperatorGlyph,
                id + QStringLiteral(" multi-integral large glyph is missing"));
        require(actual->largeOperatorGlyph->kind ==
                    math::MathCssVerticalGlyphKind::FixedVariant &&
                    actual->largeOperatorGlyph->parts.isEmpty(),
                id + QStringLiteral(" rounded multi-integral became an assembly"));
        const QJsonObject browser = scripts.front();
        const QJsonArray children = browser.value(
            QStringLiteral("children")).toArray();
        require(children.size() == 2,
                id + QStringLiteral(" multi-integral children drifted"));
        const auto compare = [&](QRectF rect, const QJsonObject& expected,
                                 QStringView component) {
          near(rect.x(), expected.value(QStringLiteral("x")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" x"));
          near(rect.y(), expected.value(QStringLiteral("y")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" y"));
          near(rect.width(), expected.value(QStringLiteral("width")).toDouble(),
               0.22, id + QLatin1Char(' ') + component +
                         QStringLiteral(" width"));
          near(rect.height(), expected.value(QStringLiteral("height")).toDouble(),
               0.22, id + QLatin1Char(' ') + component +
                         QStringLiteral(" height"));
        };
        compare(actual->container, browser, u"multi-integral container");
        compare(actual->base, children.at(0).toObject(), u"multi-integral base");
        compare(actual->subscript, children.at(1).toObject(), u"multi-integral subscript");
      }
      if (id == QLatin1String("root-mixed-under-arrow")) {
        QVector<QJsonObject> unders;
        collectNodes(tree, u"munder", &unders);
        require(unders.size() == 1,
                id + QStringLiteral(" browser under-arrow drifted"));
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto operation = operations
            ? std::find_if(
                  operations->children.cbegin(), operations->children.cend(),
                  [](const math::MathCssPaintOperation& child) {
                    return child.kind() == math::MathCssPaintKind::Accent;
                  })
            : QVector<math::MathCssPaintOperation>::const_iterator{};
        require(operations && operation != operations->children.cend(),
                id + QStringLiteral(" under-arrow operation is missing"));
        const auto* actual = std::get_if<math::MathCssAccentOperation>(
            &operation->payload);
        require(actual &&
                    actual->glyph.kind ==
                        math::MathCssHorizontalGlyphKind::FixedVariant,
                id + QStringLiteral(" short arrow variant is missing"));
        const QJsonObject browser = unders.front();
        const QJsonArray children = browser.value(
            QStringLiteral("children")).toArray();
        require(children.size() == 2,
                id + QStringLiteral(" under-arrow children drifted"));
        const auto compare = [&](QRectF rect, const QJsonObject& expected,
                                 QStringView component) {
          near(rect.x(), expected.value(QStringLiteral("x")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" x"));
          near(rect.y(), expected.value(QStringLiteral("y")).toDouble(), 0.22,
               id + QLatin1Char(' ') + component + QStringLiteral(" y"));
          near(rect.width(), expected.value(QStringLiteral("width")).toDouble(),
               0.22, id + QLatin1Char(' ') + component +
                         QStringLiteral(" width"));
          near(rect.height(), expected.value(QStringLiteral("height")).toDouble(),
               0.22, id + QLatin1Char(' ') + component +
                         QStringLiteral(" height"));
        };
        compare(actual->container, browser, u"under-arrow container");
        compare(actual->box.body, children.at(0).toObject(), u"under-arrow body");
        compare(actual->box.accent, children.at(1).toObject(), u"under-arrow glyph");
      }
      if (id == QLatin1String("fraction-radical")) {
        QVector<QJsonObject> radicals;
        collectNodes(tree, u"msqrt", &radicals);
        require(radicals.size() == 2,
                id + QStringLiteral(" browser radical nesting drifted"));
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto radicalOperations = operations
            ? childrenOfKind(*operations, math::MathSemanticKind::Radical)
            : QVector<const math::MathCssPaintOperation*>{};
        require(operations.has_value() && radicalOperations.size() == 2,
                id + QStringLiteral(" must expose two radical operations"));
        for (qsizetype index = 0; index < radicals.size(); ++index) {
          const auto* actual = std::get_if<math::MathCssRadicalOperation>(
              &radicalOperations.at(index)->payload);
          require(actual, id + QStringLiteral(" radical payload missing"));
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
          compare(actual->container, browser, u"radical container");
          compare(actual->body, children.at(0).toObject(), u"radical body");
          require(actual->radicalGlyph.glyphIndex != 0 &&
                      !actual->radicalGlyph.inkBounds.isEmpty() &&
                      !actual->radicalGlyph.clip.isEmpty() &&
                      !actual->radicalGlyph.target.isEmpty() &&
                      !actual->radicalRule.target.isEmpty(),
                  id + QStringLiteral(" radical paint operations are incomplete"));
        }
      }
      if (id.startsWith(QLatin1String("root-index"))) {
        QVector<QJsonObject> roots;
        collectNodes(tree, u"mroot", &roots);
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* radical = operation
            ? std::get_if<math::MathCssRadicalOperation>(&operation->payload)
            : nullptr;
        require(roots.size() == 1 && radical && radical->degreeNode,
                id + QStringLiteral(" degree operation is missing"));
        const QJsonArray children = roots.front().value(
            QStringLiteral("children")).toArray();
        require(children.size() == 2,
                id + QStringLiteral(" degree glyph ownership drifted"));
        if (id == QLatin1String("root-index")) {
          require(radical->degreeGlyphRuns.size() == 1 &&
                      radical->degreeGlyphRuns.front().text == QLatin1String("3") &&
                      qFuzzyCompare(radical->degreeGlyphRuns.front().fontScale,
                                    mathFont.constants()
                                        .scriptScriptPercentScaleDown),
                  id + QStringLiteral(" degree glyph ownership drifted"));
        } else {
          const math::MathCssPaintKind expectedKind =
              id.endsWith(QLatin1String("fraction"))
              ? math::MathCssPaintKind::Fraction
              : id.endsWith(QLatin1String("radical"))
                  ? math::MathCssPaintKind::Radical
                  : math::MathCssPaintKind::SupSub;
          require(radical->degreeGlyphRuns.isEmpty() &&
                      hasPaintKindPath(*operation,
                          {math::MathCssPaintKind::Radical, expectedKind}),
                  id + QStringLiteral(" recursive degree ownership drifted"));
        }
        const QJsonObject expectedDegree = children.at(1).toObject();
        const auto compareRect = [&](QRectF actual, const QJsonObject& expected,
                                     qreal tolerance, QStringView component) {
          near(actual.x(), expected.value(QStringLiteral("x")).toDouble(),
               tolerance, id + QLatin1Char(' ') + component + u" x");
          near(actual.y(), expected.value(QStringLiteral("y")).toDouble(),
               tolerance, id + QLatin1Char(' ') + component + u" y");
          near(actual.width(),
               expected.value(QStringLiteral("width")).toDouble(), tolerance,
               id + QLatin1Char(' ') + component + u" width");
          near(actual.height(),
               expected.value(QStringLiteral("height")).toDouble(), tolerance,
               id + QLatin1Char(' ') + component + u" height");
        };
        compareRect(radical->body, children.at(0).toObject(), 0.22,
                    u"radical body");
        near(radical->degree.x(),
             expectedDegree.value(QStringLiteral("x")).toDouble(), 0.22,
             id + QStringLiteral(" degree x"));
        near(radical->degree.y(),
             expectedDegree.value(QStringLiteral("y")).toDouble(), 0.22,
             id + QStringLiteral(" degree y"));
        near(radical->degree.width(),
             expectedDegree.value(QStringLiteral("width")).toDouble(), 0.22,
             id + QStringLiteral(" degree width"));
        near(radical->degree.height(),
             expectedDegree.value(QStringLiteral("height")).toDouble(), 0.22,
             id + QStringLiteral(" degree height"));
        const auto primitives = math::collectMathMlPaintPrimitives(*operation);
        QVector<math::MathMlPaintPrimitive> glyphPrimitives;
        QVector<math::MathMlPaintPrimitive> rulePrimitives;
        for (const auto& primitive : primitives) {
          if (primitive.operationPath == QLatin1String("root/radical/glyph"))
            glyphPrimitives.push_back(primitive);
          if (primitive.operationPath == QLatin1String("root/radical/rule"))
            rulePrimitives.push_back(primitive);
        }
        require(glyphPrimitives.size() == 1 && rulePrimitives.size() == 1,
                id + QStringLiteral(" radical primitive ownership drifted"));
        const QRectF nativeGlyphInk = primitiveInkBounds(
            glyphPrimitives, QSizeF(box.width, box.height));
        const QRectF nativeRuleInk = primitiveInkBounds(
            rulePrimitives, QSizeF(box.width, box.height));
        compareRect(nativeGlyphInk,
                    fixture.value(QStringLiteral("radicalGlyphInk")).toObject(),
                    1.35, u"radical glyph ink");
        compareRect(nativeRuleInk,
                    fixture.value(QStringLiteral("radicalRuleInk")).toObject(),
                    1.35, u"radical rule ink");
        require((id == QLatin1String("root-index")) ==
                    (std::count_if(
                    primitives.cbegin(), primitives.cend(),
                    [](const math::MathMlPaintPrimitive& primitive) {
                      return primitive.role ==
                          math::MathMlPaintPrimitiveRole::RadicalDegree;
                    }) == 1),
                id + QStringLiteral(" degree primitive coverage drifted"));
      }
      if (id == QLatin1String("sqrt-fraction")) {
        QVector<QJsonObject> fractions;
        collectNodes(tree, u"mfrac", &fractions);
        require(fractions.size() == 1,
                id + QStringLiteral(" browser fraction nesting drifted"));
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operations &&
                    operations->semanticKind() == math::MathSemanticKind::Radical &&
                    operations->children.size() == 1 &&
                    operations->children.front().semanticKind() ==
                        math::MathSemanticKind::Fraction,
                id + QStringLiteral(" reverse operation nesting drifted"));
        require(paintOperationCount(*operations) ==
                    semanticNodeCount(layout.root.get()),
                id + QStringLiteral(" operation ownership is incomplete"));
        const QRectF actual = operations->children.front().container();
        const QJsonObject browser = fractions.front();
        near(actual.x(), browser.value(QStringLiteral("x")).toDouble(), 0.22,
             id + QStringLiteral(" nested fraction x"));
        near(actual.y(), browser.value(QStringLiteral("y")).toDouble(), 0.22,
             id + QStringLiteral(" nested fraction y"));
        near(actual.width(), browser.value(QStringLiteral("width")).toDouble(),
             0.22, id + QStringLiteral(" nested fraction width"));
        near(actual.height(), browser.value(QStringLiteral("height")).toDouble(),
             0.22, id + QStringLiteral(" nested fraction height"));
      }
      if (id == QLatin1String("sqrt")) {
        QVector<QJsonObject> radicals;
        collectNodes(tree, u"msqrt", &radicals);
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operations &&
                    operations->semanticKind() == math::MathSemanticKind::Radical &&
                    childrenOfKind(*operations, math::MathSemanticKind::SupSub)
                            .size() == 2 &&
                    paintOperationCount(*operations) ==
                        semanticNodeCount(layout.root.get()),
                id + QStringLiteral(" radical/script ownership drifted"));
        require(radicals.size() == 1,
                id + QStringLiteral(" browser radical root drifted"));
        const auto* radical = std::get_if<math::MathCssRadicalOperation>(
            &operations->payload);
        const QJsonObject browser = radicals.front();
        const QJsonArray children = browser.value(
            QStringLiteral("children")).toArray();
        require(radical && children.size() == 1,
                id + QStringLiteral(" radical payload/body missing"));
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
        compare(radical->container, browser, u"radical container");
        compare(radical->body, children.at(0).toObject(), u"radical body");
      }
      if (id == QLatin1String("nested-script")) {
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operations &&
                    operations->semanticKind() == math::MathSemanticKind::SupSub &&
                    operations->children.size() == 1 &&
                    operations->children.front().semanticKind() ==
                        math::MathSemanticKind::SupSub &&
                    paintOperationCount(*operations) ==
                        semanticNodeCount(layout.root.get()),
                id + QStringLiteral(" recursive script ownership drifted"));
      }
      if (id == QLatin1String("radical-script-fraction") ||
          id == QLatin1String("script-radical-fraction") ||
          id == QLatin1String("fraction-cross-recursive")) {
        const auto operations = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operations && paintOperationCount(*operations) ==
                                  semanticNodeCount(layout.root.get()),
                id + QStringLiteral(" recursive operation coverage drifted"));
        if (id == QLatin1String("radical-script-fraction")) {
          require(hasOperationPath(
                      *operations,
                      {math::MathSemanticKind::Radical,
                       math::MathSemanticKind::SupSub,
                       math::MathSemanticKind::Fraction}),
                  id + QStringLiteral(" operation path drifted"));
        } else if (id == QLatin1String("script-radical-fraction")) {
          require(hasOperationPath(
                      *operations,
                      {math::MathSemanticKind::SupSub,
                       math::MathSemanticKind::Radical,
                       math::MathSemanticKind::Fraction}),
                  id + QStringLiteral(" operation path drifted"));
        } else {
          require(operations->semanticKind() ==
                      math::MathSemanticKind::Fraction &&
                      childrenOfKind(*operations,
                                     math::MathSemanticKind::Radical).size() == 1 &&
                      childrenOfKind(*operations,
                                     math::MathSemanticKind::SupSub).size() == 1,
                  id + QStringLiteral(" branch ownership drifted"));
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
        const auto glyphOperation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* glyphFraction = glyphOperation
            ? std::get_if<math::MathCssFractionPaint>(
                  &glyphOperation->payload)
            : nullptr;
        require(glyphFraction &&
                    !glyphFraction->numeratorGlyphRuns.isEmpty() &&
                    !glyphFraction->denominatorGlyphRuns.isEmpty(),
                id + QStringLiteral(" fraction glyph-run oracle is incomplete"));
        const qreal expectedRowScale = fractionBox->styleSize == 0
            ? 1.0 : fractionBox->styleSize == 1 ? 0.7 : 0.55;
        near(glyphFraction->numeratorGlyphRuns.front().fontScale,
             expectedRowScale, 0.0001,
             id + QStringLiteral(" fraction row style"));
        if (id == QLatin1String("genfrac-display-rule")) {
          require(glyphFraction->leftDelimiterGlyph &&
                      glyphFraction->rightDelimiterGlyph &&
                      glyphFraction->leftDelimiterGlyph->kind ==
                          math::MathCssVerticalGlyphKind::FixedVariant &&
                      glyphFraction->rightDelimiterGlyph->kind ==
                          math::MathCssVerticalGlyphKind::FixedVariant &&
                      glyphFraction->leftDelimiterGlyph->fixedGlyphIndex != 0 &&
                      glyphFraction->rightDelimiterGlyph->fixedGlyphIndex != 0 &&
                      glyphFraction->leftDelimiterGlyph->parts.isEmpty() &&
                      glyphFraction->rightDelimiterGlyph->parts.isEmpty() &&
                      !glyphFraction->leftDelimiterGlyph->inkBounds.isEmpty() &&
                      !glyphFraction->rightDelimiterGlyph->inkBounds.isEmpty(),
                  id + QStringLiteral(" fixed delimiter operations are missing"));
        }
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
          const auto operations = math::checkedMathMlPaintOperations(
              layout, kKatexRootFontSize);
          require(operations.has_value(),
                  id + QStringLiteral(" rule operation tree is missing"));
          near(fractionBox->rule.center().y(),
               fractionBox->fraction.top() + operations->lineAscent() -
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
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* accent = operation
            ? std::get_if<math::MathCssAccentOperation>(&operation->payload)
            : nullptr;
        QVector<QJsonObject> identifiers;
        collectNodes(tree, u"mi", &identifiers);
        require(accent && accent->bodyNode && accent->annotationNode &&
                    identifiers.size() == 3,
                id + QStringLiteral(" accent operation ownership drifted"));
        require(accent->bodyGlyphRuns.size() == 3 &&
                    accent->annotationGlyphRuns.size() == 1 &&
                    accent->glyph.scalePolicy ==
                        math::MathCssHorizontalScalePolicy::PreserveVariantScale,
                id + QStringLiteral(" glyph-run operation coverage drifted"));
        near(accent->bodyGlyphRuns.front().baselineOrigin.y(), 10.0, 0.02,
             id + QStringLiteral(" body glyph baseline"));
        const QJsonObject annotation = identifiers.back();
        near(accent->annotationContent.x(),
             annotation.value(QStringLiteral("x")).toDouble(), 0.22,
             id + QStringLiteral(" annotation content x"));
        near(accent->annotationContent.y(),
             annotation.value(QStringLiteral("y")).toDouble(), 0.22,
             id + QStringLiteral(" annotation content y"));
        near(accent->annotationContent.width(),
             annotation.value(QStringLiteral("width")).toDouble(), 0.22,
             id + QStringLiteral(" annotation content width"));
        near(accent->annotationContent.height(),
             annotation.value(QStringLiteral("height")).toDouble(), 0.22,
             id + QStringLiteral(" annotation content height"));
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
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operation && operation->kind() == math::MathCssPaintKind::Accent &&
                    std::get_if<math::MathCssAccentOperation>(
                        &operation->payload),
                id + QStringLiteral(" accent operation is missing"));
        const auto* accent = std::get_if<math::MathCssAccentOperation>(
            &operation->payload);
        require(accent->bodyGlyphRuns.size() == 3 &&
                    accent->annotationGlyphRuns.size() == 1,
                id + QStringLiteral(" glyph-run operation coverage drifted"));
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
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operation
            ? std::get_if<math::MathCssArrayOperation>(&operation->payload)
            : nullptr;
        require(array && array->leftDelimiterGlyph &&
                    !array->rightDelimiterGlyph &&
                    array->leftDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::Assembly &&
                    array->leftDelimiterGlyph->parts.size() == 7 &&
                    !array->leftDelimiterGlyph->inkBounds.isEmpty(),
                id + QStringLiteral(" vertical assembly operation is missing"));
        qreal previousOffset = -1.0;
        for (const auto& part : array->leftDelimiterGlyph->parts) {
          require(part.glyphIndex != 0 && part.offset > previousOffset &&
                      !part.inkBounds.isEmpty() &&
                      part.inkBounds.top() >= part.offset - 0.001 &&
                      part.fullAdvance > 0.0 &&
                      part.connectorOverlap >= 0.0,
                  id + QStringLiteral(" vertical assembly part drifted"));
          previousOffset = part.offset;
        }
        near(array->leftDelimiterGlyph->realizedExtent,
             array->leftDelimiterGlyph->selectionTarget, 0.001,
             id + QStringLiteral(" vertical assembly extent"));
      }
      if (id == QLatin1String("tall-paren-assembly") ||
          id == QLatin1String("tall-bracket-assembly")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operation
            ? std::get_if<math::MathCssArrayOperation>(&operation->payload)
            : nullptr;
        require(array && array->leftDelimiterGlyph &&
                    array->rightDelimiterGlyph &&
                    array->leftDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::Assembly &&
                    array->rightDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::Assembly &&
                    !array->leftDelimiterGlyph->parts.isEmpty() &&
                    !array->rightDelimiterGlyph->parts.isEmpty(),
                id + QStringLiteral(" paired vertical assemblies are missing"));
      }
      if (id == QLatin1String("tall-bar-assembly")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operation
            ? std::get_if<math::MathCssArrayOperation>(&operation->payload)
            : nullptr;
        require(array && array->leftDelimiterCharacter ==
                             QString(QChar(0x2223)) &&
                    array->rightDelimiterCharacter ==
                             QString(QChar(0x2223)) &&
                    array->leftDelimiterGlyph &&
                    array->rightDelimiterGlyph &&
                    array->leftDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::FixedVariant &&
                    array->rightDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::FixedVariant &&
                    array->leftDelimiterGlyph->parts.isEmpty() &&
                    array->rightDelimiterGlyph->parts.isEmpty() &&
                    array->leftDelimiter.width() >
                        array->leftDelimiterGlyph->inkBounds.width() * 2.0 &&
                    array->leftDelimiterGlyph->inkBounds.height() <
                        array->table.height() / 2.0,
                id + QStringLiteral(" MathML non-stretchy bar policy drifted"));
      }
      if (id == QLatin1String("nested-delimiter-array")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operation
            ? std::get_if<math::MathCssArrayOperation>(&operation->payload)
            : nullptr;
        require(array && array->leftDelimiterGlyph &&
                    array->rightDelimiterGlyph &&
                    array->leftDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::Assembly &&
                    array->rightDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::Assembly &&
                    childrenOfKind(*operation,
                                   math::MathSemanticKind::Fraction).size() == 2,
                id + QStringLiteral(" nested delimiter ownership drifted"));
      }
      if (id == QLatin1String("tall-double-bar")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operation
            ? std::get_if<math::MathCssArrayOperation>(&operation->payload)
            : nullptr;
        require(array && array->leftDelimiterCharacter ==
                             QString(QChar(0x2225)) &&
                    array->rightDelimiterCharacter ==
                             QString(QChar(0x2225)) &&
                    array->leftDelimiterGlyph &&
                    array->rightDelimiterGlyph &&
                    array->leftDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::FixedVariant &&
                    array->rightDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::FixedVariant &&
                    array->leftDelimiterGlyph->parts.isEmpty() &&
                    array->rightDelimiterGlyph->parts.isEmpty(),
                id + QStringLiteral(" MathML double-bar policy drifted"));
      }
      if (id == QLatin1String("tall-floor") ||
          id == QLatin1String("tall-ceil")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operation
            ? std::get_if<math::MathCssArrayOperation>(&operation->payload)
            : nullptr;
        require(array && array->leftDelimiterGlyph &&
                    array->rightDelimiterGlyph &&
                    array->leftDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::Assembly &&
                    array->rightDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::Assembly,
                id + QStringLiteral(" floor/ceiling assembly policy drifted"));
        require(array->leftDelimiterGlyph->parts.front().inkBounds.top() ==
                        0.0 &&
                    array->rightDelimiterGlyph->parts.front().inkBounds.top() ==
                        0.0,
                id + QStringLiteral(" assembly raster origin drifted"));
      }
      if (id == QLatin1String("tall-angle")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operation
            ? std::get_if<math::MathCssArrayOperation>(&operation->payload)
            : nullptr;
        require(array && array->leftDelimiterGlyph &&
                    array->rightDelimiterGlyph &&
                    array->leftDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::FixedVariant &&
                    array->rightDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::FixedVariant &&
                    array->leftDelimiterGlyph->realizedExtent <
                        array->table.height(),
                id + QStringLiteral(" angle fixed-variant policy drifted"));
      }
      if (id == QLatin1String("nullable-delimiter")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* array = operation
            ? std::get_if<math::MathCssArrayOperation>(&operation->payload)
            : nullptr;
        require(array && !array->leftDelimiterGlyph &&
                    array->rightDelimiterGlyph &&
                    array->rightDelimiterGlyph->kind ==
                        math::MathCssVerticalGlyphKind::Assembly,
                id + QStringLiteral(" nullable delimiter ownership drifted"));
      }
      if (id == QLatin1String("delimiter-recursive")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* fraction = operation
            ? std::get_if<math::MathCssFractionPaint>(&operation->payload)
            : nullptr;
        const bool ownsAccent = operation && std::any_of(
            operation->children.cbegin(), operation->children.cend(),
            [](const math::MathCssPaintOperation& child) {
              return child.kind() == math::MathCssPaintKind::Accent;
            });
        require(fraction && fraction->box.leftDelimiterCharacter ==
                                QString(QChar(0x27e8)) &&
                    fraction->box.rightDelimiterCharacter ==
                                QString(QChar(0x27e9)) &&
                    fraction->leftDelimiterGlyph &&
                    fraction->rightDelimiterGlyph &&
                    childrenOfKind(*operation,
                                   math::MathSemanticKind::Radical).size() == 1 &&
                    ownsAccent,
                id + QStringLiteral(" recursive fraction fence ownership drifted"));
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
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operation &&
                    hasPaintKindPath(*operation,
                        {math::MathCssPaintKind::Fraction,
                         math::MathCssPaintKind::Accent}),
                id + QStringLiteral(" fraction-to-accent operation path drifted: ") +
                    (operation ? paintKindTree(*operation)
                               : QStringLiteral("missing")));
      }
      if (id == QLatin1String("accent-fraction-recursive") ||
          id == QLatin1String("accent-radical-recursive") ||
          id == QLatin1String("accent-array-recursive")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operation && operation->kind() == math::MathCssPaintKind::Accent,
                id + QStringLiteral(" root accent operation is missing"));
        const math::MathCssPaintKind nestedKind =
            id == QLatin1String("accent-fraction-recursive")
                ? math::MathCssPaintKind::Fraction
                : id == QLatin1String("accent-radical-recursive")
                    ? math::MathCssPaintKind::Radical
                    : math::MathCssPaintKind::Array;
        require(hasPaintKindPath(
                    *operation, {math::MathCssPaintKind::Accent, nestedKind}),
                id + QStringLiteral(" recursive accent operation path drifted"));
        const auto* accent = std::get_if<math::MathCssAccentOperation>(
            &operation->payload);
        require(accent && accent->bodyGlyphRuns.isEmpty() &&
                    accent->annotationGlyphRuns.size() == 1,
                id + QStringLiteral(" recursive region ownership drifted"));
        if (id == QLatin1String("accent-array-recursive")) {
          near(accent->glyph.selectionTarget, accent->box.accent.width(),
               0.001, id + QStringLiteral(" array accent stretch target"));
          require(accent->glyph.kind ==
                      math::MathCssHorizontalGlyphKind::Assembly &&
                      accent->glyph.parts.size() == 7,
                  id + QStringLiteral(" horizontal assembly operation is missing"));
          qreal previousOffset = -1.0;
          const qreal connectorOverlap =
              accent->glyph.parts.front().connectorOverlap;
          QRectF assemblyInk;
          for (const auto& part : accent->glyph.parts) {
            require(part.glyphIndex != 0 && part.offset > previousOffset &&
                        part.fullAdvance > 0.0 &&
                        part.connectorOverlap >= 0.0 &&
                        !part.inkBounds.isEmpty(),
                    id + QStringLiteral(" horizontal assembly part drifted"));
            const QRectF positionedInk =
                part.inkBounds.translated(part.offset, 0.0);
            assemblyInk = assemblyInk.isNull()
                ? positionedInk : assemblyInk.united(positionedInk);
            previousOffset = part.offset;
          }
          for (qsizetype index = 0;
               index + 1 < accent->glyph.parts.size(); ++index)
            near(accent->glyph.parts.at(index).connectorOverlap,
                 connectorOverlap, 0.0001,
                 id + QStringLiteral(" uniform connector overlap"));
          near(accent->glyph.realizedExtent,
               accent->glyph.selectionTarget * accent->glyph.fontScale,
               0.001, id + QStringLiteral(" script-scaled assembly extent"));
          require(!assemblyInk.isEmpty() &&
                      accent->glyph.inkBounds == assemblyInk,
                  id + QStringLiteral(" assembly ink bounds drifted"));
          const auto arrayChild = std::find_if(
              operation->children.cbegin(), operation->children.cend(),
              [](const math::MathCssPaintOperation& child) {
                return child.kind() == math::MathCssPaintKind::Array;
              });
          const auto* array = arrayChild == operation->children.cend()
              ? nullptr : std::get_if<math::MathCssArrayOperation>(
                              &arrayChild->payload);
          QVector<QJsonObject> tables;
          QVector<QJsonObject> contents;
          collectNodes(tree, u"mtable", &tables);
          collectNodes(tree, u"mstyle", &contents);
          require(array && tables.size() == 1 &&
                      contents.size() == array->cells.size(),
                  id + QStringLiteral(" nested array operation is missing"));
          near(array->table.x(), tables.front().value(QStringLiteral("x")).toDouble(),
               0.22, id + QStringLiteral(" nested table x"));
          near(array->table.y(), tables.front().value(QStringLiteral("y")).toDouble(),
               0.22, id + QStringLiteral(" nested table y"));
          near(array->table.width(), tables.front().value(QStringLiteral("width")).toDouble(),
               0.22, id + QStringLiteral(" nested table width"));
          near(array->table.height(), tables.front().value(QStringLiteral("height")).toDouble(),
               0.22, id + QStringLiteral(" nested table height"));
          for (qsizetype index = 0; index < contents.size(); ++index) {
            near(array->cells.at(index).content.x(),
                 contents.at(index).value(QStringLiteral("x")).toDouble(), 0.22,
                 id + QStringLiteral(" nested cell content x"));
            near(array->cells.at(index).content.y(),
                 contents.at(index).value(QStringLiteral("y")).toDouble(), 0.22,
                 id + QStringLiteral(" nested cell content y"));
          }
        }
      }
      if (id == QLatin1String("accent-text-shaping")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* accent = operation
            ? std::get_if<math::MathCssAccentOperation>(&operation->payload)
            : nullptr;
        QString bodyText;
        qsizetype glyphCount = 0;
        if (accent) {
          for (const auto& run : accent->bodyGlyphRuns) {
            bodyText += run.text;
            glyphCount += run.glyphIndexes.size();
          }
        }
        require(accent && bodyText == QLatin1String("office") &&
                    glyphCount == bodyText.size(),
                id + QStringLiteral(" shaped accent body operation drifted: ") +
                    (operation ? paintKindTree(*operation)
                               : QStringLiteral("missing")) +
                    QStringLiteral(", text=%1, glyphs=%2")
                        .arg(bodyText).arg(glyphCount));
      }
      if (id == QLatin1String("accent-double-right-arrow")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* accent = operation
            ? std::get_if<math::MathCssAccentOperation>(&operation->payload)
            : nullptr;
        require(accent &&
                    accent->glyph.kind ==
                        math::MathCssHorizontalGlyphKind::Assembly &&
                    !accent->glyph.parts.isEmpty() &&
                    accent->glyph.selectionTarget > accent->glyph.target.width(),
                id + QStringLiteral(" overflowing horizontal assembly is missing"));
        near(accent->glyph.selectionTarget, accent->box.body.width(), 0.001,
             id + QStringLiteral(" assembly body-width target"));
        near(accent->glyph.target.width(), accent->box.accent.width(), 0.001,
             id + QStringLiteral(" logical operator width"));
      }
      if (id == QLatin1String("array-cell-accent-recursive") ||
          id == QLatin1String("radical-accent-recursive") ||
          id == QLatin1String("supsub-accent-recursive") ||
          id == QLatin1String("accent-accent-recursive") ||
          id == QLatin1String("fraction-array-accent-recursive") ||
          id == QLatin1String("accent-arrow-recursive")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        require(operation.has_value(),
                id + QStringLiteral(" recursive operation is missing"));
        bool covered = false;
        if (id == QLatin1String("array-cell-accent-recursive"))
          covered = hasPaintKindPath(
              *operation, {math::MathCssPaintKind::Array,
                           math::MathCssPaintKind::Accent});
        else if (id == QLatin1String("radical-accent-recursive"))
          covered = hasPaintKindPath(
              *operation, {math::MathCssPaintKind::Radical,
                           math::MathCssPaintKind::Accent});
        else if (id == QLatin1String("supsub-accent-recursive"))
          covered = hasPaintKindPath(
              *operation, {math::MathCssPaintKind::SupSub,
                           math::MathCssPaintKind::Accent});
        else if (id == QLatin1String("fraction-array-accent-recursive"))
          covered = hasPaintKindPath(
              *operation, {math::MathCssPaintKind::Fraction,
                           math::MathCssPaintKind::Array,
                           math::MathCssPaintKind::Accent});
        else
          covered = hasPaintKindPath(
              *operation, {math::MathCssPaintKind::Accent,
                           math::MathCssPaintKind::Accent});
        require(covered,
                id + QStringLiteral(" recursive operation path drifted: ") +
                    paintKindTree(*operation));
        if (id == QLatin1String("radical-accent-recursive") ||
            id == QLatin1String("accent-accent-recursive")) {
          const auto child = std::find_if(
              operation->children.cbegin(), operation->children.cend(),
              [](const math::MathCssPaintOperation& candidate) {
                return candidate.kind() == math::MathCssPaintKind::Accent;
              });
          require(child != operation->children.cend(),
                  id + QStringLiteral(" nested accent operation is missing"));
          const auto* accent = std::get_if<math::MathCssAccentOperation>(
              &child->payload);
          require(accent && accent->bodyGlyphRuns.size() == 1 &&
                      accent->annotationGlyphRuns.size() == 1,
                  id + QStringLiteral(" nested glyph-run ownership drifted"));
          near(accent->bodyGlyphRuns.front().fontScale, 1.0, 0.0001,
               id + QStringLiteral(" nested body style"));
          near(accent->annotationGlyphRuns.front().fontScale, 0.7, 0.0001,
               id + QStringLiteral(" nested annotation style"));
          near(accent->box.accent.height(), 3.0, 0.001,
               id + QStringLiteral(" nested brace style height"));
        }
      }

      if (id == QLatin1String("accent-mixed-fraction-body") ||
          id == QLatin1String("accent-mixed-radical-body") ||
          id == QLatin1String("accent-mixed-fraction-annotation")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* accent = operation
            ? std::get_if<math::MathCssAccentOperation>(&operation->payload)
            : nullptr;
        require(accent,
                id + QStringLiteral(" mixed accent operation is missing"));
        QString bodyText;
        for (const auto& run : accent->bodyGlyphRuns) bodyText += run.text;
        QString annotationText;
        for (const auto& run : accent->annotationGlyphRuns)
          annotationText += run.text;
        const math::MathCssPaintKind nestedKind =
            id == QLatin1String("accent-mixed-radical-body")
                ? math::MathCssPaintKind::Radical
                : math::MathCssPaintKind::Fraction;
        require(hasPaintKindPath(
                    *operation,
                    {math::MathCssPaintKind::Accent, nestedKind}),
                id + QStringLiteral(" mixed recursive ownership drifted: ") +
                    paintKindTree(*operation));
        if (id == QLatin1String("accent-mixed-fraction-body")) {
          require(bodyText == QLatin1String("x++y") &&
                      annotationText == QLatin1String("n"),
                  id + QStringLiteral(" partial body glyph ownership drifted: ") +
                      bodyText);
        } else if (id == QLatin1String("accent-mixed-radical-body")) {
          require(bodyText == QLatin1String("x+") && annotationText.isEmpty(),
                  id + QStringLiteral(" partial radical body ownership drifted: ") +
                      bodyText);
        } else {
          QVector<QJsonObject> rows;
          collectNodes(tree, u"mrow", &rows);
          require(!rows.isEmpty(),
                  id + QStringLiteral(" annotation row is missing"));
          const QJsonObject browserAnnotation = rows.back();
          near(accent->annotationContent.x(),
               browserAnnotation.value(QStringLiteral("x")).toDouble(),
               0.22, id + QStringLiteral(" annotation x"));
          near(accent->annotationContent.y(),
               browserAnnotation.value(QStringLiteral("y")).toDouble(),
               0.22, id + QStringLiteral(" annotation y"));
          near(accent->annotationContent.width(),
               browserAnnotation.value(QStringLiteral("width")).toDouble(),
               0.22, id + QStringLiteral(" annotation width"));
          near(accent->annotationContent.height(),
               browserAnnotation.value(QStringLiteral("height")).toDouble(),
               0.22, id + QStringLiteral(" annotation height"));
          require(bodyText == QLatin1String("x") &&
                      annotationText == QLatin1String("i+"),
                  id + QStringLiteral(" partial annotation ownership drifted: ") +
                      annotationText);
        }
      }

      if (id == QLatin1String("root-mixed-fraction") ||
          id == QLatin1String("root-mixed-radical") ||
          id == QLatin1String("root-multiple-semantics") ||
          id == QLatin1String("root-mixed-accent") ||
          id == QLatin1String("root-mixed-array") ||
          id == QLatin1String("root-mixed-left-right") ||
          id == QLatin1String("root-double-fraction") ||
          id == QLatin1String("root-all-paint-kinds") ||
          id == QLatin1String("root-mixed-product-limits") ||
          id == QLatin1String("root-mixed-coproduct-limits") ||
          id == QLatin1String("root-mixed-triple-integral") ||
          id == QLatin1String("root-mixed-cjk-fraction") ||
          id == QLatin1String("root-mixed-rtl-fraction")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* row = operation
            ? std::get_if<math::MathCssRowOperation>(&operation->payload)
            : nullptr;
        QString glyphText;
        if (row)
          for (const auto& run : row->glyphRuns) glyphText += run.text;
        const qsizetype expectedChildren =
            id == QLatin1String("root-multiple-semantics") ||
                    id == QLatin1String("root-all-paint-kinds")
                ? 3
                : id == QLatin1String("root-double-fraction") ? 2 : 1;
        const QString expectedGlyphText =
            id == QLatin1String("root-double-fraction")
                ? QStringLiteral("+")
                : id == QLatin1String("root-mixed-cjk-fraction")
                    ? QStringLiteral("p+中文++q")
                : id == QLatin1String("root-mixed-rtl-fraction")
                    ? QStringLiteral("p+سلام++q")
                : expectedChildren == 1 ? QStringLiteral("p++q")
                                        : QStringLiteral("++");
        require(row && row->node == layout.root.get() &&
                    row->container == QRectF(0.0, 0.0, box.width,
                                              box.height) &&
                    operation->children.size() == expectedChildren &&
                    glyphText == expectedGlyphText,
                id + QStringLiteral(" root row ownership drifted: ") +
                    (operation ? paintKindTree(*operation)
                               : QStringLiteral("missing")) +
                    QStringLiteral(", glyphs=%1").arg(glyphText));
        qreal previousRight = -1.0;
        for (const auto& child : operation->children) {
          require(child.container().left() >= previousRight - 0.001 &&
                      child.container().left() >= row->container.left() - 0.22 &&
                      child.container().right() <= row->container.right() + 0.22 &&
                      child.container().top() >= row->container.top() - 1.1 &&
                      child.container().bottom() <= row->container.bottom() + 1.1,
                  id + QStringLiteral(" root child order drifted: previous=%1, child=%2, root=%3")
                           .arg(previousRight)
                           .arg(QString::fromUtf8(QJsonDocument(
                               QJsonObject{{QStringLiteral("operation"),
                                            child.toJson()}})
                                                     .toJson(QJsonDocument::Compact)))
                           .arg(QStringLiteral("%1,%2 %3x%4")
                                    .arg(row->container.x())
                                    .arg(row->container.y())
                                    .arg(row->container.width())
                                    .arg(row->container.height())));
          previousRight = child.container().right();
        }
        if (id == QLatin1String("root-multiple-semantics")) {
          require(operation->children.at(0).kind() ==
                      math::MathCssPaintKind::Radical &&
                      operation->children.at(1).kind() ==
                          math::MathCssPaintKind::Fraction &&
                      operation->children.at(2).kind() ==
                          math::MathCssPaintKind::SupSub,
                  id + QStringLiteral(" semantic child order drifted: ") +
                      paintKindTree(*operation));
        } else if (id == QLatin1String("root-all-paint-kinds")) {
          require(operation->children.at(0).kind() ==
                      math::MathCssPaintKind::Accent &&
                      operation->children.at(1).kind() ==
                          math::MathCssPaintKind::LeftRight &&
                      operation->children.at(2).kind() ==
                          math::MathCssPaintKind::Array,
                  id + QStringLiteral(" paint-kind child order drifted: ") +
                      paintKindTree(*operation));
        } else if (id == QLatin1String("root-double-fraction")) {
          require(operation->children.at(0).kind() ==
                      math::MathCssPaintKind::Fraction &&
                      operation->children.at(1).kind() ==
                          math::MathCssPaintKind::Fraction,
                  id + QStringLiteral(" repeated child order drifted: ") +
                      paintKindTree(*operation));
        }
        if (id == QLatin1String("root-mixed-accent")) {
          QVector<QJsonObject> movers;
          QVector<QJsonObject> identifiers;
          collectNodes(tree, u"mover", &movers);
          collectNodes(tree, u"mi", &identifiers);
          const auto* accent = std::get_if<math::MathCssAccentOperation>(
              &operation->children.front().payload);
          require(accent && movers.size() == 2 && identifiers.size() == 4,
                  id + QStringLiteral(" browser accent structure drifted"));
          const QJsonObject outerMover = movers.front();
          const QJsonObject body = identifiers.at(1);
          near(accent->container.x(),
               outerMover.value(QStringLiteral("x")).toDouble(), 0.22,
               id + QStringLiteral(" outer mover x"));
          near(accent->container.y(),
               outerMover.value(QStringLiteral("y")).toDouble(), 0.22,
               id + QStringLiteral(" outer mover y"));
          near(accent->container.height(),
               outerMover.value(QStringLiteral("height")).toDouble(), 0.22,
               id + QStringLiteral(" outer mover height"));
          near(accent->box.body.y(),
               body.value(QStringLiteral("y")).toDouble(), 0.22,
               id + QStringLiteral(" row-aligned body y"));
          require(!row->glyphRuns.isEmpty() &&
                      !accent->bodyGlyphRuns.isEmpty(),
                  id + QStringLiteral(" row/body glyph runs are missing"));
          near(row->glyphRuns.front().baselineOrigin.y(),
               accent->bodyGlyphRuns.front().baselineOrigin.y(), 0.001,
               id + QStringLiteral(" sibling body baseline"));
        }
        if (id == QLatin1String("root-mixed-radical")) {
          QVector<QJsonObject> identifiers;
          collectNodes(tree, u"mi", &identifiers);
          const auto* radical = std::get_if<math::MathCssRadicalOperation>(
              &operation->children.front().payload);
          require(radical && !radical->bodyGlyphRuns.isEmpty() &&
                      identifiers.size() == 3,
                  id + QStringLiteral(" radical body structure drifted"));
          near(radical->body.y(),
               identifiers.at(1).value(QStringLiteral("y")).toDouble(),
               0.22, id + QStringLiteral(" radical body y"));
          near(row->glyphRuns.front().baselineOrigin.y(),
               radical->bodyGlyphRuns.front().baselineOrigin.y(), 0.001,
               id + QStringLiteral(" radical sibling baseline"));
        }
        if (id == QLatin1String("root-mixed-cjk-fraction") ||
            id == QLatin1String("root-mixed-rtl-fraction")) {
          const QString expectedFamily =
              id == QLatin1String("root-mixed-cjk-fraction")
                  ? QStringLiteral("Noto Sans CJK SC")
                  : QStringLiteral("Noto Sans Arabic");
          const auto fallbackRun = std::find_if(
              row->glyphRuns.cbegin(), row->glyphRuns.cend(),
              [&](const math::MathCssGlyphRunOperation& run) {
                return run.fontFamily == expectedFamily;
              });
          require(fallbackRun != row->glyphRuns.cend() &&
                      fallbackRun->rawFont.isValid() &&
                      !fallbackRun->glyphIndexes.contains(0) &&
                      fallbackRun->glyphIndexes.size() ==
                          fallbackRun->positions.size(),
                  id + QStringLiteral(" fixed fallback glyph run drifted"));
        }
      }

      if (id == QLatin1String("operator-name")) {
        const auto operation = math::checkedMathMlPaintOperations(
            layout, kKatexRootFontSize);
        const auto* glyphGroup = operation
            ? std::get_if<math::MathCssGlyphRunGroupOperation>(
                  &operation->payload)
            : nullptr;
        require(glyphGroup && operation->children.isEmpty(),
                id + QStringLiteral(" root glyph-run operation is missing"));
        const auto rank = std::find_if(
            glyphGroup->runs.cbegin(), glyphGroup->runs.cend(),
            [](const math::MathCssGlyphRunOperation& run) {
              return run.text == QLatin1String("rank");
            });
        require(rank != glyphGroup->runs.cend() &&
                    rank->glyphIndexes.size() == 4 &&
                    rank->positions.size() == 4 && rank->advance > 0.0,
                id + QStringLiteral(" multi-glyph run drifted"));
        require(std::is_sorted(
                    rank->positions.cbegin(), rank->positions.cend(),
                    [](QPointF left, QPointF right) {
                      return left.x() < right.x();
                    }),
                id + QStringLiteral(" glyph positions are not monotonic"));

        math::MathLayoutResult complexShaping;
        complexShaping.root = std::make_unique<math::MathRenderNode>();
        complexShaping.root->kind = math::MathRenderKind::Span;
        complexShaping.root->width = 16.0;
        complexShaping.root->height = 12.0;
        complexShaping.root->depth = 4.0;
        auto combining = std::make_unique<math::MathRenderNode>();
        combining->kind = math::MathRenderKind::Symbol;
        combining->text = QStringLiteral("a\u0301");
        combining->width = 16.0;
        combining->height = 12.0;
        combining->depth = 4.0;
        complexShaping.root->children.push_back(std::move(combining));
        complexShaping.size = QSizeF(16.0, 16.0);
        complexShaping.naturalSize = complexShaping.size;
        complexShaping.baseline = 12.0;
        const auto shapedOperation = math::checkedMathMlPaintOperations(
            complexShaping, kKatexRootFontSize);
        const auto* shapedGroup = shapedOperation
            ? std::get_if<math::MathCssGlyphRunGroupOperation>(
                  &shapedOperation->payload)
            : nullptr;
        require(shapedGroup && shapedGroup->runs.size() == 1 &&
                    shapedGroup->runs.front().text == QStringLiteral("a\u0301") &&
                    shapedGroup->runs.front().glyphIndexes.size() == 1 &&
                    shapedGroup->runs.front().glyphIndexes.size() ==
                        shapedGroup->runs.front().positions.size(),
                id + QStringLiteral(" complex shaping operation is missing"));
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

      const auto paintBuild = math::buildMathMlPaintOperations(
          layout, kKatexRootFontSize);
      const auto& paintOperation = paintBuild.operation;
      if (paintOperation) {
        require(!paintBuild.failure.has_value(),
                id + QStringLiteral(" successful paint build has a failure"));
        require(hasExplicitRegionOwnership(*paintOperation),
                id + QStringLiteral(" has an unowned paint region: ") +
                    paintKindTree(*paintOperation));
        const QHash<QString, QSet<QString>> fallbackFamilies{
            {QStringLiteral("text-hebrew"),
             {QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("text-arabic-hebrew"),
             {QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("text-bidi-digits-punctuation"),
             {QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("text-bidi-isolates"),
             {QStringLiteral("Noto Sans Arabic")}},
            {QStringLiteral("fraction-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic")}},
            {QStringLiteral("radical-fallback-text"),
             {QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("supsub-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic")}},
            {QStringLiteral("accent-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("array-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("limits-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic")}},
            {QStringLiteral("limits-fallback-recursive"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("under-accent-fallback-text"),
             {QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans CJK SC")}},
            {QStringLiteral("delimiter-assembly-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("product-fallback-limits"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic")}},
            {QStringLiteral("coproduct-fallback-limits"),
             {QStringLiteral("Noto Sans Hebrew"),
              QStringLiteral("Noto Sans CJK SC")}},
            {QStringLiteral("over-arrow-fallback-text"),
             {QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans CJK SC")}},
            {QStringLiteral("under-arrow-fallback-text"),
             {QStringLiteral("Noto Sans Hebrew"),
              QStringLiteral("Noto Sans CJK SC")}},
            {QStringLiteral("brace-assembly-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("bracket-assembly-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans Hebrew")}},
            {QStringLiteral("angle-assembly-fallback-text"),
             {QStringLiteral("Noto Sans CJK SC"),
              QStringLiteral("Noto Sans Arabic"),
              QStringLiteral("Noto Sans Hebrew")}},
        };
        if (const auto expectedFamilies = fallbackFamilies.constFind(id);
            expectedFamilies != fallbackFamilies.cend()) {
          QVector<const math::MathCssGlyphRunOperation*> runs;
          collectGlyphRuns(*paintOperation, &runs);
          QSet<QString> actual;
          for (const auto* run : runs) {
            if (!run->fontFamily.isEmpty()) actual.insert(run->fontFamily);
            require(!run->rawFont.isValid() ||
                        (!run->glyphIndexes.contains(0) &&
                         run->glyphIndexes.size() == run->positions.size()),
                    id + QStringLiteral(" fallback glyph data drifted"));
          }
          require(std::all_of(expectedFamilies->cbegin(),
                              expectedFamilies->cend(),
                              [&](const QString& family) {
                                return actual.contains(family);
                              }),
                  id + QStringLiteral(" fallback family ownership drifted: ") +
                      actual.values().join(","));
          QVector<QJsonObject> browserTextRuns;
          collectNodes(tree, u"mtext", &browserTextRuns);
          require(!browserTextRuns.isEmpty(),
                  id + QStringLiteral(" browser mtext structure regressed"));
          if (id == QLatin1String("fraction-fallback-text"))
            require(paintOperation->kind() == math::MathCssPaintKind::Fraction,
                    id + QStringLiteral(" fraction operation was bypassed"));
          if (id == QLatin1String("radical-fallback-text"))
            require(paintOperation->kind() == math::MathCssPaintKind::Radical,
                    id + QStringLiteral(" radical operation was bypassed"));
          if (id == QLatin1String("supsub-fallback-text"))
            require(paintOperation->kind() == math::MathCssPaintKind::SupSub,
                    id + QStringLiteral(" script operation was bypassed"));
          if (id == QLatin1String("accent-fallback-text"))
            require(paintOperation->kind() ==
                            math::MathCssPaintKind::Accent ||
                        std::any_of(
                            paintOperation->children.cbegin(),
                            paintOperation->children.cend(),
                            [](const math::MathCssPaintOperation& child) {
                              return child.kind() ==
                                  math::MathCssPaintKind::Accent;
                            }),
                    id + QStringLiteral(" accent operation was bypassed"));
          if (id == QLatin1String("array-fallback-text"))
            require(paintOperation->kind() == math::MathCssPaintKind::Array,
                    id + QStringLiteral(" array operation was bypassed"));
          if (id == QLatin1String("limits-fallback-text") ||
              id == QLatin1String("limits-fallback-recursive"))
            require(paintOperation->kind() == math::MathCssPaintKind::SupSub,
                    id + QStringLiteral(" limits operation was bypassed"));
          if (id == QLatin1String("under-accent-fallback-text"))
            require(paintOperation->kind() == math::MathCssPaintKind::Accent ||
                        paintOperation->kind() == math::MathCssPaintKind::SupSub,
                    id + QStringLiteral(" under-accent operation was bypassed"));
          if (id == QLatin1String("delimiter-assembly-fallback-text"))
          {
            const auto* array = std::get_if<math::MathCssArrayOperation>(
                &paintOperation->payload);
            require(array && array->leftDelimiterGlyph &&
                        array->rightDelimiterGlyph &&
                        array->leftDelimiterGlyph->kind ==
                            math::MathCssVerticalGlyphKind::Assembly &&
                        array->rightDelimiterGlyph->kind ==
                            math::MathCssVerticalGlyphKind::Assembly &&
                        !array->leftDelimiterGlyph->parts.isEmpty() &&
                        !array->rightDelimiterGlyph->parts.isEmpty(),
                    id + QStringLiteral(" delimiter assembly was bypassed"));
          }
          if (id == QLatin1String("product-fallback-limits") ||
              id == QLatin1String("coproduct-fallback-limits"))
            require(paintOperation->kind() == math::MathCssPaintKind::SupSub,
                    id + QStringLiteral(" operator limits were bypassed"));
          if (id == QLatin1String("over-arrow-fallback-text") ||
              id == QLatin1String("under-arrow-fallback-text"))
            require(paintOperation->kind() == math::MathCssPaintKind::Accent,
                    id + QStringLiteral(" arrow accent was bypassed"));
          if (id == QLatin1String("brace-assembly-fallback-text") ||
              id == QLatin1String("bracket-assembly-fallback-text") ||
              id == QLatin1String("angle-assembly-fallback-text")) {
            const auto* array = std::get_if<math::MathCssArrayOperation>(
                &paintOperation->payload);
            require(array && array->leftDelimiterGlyph &&
                        array->rightDelimiterGlyph,
                    id + QStringLiteral(" fence operation was bypassed"));
          }
        }
        QJsonObject goldenCase;
        goldenCase.insert(QStringLiteral("id"), id);
        goldenCase.insert(
            QStringLiteral("operation"),
            semanticPaintOperationJson(paintOperation->toJson()));
        paintOperationGolden.append(goldenCase);
      } else {
        require(paintBuild.failure.has_value(),
                id + QStringLiteral(" missing structured paint failure"));
        QJsonObject failure = paintBuild.failure->toJson();
        failure.insert(QStringLiteral("id"), id);
        paintFailureGolden.append(failure);
      }
    }
    require(paintOperationGolden.size() == 167,
            QStringLiteral("MathML paint operation coverage regressed: %1")
                .arg(paintOperationGolden.size()));
    const QJsonArray expectedPaintFailures;
    require(paintFailureGolden == expectedPaintFailures,
            QStringLiteral("MathML whole-tree fallback matrix changed: %1")
                .arg(QString::fromUtf8(
                    QJsonDocument(paintFailureGolden)
                        .toJson(QJsonDocument::Compact))));
    const math::MathLayoutResult invalidLayout;
    const auto invalidLayoutBuild = math::buildMathMlPaintOperations(
        invalidLayout, kKatexRootFontSize);
    require(invalidLayoutBuild.failure &&
                invalidLayoutBuild.failure->code ==
                    math::MathMlPaintFailureCode::InvalidLayout,
            QStringLiteral("MathML invalid-layout diagnostic drifted"));
    const auto validProbe = renderer.render(
        QStringLiteral("x"), kKatexRootFontSize, Qt::black, true);
    const auto invalidFontBuild = math::buildMathMlPaintOperations(
        validProbe, 0.0);
    require(invalidFontBuild.failure &&
                invalidFontBuild.failure->code ==
                    math::MathMlPaintFailureCode::InvalidFontSize,
            QStringLiteral("MathML invalid-font-size diagnostic drifted"));
    require(math::mathMlPaintFailureCodeName(
                invalidFontBuild.failure->code) ==
                QLatin1String("invalid-font-size"),
            QStringLiteral("MathML failure code name drifted"));
    const QString formattedFailure =
        math::formatMathMlPaintFailure(*invalidFontBuild.failure);
    require(formattedFailure.contains(
                QStringLiteral("[invalid-font-size]")) &&
                formattedFailure.contains(invalidFontBuild.failure->nodePath),
            QStringLiteral("MathML structured failure formatting drifted"));
    try {
      throw math::MathMlPaintError(*invalidFontBuild.failure);
    } catch (const math::MathMlPaintError& error) {
      require(error.failure().toJson() == invalidFontBuild.failure->toJson() &&
                  QString::fromUtf8(error.what()) == formattedFailure,
              QStringLiteral("MathML paint exception lost diagnostics"));
    }
    const QByteArray paintOperationJson =
        QJsonDocument(paintOperationGolden).toJson(QJsonDocument::Compact);
    const QString paintOperationHash = QString::fromLatin1(
        QCryptographicHash::hash(paintOperationJson,
                                 QCryptographicHash::Sha256).toHex());
    const QString expectedPaintOperationHash = QStringLiteral(
        "f9506692e95485c1ffe407f8453b8e4ba6df1d8ef90ddad37b311e378676e0c4");
    if (paintOperationHash != expectedPaintOperationHash ||
        qEnvironmentVariableIsSet("MUFFIN_MATH_DUMP_PAINT_HASHES")) {
      static const QSet<QString> diagnosticCases = {
          QStringLiteral("relations"),
          QStringLiteral("integral-limits"),
          QStringLiteral("tall-paren-assembly"),
          QStringLiteral("text-bidi-digits-punctuation"),
          QStringLiteral("over-arrow-fallback-text"),
      };
      for (const QJsonValue& value : paintOperationGolden) {
        const QJsonObject goldenCase = value.toObject();
        const QByteArray json = QJsonDocument(goldenCase)
                                    .toJson(QJsonDocument::Compact);
        const QByteArray hash = QCryptographicHash::hash(
            json, QCryptographicHash::Sha256).toHex();
        qWarning().noquote()
            << "MathML paint case hash"
            << goldenCase.value(QStringLiteral("id")).toString()
            << hash;
        if (diagnosticCases.contains(
                goldenCase.value(QStringLiteral("id")).toString()))
          qWarning().noquote() << "MathML paint case json"
                               << json.toBase64();
      }
    }
    require(paintOperationHash == expectedPaintOperationHash,
            QStringLiteral("MathML paint operation golden changed: %1")
                .arg(paintOperationHash));
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
