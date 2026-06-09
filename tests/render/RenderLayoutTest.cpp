#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "math/MathBuilder.h"
#include "math/MathDelimiter.h"
#include "math/MathDimension.h"
#include "math/MathFontMetrics.h"
#include "math/MathFunctionRegistry.h"
#include "math/MathLayoutTree.h"
#include "math/MathMacroExpander.h"
#include "math/MathParseError.h"
#include "math/MathParser.h"
#include "math/MathRenderer.h"
#include "math/MathSvgGeometry.h"
#include "math/MathSymbols.h"
#include "render/TreeSitterHighlighter.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QTemporaryDir>

#include <algorithm>
#include <functional>

using namespace muffin;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) {
    fail(message);
  }
}

void runTest(const char* name, const std::function<void()>& test) {
  qInfo().noquote() << QStringLiteral("RUN %1").arg(QString::fromUtf8(name));
  test();
}

void testThemeCodeFontFallbackOrder() {
  const RenderTheme theme = RenderTheme::github();
  const QFont codeFont = theme.codeFont();
  require(!codeFont.family().isEmpty(), QStringLiteral("code font should resolve to an available family"));
  require(codeFont.styleHint() == QFont::Monospace, QStringLiteral("code font should keep monospace style hint"));
  require(qAbs(codeFont.pointSizeF() * 96.0 / 72.0 - 14.4) < 0.01, QStringLiteral("code font should be 14.4 CSS px at 100% zoom"));
  require(qAbs(theme.codeLineHeight() - 23.04) < 0.01, QStringLiteral("code line height should be 23.04 px at 100% zoom"));
}

void testThemeCodeHighlightPalette() {
  const RenderTheme theme = RenderTheme::github();
  require(theme.codeHighlightColor(CodeHighlightRole::Keyword).name() == QStringLiteral("#9b008b"),
          QStringLiteral("light code keyword color should match Typora-like purple"));
  require(theme.codeHighlightColor(CodeHighlightRole::Function).name() == QStringLiteral("#0000a8"),
          QStringLiteral("light code function color should match Typora-like blue"));
  require(theme.codeHighlightColor(CodeHighlightRole::String).name() == QStringLiteral("#a31515"),
          QStringLiteral("light code string color should match Typora-like red"));
  require(theme.codeHighlightColor(CodeHighlightRole::Preprocessor).name() == theme.textColor().name(),
          QStringLiteral("light code preprocessor color should stay close to plain text"));
  require(theme.codeHighlightColor(CodeHighlightRole::Type).name() == QStringLiteral("#008000"),
          QStringLiteral("light code type color should match Typora-like green"));
}

int changedPixelCount(const QImage& image, QColor background);
QRect imageInkBounds(const QImage& image, QColor background);
const math::MathRenderNode& nativeInkRoot(const math::MathRenderNode& root);

QString readFixture(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("Could not open fixture: %1").arg(path));
  return QString::fromUtf8(file.readAll());
}

QJsonArray readJsonArrayFixture(const QString& path) {
  const QJsonDocument document = QJsonDocument::fromJson(readFixture(path).toUtf8());
  require(document.isArray(), QStringLiteral("Fixture should be a JSON array: %1").arg(path));
  return document.array();
}

QJsonArray readOptionalJsonArrayFixture(const QString& path) {
  if (!QFileInfo::exists(path)) {
    return {};
  }
  return readJsonArrayFixture(path);
}

QMap<QString, QJsonObject> readObjectArrayById(const QString& path) {
  QMap<QString, QJsonObject> byId;
  const QJsonArray entries = readOptionalJsonArrayFixture(path);
  for (const QJsonValue& value : entries) {
    require(value.isObject(), QStringLiteral("Golden metrics entry should be an object: %1").arg(path));
    const QJsonObject object = value.toObject();
    const QString id = object.value(QStringLiteral("id")).toString();
    require(!id.isEmpty(), QStringLiteral("Golden metrics entry should include id: %1").arg(path));
    byId.insert(id, object);
  }
  return byId;
}

QString katexGoldenPathForFixture(const QString& fixturePath, const QString& goldenName, const QString& fileName) {
  QDir testsDir(QFileInfo(fixturePath).absoluteDir());
  require(testsDir.cdUp() && testsDir.cdUp(), QStringLiteral("Could not resolve tests directory from %1").arg(fixturePath));
  return testsDir.filePath(QStringLiteral("golden/%1/%2").arg(goldenName, fileName));
}

QString katexMetricsPathForFixture(const QString& fixturePath, const QString& goldenName = QStringLiteral("katex")) {
  return katexGoldenPathForFixture(fixturePath, goldenName, QStringLiteral("metrics.json"));
}

QString katexBBoxPathForFixture(const QString& fixturePath, const QString& goldenName = QStringLiteral("katex")) {
  return katexGoldenPathForFixture(fixturePath, goldenName, QStringLiteral("bbox.json"));
}

QString katexGlyphsPathForFixture(const QString& fixturePath, const QString& goldenName = QStringLiteral("katex")) {
  return katexGoldenPathForFixture(fixturePath, goldenName, QStringLiteral("glyphs.json"));
}

const MarkdownNode* findFirstBlock(const MarkdownNode& node, BlockType type) {
  if (node.type() == type) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findFirstBlock(*child, type)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findBlockWithLiteral(const MarkdownNode& node, BlockType type, const QString& literalPart) {
  if (node.type() == type && node.literal().contains(literalPart)) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findBlockWithLiteral(*child, type, literalPart)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findFirstTableCell(const MarkdownNode& node) {
  if (node.type() == BlockType::TableCell) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findFirstTableCell(*child)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findListItemWithText(const MarkdownNode& node, const QString& text) {
  if (node.type() == BlockType::ListItem) {
    if (node.literal().contains(text)) {
      return &node;
    }
    for (const auto& child : node.children()) {
      if (child->literal().contains(text)) {
        return &node;
      }
    }
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findListItemWithText(*child, text)) {
      return found;
    }
  }
  return nullptr;
}

void collectListItems(const MarkdownNode& node, QVector<const MarkdownNode*>& items) {
  if (node.type() == BlockType::ListItem) {
    items.push_back(&node);
  }
  for (const auto& child : node.children()) {
    collectListItems(*child, items);
  }
}

std::unique_ptr<math::MathLayoutNode> makeTestLayoutBox(qreal width, qreal height, qreal depth) {
  auto node = std::make_unique<math::MathLayoutNode>();
  node->kind = math::MathLayoutKind::Span;
  node->renderKind = math::MathRenderKind::Span;
  node->width = width;
  node->height = height;
  node->depth = depth;
  return node;
}

bool renderTreeContainsValue(const QJsonObject& node, const QString& value) {
  for (auto it = node.constBegin(); it != node.constEnd(); ++it) {
    if (it.value().isString() && it.value().toString() == value) {
      return true;
    }
  }
  const QJsonArray children = node.value(QStringLiteral("children")).toArray();
  for (const QJsonValue& child : children) {
    if (child.isObject() && renderTreeContainsValue(child.toObject(), value)) {
      return true;
    }
  }
  return false;
}

bool renderTreeContainsClass(const QJsonObject& node, const QString& className) {
  const QJsonArray classes = node.value(QStringLiteral("classes")).toArray();
  for (const QJsonValue& value : classes) {
    if (value.toString() == className) {
      return true;
    }
  }
  const QJsonArray children = node.value(QStringLiteral("children")).toArray();
  for (const QJsonValue& child : children) {
    if (child.isObject() && renderTreeContainsClass(child.toObject(), className)) {
      return true;
    }
  }
  return false;
}

bool findRenderTreeText(const QJsonObject& node, const QString& text, QJsonObject& out) {
  if (node.value(QStringLiteral("text")).toString() == text) {
    out = node;
    return true;
  }
  const QJsonArray children = node.value(QStringLiteral("children")).toArray();
  for (const QJsonValue& child : children) {
    if (!child.isObject()) {
      continue;
    }
    if (findRenderTreeText(child.toObject(), text, out)) {
      return true;
    }
  }
  return false;
}

bool findRenderTreeClass(const QJsonObject& node, const QString& className, QJsonObject& out) {
  const QJsonArray classes = node.value(QStringLiteral("classes")).toArray();
  for (const QJsonValue& value : classes) {
    if (value.toString() == className) {
      out = node;
      return true;
    }
  }
  const QJsonArray children = node.value(QStringLiteral("children")).toArray();
  for (const QJsonValue& child : children) {
    if (!child.isObject()) {
      continue;
    }
    if (findRenderTreeClass(child.toObject(), className, out)) {
      return true;
    }
  }
  return false;
}

bool jsonHasNumber(const QJsonObject& object, const QString& key) {
  return object.contains(key) && object.value(key).isDouble();
}

qreal jsonNumber(const QJsonObject& object, const QString& key) {
  require(jsonHasNumber(object, key), QStringLiteral("Expected numeric JSON metric: %1").arg(key));
  return object.value(key).toDouble();
}

QRectF jsonRect(const QJsonObject& object, const QString& key) {
  const QJsonObject rect = object.value(key).toObject();
  require(!rect.isEmpty(), QStringLiteral("Expected JSON rect: %1").arg(key));
  return QRectF(jsonNumber(rect, QStringLiteral("x")),
                jsonNumber(rect, QStringLiteral("y")),
                jsonNumber(rect, QStringLiteral("width")),
                jsonNumber(rect, QStringLiteral("height")));
}

void requireCloseMetric(qreal actual, qreal expected, qreal tolerance, const QString& label) {
  require(qAbs(actual - expected) <= tolerance,
          QStringLiteral("%1 mismatch: actual=%2 expected=%3 tolerance=%4")
              .arg(label)
              .arg(actual, 0, 'f', 6)
              .arg(expected, 0, 'f', 6)
              .arg(tolerance, 0, 'f', 6));
}

qreal maxAbsJsonMetric(const QJsonObject& node, const QString& key) {
  qreal result = 0.0;
  if (jsonHasNumber(node, key)) {
    result = qMax(result, qAbs(node.value(key).toDouble()));
  }
  const QJsonArray children = node.value(QStringLiteral("children")).toArray();
  for (const QJsonValue& child : children) {
    if (child.isObject()) {
      result = qMax(result, maxAbsJsonMetric(child.toObject(), key));
    }
  }
  return result;
}

qreal maxAbsFlatMetric(const QJsonArray& flat, const QString& key) {
  qreal result = 0.0;
  for (const QJsonValue& value : flat) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject object = value.toObject();
    if (jsonHasNumber(object, key)) {
      result = qMax(result, qAbs(object.value(key).toDouble()));
    }
  }
  return result;
}

QJsonArray flattenNativeMetrics(const QJsonObject& node, const QString& path = QString()) {
  QJsonArray result;
  QJsonObject metric;
  metric.insert(QStringLiteral("path"), path);
  metric.insert(QStringLiteral("kind"), node.value(QStringLiteral("kind")).toString());
  if (node.contains(QStringLiteral("text"))) {
    metric.insert(QStringLiteral("text"), node.value(QStringLiteral("text")));
  }
  for (const QString& key : {QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("depth"), QStringLiteral("shift")}) {
    if (jsonHasNumber(node, key)) {
      metric.insert(key, node.value(key));
    }
  }
  result.push_back(metric);

  const QJsonArray children = node.value(QStringLiteral("children")).toArray();
  for (int i = 0; i < children.size(); ++i) {
    if (!children.at(i).isObject()) {
      continue;
    }
    const QString childPath = path.isEmpty() ? QString::number(i) : path + QStringLiteral("/") + QString::number(i);
    const QJsonArray childFlat = flattenNativeMetrics(children.at(i).toObject(), childPath);
    for (const QJsonValue& childMetric : childFlat) {
      result.push_back(childMetric);
    }
  }
  return result;
}

QMap<QString, QJsonObject> flatMetricsByPath(const QJsonArray& flat) {
  QMap<QString, QJsonObject> result;
  for (const QJsonValue& value : flat) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject object = value.toObject();
    result.insert(object.value(QStringLiteral("path")).toString(), object);
  }
  return result;
}

void dumpBuilderMetricDiffs(const QString& id, const QJsonObject& nativeRoot, const QJsonObject& golden, qreal em) {
  QJsonObject goldenRoot = golden.value(QStringLiteral("root")).toObject();
  QJsonObject goldenHtml;
  if (findRenderTreeClass(goldenRoot, QStringLiteral("katex-html"), goldenHtml)) {
    goldenRoot = goldenHtml;
  }
  if (goldenRoot.isEmpty()) {
    return;
  }
  const QMap<QString, QJsonObject> nativeByPath = flatMetricsByPath(flattenNativeMetrics(nativeRoot));
  const QJsonArray goldenFlat = flattenNativeMetrics(goldenRoot);
  qreal worstDiff = 0.0;
  QString worstLine;
  int compared = 0;
  for (const QJsonValue& value : goldenFlat) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject goldenNode = value.toObject();
    const QString path = goldenNode.value(QStringLiteral("path")).toString();
    const QJsonObject nativeNode = nativeByPath.value(path);
    if (nativeNode.isEmpty()) {
      continue;
    }
    ++compared;
    QStringList parts;
    qreal nodeWorst = 0.0;
    for (const QString& key : {QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("depth"), QStringLiteral("shift")}) {
      if (!jsonHasNumber(goldenNode, key) || !jsonHasNumber(nativeNode, key)) {
        continue;
      }
      const qreal nativeValue = jsonNumber(nativeNode, key) / em;
      const qreal goldenValue = jsonNumber(goldenNode, key);
      const qreal diff = qAbs(nativeValue - goldenValue);
      nodeWorst = qMax(nodeWorst, diff);
      if (diff > 0.02) {
        parts.push_back(QStringLiteral("%1 native=%2 katex=%3 diff=%4")
                            .arg(key)
                            .arg(nativeValue, 0, 'f', 4)
                            .arg(goldenValue, 0, 'f', 4)
                            .arg(diff, 0, 'f', 4));
      }
    }
    if (nodeWorst > worstDiff && !parts.isEmpty()) {
      worstDiff = nodeWorst;
      worstLine = QStringLiteral("%1 path=%2 nativeKind=%3 katexKind=%4 %5")
                      .arg(id,
                           path.isEmpty() ? QStringLiteral("<root>") : path,
                           nativeNode.value(QStringLiteral("kind")).toString(),
                           goldenNode.value(QStringLiteral("kind")).toString(),
                           parts.join(QStringLiteral("; ")));
    }
  }
  if (!worstLine.isEmpty()) {
    qInfo().noquote() << QStringLiteral("KaTeX builder diff compared=%1 worst=%2").arg(compared).arg(worstLine);
  }
}

struct BBoxAuditEntry {
  QString id;
  qreal katexWidthEm = 0.0;
  qreal katexHeightEm = 0.0;
  qreal katexInkWidthEm = 0.0;
  qreal katexInkHeightEm = 0.0;
  qreal nativeLayoutWidthEm = 0.0;
  qreal nativeLayoutHeightEm = 0.0;
  qreal nativeInkWidthEm = 0.0;
  qreal nativeInkHeightEm = 0.0;

  qreal layoutError() const {
    return qMax(qAbs(nativeLayoutWidthEm - katexWidthEm), qAbs(nativeLayoutHeightEm - katexHeightEm));
  }

  qreal inkError() const {
    return qMax(qAbs(nativeInkWidthEm - katexInkWidthEm), qAbs(nativeInkHeightEm - katexInkHeightEm));
  }
};

struct GlyphAuditItem {
  QString kind;
  QString text;
  QString atomClass;
  QString fontClass;
  QRectF domRect;
  QRectF inkRect;
  qreal advanceWidth = 0.0;
  qreal glyphWidth = 0.0;
  qreal italicMarginRight = 0.0;
  bool hasInkRect = false;
};

QRectF nativeGlyphInkRect(const math::MathRenderNode& node, QPointF origin, qreal em) {
  constexpr int padding = 16;
  const QSize imageSize(qMax(1, qCeil(node.width) + padding * 2),
                        qMax(1, qCeil(node.height + node.depth) + padding * 2));
  QImage image(imageSize, QImage::Format_ARGB32);
  const QColor background(Qt::white);
  image.fill(background);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  node.paint(painter, QPointF(padding, padding + node.height));
  painter.end();

  const QRect ink = imageInkBounds(image, background);
  if (ink.isNull()) {
    return {};
  }

  return QRectF((origin.x() + ink.x() - padding) / em,
                (origin.y() - node.height + ink.y() - padding) / em,
                ink.width() / em,
                ink.height() / em);
}

void collectNativeGlyphItems(const math::MathRenderNode& node,
                             QPointF origin,
                             qreal em,
                             QVector<GlyphAuditItem>& out) {
  const QRectF domRect(origin.x() / em, (origin.y() - node.height) / em, node.width / em, (node.height + node.depth) / em);
  if (node.kind == math::MathRenderKind::Symbol || node.kind == math::MathRenderKind::Error) {
    GlyphAuditItem item;
    item.kind = QStringLiteral("glyph");
    item.text = node.text;
    item.atomClass = node.atomClass;
    item.fontClass = node.fontClass;
    item.domRect = domRect;
    item.inkRect = nativeGlyphInkRect(node, origin, em);
    item.advanceWidth = node.width / em;
    item.glyphWidth = qMax<qreal>(0.0, node.width - node.italicMarginRight) / em;
    item.italicMarginRight = node.italicMarginRight / em;
    item.hasInkRect = !item.inkRect.isNull();
    out.push_back(item);
  } else if (node.atomClass == QStringLiteral("mspace") && node.width > 0.0) {
    GlyphAuditItem item;
    item.kind = QStringLiteral("space");
    item.text = node.text.isEmpty() ? QStringLiteral("mspace") : node.text;
    item.atomClass = node.atomClass;
    item.fontClass = node.fontClass;
    item.domRect = domRect;
    item.advanceWidth = node.width / em;
    item.glyphWidth = node.width / em;
    out.push_back(item);
  }

  switch (node.kind) {
    case math::MathRenderKind::Span: {
      qreal x = origin.x();
      for (const auto& child : node.children) {
        collectNativeGlyphItems(*child, QPointF(x + child->xOffset, origin.y() + child->yOffset + child->shift), em, out);
        x += child->width;
      }
      return;
    }
    case math::MathRenderKind::SupSub: {
      if (!node.children.empty()) {
        const auto& base = node.children.at(0);
        collectNativeGlyphItems(*base, QPointF(origin.x() + base->xOffset, origin.y() + base->yOffset), em, out);
      }
      for (size_t i = 1; i < node.children.size(); ++i) {
        const auto& child = node.children.at(i);
        collectNativeGlyphItems(*child, QPointF(origin.x() + child->xOffset, origin.y() + child->yOffset + child->shift), em, out);
      }
      return;
    }
    case math::MathRenderKind::Fraction: {
      if (node.children.size() == 2) {
        const auto& numerator = node.children.at(0);
        const auto& denominator = node.children.at(1);
        collectNativeGlyphItems(*numerator, QPointF(origin.x() + (node.width - numerator->width) / 2.0, origin.y() + numerator->shift), em, out);
        collectNativeGlyphItems(*denominator, QPointF(origin.x() + (node.width - denominator->width) / 2.0, origin.y() + denominator->shift), em, out);
      } else if (node.children.size() >= 3) {
        const auto& numerator = node.children.at(0);
        const auto& denominator = node.children.at(2);
        collectNativeGlyphItems(*numerator, QPointF(origin.x() + (node.width - numerator->width) / 2.0, origin.y() + numerator->shift), em, out);
        collectNativeGlyphItems(*denominator, QPointF(origin.x() + (node.width - denominator->width) / 2.0, origin.y() + denominator->shift), em, out);
      }
      return;
    }
    default:
      break;
  }

  for (const auto& child : node.children) {
    collectNativeGlyphItems(*child, QPointF(origin.x() + child->xOffset, origin.y() + child->yOffset + child->shift), em, out);
  }
}

QVector<GlyphAuditItem> nativeGlyphItems(const math::MathRenderNode& root, qreal em) {
  const math::MathRenderNode& htmlRoot = nativeInkRoot(root);
  QVector<GlyphAuditItem> out;
  collectNativeGlyphItems(htmlRoot, QPointF(0.0, htmlRoot.height), em, out);
  return out;
}

QVector<GlyphAuditItem> nativeVisibleGlyphItems(const math::MathRenderNode& root, qreal em) {
  const QVector<GlyphAuditItem> all = nativeGlyphItems(root, em);
  QVector<GlyphAuditItem> glyphs;
  for (const GlyphAuditItem& item : all) {
    // Match KaTeX's katexVisibleGlyphItems filters: exclude empty text and
    // zero-width space (U+200B).  The width-based filter is NOT applied here
    // because native domRect.width and browser rect.width differ in origin.
    if (item.kind == QStringLiteral("glyph") && !item.text.isEmpty() &&
        item.text != QString::fromUtf8("\xE2\x80\x8B")) {
      glyphs.push_back(item);
    }
  }
  return glyphs;
}

QRectF glyphJsonRect(const QJsonObject& item, const QString& key) {
  const QJsonObject rect = item.value(key).toObject();
  return QRectF(rect.value(QStringLiteral("x")).toDouble(),
                rect.value(QStringLiteral("y")).toDouble(),
                rect.value(QStringLiteral("width")).toDouble(),
                rect.value(QStringLiteral("height")).toDouble());
}

QVector<QJsonObject> katexVisibleGlyphItems(const QJsonObject& golden) {
  const QJsonArray katexGlyphs = golden.value(QStringLiteral("glyphs")).toArray();
  QVector<QJsonObject> visible;
  for (const QJsonValue& value : katexGlyphs) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject item = value.toObject();
    const QString kind = item.value(QStringLiteral("kind")).toString();
    const QString text = item.value(QStringLiteral("text")).toString();
    const QJsonObject rect = item.value(item.contains(QStringLiteral("domRect")) ? QStringLiteral("domRect") : QStringLiteral("rect")).toObject();
    if (kind == QStringLiteral("glyph") && !text.isEmpty() && text != QString::fromUtf8("\xE2\x80\x8B") &&
        rect.value(QStringLiteral("width")).toDouble() > 0.0001) {
      visible.push_back(item);
    }
  }
  return visible;
}

void compareKatexGlyphs(const QString& id,
                        const math::MathRenderNode& root,
                        const QJsonObject& golden,
                        qreal em,
                        qreal xTolerance,
                        qreal widthTolerance) {
  if (golden.value(QStringLiteral("error")).toBool()) {
    return;
  }
  const QVector<GlyphAuditItem> native = nativeVisibleGlyphItems(root, em);
  const QVector<QJsonObject> katex = katexVisibleGlyphItems(golden);
  for (const QJsonObject& item : katex) {
    const QJsonArray classes = item.value(QStringLiteral("classes")).toArray();
    for (const QJsonValue& classValue : classes) {
      if (classValue.toString() == QStringLiteral("katex-error")) {
        return;
      }
    }
  }
  require(native.size() == katex.size(),
          QStringLiteral("KaTeX glyph count for %1 should match native glyph count: native=%2 katex=%3")
              .arg(id)
              .arg(native.size())
              .arg(katex.size()));

  for (int i = 0; i < native.size(); ++i) {
    const GlyphAuditItem& nativeGlyph = native.at(i);
    const QJsonObject katexGlyph = katex.at(i);
    const QString katexText = katexGlyph.value(QStringLiteral("text")).toString();
    require(nativeGlyph.text == katexText,
            QStringLiteral("KaTeX glyph text for %1 #%2 should match: native='%3' katex='%4'")
                .arg(id)
                .arg(i)
                .arg(nativeGlyph.text, katexText));

    const QRectF katexRect = glyphJsonRect(katexGlyph, katexGlyph.contains(QStringLiteral("domRect")) ? QStringLiteral("domRect") : QStringLiteral("rect"));
    requireCloseMetric(nativeGlyph.domRect.x(), katexRect.x(), xTolerance,
                       QStringLiteral("KaTeX glyph x for %1 #%2 '%3'").arg(id).arg(i).arg(nativeGlyph.text));
    if (nativeGlyph.text != QString(QChar(0xe020))) {
      requireCloseMetric(nativeGlyph.glyphWidth, katexRect.width(), widthTolerance,
                         QStringLiteral("KaTeX glyph width for %1 #%2 '%3'").arg(id).arg(i).arg(nativeGlyph.text));
    }
  }
}

void dumpGlyphDiffs(const QString& id, const math::MathRenderNode& root, const QJsonObject& golden, qreal em) {
  const QVector<GlyphAuditItem> nativeAll = nativeGlyphItems(root, em);
  QVector<GlyphAuditItem> nativeGlyphs;
  QVector<GlyphAuditItem> nativeSpaces;
  for (const GlyphAuditItem& item : nativeAll) {
    if (item.kind == QStringLiteral("glyph")) {
      nativeGlyphs.push_back(item);
    } else if (item.kind == QStringLiteral("space")) {
      nativeSpaces.push_back(item);
    }
  }

  const QVector<QJsonObject> katexVisibleGlyphs = katexVisibleGlyphItems(golden);
  const QJsonArray katexGlyphs = golden.value(QStringLiteral("glyphs")).toArray();
  QVector<QJsonObject> katexSpaces;
  for (const QJsonValue& value : katexGlyphs) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject item = value.toObject();
    const QString kind = item.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("space")) {
      katexSpaces.push_back(item);
    }
  }

  const int count = qMax(nativeGlyphs.size(), katexVisibleGlyphs.size());
  qInfo().noquote() << QStringLiteral("Glyph diff for %1 nativeGlyphs=%2 katexGlyphs=%3 nativeSpaces=%4 katexSpaces=%5")
                           .arg(id)
                           .arg(nativeGlyphs.size())
                           .arg(katexVisibleGlyphs.size())
                           .arg(nativeSpaces.size())
                           .arg(katexSpaces.size());
  for (int i = 0; i < count; ++i) {
    QString nativeText;
    QRectF nativeDomRect;
    QRectF nativeInkRect;
    qreal nativeAdvance = 0.0;
    qreal nativeGlyphWidth = 0.0;
    qreal nativeItalic = 0.0;
    bool nativeHasInk = false;
    if (i < nativeGlyphs.size()) {
      const GlyphAuditItem& item = nativeGlyphs.at(i);
      nativeText = item.text;
      nativeDomRect = item.domRect;
      nativeInkRect = item.inkRect;
      nativeAdvance = item.advanceWidth;
      nativeGlyphWidth = item.glyphWidth;
      nativeItalic = item.italicMarginRight;
      nativeHasInk = item.hasInkRect;
    }
    QString katexText;
    QRectF katexDomRect;
    QRectF katexInkRect;
    bool katexHasInk = false;
    if (i < katexVisibleGlyphs.size()) {
      const QJsonObject item = katexVisibleGlyphs.at(i);
      katexText = item.value(QStringLiteral("text")).toString();
      katexDomRect = glyphJsonRect(item, item.contains(QStringLiteral("domRect")) ? QStringLiteral("domRect") : QStringLiteral("rect"));
      const QJsonValue inkValue = item.value(QStringLiteral("inkRect"));
      if (inkValue.isObject()) {
        katexInkRect = glyphJsonRect(item, QStringLiteral("inkRect"));
        katexHasInk = true;
      }
    }
    qInfo().noquote()
        << QStringLiteral("#%1 '%2'/'%3' dom native x=%4 w=%5 adv=%6 glyphW=%7 italic=%8 | katex x=%9 w=%10 | dx=%11 dw=%12")
               .arg(i)
               .arg(nativeText, katexText)
               .arg(nativeDomRect.x(), 0, 'f', 4)
               .arg(nativeDomRect.width(), 0, 'f', 4)
               .arg(nativeAdvance, 0, 'f', 4)
               .arg(nativeGlyphWidth, 0, 'f', 4)
               .arg(nativeItalic, 0, 'f', 4)
               .arg(katexDomRect.x(), 0, 'f', 4)
               .arg(katexDomRect.width(), 0, 'f', 4)
               .arg(nativeDomRect.x() - katexDomRect.x(), 0, 'f', 4)
               .arg(nativeGlyphWidth - katexDomRect.width(), 0, 'f', 4);
    if (nativeHasInk || katexHasInk) {
      qInfo().noquote()
          << QStringLiteral("   ink native x=%1 y=%2 w=%3 h=%4 | katex x=%5 y=%6 w=%7 h=%8 | dx=%9 dw=%10")
                 .arg(nativeInkRect.x(), 0, 'f', 4)
                 .arg(nativeInkRect.y(), 0, 'f', 4)
                 .arg(nativeInkRect.width(), 0, 'f', 4)
                 .arg(nativeInkRect.height(), 0, 'f', 4)
                 .arg(katexInkRect.x(), 0, 'f', 4)
                 .arg(katexInkRect.y(), 0, 'f', 4)
                 .arg(katexInkRect.width(), 0, 'f', 4)
                 .arg(katexInkRect.height(), 0, 'f', 4)
                 .arg(nativeInkRect.x() - katexInkRect.x(), 0, 'f', 4)
                 .arg(nativeInkRect.width() - katexInkRect.width(), 0, 'f', 4);
    }
  }

  if (!nativeSpaces.empty() || !katexSpaces.empty()) {
    qInfo().noquote() << QStringLiteral("Glyph glue for %1:").arg(id);
    for (int i = 0; i < nativeSpaces.size(); ++i) {
      const GlyphAuditItem& item = nativeSpaces.at(i);
      qInfo().noquote()
          << QStringLiteral("  native space #%1 x=%2 w=%3")
                 .arg(i)
                 .arg(item.domRect.x(), 0, 'f', 4)
                 .arg(item.domRect.width(), 0, 'f', 4);
    }
  }
}

QRect nativeInkBounds(const math::MathRenderNode& root) {
  constexpr int padding = 32;
  const QSize imageSize(qMax(1, qCeil(root.width) + padding * 2),
                        qMax(1, qCeil(root.height + root.depth) + padding * 2));
  QImage image(imageSize, QImage::Format_ARGB32);
  const QColor background(Qt::white);
  image.fill(background);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  root.paint(painter, QPointF(padding, padding + root.height));
  painter.end();

  return imageInkBounds(image, background);
}

const math::MathRenderNode& nativeInkRoot(const math::MathRenderNode& root) {
  if (root.text == QStringLiteral("katex") && !root.children.empty()) {
    const math::MathRenderNode* html = root.children.front().get();
    if (html != nullptr && html->text == QStringLiteral("katex-html") && !html->children.empty()) {
      return *html;
    }
  }
  return root;
}

BBoxAuditEntry makeBBoxAuditEntry(const QString& id,
                                  const math::MathRenderNode& root,
                                  const QJsonObject& golden,
                                  qreal nativeEm) {
  const qreal katexRootFontPx = golden.value(QStringLiteral("rootFontPx")).toDouble(19.36);
  require(katexRootFontPx > 0.0, QStringLiteral("KaTeX bbox for %1 should include rootFontPx").arg(id));
  const QRectF katexBox = jsonRect(golden, QStringLiteral("bbox"));
  QRectF katexInkBox;
  if (golden.value(QStringLiteral("inkBBox")).isObject()) {
    katexInkBox = jsonRect(golden, QStringLiteral("inkBBox"));
  }
  const QRect ink = nativeInkBounds(nativeInkRoot(root));

  BBoxAuditEntry entry;
  entry.id = id;
  entry.katexWidthEm = katexBox.width() / katexRootFontPx;
  entry.katexHeightEm = katexBox.height() / katexRootFontPx;
  entry.katexInkWidthEm = katexInkBox.isEmpty() ? 0.0 : katexInkBox.width() / katexRootFontPx;
  entry.katexInkHeightEm = katexInkBox.isEmpty() ? 0.0 : katexInkBox.height() / katexRootFontPx;
  entry.nativeLayoutWidthEm = root.width / nativeEm;
  entry.nativeLayoutHeightEm = (root.height + root.depth) / nativeEm;
  entry.nativeInkWidthEm = ink.isEmpty() ? 0.0 : ink.width() / nativeEm;
  entry.nativeInkHeightEm = ink.isEmpty() ? 0.0 : ink.height() / nativeEm;
  return entry;
}

void compareKatexBrowserBBox(const QString& id,
                             const math::MathRenderNode& root,
                             const QJsonObject& golden,
                             qreal nativeEm,
                             qreal widthTolerance,
                             qreal heightTolerance) {
  if (golden.value(QStringLiteral("error")).toBool()) {
    return;
  }

  const BBoxAuditEntry entry = makeBBoxAuditEntry(id, root, golden, nativeEm);
  require(entry.katexWidthEm > 0.0 && entry.katexHeightEm > 0.0,
          QStringLiteral("KaTeX bbox for %1 should have positive dimensions").arg(id));

  requireCloseMetric(entry.nativeLayoutWidthEm, entry.katexWidthEm, widthTolerance,
                     QStringLiteral("KaTeX browser layout bbox width for %1").arg(id));
  requireCloseMetric(entry.nativeLayoutHeightEm, entry.katexHeightEm, heightTolerance,
                     QStringLiteral("KaTeX browser layout bbox height for %1").arg(id));

  require(entry.nativeInkWidthEm > 0.0 && entry.nativeInkHeightEm > 0.0,
          QStringLiteral("Native painted ink bbox for %1 should not be empty").arg(id));
}

void compareKatexInkBBox(const QString& id,
                         const math::MathRenderNode& root,
                         const QJsonObject& golden,
                         qreal nativeEm,
                         qreal widthTolerance,
                         qreal heightTolerance) {
  if (golden.value(QStringLiteral("error")).toBool()) {
    return;
  }

  const BBoxAuditEntry entry = makeBBoxAuditEntry(id, root, golden, nativeEm);
  require(entry.katexInkWidthEm > 0.0 && entry.katexInkHeightEm > 0.0,
          QStringLiteral("KaTeX ink bbox for %1 should have positive dimensions").arg(id));
  require(entry.nativeInkWidthEm > 0.0 && entry.nativeInkHeightEm > 0.0,
          QStringLiteral("Native painted ink bbox for %1 should not be empty").arg(id));

  requireCloseMetric(entry.nativeInkWidthEm, entry.katexInkWidthEm, widthTolerance,
                     QStringLiteral("KaTeX ink bbox width for %1").arg(id));
  requireCloseMetric(entry.nativeInkHeightEm, entry.katexInkHeightEm, heightTolerance,
                     QStringLiteral("KaTeX ink bbox height for %1").arg(id));
}

void compareKatexBuilderRootMetrics(const QString& id,
                                    const math::MathRenderNode& root,
                                    const QJsonObject& nativeDump,
                                    const QJsonObject& golden,
                                    qreal em,
                                    qreal heightTolerance,
                                    qreal depthTolerance,
                                    qreal shiftTolerance) {
  const QJsonObject goldenRoot = golden.value(QStringLiteral("root")).toObject();
  require(!goldenRoot.isEmpty(), QStringLiteral("KaTeX golden metrics for %1 should include root").arg(id));
  if (renderTreeContainsClass(goldenRoot, QStringLiteral("katex-error"))) {
    return;
  }
  if (renderTreeContainsClass(goldenRoot, QStringLiteral("newline"))) {
    return;
  }

  QJsonObject nativeMetricsRoot = nativeDump;
  QJsonObject katexHtml;
  if (findRenderTreeText(nativeDump, QStringLiteral("katex-html"), katexHtml)) {
    nativeMetricsRoot = katexHtml;
  }
  const qreal nativeHeight = jsonHasNumber(nativeMetricsRoot, QStringLiteral("height")) ? jsonNumber(nativeMetricsRoot, QStringLiteral("height")) : root.height;
  const qreal nativeDepth = jsonHasNumber(nativeMetricsRoot, QStringLiteral("depth")) ? jsonNumber(nativeMetricsRoot, QStringLiteral("depth")) : root.depth;
  const qreal nativeWidth = jsonHasNumber(nativeMetricsRoot, QStringLiteral("width")) ? jsonNumber(nativeMetricsRoot, QStringLiteral("width")) : root.width;
  const qreal actualHeight = nativeHeight / em;
  const qreal actualDepth = nativeDepth / em;
  if (jsonHasNumber(goldenRoot, QStringLiteral("height"))) {
    requireCloseMetric(actualHeight, jsonNumber(goldenRoot, QStringLiteral("height")), heightTolerance,
                       QStringLiteral("KaTeX builder root height for %1").arg(id));
  }
  if (jsonHasNumber(goldenRoot, QStringLiteral("depth"))) {
    requireCloseMetric(actualDepth, jsonNumber(goldenRoot, QStringLiteral("depth")), depthTolerance,
                       QStringLiteral("KaTeX builder root depth for %1").arg(id));
  }
  if (jsonHasNumber(goldenRoot, QStringLiteral("width"))) {
    requireCloseMetric(nativeWidth / em, jsonNumber(goldenRoot, QStringLiteral("width")), 0.50,
                       QStringLiteral("KaTeX builder root width for %1").arg(id));
  }

  const QJsonArray goldenFlat = golden.value(QStringLiteral("flat")).toArray();
  require(!goldenFlat.isEmpty(), QStringLiteral("KaTeX golden metrics for %1 should include flat nodes").arg(id));
  const qreal goldenMaxShift = maxAbsFlatMetric(goldenFlat, QStringLiteral("shift"));
  if (goldenMaxShift > 0.0) {
    requireCloseMetric(maxAbsJsonMetric(nativeMetricsRoot, QStringLiteral("shift")) / em, goldenMaxShift, shiftTolerance,
                       QStringLiteral("KaTeX builder max shift for %1").arg(id));
  }
}

MarkdownNode* mutableBlockAt(MarkdownDocument& document, qsizetype index) {
  auto& children = document.root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), QStringLiteral("mutable block index out of range"));
  return children.at(static_cast<size_t>(index)).get();
}

void replaceDocumentText(MarkdownDocument& document, const QString& markdown) {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("incremental test parse returned null root"));
  document.setMarkdownText(markdown, std::move(parsed.root));
}

void requireUsableRect(const QRectF& rect, const QString& label) {
  require(rect.isValid(), QStringLiteral("%1 rect is invalid").arg(label));
  require(rect.width() > 20.0, QStringLiteral("%1 rect width too small").arg(label));
  require(rect.height() > 10.0, QStringLiteral("%1 rect height too small").arg(label));
}

void testIncrementalBlockRebuildContract() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"), false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  const NodeId firstParagraphId = mutableBlockAt(session.document(), 0)->id();
  const NodeId secondParagraphId = mutableBlockAt(session.document(), 1)->id();
  const QRectF firstBefore = layout.block(firstParagraphId)->rect();
  const QRectF secondBefore = layout.block(secondParagraphId)->rect();
  const qreal totalBefore = layout.totalHeight();

  require(session.applyTextDelta(
              5,
              0,
              QStringLiteral(" alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"
                             " alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"),
              true,
              {LocalEditNodeHint{firstParagraphId, 0, BlockType::Paragraph}}),
          QStringLiteral("paragraph local delta should apply"));
  const DocumentLayout::BlockRebuildResult paragraphResult = layout.rebuildBlock(firstParagraphId, session.document(), theme, {});
  require(paragraphResult.rebuilt, QStringLiteral("paragraph block rebuild should succeed"));
  require(paragraphResult.blockId == firstParagraphId, QStringLiteral("paragraph rebuild result id mismatch"));
  require(paragraphResult.oldRect == firstBefore, QStringLiteral("paragraph rebuild old rect mismatch"));
  require(paragraphResult.newRect == layout.block(firstParagraphId)->rect(), QStringLiteral("paragraph rebuild new rect mismatch"));
  require(paragraphResult.heightDelta != 0, QStringLiteral("paragraph rebuild should report height delta"));
  require(!paragraphResult.shiftedRect.isEmpty(), QStringLiteral("paragraph rebuild should report shifted rect"));
  require(layout.block(secondParagraphId)->rect().top() != secondBefore.top(), QStringLiteral("paragraph rebuild should shift following block"));
  require(layout.totalHeight() != totalBefore, QStringLiteral("paragraph rebuild should update total height"));

  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(tableCell != nullptr, QStringLiteral("incremental table cell missing"));
  MarkdownNode* table = mutableBlockAt(session.document(), 2);
  require(layout.block(tableCell->id()) != nullptr, QStringLiteral("layout index should include table cell"));
  const QRectF tableBefore = layout.block(table->id())->rect();
  const DocumentLayout::BlockRebuildResult tableResult = layout.rebuildBlock(tableCell->id(), session.document(), theme, {});
  require(tableResult.rebuilt, QStringLiteral("table cell rebuild should map to top-level table"));
  require(tableResult.blockId == table->id(), QStringLiteral("table cell rebuild should report table block id"));
  require(tableResult.oldRect == tableBefore, QStringLiteral("table cell rebuild old rect mismatch"));
  require(tableResult.newRect == layout.block(table->id())->rect(), QStringLiteral("table cell rebuild new rect mismatch"));
}

void requireSameTopLevelLayout(const DocumentLayout& incremental, const DocumentLayout& full, const MarkdownDocument& document, const QString& label) {
  require(incremental.blocks().size() == full.blocks().size(), label + QStringLiteral(" block count mismatch"));
  require(qAbs(incremental.totalHeight() - full.totalHeight()) < 0.01, label + QStringLiteral(" total height mismatch"));
  for (const auto& child : document.root().children()) {
    const BlockLayout* incrementalBlock = incremental.block(child->id());
    const BlockLayout* fullBlock = full.block(child->id());
    require(incrementalBlock != nullptr, label + QStringLiteral(" incremental block index missing"));
    require(fullBlock != nullptr, label + QStringLiteral(" full block index missing"));
    const QRectF incrementalRect = incrementalBlock->rect();
    const QRectF fullRect = fullBlock->rect();
    require(qAbs(incrementalRect.left() - fullRect.left()) < 0.01, label + QStringLiteral(" block left mismatch"));
    require(qAbs(incrementalRect.top() - fullRect.top()) < 0.01, label + QStringLiteral(" block top mismatch"));
    require(qAbs(incrementalRect.width() - fullRect.width()) < 0.01, label + QStringLiteral(" block width mismatch"));
    require(qAbs(incrementalRect.height() - fullRect.height()) < 0.01, label + QStringLiteral(" block height mismatch"));
  }
}

void testIncrementalTopLevelRangeRebuildContract() {
  RenderTheme theme = RenderTheme::github();

  auto verifyRangeEdit = [&](const QString& initial, qsizetype sourceStart, qsizetype removedLength, const QString& insertedText, const QString& label) {
    DocumentSession session;
    session.setMarkdownText(initial, false);
    DocumentLayout incremental;
    incremental.rebuild(session.document(), theme, 800.0);

    require(session.applyTextDelta(sourceStart, removedLength, insertedText, true), label + QStringLiteral(" local edit should apply"));
    const TopLevelRangeChange range = session.lastLocalTopLevelRangeChange();
    require(range.isValid(), label + QStringLiteral(" range should be valid"));
    const DocumentLayout::RangeRebuildResult result = incremental.rebuildTopLevelRange(range, session.document(), theme, {});
    require(result.rebuilt, label + QStringLiteral(" range rebuild should succeed"));

    DocumentLayout full;
    full.rebuild(session.document(), theme, 800.0);
    requireSameTopLevelLayout(incremental, full, session.document(), label);
  };

  verifyRangeEdit(QStringLiteral("alpha beta\n\ngamma"), 5, 0, QStringLiteral("\n\n"), QStringLiteral("paragraph split"));
  verifyRangeEdit(QStringLiteral("alpha\n\nbeta\n\ngamma"), 5, 2, QStringLiteral(" "), QStringLiteral("paragraph merge"));
  verifyRangeEdit(QStringLiteral("# Heading\n\nalpha\n\nbeta"), 9, 0, QStringLiteral("\n\n"), QStringLiteral("heading boundary insert"));
  verifyRangeEdit(QStringLiteral("alpha\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nomega"), 5, 0, QStringLiteral("\n\ninserted"), QStringLiteral("table suffix shift"));
}

void testUnorderedListMarkerKindsByDepth() {
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("- level one\n"
                     "    - level two\n"
                     "        - level three\n"
                     "            - level four\n"
                     "\n"
                     "1. ordered one\n"
                     "    - ordered child bullet"),
      false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 6, QStringLiteral("list marker fixture should parse six list items"));
  require(layout.block(items.at(0)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletDisc,
          QStringLiteral("first unordered level should use a filled disc marker"));
  require(layout.block(items.at(1)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletCircle,
          QStringLiteral("second unordered level should use a hollow circle marker"));
  require(layout.block(items.at(2)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletSquare,
          QStringLiteral("third unordered level should use a filled square marker"));
  require(layout.block(items.at(3)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletSquare,
          QStringLiteral("fourth unordered level should continue using a filled square marker"));
  require(layout.block(items.at(4)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::OrderedText,
          QStringLiteral("ordered list marker should remain text"));
  require(layout.block(items.at(5)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletDisc,
          QStringLiteral("unordered child inside ordered list should start its own bullet depth"));
}

void testInlineMarkerExpansion() {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("before ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), QVector<InlineNode>{InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" after")));

  RenderTheme theme = RenderTheme::github();
  InlineLayout collapsed;
  collapsed.build(inlines, theme, 400.0, theme.paragraphFont());
  require(!collapsed.displayText().contains(QStringLiteral("**")), QStringLiteral("collapsed inline should hide strong markers"));

  InlineLayout expanded;
  InlineLayout::BuildOptions options;
  options.projectionState.cursorVisibleOffset = 8;
  options.projectionState.cursorSourceOffset = 10;
  expanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), options);
  require(expanded.displayText().contains(QStringLiteral("**")), QStringLiteral("active inline should show strong markers"));
  require(expanded.plainText() == QStringLiteral("before bold after"), QStringLiteral("expanded plain text should stay collapsed"));
  require(expanded.hitTestTextOffset(expanded.cursorRect(8).center()) == 8,
          QStringLiteral("expanded hit test should map display marker offsets back to visible offsets"));
  require(expanded.hitTestSourceOffset(expanded.cursorRectForSourceOffset(9).center()) == 9,
          QStringLiteral("expanded hit test should map opener marker display to source offset"));

  QVector<InlineNode> mathInlines;
  mathInlines.push_back(InlineNode::inlineMath(QStringLiteral("a123")));
  InlineLayout math;
  InlineLayout::BuildOptions mathOptions;
  mathOptions.projectionState.cursorSourceOffset = 2;
  math.build(mathInlines, QStringLiteral("$a123$"), theme, 400.0, theme.paragraphFont(), mathOptions);
  require(math.mathAtomCount() == 0 && math.displayText() == QStringLiteral("$a123$"),
          QStringLiteral("active inline math should expand to editable source text"));
  require(math.hitTestSourceOffset(math.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("math cursor rect should round-trip source offset after first char"));

  InlineLayout inactiveMath;
  inactiveMath.build(mathInlines, QStringLiteral("$a123$"), theme, 400.0, theme.paragraphFont(), InlineLayout::BuildOptions{});
  require(inactiveMath.mathAtomCount() == 1 && !inactiveMath.displayText().contains(QStringLiteral("a123")),
          QStringLiteral("inactive inline math should collapse to a native math atom"));

  InlineLayout selectionExpanded;
  InlineLayout::BuildOptions selectionOptions;
  selectionOptions.projectionState.selectionVisibleStart = 8;
  selectionOptions.projectionState.selectionVisibleEnd = 10;
  selectionExpanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), selectionOptions);
  require(selectionExpanded.displayText().contains(QStringLiteral("**")), QStringLiteral("selection touching inline should show strong markers"));
}

void testInlineProjectionContract() {
  RenderTheme theme = RenderTheme::github();
  InlineLayout::BuildOptions options;
  const QString linkMarkdown = QStringLiteral("[label](https://example.com)");
  DocumentSession linkSession;
  linkSession.setMarkdownText(linkMarkdown, false);
  const QVector<InlineNode> linkInlines = linkSession.document().root().children().front()->inlines();

  InlineLayout collapsedLink;
  collapsedLink.build(linkInlines, linkMarkdown, theme, 400.0, theme.paragraphFont(), options);
  require(collapsedLink.displayText() == QStringLiteral("label"), QStringLiteral("inactive link projection text mismatch"));
  require(!collapsedLink.displayText().contains(QStringLiteral("](")), QStringLiteral("inactive link should not render source syntax"));

  InlineLayout::BuildOptions activeLinkOptions = options;
  activeLinkOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout activeLink;
  activeLink.build(linkInlines, linkMarkdown, theme, 400.0, theme.paragraphFont(), activeLinkOptions);
  require(activeLink.displayText() == linkMarkdown, QStringLiteral("active link projection text mismatch"));
  require(activeLink.displayText().contains(QStringLiteral("](")), QStringLiteral("active link should render source syntax"));
  require(activeLink.hitTestSourceOffset(activeLink.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("active link cursor rect should round-trip source offset"));
  require(!activeLink.selectionRects(0, 5).isEmpty(), QStringLiteral("active link selection rects should remain valid"));

  const QString imageMarkdown = QStringLiteral("![alt](https://example.com/image.png)");
  DocumentSession imageSession;
  imageSession.setMarkdownText(imageMarkdown, false);
  const QVector<InlineNode> imageInlines = imageSession.document().root().children().front()->inlines();

  InlineLayout collapsedImage;
  collapsedImage.build(imageInlines, imageMarkdown, theme, 400.0, theme.paragraphFont(), options);
  require(collapsedImage.displayText() == QStringLiteral("alt"), QStringLiteral("inactive image projection text mismatch"));
  require(!collapsedImage.displayText().contains(QStringLiteral("![")), QStringLiteral("inactive image should not render source syntax"));

  InlineLayout::BuildOptions activeImageOptions = options;
  activeImageOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout activeImage;
  activeImage.build(imageInlines, imageMarkdown, theme, 400.0, theme.paragraphFont(), activeImageOptions);
  require(activeImage.displayText() == imageMarkdown, QStringLiteral("active image projection text mismatch"));
  require(activeImage.displayText().contains(QStringLiteral("![")), QStringLiteral("active image should render source syntax"));
  require(activeImage.hitTestSourceOffset(activeImage.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("active image cursor rect should round-trip source offset"));
  require(!activeImage.selectionRects(0, 3).isEmpty(), QStringLiteral("active image selection rects should remain valid"));
}

void testActiveLoadedImageKeepsSourceTextAndAddsPreviewSpace() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("temporary image directory should be valid"));
  const QString imagePath = dir.filePath(QStringLiteral("active-image.png"));
  QImage image(QSize(640, 480), QImage::Format_ARGB32);
  image.fill(QColor(20, 120, 200));
  require(image.save(imagePath), QStringLiteral("temporary image should save"));

  const QString markdown = QStringLiteral("![San Juan Mountains](%1 \"San Juan Mountains\")").arg(imagePath);
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  const QVector<InlineNode> inlines = session.document().root().children().front()->inlines();
  const RenderTheme theme = RenderTheme::github();

  InlineLayout inactive;
  inactive.build(inlines, markdown, theme, 400.0, theme.paragraphFont(), InlineLayout::BuildOptions{});
  require(inactive.displayText() != markdown, QStringLiteral("inactive loaded image should collapse"));
  require(!inactive.displayText().contains(QStringLiteral("![")), QStringLiteral("inactive loaded image should hide source syntax"));

  InlineLayout::BuildOptions activeOptions;
  activeOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout active;
  active.build(inlines, markdown, theme, 400.0, theme.paragraphFont(), activeOptions);
  require(active.displayText() == markdown,
          QStringLiteral("active loaded image should show complete source, got: %1").arg(active.displayText()));
  require(active.height() > inactive.height(), QStringLiteral("active loaded image should reserve preview space below source text"));

  const auto renderBlueBounds = [&](const InlineLayout& layout) {
    QImage canvas(QSize(420, qCeil(layout.height()) + 20), QImage::Format_ARGB32);
    canvas.fill(theme.backgroundColor());
    QPainter painter(&canvas);
    layout.paint(painter, QPointF(0.0, 0.0));
    painter.end();

    QRect bounds;
    const QColor imageColor(20, 120, 200);
    for (int y = 0; y < canvas.height(); ++y) {
      for (int x = 0; x < canvas.width(); ++x) {
        if (QColor(canvas.pixel(x, y)) == imageColor) {
          bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
        }
      }
    }
    return bounds;
  };
  const QRect inactiveImageBounds = renderBlueBounds(inactive);
  const QRect activeImageBounds = renderBlueBounds(active);
  require(!inactiveImageBounds.isNull() && !activeImageBounds.isNull(), QStringLiteral("loaded image should paint in both states"));
  require(qAbs(inactiveImageBounds.width() - activeImageBounds.width()) <= 1 &&
              qAbs(inactiveImageBounds.height() - activeImageBounds.height()) <= 1,
          QStringLiteral("active preview should keep the same image size as inactive rendering"));
}

void testEntityDisplayAfterEdit() {
  DocumentSession session;
  const QString markdown = QStringLiteral("Entities: &amp; &lt; &gt; &copy;.");
  session.setMarkdownText(markdown, false);
  const QVector<InlineNode> inlines = session.document().root().children().front()->inlines();

  // Build layout with raw source text — this is what buildEditable does
  RenderTheme theme = RenderTheme::github();
  InlineLayout layout;
  InlineLayout::BuildOptions options;
  layout.build(inlines, markdown, theme, 400.0, theme.paragraphFont(), options);
  require(layout.displayText() == QString::fromUtf8("Entities: & < > ©."),
          QStringLiteral("initial entity display text mismatch: %1").arg(layout.displayText()));

  // Simulate inserting 'a' at the beginning
  require(session.applyTextDelta(0, 0, QStringLiteral("a"), true),
          "entity edit should apply");

  const QString edited = session.markdownText();
  const QVector<InlineNode> editedInlines = session.document().root().children().front()->inlines();

  InlineLayout editedLayout;
  editedLayout.build(editedInlines, edited, theme, 400.0, theme.paragraphFont(), options);
  require(editedLayout.displayText() == QString::fromUtf8("aEntities: & < > ©."),
          QStringLiteral("post-edit entity display text mismatch: %1").arg(editedLayout.displayText()));
}

void testInlineCodeEndSourceHitUsesForwardBias() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("vendored ")));
  inlines.push_back(InlineNode::code(QStringLiteral("cmark-gfm")));
  const QString source = QStringLiteral("vendored `cmark-gfm`");

  InlineLayout layout;
  InlineLayout::BuildOptions options;
  options.projectionState.cursorSourceOffset = source.size();
  layout.build(inlines, source, theme, 400.0, theme.paragraphFont(), options);

  const QRectF endCursor = layout.cursorRectForSourceOffset(source.size());
  require(endCursor.left() >= layout.cursorRectForSourceOffset(source.size() - 1).left(),
          QStringLiteral("inline code end cursor should be at or after content end"));
  require(layout.hitTestSourceOffset(QPointF(endCursor.left() + 2.0, endCursor.center().y())) == source.size(),
          QStringLiteral("inline code end hit should map after closing marker"));
}

void testInlineLayoutGeometryContract() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta")));

  InlineLayout plain;
  plain.build(plainInlines, theme, 130.0, theme.paragraphFont());
  require(plain.height() > 0.0, QStringLiteral("inline layout should measure plain inline height"));
  require(plain.size().height() == plain.height(), QStringLiteral("inline layout size should expose measured height"));

  const QRectF plainCursor = plain.cursorRect(6);
  require(!plainCursor.isEmpty(), QStringLiteral("inline layout cursor rect should be available"));
  require(plain.hitTestTextOffset(QPointF(plainCursor.left(), plainCursor.center().y())) == 6,
          QStringLiteral("inline layout cursor point should round-trip plain offset"));
  require(!plain.selectionRects(1, 18).isEmpty(), QStringLiteral("inline layout selection rects should be available"));

  QVector<InlineNode> styledInlines;
  styledInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  styledInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  styledInlines.push_back(InlineNode::text(QStringLiteral(" ")));
  styledInlines.push_back(InlineNode::link(QStringLiteral("u"), QString(), {InlineNode::text(QStringLiteral("link"))}));
  styledInlines.push_back(InlineNode::text(QStringLiteral(" ")));
  styledInlines.push_back(InlineNode::code(QStringLiteral("code")));

  InlineLayout::BuildOptions options;
  options.projectionState.revealMarkdownMarkers = true;
  InlineLayout styled;
  styled.build(styledInlines, QStringLiteral("before **bold** [link](u) `code`"), theme, 180.0, theme.paragraphFont(), options);
  require(styled.height() > 0.0, QStringLiteral("inline layout should measure styled inline height"));
  require(styled.displayText() == QStringLiteral("before **bold** [link](u) `code`"), QStringLiteral("styled display text should match projection"));
  const QRectF styledCursor = styled.cursorRect(9);
  require(!styledCursor.isEmpty(), QStringLiteral("inline layout styled cursor rect should be available"));
  const qsizetype styledHit = styled.hitTestTextOffset(QPointF(styledCursor.left(), styledCursor.center().y()));
  require(styledHit >= 0 && styledHit <= styled.plainText().size(), QStringLiteral("inline layout hit-test should return a valid styled offset"));
  require(!styled.selectionRects(0, styled.plainText().size()).isEmpty(), QStringLiteral("inline layout should produce styled selection rects"));
}

void testInlineLayoutZeroWidthTabIndentGeometry() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("\u200balpha")));

  InlineLayout layout;
  layout.build(inlines, theme, 400.0, theme.paragraphFont());
  require(layout.displayText() == QStringLiteral("\u200balpha"), QStringLiteral("zero-width tab source should remain in display text"));

  const qreal actualIndent = layout.cursorRect(1).left() - layout.cursorRect(0).left();
  const qreal expectedIndent = QFontMetricsF(theme.paragraphFont()).horizontalAdvance(QStringLiteral("汉汉"));
  require(qAbs(actualIndent - expectedIndent) < 1.0,
          QStringLiteral("zero-width tab indent should render as two CJK characters, actual=%1 expected=%2").arg(actualIndent).arg(expectedIndent));
  require(actualIndent < QFontMetricsF(theme.paragraphFont()).horizontalAdvance(QStringLiteral("汉汉汉")),
          QStringLiteral("zero-width tab indent should not expand to a wide editor tab stop"));
}

void testInlineLayoutStyleFormats() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("em"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::strikethrough(QStringLiteral("~~"), {InlineNode::text(QStringLiteral("gone"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::strong(
      QStringLiteral("**"),
      {InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("both"))})}));

  InlineLayout layout;
  layout.build(inlines, QStringLiteral("**bold** *em* ~~gone~~ ***both***"), theme, 500.0, theme.paragraphFont(), InlineLayout::BuildOptions{});
  require(layout.displayText() == QStringLiteral("bold em gone both"), QStringLiteral("style format fixture display text mismatch"));

  const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
  auto formatAt = [&formats](int offset) -> QTextCharFormat {
    QTextCharFormat result;
    for (const QTextLayout::FormatRange& range : formats) {
      if (offset >= range.start && offset < range.start + range.length) {
        result = range.format;
      }
    }
    return result;
  };

  require(formatAt(0).fontWeight() >= QFont::Bold, QStringLiteral("strong text should use bold font weight"));
  require(formatAt(5).fontItalic(), QStringLiteral("emphasis text should use italic font"));
  require(formatAt(8).fontStrikeOut(), QStringLiteral("strikethrough text should use strikeout font"));
  require(formatAt(13).fontWeight() >= QFont::Bold && formatAt(13).fontItalic(),
          QStringLiteral("nested strong/emphasis text should combine style flags"));
}

void testInlineLayoutProjectionDisplayMappingAfterCollapsedMath() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("math ")));
  inlines.push_back(InlineNode::inlineMath(QStringLiteral("E = mc^2")));
  inlines.push_back(InlineNode::text(QStringLiteral(" then ")));
  inlines.push_back(InlineNode::code(QStringLiteral("code")));
  inlines.push_back(InlineNode::text(QStringLiteral(" and ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));

  InlineLayout layout;
  layout.build(
      inlines,
      QStringLiteral("math $E = mc^2$ then `code` and **bold**"),
      theme,
      500.0,
      theme.paragraphFont(),
      InlineLayout::BuildOptions{});
  require(layout.mathAtomCount() == 1, QStringLiteral("mapping fixture should collapse first inline math"));
  require(layout.displayText().contains(QStringLiteral(" then code and bold")),
          QStringLiteral("mapping fixture should keep text after collapsed math"));

  const qsizetype codeStart = layout.displayText().indexOf(QStringLiteral("code"));
  const qsizetype boldStart = layout.displayText().indexOf(QStringLiteral("bold"));
  require(codeStart >= 0 && boldStart > codeStart, QStringLiteral("mapping fixture display offsets should be discoverable"));

  const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
  auto formatAt = [&formats](int offset) -> QTextCharFormat {
    QTextCharFormat result;
    for (const QTextLayout::FormatRange& range : formats) {
      if (offset >= range.start && offset < range.start + range.length) {
        result = range.format;
      }
    }
    return result;
  };

  require(formatAt(static_cast<int>(codeStart)).fontFamilies().toStringList().first() == theme.codeFont().family(),
          QStringLiteral("code format after collapsed math should use rebuilt display offset"));
  require(formatAt(static_cast<int>(boldStart)).fontWeight() >= QFont::Bold,
          QStringLiteral("strong format after collapsed math should use rebuilt display offset"));

  const QVector<QRectF> codeSelection =
      layout.selectionRectsForSourceOffsets(QStringLiteral("math $E = mc^2$ then `").size(),
                                            QStringLiteral("math $E = mc^2$ then `code").size());
  require(codeSelection.size() == 1, QStringLiteral("source selection after collapsed math should select code text"));
  const QRectF codeCursor = layout.cursorRectForSourceOffset(QStringLiteral("math $E = mc^2$ then `").size());
  require(!codeCursor.isEmpty(), QStringLiteral("source cursor after collapsed math should exist"));
  require(qAbs(codeSelection.first().left() - codeCursor.left()) < 1.0,
          QStringLiteral("source selection after collapsed math should start at rebuilt code cursor"));
}

void testInlineLayoutPainting() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("before ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::link(QStringLiteral("https://example.com"), QString(), {InlineNode::text(QStringLiteral("link"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::code(QStringLiteral("code")));
  inlines.push_back(InlineNode::text(QStringLiteral(" after")));

  InlineLayout::BuildOptions options;
  options.projectionState.revealMarkdownMarkers = true;

  InlineLayout layout;
  layout.build(inlines, QStringLiteral("before **bold** [link](https://example.com) `code` after"), theme, 280.0, theme.paragraphFont(), options);
  require(layout.height() > 0.0, QStringLiteral("inline layout paint fixture should have height"));

  QImage image(QSize(320, qCeil(layout.height()) + 20), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  layout.paint(painter, QPointF(8.0, 8.0));
  painter.end();

  int changedPixels = 0;
  const QRgb background = theme.backgroundColor().rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (background & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  require(changedPixels > 25, QStringLiteral("inline layout paint should draw visible pixels"));
}

void testMathRenderingLayout() {
  RenderTheme theme = RenderTheme::github();

  CmarkGfmParser parser;
  ParseResult parsed = parser.parseDocument(QStringLiteral("before $x_i^2 + \\frac{a}{b}$ after\n\n$$\n\\sqrt{x} = \\frac{1}{2}\n$$"), {});
  require(parsed.root != nullptr, QStringLiteral("math render parse should produce document"));

  DocumentLayout documentLayout;
  MarkdownDocument document;
  document.setMarkdownText(QStringLiteral("before $x_i^2 + \\frac{a}{b}$ after\n\n$$\n\\sqrt{x} = \\frac{1}{2}\n$$"), std::move(parsed.root));
  documentLayout.rebuild(document, theme, 800.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("math block should parse"));
  const BlockLayout* mathBlockLayout = documentLayout.block(mathBlock->id());
  require(mathBlockLayout != nullptr, QStringLiteral("math block layout should exist"));
  require(mathBlockLayout->mathLayout() != nullptr && mathBlockLayout->mathLayout()->valid(), QStringLiteral("math block should have native math layout"));
  require(qAbs(mathBlockLayout->height() -
               std::ceil(mathBlockLayout->mathLayout()->size.height() + theme.codePadding().top() + theme.codePadding().bottom())) <= 1.0,
          QStringLiteral("inactive math block height should be native formula height plus block padding"));
  require(!mathBlockLayout->literalEditing(), QStringLiteral("inactive math block should render native formula instead of literal source"));

  const math::MathLayoutResult blockFormulaLayout = math::MathRenderer().render(QStringLiteral("\\sqrt{x} = \\frac{1}{2}"), theme, true);
  require(blockFormulaLayout.valid(), QStringLiteral("display math renderer should render sample formula directly"));
  require(qAbs(blockFormulaLayout.size.width() - blockFormulaLayout.root->width) < 0.01 &&
              qAbs(blockFormulaLayout.size.height() - (blockFormulaLayout.root->height + blockFormulaLayout.root->depth)) < 0.01,
          QStringLiteral("display math renderer should expose pure KaTeX root bbox without block padding"));

  HitTestResult inactiveMathHit = mathBlockLayout->hitTest(mathBlockLayout->rect().center(), theme);
  require(inactiveMathHit.zone == HitTestResult::Zone::Math, QStringLiteral("inactive math block hit should target math zone"));
  require(inactiveMathHit.cursorRect.left() == mathBlockLayout->rect().right() || inactiveMathHit.cursorRect.left() == mathBlockLayout->rect().left(),
          QStringLiteral("inactive math block cursor should be atomic at block edge"));

  SelectionRange mathEditingSelection;
  mathEditingSelection.anchor.blockId = mathBlock->id();
  mathEditingSelection.anchor.text.nodeId = mathBlock->id();
  mathEditingSelection.focus = mathEditingSelection.anchor;
  DocumentLayout editingDocumentLayout;
  editingDocumentLayout.rebuild(document, theme, 800.0, mathEditingSelection);
  const BlockLayout* editingMathBlockLayout = editingDocumentLayout.block(mathBlock->id());
  require(editingMathBlockLayout != nullptr && editingMathBlockLayout->literalEditing(),
          QStringLiteral("focused math block should enter literal editing layout"));
  require(editingMathBlockLayout->height() > mathBlockLayout->height() + theme.codeLineHeight() * 2.0,
          QStringLiteral("focused math block should reserve both TeX source editor and rendered preview"));
  const QPointF editingSourcePoint(editingMathBlockLayout->rect().left() + theme.codePadding().left() + QFontMetricsF(theme.codeFont()).horizontalAdvance(QStringLiteral("\\sqrt")),
                                   editingMathBlockLayout->rect().top() + theme.codePadding().top() + theme.codeLineHeight() * 1.5);
  HitTestResult editingMathHit = editingMathBlockLayout->hitTest(editingSourcePoint, theme);
  require(editingMathHit.zone == HitTestResult::Zone::Math &&
              editingMathHit.cursorRect.left() > editingMathBlockLayout->rect().left() &&
              editingMathHit.cursorRect.left() < editingMathBlockLayout->rect().right(),
          QStringLiteral("editing math block cursor should use literal source coordinates"));
  require(editingMathHit.textOffset > 0 && editingMathHit.textOffset < editingMathBlockLayout->literal().size(),
          QStringLiteral("editing math block hit-test should map into TeX content, not decorative dollar delimiters"));

  const MarkdownNode* paragraph = findFirstBlock(document.root(), BlockType::Paragraph);
  require(paragraph != nullptr, QStringLiteral("math inline paragraph should parse"));
  const BlockLayout* paragraphLayout = documentLayout.block(paragraph->id());
  require(paragraphLayout != nullptr && paragraphLayout->inlineLayout() != nullptr, QStringLiteral("math inline layout should exist"));
  require(!paragraphLayout->inlineLayout()->displayText().contains(QStringLiteral("\\frac")), QStringLiteral("collapsed inline math should render as atom"));
  require(paragraphLayout->inlineLayout()->mathAtomCount() == 1, QStringLiteral("collapsed inline math should create one native math atom"));

  const QString userInlineMathSample =
      QStringLiteral("行内公式使用单美元符号：$E = mc^2$ 和 $a_1 + b_1 = c_1$。");
  ParseResult userParsed = parser.parseDocument(userInlineMathSample, {});
  require(userParsed.root != nullptr, QStringLiteral("user inline math sample should parse"));
  MarkdownDocument userDocument;
  userDocument.setMarkdownText(userInlineMathSample, std::move(userParsed.root));
  DocumentLayout userLayout;
  userLayout.rebuild(userDocument, theme, 800.0);
  const MarkdownNode* userParagraph = findFirstBlock(userDocument.root(), BlockType::Paragraph);
  require(userParagraph != nullptr, QStringLiteral("user inline math sample paragraph should exist"));
  int inlineMathCount = 0;
  for (const InlineNode& inlineNode : userParagraph->inlines()) {
    if (inlineNode.type() == InlineType::InlineMath) {
      ++inlineMathCount;
    }
  }
  require(inlineMathCount == 2, QStringLiteral("single dollar sample should parse two inline math nodes"));
  const BlockLayout* userParagraphLayout = userLayout.block(userParagraph->id());
  require(userParagraphLayout != nullptr && userParagraphLayout->inlineLayout() != nullptr,
          QStringLiteral("user inline math sample should have inline layout"));
  require(userParagraphLayout->inlineLayout()->mathAtomCount() == 2,
          QStringLiteral("single dollar sample should create two native math atoms"));
  require(!userParagraphLayout->inlineLayout()->displayText().contains(QLatin1Char('$')) &&
              !userParagraphLayout->inlineLayout()->displayText().contains(QStringLiteral("mc^2")) &&
              !userParagraphLayout->inlineLayout()->displayText().contains(QStringLiteral("a_1")),
          QStringLiteral("inactive single dollar math should be collapsed to native atoms"));
  {
    const QString inlineMathSample = QStringLiteral("before $E=mc^2$ after");
    ParseResult inlineParsed = parser.parseDocument(inlineMathSample, {});
    require(inlineParsed.root != nullptr, QStringLiteral("inline math exact placeholder sample should parse"));
    MarkdownDocument inlineDocument;
    inlineDocument.setMarkdownText(inlineMathSample, std::move(inlineParsed.root));
    DocumentLayout inlineLayoutDocument;
    inlineLayoutDocument.rebuild(inlineDocument, theme, 800.0);
    const MarkdownNode* inlineParagraph = findFirstBlock(inlineDocument.root(), BlockType::Paragraph);
    require(inlineParagraph != nullptr, QStringLiteral("inline math exact placeholder paragraph should exist"));
    const BlockLayout* inlineParagraphLayout = inlineLayoutDocument.block(inlineParagraph->id());
    require(inlineParagraphLayout != nullptr && inlineParagraphLayout->inlineLayout() != nullptr,
            QStringLiteral("inline math exact placeholder layout should exist"));
    const qsizetype formulaStart = QStringLiteral("before ").size();
    const qsizetype formulaEnd = formulaStart + QStringLiteral("E=mc^2").size();
    const qreal reservedWidth = inlineParagraphLayout->inlineLayout()->cursorRect(formulaEnd).left() -
                                inlineParagraphLayout->inlineLayout()->cursorRect(formulaStart).left();
    const math::MathLayoutResult formulaLayout = math::MathRenderer().render(QStringLiteral("E=mc^2"), theme, false);
    require(formulaLayout.valid() && qAbs(reservedWidth - formulaLayout.size.width()) < 1.5,
            QStringLiteral("collapsed inline math placeholder should reserve the native math box width exactly"));

    const QColor transparent(Qt::transparent);
    QImage directImage(QSize(qCeil(formulaLayout.size.width()) + 32, qCeil(formulaLayout.size.height()) + 32), QImage::Format_ARGB32);
    directImage.fill(transparent);
    {
      QPainter painter(&directImage);
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setRenderHint(QPainter::TextAntialiasing, true);
      formulaLayout.paint(painter, QPointF(16.0, 16.0));
    }
    const QRect directInk = imageInkBounds(directImage, transparent);
    require(!directInk.isEmpty(), QStringLiteral("direct inline formula paint should produce ink"));

    QVector<InlineNode> mathOnlyInlines;
    mathOnlyInlines.push_back(InlineNode::inlineMath(QStringLiteral("E=mc^2")));
    InlineLayout mathOnlyLayout;
    mathOnlyLayout.build(mathOnlyInlines, QStringLiteral("$E=mc^2$"), theme, 360.0, theme.paragraphFont(), InlineLayout::BuildOptions{});
    require(mathOnlyLayout.mathAtomCount() == 1, QStringLiteral("isolated inline formula should collapse to one math atom"));
    require(mathOnlyLayout.displayText().size() == 1, QStringLiteral("isolated inline formula should use a single QTextLayout placeholder"));

    QImage inlineImage(QSize(360, qCeil(mathOnlyLayout.height()) + 32), QImage::Format_ARGB32);
    inlineImage.fill(transparent);
    {
      QPainter painter(&inlineImage);
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setRenderHint(QPainter::TextAntialiasing, true);
      mathOnlyLayout.paint(painter, QPointF(16.0, 16.0));
    }
    const QRect inlineInk = imageInkBounds(inlineImage, transparent);
    require(!inlineInk.isEmpty(), QStringLiteral("inline layout formula paint should produce ink"));

    const qreal isolatedReservedWidth = mathOnlyLayout.cursorRect(QStringLiteral("E=mc^2").size()).left() - mathOnlyLayout.cursorRect(0).left();
    require(qAbs(isolatedReservedWidth - formulaLayout.size.width()) < 1.5,
            QStringLiteral("isolated inline formula cursor span should match native formula width"));
    require(qAbs(inlineInk.width() - directInk.width()) <= 2,
            QStringLiteral("inline formula painted ink width should match direct native formula paint"));
    require(qAbs((inlineInk.left() - 16) - (directInk.left() - 16)) <= 2,
            QStringLiteral("inline formula painted ink should keep the same left bearing as direct native paint"));
  }
  {
    math::MathRenderNode rule;
    rule.kind = math::MathRenderKind::Rule;
    rule.width = 48.0;
    rule.height = 2.0;
    rule.ruleThickness = 2.0;
    rule.shift = -12.0;
    rule.color = theme.textColor();

    const QColor transparent(Qt::transparent);
    QImage ruleImage(QSize(96, 64), QImage::Format_ARGB32);
    ruleImage.fill(transparent);
    {
      QPainter rulePainter(&ruleImage);
      rule.paint(rulePainter, QPointF(16.0, 32.0));
    }
    const QRect ruleInk = imageInkBounds(ruleImage, transparent);
    require(!ruleInk.isEmpty(), QStringLiteral("horizontal rule paint should produce ink"));
    require(ruleInk.width() >= 46, QStringLiteral("fraction-style horizontal rule should paint across its full width"));
    require(qAbs(ruleInk.top() - 30) <= 1,
            QStringLiteral("horizontal rule paint should not apply node shift a second time"));
  }

  QImage image(QSize(480, qCeil(mathBlockLayout->height()) + 30), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  mathBlockLayout->paint(painter, theme, mathBlockLayout->rect().top() - 10.0);
  painter.end();

  int changedPixels = 0;
  const QRgb background = theme.backgroundColor().rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (background & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  require(changedPixels > 50, QStringLiteral("math block paint should draw visible pixels"));
}

void testLiteralBlockWrappedEditingGeometry() {
  RenderTheme theme = RenderTheme::github();
  const QString longLine = QStringLiteral("0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz");
  const QString markdown = QStringLiteral("```text\n%1\n```\n\n$$\n%1\n$$").arg(longLine);

  CmarkGfmParser parser;
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("wrapped literal parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));

  const MarkdownNode* code = findFirstBlock(document.root(), BlockType::CodeFence);
  const MarkdownNode* math = findFirstBlock(document.root(), BlockType::MathBlock);
  require(code != nullptr, QStringLiteral("wrapped code block should exist"));
  require(math != nullptr, QStringLiteral("wrapped math block should exist"));

  DocumentLayout codeLayout;
  codeLayout.rebuild(document, theme, 360.0);
  const BlockLayout* codeBlock = codeLayout.block(code->id());
  require(codeBlock != nullptr, QStringLiteral("wrapped code layout should exist"));
  const QRectF codeContent = codeBlock->literalContentRect(theme);
  const qreal codeLineHeight = theme.codeLineHeight();
  const HitTestResult codeSecondLineHit =
      codeBlock->hitTest(QPointF(codeContent.left() + 30.0, codeContent.top() + codeLineHeight * 1.4), theme);
  require(codeSecondLineHit.textOffset > 10, QStringLiteral("wrapped code hit-test should map into later visual line"));
  require(codeSecondLineHit.cursorRect.top() > codeContent.top() + codeLineHeight * 0.5,
          QStringLiteral("wrapped code cursor should move to visual wrapped line"));
  require(codeBlock->selectionRectsForOffsets(0, longLine.size(), theme).size() > 1,
          QStringLiteral("wrapped code selection should span multiple visual lines"));

  SelectionRange mathSelection;
  mathSelection.anchor.blockId = math->id();
  mathSelection.anchor.text.nodeId = math->id();
  mathSelection.focus = mathSelection.anchor;
  DocumentLayout mathEditingLayout;
  mathEditingLayout.rebuild(document, theme, 360.0, mathSelection);
  const BlockLayout* mathBlock = mathEditingLayout.block(math->id());
  require(mathBlock != nullptr && mathBlock->literalEditing(), QStringLiteral("wrapped math block should be editing"));
  const QRectF mathSource = mathBlock->literalContentRect(theme);
  const HitTestResult mathSecondLineHit =
      mathBlock->hitTest(QPointF(mathSource.left() + 30.0, mathSource.top() + codeLineHeight * 1.4), theme);
  require(mathSecondLineHit.textOffset > 10, QStringLiteral("wrapped math hit-test should map into later visual line"));
  require(mathSecondLineHit.cursorRect.top() > mathSource.top() + codeLineHeight * 0.5,
          QStringLiteral("wrapped math cursor should move to visual wrapped line"));
}

void testFrontMatterLayoutPreservesTrailingLiteralNewline() {
  RenderTheme theme = RenderTheme::github();

  DocumentSession withoutTrailingNewline;
  withoutTrailingNewline.setMarkdownText(QStringLiteral("---\ntitle: Muffin\n---"), false);
  DocumentLayout compactLayout;
  compactLayout.rebuild(withoutTrailingNewline.document(), theme, 800.0);
  const MarkdownNode* compactFrontMatter = findFirstBlock(withoutTrailingNewline.document().root(), BlockType::FrontMatter);
  require(compactFrontMatter != nullptr, QStringLiteral("compact front matter should parse"));
  const BlockLayout* compactBlock = compactLayout.block(compactFrontMatter->id());
  require(compactBlock != nullptr, QStringLiteral("compact front matter should layout"));

  DocumentSession withTrailingNewline;
  withTrailingNewline.setMarkdownText(QStringLiteral("---\ntitle: Muffin\n\n---"), false);
  DocumentLayout expandedLayout;
  expandedLayout.rebuild(withTrailingNewline.document(), theme, 800.0);
  const MarkdownNode* expandedFrontMatter = findFirstBlock(withTrailingNewline.document().root(), BlockType::FrontMatter);
  require(expandedFrontMatter != nullptr, QStringLiteral("expanded front matter should parse"));
  const BlockLayout* expandedBlock = expandedLayout.block(expandedFrontMatter->id());
  require(expandedBlock != nullptr, QStringLiteral("expanded front matter should layout"));

  require(expandedFrontMatter->literal().endsWith(QLatin1Char('\n')), QStringLiteral("expanded front matter literal should include user newline"));
  require(expandedBlock->literal() == expandedFrontMatter->literal(), QStringLiteral("front matter layout should preserve trailing literal newline"));
  require(expandedBlock->rect().height() > compactBlock->rect().height() + theme.codeLineHeight() * 0.5,
          QStringLiteral("front matter trailing newline should reserve a visible empty line"));
}

void testExtendedMathFunctionRendering() {
  RenderTheme theme = RenderTheme::github();

  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\n\\hat{x}+\\bar{y}+\\vec{v}+\\Bigl(\\operatorname{rank}_A B\\Bigr)+"
      "\\sum_{i=1}^{n} i+\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}\n$$");
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("extended math parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 800.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("extended math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          QStringLiteral("extended math block should have native layout"));
  require(blockLayout->mathLayout()->size.width() > 120.0, QStringLiteral("extended math layout should include function width"));
  require(blockLayout->mathLayout()->size.height() > theme.mathFont().pointSizeF() * 2.0,
          QStringLiteral("extended math layout should include scripts/array height"));

  QImage image(QSize(760, qCeil(blockLayout->height()) + 30), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  blockLayout->paint(painter, theme, blockLayout->rect().top() - 10.0);
  painter.end();

  int changedPixels = 0;
  const QRgb background = theme.backgroundColor().rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (background & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  require(changedPixels > 100, QStringLiteral("extended math paint should draw visible pixels"));
}

void testStrictMathGeometryFeatures() {
  math::MathSettings delimiterSettings;
  math::MathOptions delimiterOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), delimiterSettings);
  std::unique_ptr<math::MathRenderNode> biggParen =
      math::MathDelimiter::makeSized(QStringLiteral("("), 4, math::MathNodeType::Open, delimiterOptions);
  require(biggParen != nullptr && biggParen->fontClass == QStringLiteral("size4"),
          QStringLiteral("Bigg delimiter should use KaTeX Size4 font"));
  std::unique_ptr<math::MathRenderNode> tallBrace =
      math::MathDelimiter::makeLeftRight(QStringLiteral("{"), 95.0, 55.0, math::MathNodeType::Open, delimiterOptions);
  require(tallBrace != nullptr && !tallBrace->children.empty(), QStringLiteral("tall left brace should use stacked delimiter pieces"));
  require(tallBrace->height + tallBrace->depth > 80.0, QStringLiteral("stacked delimiter should grow to target height"));
  std::unique_ptr<math::MathRenderNode> tallBracket =
      math::MathDelimiter::makeLeftRight(QStringLiteral("["), 95.0, 55.0, math::MathNodeType::Open, delimiterOptions);
  require(tallBracket != nullptr && !tallBracket->svgPath.isEmpty() && tallBracket->viewBox.width() > 0.0,
          QStringLiteral("tall bracket delimiter should use KaTeX SVG geometry"));
  require(!math::MathSvgGeometry::tallDelimiterPath(QStringLiteral("lbrack"), 1000).isEmpty(),
          QStringLiteral("KaTeX tall delimiter SVG path should be generated"));
  require(!math::MathSvgGeometry::sqrtPath(QStringLiteral("sqrtTall"), 0.0, 3200).isEmpty(),
          QStringLiteral("KaTeX sqrt SVG path should be generated"));

  const auto requireMathSnippetLayout = [](const QString& tex, const QString& label) {
    math::MathSettings settings;
    settings.displayMode = true;
    math::MathParser mathParser(tex, settings);
    const QVector<math::MathParseNode> mathTree = mathParser.parse();
    math::MathOptions mathOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
    math::MathBuilder mathBuilder{mathOptions};
    std::unique_ptr<math::MathRenderNode> mathRoot = mathBuilder.buildExpression(mathTree);
    require(mathRoot != nullptr && mathRoot->width > 0.0, label + QStringLiteral(" direct math builder should produce layout"));
    RenderTheme theme = RenderTheme::github();
    CmarkGfmParser parser;
    const QString markdown = QStringLiteral("$$\n%1\n$$").arg(tex);
    ParseResult parsed = parser.parseDocument(markdown, {});
    require(parsed.root != nullptr, label + QStringLiteral(" parse should produce document"));
    MarkdownDocument document;
    document.setMarkdownText(markdown, std::move(parsed.root));
    DocumentLayout layout;
    layout.rebuild(document, theme, 900.0);
    const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
    require(mathBlock != nullptr, label + QStringLiteral(" math block should parse"));
    const BlockLayout* blockLayout = layout.block(mathBlock->id());
    require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
            label + QStringLiteral(" should have native layout"));
  };
  requireMathSnippetLayout(QStringLiteral("\\left\\lbrace\\frac{\\frac{a}{b}}{\\frac{c}{d}}\\right\\rbrace"), QStringLiteral("stacked brace delimiter"));
  requireMathSnippetLayout(QStringLiteral("\\left[\\begin{matrix}a\\\\\\frac{b}{c}\\\\d\\end{matrix}\\right]"), QStringLiteral("stacked bracket matrix delimiter"));
  requireMathSnippetLayout(QStringLiteral("\\left|\\frac{a}{\\frac{b}{c}}\\right|+\\left\\langle\\frac{x}{y}\\right\\rangle"), QStringLiteral("vertical and angle delimiter"));
  requireMathSnippetLayout(QStringLiteral("\\bigl( a \\bigr)+\\Bigl[ b \\Bigr]+\\biggl\\lbrace c \\biggr\\rbrace+\\Biggl| d \\Biggr|"), QStringLiteral("sized delimiter"));
  requireMathSnippetLayout(QStringLiteral("\\begin{cases}x^2,&x>0\\\\0,&x=0\\end{cases}+\\begin{smallmatrix}a&b\\\\c&d\\end{smallmatrix}"),
                           QStringLiteral("cases and smallmatrix environments"));
  requireMathSnippetLayout(QStringLiteral("\\begin{aligned}a&=b+c\\\\d&=e+f\\end{aligned}+\\begin{gathered}x+y\\\\z\\end{gathered}"),
                           QStringLiteral("aligned and gathered environments"));
  requireMathSnippetLayout(QStringLiteral("\\begin{array}{|c:rl@{}c|}\\hline\\hdashline a&b&c&d\\cr e&f&g&h\\\\[0.4em]\\hline\\end{array}"),
                           QStringLiteral("array preamble separators cr and row gap"));

  {
    math::MathParser parser(QStringLiteral("\\begin{array}{|c:rl@{}c|}\\hline\\hdashline a&b&c&d\\cr e&f&g&h\\\\[0.4em]\\hline\\end{array}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    require(!nodes.isEmpty() && nodes.first().type == math::MathNodeType::Array, QStringLiteral("array parser should produce array node"));
    const math::MathParseNode& array = nodes.first();
    int separators = 0;
    for (const math::MathArrayColumn& column : array.columns) {
      if (column.type == math::MathArrayColumn::Type::Separator) {
        ++separators;
      }
    }
    require(separators >= 3, QStringLiteral("array preamble should preserve solid and dashed separators"));
    require(array.arrayLines.size() == 3 && array.arrayLines.at(1).dashed, QStringLiteral("array should preserve multiple hline and hdashline entries"));
    require(!array.rowGaps.isEmpty() && array.rowGaps.last() > 0.0, QStringLiteral("array row gap should parse optional newline size"));
  }
  {
    math::MathSettings settings;
    settings.displayMode = true;
    math::MathParser parser(QStringLiteral("\\sum\\nolimits_{i=1}^n+\\lim\\limits_{x\\to0}+\\mathop{AB}\\limits^c"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    require(!nodes.isEmpty() && nodes.first().type == math::MathNodeType::SupSub,
            QStringLiteral("sum with nolimits should parse as supsub"));
    require(!nodes.first().base.isEmpty() && nodes.first().base.first().type == math::MathNodeType::Operator,
            QStringLiteral("sum supsub base should remain an operator"));
    require(nodes.first().base.first().explicitLimits && !nodes.first().base.first().limits,
            QStringLiteral("nolimits should explicitly disable operator limits"));

    bool sawExplicitLim = false;
    bool sawMathopBody = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::SupSub && !node.base.isEmpty() && node.base.first().type == math::MathNodeType::Operator) {
        const math::MathParseNode& op = node.base.first();
        if (op.label == QStringLiteral("\\lim") && op.explicitLimits && op.limits) {
          sawExplicitLim = true;
        }
        if (op.label == QStringLiteral("\\mathop") && !op.body.isEmpty() && op.explicitLimits && op.limits) {
          sawMathopBody = true;
        }
      }
    }
    require(sawExplicitLim && sawMathopBody, QStringLiteral("limits should apply to lim and mathop operators"));

    math::MathOptions displayOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
    std::unique_ptr<math::MathRenderNode> displaySum = math::MathBuilder(displayOptions).buildExpression(
        QVector<math::MathParseNode>{math::MathParser(QStringLiteral("\\sum"), settings).parse().first()});
    math::MathOptions textOptions(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), settings);
    std::unique_ptr<math::MathRenderNode> textSum = math::MathBuilder(textOptions).buildExpression(
        QVector<math::MathParseNode>{math::MathParser(QStringLiteral("\\sum"), settings).parse().first()});
    require(!displaySum->children.empty() && displaySum->children.front()->fontClass == QStringLiteral("size2"),
            QStringLiteral("display large operator should use KaTeX Size2 font"));
    require(!textSum->children.empty() && textSum->children.front()->fontClass == QStringLiteral("size1"),
            QStringLiteral("text large operator should use KaTeX Size1 font"));
    require(displaySum->height + displaySum->depth > textSum->height + textSum->depth,
            QStringLiteral("display large operator should be taller than text style operator"));

    std::vector<math::MathVListChild> individualChildren;
    individualChildren.push_back(math::MathVListChild{makeTestLayoutBox(4.0, 2.0, 1.0), 3.0});
    individualChildren.push_back(math::MathVListChild{makeTestLayoutBox(3.0, 1.0, 0.5), -2.0});
    auto individual = math::makeLayoutVListIndividualShift(std::move(individualChildren));
    require(individual->children.size() == 2 &&
                qAbs(individual->children.front()->shift - 3.0) < 0.01 &&
                qAbs(individual->children.back()->shift + 2.0) < 0.01 &&
                qAbs(individual->height - 3.0) < 0.01 &&
                qAbs(individual->depth - 4.0) < 0.01,
            QStringLiteral("native individualShift VList should match KaTeX getVListChildrenAndDepth semantics"));

    std::vector<math::MathVListEntry> shiftEntries;
    shiftEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(4.0, 2.0, 1.0)}));
    auto shifted = math::makeLayoutVListShift(0.5, std::move(shiftEntries));
    require(shifted->children.size() == 1 &&
                qAbs(shifted->children.front()->shift - 0.5) < 0.01 &&
                qAbs(shifted->height - 1.5) < 0.01 &&
                qAbs(shifted->depth - 1.5) < 0.01,
            QStringLiteral("native shift VList should position baseline relative to first child like KaTeX"));

    std::vector<math::MathVListEntry> bottomEntries;
    bottomEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(4.0, 2.0, 1.0)}));
    bottomEntries.push_back(math::makeLayoutVListKern(0.5));
    bottomEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(3.0, 1.0, 0.5)}));
    auto bottom = math::makeLayoutVListBottom(2.2, std::move(bottomEntries));
    require(qAbs(bottom->height - 2.8) < 0.01 && qAbs(bottom->depth - 2.2) < 0.01,
            QStringLiteral("native bottom VList should expose KaTeX maxPos/minPos height and depth"));

    std::vector<math::MathVListEntry> topEntries;
    topEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(4.0, 2.0, 1.0)}));
    topEntries.push_back(math::makeLayoutVListKern(0.5));
    topEntries.push_back(math::makeLayoutVListElem(math::MathVListChild{makeTestLayoutBox(3.0, 1.0, 0.5)}));
    auto top = math::makeLayoutVListTop(3.0, std::move(topEntries));
    require(qAbs(top->height - 3.0) < 0.01 && qAbs(top->depth - 2.0) < 0.01,
            QStringLiteral("native top VList should derive bottom from total child stack like KaTeX"));

    const QVector<math::MathParseNode> integralNodes = math::MathParser(QStringLiteral("\\int_0^1"), settings).parse();
    require(!integralNodes.isEmpty() && integralNodes.first().type == math::MathNodeType::SupSub,
            QStringLiteral("integral limits syntax should parse as supsub"));
    require(!integralNodes.first().base.isEmpty() && integralNodes.first().base.first().type == math::MathNodeType::Operator,
            QStringLiteral("integral supsub base should remain an operator"));
    require(integralNodes.first().base.first().label == QStringLiteral("\\int") &&
                integralNodes.first().base.first().opSymbol &&
                !integralNodes.first().base.first().limits,
            QStringLiteral("integral operators should match KaTeX op.ts no-limits symbol category"));

    std::unique_ptr<math::MathRenderNode> integralSupSub = math::MathBuilder(displayOptions).buildExpression(integralNodes);
    std::unique_ptr<math::MathRenderNode> sumLimits =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("\\sum_0^1"), settings).parse());
    require(integralSupSub != nullptr && !integralSupSub->children.empty() &&
                integralSupSub->children.front()->kind == math::MathRenderKind::Span &&
                integralSupSub->children.front()->children.size() == 2 &&
                integralSupSub->children.front()->children.back()->kind == math::MathRenderKind::VList,
            QStringLiteral("integral should render side scripts through KaTeX-style base plus msupsub VList"));
    require(sumLimits != nullptr && !sumLimits->children.empty() &&
                sumLimits->children.front()->kind == math::MathRenderKind::VList,
            QStringLiteral("sum should render display limits through native KaTeX VList layout"));
    const math::MathRenderNode& sumVList = *sumLimits->children.front();
    require(sumVList.children.size() == 3, QStringLiteral("sum limits VList should contain base, superscript, and subscript"));
    bool sawLimitBase = false;
    bool sawUpperLimit = false;
    bool sawLowerLimit = false;
    const std::function<bool(const math::MathRenderNode*)> containsSize2 =
        [&](const math::MathRenderNode* node) -> bool {
      if (node == nullptr) {
        return false;
      }
      if (node->fontClass == QStringLiteral("size2")) {
        return true;
      }
      for (const auto& child : node->children) {
        if (containsSize2(child.get())) {
          return true;
        }
      }
      return false;
    };
    for (const auto& child : sumVList.children) {
      if (containsSize2(child.get())) {
        sawLimitBase = true;
      } else if (child->shift < 0.0) {
        sawUpperLimit = true;
      } else if (child->shift > 0.0) {
        sawLowerLimit = true;
      }
    }
    require(sawLimitBase && sawUpperLimit && sawLowerLimit,
            QStringLiteral("operator limits should preserve KaTeX baseShift and upper/lower individual shifts"));
    const math::MathRenderNode& integralScripts = *integralSupSub->children.front();
    const math::MathRenderNode& integralScriptVList = *integralScripts.children.back();
    require(integralScriptVList.children.size() == 2 &&
                integralScriptVList.children.front()->shift > 0.0 &&
                integralScriptVList.children.back()->shift < 0.0,
            QStringLiteral("ordinary supsub VList should preserve KaTeX sub and sup individual shifts"));
    require(integralScriptVList.children.front()->xOffset < integralScriptVList.children.back()->xOffset,
            QStringLiteral("integral side scripts should use KaTeX side-script italic correction"));
    require(integralScriptVList.width > integralScriptVList.children.back()->xOffset + integralScriptVList.children.back()->width,
            QStringLiteral("ordinary supsub VList width should include KaTeX scriptspace marginRight"));

    std::unique_ptr<math::MathRenderNode> integralThenOrd =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("\\int_0^1x"), settings).parse());
    require(integralThenOrd != nullptr && integralThenOrd->children.size() >= 2,
            QStringLiteral("integral followed by ord should build adjacent atoms"));
    require(integralThenOrd->children.front()->width >= integralScripts.width,
            QStringLiteral("parent hlist should advance past the ordinary supsub script box"));

    std::unique_ptr<math::MathRenderNode> italicSupSub =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("y_i^2"), settings).parse());
    require(italicSupSub != nullptr && !italicSupSub->children.empty() &&
                italicSupSub->children.front()->kind == math::MathRenderKind::Span &&
                italicSupSub->children.front()->children.size() == 2 &&
                italicSupSub->children.front()->children.back()->kind == math::MathRenderKind::VList,
            QStringLiteral("single italic base should render supsub through KaTeX-style msupsub VList"));
    const math::MathRenderNode& italicScripts = *italicSupSub->children.front();
    const math::MathRenderNode& italicScriptVList = *italicScripts.children.back();
    require(italicScriptVList.children.size() == 2 &&
                italicScriptVList.children.front()->xOffset < italicScriptVList.children.back()->xOffset,
            QStringLiteral("superscript should include base italic correction while subscript backs it out"));

    std::unique_ptr<math::MathRenderNode> groupedSup =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("{xy}^2"), settings).parse());
    std::unique_ptr<math::MathRenderNode> charSup =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("x^2"), settings).parse());
    require(groupedSup != nullptr && charSup != nullptr && !groupedSup->children.empty() && !charSup->children.empty(),
            QStringLiteral("character-box supsub comparison should build"));
    require(groupedSup->height >= charSup->height,
            QStringLiteral("non-character-box superscript should apply supDrop-derived shift"));
  }
  {
    math::MathSettings settings;
    math::MathOptions displayOptions(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
    QVector<math::MathParseNode> topLevelCrNodes = math::MathParser(QStringLiteral("a\\\\b"), settings).parse();
    require(topLevelCrNodes.size() == 3 && topLevelCrNodes.at(1).type == math::MathNodeType::Cr,
            QStringLiteral("top-level double backslash should parse as KaTeX cr node"));
    std::unique_ptr<math::MathRenderNode> topLevelCr =
        math::MathBuilder(displayOptions).buildExpression(topLevelCrNodes);
    std::unique_ptr<math::MathRenderNode> topLevelCrGap =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("a\\\\[0.4em]b"), settings).parse());
    require(topLevelCr != nullptr && topLevelCr->kind == math::MathRenderKind::VList && topLevelCr->children.size() == 2,
            QStringLiteral("top-level cr should render as native KaTeX newline VList"));
    require(topLevelCrGap->height + topLevelCrGap->depth > topLevelCr->height + topLevelCr->depth + displayOptions.fontPointSize() * 0.3,
            QStringLiteral("top-level cr optional size should increase native newline gap"));

    std::unique_ptr<math::MathRenderNode> defaultArray =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("\\begin{array}{c}a\\\\b\\end{array}"), settings).parse());
    std::unique_ptr<math::MathRenderNode> gapArray =
        math::MathBuilder(displayOptions).buildExpression(math::MathParser(QStringLiteral("\\begin{array}{c}a\\\\[0.4em]b\\end{array}"), settings).parse());
    require(defaultArray != nullptr && gapArray != nullptr,
            QStringLiteral("array row gap comparison should build"));
    require(gapArray->height + gapArray->depth > defaultArray->height + defaultArray->depth + displayOptions.fontPointSize() * 0.3,
            QStringLiteral("positive array row gap should include KaTeX arstrutDepth plus requested gap"));
    require(!defaultArray->children.empty() && defaultArray->children.front()->kind == math::MathRenderKind::Array,
            QStringLiteral("array individualShift comparison should unwrap root hlist"));
    const math::MathRenderNode& arrayNode = *defaultArray->children.front();
    require(!arrayNode.children.empty() && arrayNode.children.front()->kind == math::MathRenderKind::VList,
            QStringLiteral("array should build a KaTeX-style table body VList"));
    require(!arrayNode.children.front()->children.empty() &&
                arrayNode.children.front()->children.front()->kind == math::MathRenderKind::Span &&
                !arrayNode.children.front()->children.front()->children.empty() &&
                arrayNode.children.front()->children.front()->children.front()->kind == math::MathRenderKind::VList,
            QStringLiteral("array columns should be native KaTeX column VLists"));
    const math::MathRenderNode& firstColumn = *arrayNode.children.front()->children.front()->children.front();
    require(firstColumn.children.size() >= 2,
            QStringLiteral("array individualShift comparison should find row wrapper cells"));
    const qreal baselineDelta = firstColumn.children.at(1)->shift - firstColumn.children.at(0)->shift;
    require(qAbs(baselineDelta - (firstColumn.children.at(0)->depth + firstColumn.children.at(1)->height)) < displayOptions.fontPointSize() * 0.2,
            QStringLiteral("array row baselines should follow KaTeX makeVList individualShift row.pos-offset semantics"));
    require(!firstColumn.children.at(0)->children.empty() &&
                firstColumn.children.at(0)->height > firstColumn.children.at(0)->children.front()->height,
            QStringLiteral("array row wrapper should keep KaTeX arstrut height separate from real cell content"));
  }

  RenderTheme theme = RenderTheme::github();
  CmarkGfmParser parser;
  const QString markdown = QStringLiteral(
      "$$\n"
      "\\left(\\frac{a}{b}+\\sqrt{\\frac{c}{d}}+\\sqrt{\\frac{\\frac{a}{b}}{\\frac{c}{d}}}\\right)"
      "+\\widehat{abcdef}+\\widetilde{abcdef}+\\overleftrightarrow{AB}"
      "+\\begin{array}{rcl}\\hline x&=&1\\\\longname&=&\\frac{a}{b}\\\\\\hline\\end{array}"
      "+\\begin{cases}x,&x>0\\\\0,&x=0\\end{cases}+\\begin{aligned}a&=b\\\\c&=d\\end{aligned}"
      "+\\binom{n}{k}+\\underline{text}+\\overline{abc}+\\text{hello}"
      "+\\underbrace{x+y}_{n}+\\overbrace{a+b}^{m}+\\underleftrightarrow{AB}"
      "\n$$");
  ParseResult parsed = parser.parseDocument(markdown, {});
  require(parsed.root != nullptr, QStringLiteral("strict geometry parse should produce document"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));
  DocumentLayout layout;
  layout.rebuild(document, theme, 900.0);

  const MarkdownNode* mathBlock = findFirstBlock(document.root(), BlockType::MathBlock);
  require(mathBlock != nullptr, QStringLiteral("strict geometry math block should parse"));
  const BlockLayout* blockLayout = layout.block(mathBlock->id());
  require(blockLayout != nullptr && blockLayout->mathLayout() != nullptr && blockLayout->mathLayout()->valid(),
          QStringLiteral("strict geometry math block should have native layout"));
  require(blockLayout->mathLayout()->size.height() > theme.mathFont().pointSizeF() * 3.0,
          QStringLiteral("left/right and array should increase native math height"));

  QImage image(QSize(860, qCeil(blockLayout->height()) + 40), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  blockLayout->paint(painter, theme, blockLayout->rect().top() - 12.0);
  painter.end();

  int changedPixels = 0;
  const QRgb background = theme.backgroundColor().rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (background & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  require(changedPixels > 120, QStringLiteral("strict geometry math paint should draw visible pixels"));
}

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

void testRemainingKatexFunctionFamilies() {
  const math::MathFunctionSpec* fracSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\frac"));
  require(fracSpec != nullptr && fracSpec->typeName == QStringLiteral("genfrac") && fracSpec->numArgs == 2,
          QStringLiteral("function registry should expose genfrac metadata"));
  require(fracSpec->handlerKind == math::MathFunctionHandlerKind::Fraction &&
              fracSpec->builderKind == math::MathFunctionBuilderKind::Fraction,
          QStringLiteral("function registry should map frac to parser and builder kinds"));

  const math::MathFunctionSpec* sqrtSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\sqrt"));
  require(sqrtSpec != nullptr && sqrtSpec->numArgs == 1 && sqrtSpec->numOptionalArgs == 1,
          QStringLiteral("function registry should expose sqrt optional argument metadata"));
  const math::MathFunctionSpec* genfracSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\genfrac"));
  require(genfracSpec != nullptr && genfracSpec->numArgs == 6 && genfracSpec->typeName == QStringLiteral("genfrac"),
          QStringLiteral("function registry should expose explicit genfrac metadata"));
  const math::MathFunctionSpec* textbfSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\textbf"));
  require(textbfSpec != nullptr && textbfSpec->typeName == QStringLiteral("text"),
          QStringLiteral("function registry should expose text font commands"));

  const math::MathFunctionSpec* hrefSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\href"));
  require(hrefSpec != nullptr && hrefSpec->requiresTrust && hrefSpec->allowedInText &&
              hrefSpec->argTypes.size() == 2 && hrefSpec->argTypes.first() == math::MathFunctionArgType::Url,
          QStringLiteral("function registry should expose href trust and argTypes metadata"));

  const math::MathFunctionSpec* htmlSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\htmlClass"));
  require(htmlSpec != nullptr && htmlSpec->strictCategory == QStringLiteral("htmlExtension"),
          QStringLiteral("function registry should expose html strict category"));

  const math::MathFunctionSpec* bigrSpec = math::MathFunctionRegistry::lookup(QStringLiteral("\\bigr"));
  require(bigrSpec != nullptr && bigrSpec->delimiterSize == 1 && bigrSpec->delimiterNodeType == math::MathNodeType::Close,
          QStringLiteral("function registry should expose delimiter sizing side metadata"));

  math::MathSettings strictSettings;
  strictSettings.throwOnError = true;
  {
    math::MathParser parser(QStringLiteral("\\sqrt[3]{x}+\\genfrac{[}{]}{0.08em}{0}{a}{b}+\\textbf{B}+\\mathit{x}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    require(!nodes.isEmpty() && nodes.first().type == math::MathNodeType::Sqrt && !nodes.first().rootIndex.isEmpty(),
            QStringLiteral("sqrt optional root index should parse"));
    bool sawGenfrac = false;
    bool sawTextbf = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::Fraction) {
        sawGenfrac = node.leftDelim == QStringLiteral("[") && node.rightDelim == QStringLiteral("]") &&
                     node.lineThickness > 0.0 && node.style == QStringLiteral("\\displaystyle");
      }
      if (node.type == math::MathNodeType::Text && node.fontClass == QStringLiteral("mathbf")) {
        sawTextbf = true;
      }
    }
    require(sawGenfrac, QStringLiteral("genfrac should parse delimiters line thickness style numerator and denominator"));
    require(sawTextbf, QStringLiteral("font text command should preserve font class"));
  }
  {
    math::MathSettings settings;
    math::MathOptions options(math::MathStyle::display(), 16.0, QColor(QStringLiteral("#111111")), settings);
    auto simpleSqrt = math::MathBuilder(options).buildExpression(math::MathParser(QStringLiteral("\\sqrt{x}"), settings).parse());
    auto indexedSqrt = math::MathBuilder(options).buildExpression(math::MathParser(QStringLiteral("\\sqrt[123]{x}"), settings).parse());
    auto tallSqrt = math::MathBuilder(options).buildExpression(
        math::MathParser(QStringLiteral("\\sqrt{\\frac{\\frac{\\frac{a}{b}}{\\frac{c}{d}}}{\\frac{\\frac{e}{f}}{\\frac{g}{h}}}}"), settings).parse());
    require(simpleSqrt != nullptr && indexedSqrt != nullptr && tallSqrt != nullptr, QStringLiteral("sqrt builder should produce render nodes"));
    require(indexedSqrt->width > simpleSqrt->width && indexedSqrt->height >= simpleSqrt->height,
            QStringLiteral("sqrt root index should add KaTeX-style left kern and vertical raise"));
    require(simpleSqrt->kind == math::MathRenderKind::Span && simpleSqrt->children.size() == 1 &&
                simpleSqrt->children.front()->kind == math::MathRenderKind::VList &&
                simpleSqrt->children.front()->children.size() == 2,
            QStringLiteral("sqrt body should render through native KaTeX VList span with radical and argument"));
    require(indexedSqrt->kind == math::MathRenderKind::Span && indexedSqrt->children.size() == 1 &&
                indexedSqrt->children.front()->kind == math::MathRenderKind::Span &&
                indexedSqrt->children.front()->children.size() == 2 &&
                indexedSqrt->children.front()->children.front()->kind == math::MathRenderKind::VList &&
                indexedSqrt->children.front()->children.back()->kind == math::MathRenderKind::VList,
            QStringLiteral("indexed sqrt should render root index and body through native KaTeX Span/VList layout"));
    const std::function<bool(const math::MathRenderNode*)> hasTallSqrt = [&](const math::MathRenderNode* node) -> bool {
      if (node == nullptr) {
        return false;
      }
      if (node->pathName == QStringLiteral("sqrtTall")) {
        return true;
      }
      for (const auto& child : node->children) {
        if (hasTallSqrt(child.get())) {
          return true;
        }
      }
      return false;
    };
    require(hasTallSqrt(tallSqrt.get()), QStringLiteral("large sqrt should use KaTeX tall sqrt SVG geometry"));
  }
  {
    math::MathParser parser(QStringLiteral("{a+b\\over c+d}+{n\\choose k}+{x\\atop y}+{p\\brace q}+{r\\brack s}+{u\\above 0.08em v}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    int sawFractions = 0;
    bool sawChooseDelims = false;
    bool sawBraceDelims = false;
    bool sawBrackDelims = false;
    bool sawAtopNoBar = false;
    bool sawAboveRule = false;
    const std::function<void(const math::MathParseNode&)> scanInfix = [&](const math::MathParseNode& node) {
      if (node.type == math::MathNodeType::Fraction) {
        ++sawFractions;
        if (node.leftDelim == QStringLiteral("(") && node.rightDelim == QStringLiteral(")")) {
          sawChooseDelims = true;
        }
        if (node.leftDelim == QStringLiteral("\\lbrace") && node.rightDelim == QStringLiteral("\\rbrace")) {
          sawBraceDelims = true;
        }
        if (node.leftDelim == QStringLiteral("[") && node.rightDelim == QStringLiteral("]")) {
          sawBrackDelims = true;
        }
        if (node.lineThickness == 0.0) {
          sawAtopNoBar = true;
        }
        if (node.lineThickness > 0.0) {
          sawAboveRule = true;
        }
      }
      for (const math::MathParseNode& child : node.body) {
        scanInfix(child);
      }
      for (const math::MathParseNode& child : node.base) {
        scanInfix(child);
      }
    };
    for (const math::MathParseNode& node : nodes) {
      scanInfix(node);
    }
    require(sawFractions >= 6 && sawChooseDelims && sawBraceDelims && sawBrackDelims && sawAtopNoBar && sawAboveRule,
            QStringLiteral("infix genfrac commands should rewrite to fractions with KaTeX delimiter and rule semantics"));

    math::MathSettings settings;
    math::MathOptions options(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), settings);
    std::unique_ptr<math::MathRenderNode> noBar = math::MathBuilder(options).buildExpression(
        QVector<math::MathParseNode>{math::MathParser(QStringLiteral("{x\\atop y}")).parse().first()});
    const std::function<bool(const math::MathRenderNode*)> hasRule =
        [&](const math::MathRenderNode* node) -> bool {
      if (node == nullptr) {
        return false;
      }
      if (node->kind == math::MathRenderKind::Rule) {
        return true;
      }
      for (const auto& child : node->children) {
        if (hasRule(child.get())) {
          return true;
        }
      }
      return false;
    };
    require(noBar != nullptr && !hasRule(noBar.get()), QStringLiteral("atop fraction should render without a rule child"));
  }
  {
    math::MathParser parser(QStringLiteral("\\@binrel{=}{x}+\\@binrel{+}{y}+\\overset{a}{=}+{ab}+\\boldsymbol{=}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    bool sawRelBinrel = false;
    bool sawBinBinrel = false;
    bool sawOversetRel = false;
    bool sawBoldRel = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::Class && node.mathClass == QStringLiteral("\\mathrel")) {
        if (!node.body.isEmpty() && node.body.first().type != math::MathNodeType::SupSub) {
          sawRelBinrel = true;
        }
        if (!node.body.isEmpty() && node.body.first().type == math::MathNodeType::SupSub) {
          sawOversetRel = true;
        }
        if (node.fontClass == QStringLiteral("mathbf")) {
          sawBoldRel = true;
        }
      }
      if (node.type == math::MathNodeType::Class && node.mathClass == QStringLiteral("\\mathbin")) {
        sawBinBinrel = true;
      }
    }
    require(sawRelBinrel && sawBinBinrel && sawOversetRel && sawBoldRel,
            QStringLiteral("mclass binrel and boldsymbol should preserve inferred math classes"));
  }
  {
    math::MathSettings settings;
    math::MathOptions textOptions(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), settings);
    const auto widthForWithOptions = [&](const QString& tex, math::MathOptions options) {
      math::MathParser parser(tex, settings);
      return math::MathBuilder(options).buildExpression(parser.parse())->width;
    };
    const auto widthFor = [&](const QString& tex) {
      return widthForWithOptions(tex, textOptions);
    };
    std::unique_ptr<math::MathRenderNode> italicSymbol =
        math::MathBuilder(textOptions).buildExpression(math::MathParser(QStringLiteral("f"), settings).parse());
    require(italicSymbol != nullptr && !italicSymbol->children.empty() &&
                italicSymbol->children.front()->italicMarginRight > 0.0,
            QStringLiteral("math italic symbol should preserve KaTeX SymbolNode italic margin-right"));
    const qreal plusBetweenOrd = widthFor(QStringLiteral("a+b"));
    const qreal plusAtStart = widthFor(QStringLiteral("+b"));
    const qreal plainOrdPair = widthFor(QStringLiteral("ab"));
    require(plusBetweenOrd > plusAtStart && plusBetweenOrd > plainOrdPair,
            QStringLiteral("mbin should keep medium spacing only between compatible atoms"));
    require(widthFor(QStringLiteral("a=+b")) < widthFor(QStringLiteral("a=b+c")),
            QStringLiteral("mbin after relation should downgrade to ord spacing"));
    require(widthFor(QStringLiteral("a,b")) > widthFor(QStringLiteral("ab")),
            QStringLiteral("mpunct should insert KaTeX thin spacing before following ord"));
    require(widthFor(QStringLiteral("\\left(x\\right)y")) > widthFor(QStringLiteral("xy")),
            QStringLiteral("minner should insert KaTeX thin spacing before following ord"));
    require(widthFor(QStringLiteral("a=b")) > widthFor(QStringLiteral("a{=}b")),
            QStringLiteral("ordgroup should participate as mord in outer spacing"));
    require(widthFor(QStringLiteral("a\\color{red}{+}b")) > widthFor(QStringLiteral("\\color{red}{+}b")),
            QStringLiteral("partial color group should still participate in KaTeX bin cancellation and spacing"));
    require(widthFor(QStringLiteral("\\color{red}{a}=b")) > widthFor(QStringLiteral("\\color{red}{ab}")),
            QStringLiteral("partial color group should expose outer atom class for implicit relation spacing"));
    math::MathOptions scriptOptions(math::MathStyle::script(), 16.0, QColor(QStringLiteral("#111111")), settings);
    require(widthForWithOptions(QStringLiteral("a+b"), scriptOptions) < plusBetweenOrd,
            QStringLiteral("tight script style should suppress binary spacing"));
    std::unique_ptr<math::MathRenderNode> scriptEquation =
        math::MathBuilder(textOptions).buildExpression(math::MathParser(QStringLiteral("a_1+b_1=c_1"), settings).parse());
    require(scriptEquation != nullptr && scriptEquation->children.size() >= 9,
            QStringLiteral("script equation should build a top-level hlist with atoms and glue"));
    int mediumGlueCount = 0;
    int thickGlueCount = 0;
    qreal summedWidth = 0.0;
    const qreal mediumGlue = textOptions.fontPointSize() * 4.0 / 18.0;
    const qreal thickGlue = textOptions.fontPointSize() * 5.0 / 18.0;
    for (const auto& child : scriptEquation->children) {
      summedWidth += child->width;
      if (child->text == QStringLiteral("glue")) {
        if (qAbs(child->width - mediumGlue) < 0.01) {
          ++mediumGlueCount;
        } else if (qAbs(child->width - thickGlue) < 0.01) {
          ++thickGlueCount;
        }
      }
    }
    require(mediumGlueCount == 2 && thickGlueCount == 2,
            QStringLiteral("a_1+b_1=c_1 should contain KaTeX medium glue around bin and thick glue around rel"));
    require(qAbs(scriptEquation->width - summedWidth) < 0.01,
            QStringLiteral("spacing glue widths should participate in hlist advance"));
  }
  {
    math::MathSettings fractionSettings;
    math::MathOptions fractionOptions(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), fractionSettings);
    std::unique_ptr<math::MathRenderNode> fraction =
        math::MathBuilder(fractionOptions).buildExpression(math::MathParser(QStringLiteral("\\frac{a}{b}"), fractionSettings).parse());
    require(fraction != nullptr && fraction->kind == math::MathRenderKind::Span && !fraction->children.empty(),
            QStringLiteral("fraction should be built from native KaTeX span/vlist layout instead of a paint-special fraction node"));
    const std::function<const math::MathRenderNode*(const math::MathRenderNode*)> findFractionVList =
        [&](const math::MathRenderNode* node) -> const math::MathRenderNode* {
      if (node == nullptr) {
        return nullptr;
      }
      if (node->kind == math::MathRenderKind::VList && node->children.size() == 3) {
        return node;
      }
      for (const auto& child : node->children) {
        if (const math::MathRenderNode* found = findFractionVList(child.get())) {
          return found;
        }
      }
      return nullptr;
    };
    const math::MathRenderNode* vlist = findFractionVList(fraction.get());
    require(vlist != nullptr && vlist->kind == math::MathRenderKind::VList && vlist->children.size() == 3,
            QStringLiteral("fraction vlist should contain denominator, rule, and numerator children"));
    require(vlist->height > 0.0 && vlist->depth > 0.0,
            QStringLiteral("fraction vlist should expose KaTeX height/depth box metrics"));
    require(vlist->children.at(0)->shift > 0.0 && vlist->children.at(2)->shift < 0.0,
            QStringLiteral("fraction vlist should use individualShift positions for denominator and numerator"));
    const auto metrics = math::MathFontMetrics::globalMetrics(fractionOptions.style().size());
    const qreal em = fractionOptions.fontPointSize();
    qreal numShift = metrics.num2 * em;
    qreal denomShift = metrics.denom2 * em;
    const qreal rule = qMax(metrics.defaultRuleThickness, fractionSettings.minRuleThickness) * em;
    const qreal axis = metrics.axisHeight * em;
    const qreal clearance = rule;
    const math::MathRenderNode* denominator = vlist->children.at(0).get();
    const math::MathRenderNode* numerator = vlist->children.at(2).get();
    if ((numShift - numerator->depth) - (axis + 0.5 * rule) < clearance) {
      numShift += clearance - ((numShift - numerator->depth) - (axis + 0.5 * rule));
    }
    if ((axis - 0.5 * rule) - (denominator->height - denomShift) < clearance) {
      denomShift += clearance - ((axis - 0.5 * rule) - (denominator->height - denomShift));
    }
    require(qAbs(vlist->children.at(0)->shift - denomShift) < 0.01 &&
                qAbs(vlist->children.at(1)->shift - (-(axis - 0.5 * rule))) < 0.01 &&
                qAbs(vlist->children.at(2)->shift - (-numShift)) < 0.01,
            QStringLiteral("fraction VList shifts should match KaTeX genfrac.ts without double-applying child shifts"));
    require(vlist->children.at(1)->kind == math::MathRenderKind::Rule,
            QStringLiteral("fraction rule should be a normal rule child in the vlist"));
  }
  {
    math::MathSettings settings;
    math::MathOptions textOptions(math::MathStyle::textStyle(), 20.0, QColor(QStringLiteral("#111111")), settings);
    math::MathOptions scriptOptions(math::MathStyle::script(), 20.0, QColor(QStringLiteral("#111111")), settings);
    auto widthFor = [&](const QString& tex, const math::MathOptions& options) {
      std::unique_ptr<math::MathRenderNode> root =
          math::MathBuilder(options).buildExpression(math::MathParser(tex, settings).parse());
      require(root != nullptr, QStringLiteral("math dimension sample should build: %1").arg(tex));
      return root->width;
    };
    require(qAbs(widthFor(QStringLiteral("\\rule{1em}{1em}"), textOptions) - 20.0) < 0.01,
            QStringLiteral("1em rule width should scale with current font size"));
    require(qAbs(widthFor(QStringLiteral("\\scriptstyle\\rule{1em}{1em}"), scriptOptions) - 20.0) < 0.01,
            QStringLiteral("1em rule width should use text-style font in script context (KaTeX units.ts:67-75)"));
    require(qAbs(widthFor(QStringLiteral("\\rule{1pt}{1pt}"), textOptions) - 2.0) < 0.01,
            QStringLiteral("1pt rule width should match KaTeX ptPerEm conversion"));
    require(qAbs(widthFor(QStringLiteral("\\scriptstyle\\rule{1pt}{1pt}"), scriptOptions) - 2.0) < 0.01,
            QStringLiteral("absolute pt rule width should not shrink in script style"));
    require(qAbs(widthFor(QStringLiteral("\\rule{1px}{1px}"), textOptions) - (803.0 / 800.0) * 2.0) < 0.01,
            QStringLiteral("1px rule width should follow KaTeX pdfTeX 1bp default"));
    require(qAbs(widthFor(QStringLiteral("\\rule{1bp}{1bp}"), textOptions) - (803.0 / 800.0) * 2.0) < 0.01,
            QStringLiteral("1bp rule width should follow KaTeX ptPerUnit"));
    require(qAbs(widthFor(QStringLiteral("\\rule{1in}{1in}"), textOptions) - 72.27 * 2.0) < 0.01,
            QStringLiteral("1in rule width should use TeX points like KaTeX"));
    require(qAbs(math::dimensionToPoints(QStringLiteral("1zz"), 20.0, 20.0)) < 0.01,
            QStringLiteral("unknown dimension units should not be treated as em"));
  }
  {
    RenderTheme theme = RenderTheme::github();
    math::MathSettings settings;
    const qreal rawWidth = math::MathBuilder(math::MathOptions(math::MathStyle::textStyle(),
                                                              theme.mathFont().pointSizeF() * 96.0 / 72.0,
                                                              theme.textColor(),
                                                              settings))
                           .buildExpression(math::MathParser(QStringLiteral("E=mc^2"), settings).parse())
                           ->width;
    require(qAbs(math::MathRenderer::katexRootFontPixelSize(theme) - theme.mathFont().pointSizeF() * 96.0 / 72.0 * 1.21) < 0.001,
            QStringLiteral("MathRenderer should convert Qt point size to CSS pixels before applying KaTeX root font scale"));
    const math::MathLayoutResult rendered = math::MathRenderer().render(QStringLiteral("E=mc^2"), theme, false);
    require(rendered.valid() && rendered.size.width() > rawWidth * 1.15,
            QStringLiteral("MathRenderer should apply KaTeX root font scale 1.21 after point-to-pixel conversion"));
  }
  {
    RenderTheme theme = RenderTheme::github();
    const QString ungrouped =
        QStringLiteral("\\int_0^1 x^2\\,dx = \\frac{1}{3}\\cdot\\frac{1}{\\frac{123}{\\sum_1^123}123}");
    const QString grouped =
        QStringLiteral("\\int_0^1 x^2\\,dx = \\frac{1}{3}\\cdot\\frac{1}{\\frac{123}{\\sum_1^{123}}123}");
    const math::MathLayoutResult ungroupedLayout = math::MathRenderer().render(ungrouped, theme, true);
    const math::MathLayoutResult groupedLayout = math::MathRenderer().render(grouped, theme, true);
    require(ungroupedLayout.valid() && groupedLayout.valid(), QStringLiteral("nested fraction with scripts should render"));
    require(ungroupedLayout.size.height() > theme.mathFont().pointSizeF() * 2.5,
            QStringLiteral("nested denominator sum should contribute to display formula layout height"));
    require(groupedLayout.size.height() >= ungroupedLayout.size.height(),
            QStringLiteral("braced sum exponent should not collapse nested denominator vlist height"));

    QImage image(QSize(qCeil(groupedLayout.size.width()) + 32, qCeil(groupedLayout.size.height()) + 32), QImage::Format_ARGB32);
    image.fill(theme.backgroundColor());
    {
      QPainter painter(&image);
      groupedLayout.paint(painter, QPointF(16.0, 16.0));
    }
    const QRect ink = imageInkBounds(image, theme.backgroundColor());
    require(!ink.isEmpty(), QStringLiteral("nested fraction grouped sum should paint visible ink"));
    require(ink.height() > groupedLayout.size.height() * 0.45,
            QStringLiteral("nested fraction grouped sum paint should occupy the reserved vertical box"));
  }
  {
    RenderTheme theme = RenderTheme::github();
    const QString wide = QStringLiteral("a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a+a");
    const math::MathLayoutResult natural = math::MathRenderer().render(wide, theme, true);
    const qreal maxWidth = qMax<qreal>(20.0, natural.size.width() * 0.45);
    const math::MathLayoutResult constrained = math::MathRenderer().render(wide, theme, true, maxWidth);
    require(constrained.valid(), QStringLiteral("constrained math layout should remain valid"));
    require(constrained.overflow, QStringLiteral("wide constrained math layout should report overflow"));
    require(qAbs(constrained.size.width() - maxWidth) < 0.01, QStringLiteral("constrained math layout should expose max width"));
    require(constrained.naturalSize.width() > constrained.size.width(), QStringLiteral("constrained math layout should preserve natural width"));

    QImage image(QSize(qCeil(maxWidth) + 80, qCeil(constrained.size.height()) + 32), QImage::Format_ARGB32);
    image.fill(theme.backgroundColor());
    {
      QPainter painter(&image);
      constrained.paint(painter, QPointF(16.0, 16.0));
    }
    const QRect ink = imageInkBounds(image, theme.backgroundColor());
    require(!ink.isEmpty(), QStringLiteral("constrained math layout should paint visible ink"));
    require(ink.right() <= 16 + qCeil(maxWidth), QStringLiteral("constrained math paint should be clipped to max width"));
  }
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
    bool sawHtmlBranch = false;
    bool sawHbox = false;
    bool sawRmDeclaration = false;
    for (const math::MathParseNode& node : nodes) {
      if (node.type == math::MathNodeType::Group && node.label == QStringLiteral("\\html@mathml") && !node.body.isEmpty()) {
        sawHtmlBranch = true;
      }
      if (node.type == math::MathNodeType::Group && node.label == QStringLiteral("\\hbox")) {
        sawHbox = true;
      }
      if (node.type == math::MathNodeType::Text && node.label == QStringLiteral("\\rm") && !node.body.isEmpty()) {
        sawRmDeclaration = true;
      }
    }
    require(sawHtmlBranch && sawHbox && sawRmDeclaration,
            QStringLiteral("htmlmathml hbox and old font declarations should preserve KaTeX-like parse semantics"));
  }
  {
    math::MathParser parser(QStringLiteral("\\begin{matrix}\\text{\\underline{text}}\\end{matrix}"));
    const QVector<math::MathParseNode> nodes = parser.parse();
    require(nodes.size() == 1 && nodes.first().type == math::MathNodeType::Array,
            QStringLiteral("text command inside matrix should not leak the environment terminator"));
    require(nodes.first().rows.size() == 1 && nodes.first().rows.first().size() == 1,
            QStringLiteral("matrix containing text should preserve a single cell"));

    math::MathParser oldFontParser(QStringLiteral("\\begin{matrix}\\rm rm & it\\\\ x&y\\end{matrix}"));
    const QVector<math::MathParseNode> oldFontNodes = oldFontParser.parse();
    require(oldFontNodes.size() == 1 && oldFontNodes.first().type == math::MathNodeType::Array &&
                oldFontNodes.first().rows.size() == 2,
            QStringLiteral("old font declarations should stop at array cell and row boundaries"));
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
        QStringLiteral("\\overrightarrow"), QStringLiteral("\\overleftarrow"),    QStringLiteral("\\Overrightarrow"),
        QStringLiteral("\\overleftrightarrow"), QStringLiteral("\\overgroup"),   QStringLiteral("\\overlinesegment"),
        QStringLiteral("\\overleftharpoon"), QStringLiteral("\\overrightharpoon")};
    for (const QString& label : stretchyAccentLabels) {
      math::MathParser parser(QStringLiteral("%1{AB}").arg(label));
      const QVector<math::MathParseNode> nodes = parser.parse();
      require(nodes.size() == 1 && nodes.first().type == math::MathNodeType::Accent,
              QStringLiteral("%1 should parse as a KaTeX stretchy accent").arg(label));
      std::unique_ptr<math::MathRenderNode> tree =
          math::MathBuilder(math::MathOptions(math::MathStyle::textStyle(), 16.0, QColor(QStringLiteral("#111111")), math::MathSettings())).buildExpression(nodes);
      require(tree != nullptr && tree->width < 16.0 * 4.0,
              QStringLiteral("%1 should render as SVG accent, not fallback command text").arg(label));
    }
  }

  bool threw = false;
  try {
    math::MathParser parser(QStringLiteral("a+\\definitelyUnsupported"), strictSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.tokenText() == QStringLiteral("\\definitelyUnsupported"), QStringLiteral("unsupported command error should keep token text"));
    require(error.position() == 2, QStringLiteral("unsupported command error should keep token position"));
    require(error.endPosition() == 24, QStringLiteral("unsupported command error should keep token end position"));
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
    require(error.tokenText() == QStringLiteral("\\htmlClass") && error.position() == 0,
            QStringLiteral("strict error should include source token position"));
    require(error.endPosition() == QStringLiteral("\\htmlClass").size(), QStringLiteral("strict error should include source token end position"));
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
    require(error.tokenText() == QStringLiteral("\\notInRegistryYet"), QStringLiteral("unknownSymbol should keep command token"));
  }
  require(threw, QStringLiteral("strict unknown command should throw unknownSymbol"));

  math::MathSettings strictUnicodeSettings;
  strictUnicodeSettings.strict = math::MathStrictMode::Error;
  threw = false;
  try {
    math::MathParser parser(QStringLiteral("\u4e2d"), strictUnicodeSettings);
    parser.parse();
  } catch (const math::MathParseError& error) {
    threw = true;
    require(error.message().contains(QStringLiteral("unicodeTextInMathMode")),
            QStringLiteral("strict unicode text in math should report category"));
    require(error.position() == 0, QStringLiteral("unicode strict error should keep token position"));
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
    require(error.tokenText() == QStringLiteral("\\href") && error.position() == 0,
            QStringLiteral("trust error should include href token position"));
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
                         context.attributes.value(QStringLiteral("data-y")).trimmed() == QStringLiteral("two") &&
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

void requireTextLayoutCursorRoundTrip(const InlineLayout& layout, qsizetype offset, const QString& label) {
  const QRectF cursor = layout.cursorRect(offset);
  require(!cursor.isEmpty(), label + QStringLiteral(" cursor rect should exist"));
  require(layout.hitTestTextOffset(QPointF(cursor.left(), cursor.center().y())) == offset,
          label + QStringLiteral(" cursor-left hit-test should round-trip"));
}

void requireTextLayoutCharacterBias(const InlineLayout& layout, qsizetype offset, const QString& label) {
  const QRectF leftCursor = layout.cursorRect(offset);
  const QRectF rightCursor = layout.cursorRect(offset + 1);
  require(!leftCursor.isEmpty() && !rightCursor.isEmpty(), label + QStringLiteral(" bias cursor rects should exist"));
  const qreal left = leftCursor.left();
  const qreal right = rightCursor.left();
  if (qAbs(right - left) < 0.5) {
    return;
  }
  const qreal quarter = left + (right - left) * 0.25;
  const qreal threeQuarter = left + (right - left) * 0.75;
  const qreal y = leftCursor.center().y();
  require(layout.hitTestTextOffset(QPointF(quarter, y)) == offset, label + QStringLiteral(" left half hit-test mismatch"));
  require(layout.hitTestTextOffset(QPointF(threeQuarter, y)) == offset + 1, label + QStringLiteral(" right half hit-test mismatch"));
}

void testInlineLayoutHitTesting() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));

  InlineLayout::BuildOptions probeOptions;
  InlineLayout plain;
  plain.build(plainInlines, theme, 125.0, theme.paragraphFont(), probeOptions);

  const QVector<qsizetype> offsets{0, 1, 5, 6, 12, 20, plain.plainText().size()};
  for (qsizetype offset : offsets) {
    requireTextLayoutCursorRoundTrip(plain, offset, QStringLiteral("plain offset %1").arg(offset));
  }
  requireTextLayoutCharacterBias(plain, 0, QStringLiteral("plain first char"));
  bool testedWrappedBias = false;
  for (qsizetype offset = 1; offset + 1 < plain.plainText().size(); ++offset) {
    const QRectF leftCursor = plain.cursorRect(offset);
    const QRectF rightCursor = plain.cursorRect(offset + 1);
    if (!leftCursor.isEmpty() && !rightCursor.isEmpty() && qAbs(leftCursor.center().y() - rightCursor.center().y()) < 0.5 &&
        qAbs(leftCursor.left() - rightCursor.left()) >= 0.5) {
      requireTextLayoutCharacterBias(plain, offset, QStringLiteral("plain same-line char"));
      testedWrappedBias = true;
      break;
    }
  }
  require(testedWrappedBias, QStringLiteral("inline hit-test should find a same-line bias fixture"));

  const QVector<QRectF> wrappedSelection = plain.selectionRects(0, plain.plainText().size());
  require(wrappedSelection.size() >= 2, QStringLiteral("inline hit-test fixture should wrap across lines"));
  for (const QRectF& rect : wrappedSelection) {
    require(plain.hitTestTextOffset(QPointF(rect.left(), rect.center().y())) >= 0, QStringLiteral("wrapped line start hit should be valid"));
    require(plain.hitTestTextOffset(rect.center()) >= 0, QStringLiteral("wrapped line middle hit should be valid"));
    require(plain.hitTestTextOffset(QPointF(rect.right(), rect.center().y())) >= 0, QStringLiteral("wrapped line end hit should be valid"));
  }

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions = probeOptions;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  requireTextLayoutCursorRoundTrip(active, 7, QStringLiteral("active visible bold start"));
  require(active.hitTestSourceOffset(QPointF(active.cursorRectForSourceOffset(9).left(), active.cursorRectForSourceOffset(9).center().y())) == 9,
          QStringLiteral("active marker source hit-test should not drift"));
}

void testInlineLayoutCursorRects() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));

  InlineLayout layout;
  InlineLayout::BuildOptions options;
  layout.build(plainInlines, theme, 125.0, theme.paragraphFont(), options);

  const QVector<qsizetype> offsets{0, 1, 5, 6, 12, 20, layout.plainText().size()};
  for (qsizetype offset : offsets) {
    const QRectF cursor = layout.cursorRect(offset);
    require(!cursor.isEmpty(), QStringLiteral("inline cursor rect should be non-empty"));
  }

  bool testedMonotonic = false;
  for (qsizetype offset = 0; offset + 3 < layout.plainText().size(); ++offset) {
    const QRectF first = layout.cursorRect(offset);
    const QRectF second = layout.cursorRect(offset + 1);
    const QRectF third = layout.cursorRect(offset + 2);
    if (!first.isEmpty() && !second.isEmpty() && !third.isEmpty() &&
        qAbs(first.center().y() - second.center().y()) < 0.5 && qAbs(second.center().y() - third.center().y()) < 0.5) {
      require(first.left() <= second.left() && second.left() <= third.left(), QStringLiteral("inline cursor x should be monotonic on same line"));
      testedMonotonic = true;
      break;
    }
  }
  require(testedMonotonic, QStringLiteral("inline cursor test should find same-line offsets"));

  bool foundWrap = false;
  for (qsizetype offset = 0; offset + 1 < layout.plainText().size(); ++offset) {
    const QRectF previous = layout.cursorRect(offset);
    const QRectF next = layout.cursorRect(offset + 1);
    if (!previous.isEmpty() && !next.isEmpty() && next.center().y() > previous.center().y() + previous.height() * 0.5) {
      require(next.left() < previous.left(), QStringLiteral("inline cursor x should return toward line start after wrap"));
      foundWrap = true;
      break;
    }
  }
  require(foundWrap, QStringLiteral("inline cursor test should observe a wrapped line"));

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions = options;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  const QRectF markerRect = active.cursorRectForSourceOffset(9);
  require(!markerRect.isEmpty(), QStringLiteral("inline source marker cursor rect should be non-empty"));
  require(active.hitTestSourceOffset(QPointF(markerRect.left(), markerRect.center().y())) == 9,
          QStringLiteral("inline source marker cursor rect should hit marker source offset"));
  const QRectF visibleRect = active.cursorRect(7);
  require(!visibleRect.isEmpty(), QStringLiteral("inline visible active cursor rect should be non-empty"));
  require(qAbs(visibleRect.center().y() - markerRect.center().y()) < qMax(visibleRect.height(), markerRect.height()),
          QStringLiteral("inline visible/source active cursor rects should stay on same line"));
}

void requireValidSelectionRects(const QVector<QRectF>& rects, const QString& label) {
  require(!rects.isEmpty(), label + QStringLiteral(" selection rects should not be empty"));
  for (const QRectF& rect : rects) {
    require(rect.width() > 0.0, label + QStringLiteral(" selection rect width should be positive"));
    require(rect.height() > 0.0, label + QStringLiteral(" selection rect height should be positive"));
  }
}

void testInlineLayoutSelectionRects() {
  RenderTheme theme = RenderTheme::github();

  QVector<InlineNode> shortInlines;
  shortInlines.push_back(InlineNode::text(QStringLiteral("alpha beta")));
  InlineLayout singleLine;
  singleLine.build(shortInlines, theme, 400.0, theme.paragraphFont());
  const QVector<QRectF> single = singleLine.selectionRects(1, 6);
  require(single.size() == 1, QStringLiteral("inline single-line selection should produce one rect"));
  requireValidSelectionRects(single, QStringLiteral("inline single-line"));
  const QVector<QRectF> reverseSingle = singleLine.selectionRects(6, 1);
  require(reverseSingle.size() == single.size(), QStringLiteral("inline reverse selection should keep rect count"));
  require(qAbs(reverseSingle.first().left() - single.first().left()) < 0.5 &&
              qAbs(reverseSingle.first().width() - single.first().width()) < 0.5,
          QStringLiteral("inline reverse selection should match forward rect"));
  require(singleLine.selectionRects(3, 3).isEmpty(), QStringLiteral("inline collapsed selection should return no rects"));

  QVector<InlineNode> wrappedInlines;
  wrappedInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));
  InlineLayout wrapped;
  wrapped.build(wrappedInlines, theme, 125.0, theme.paragraphFont());
  const QVector<QRectF> wrappedRects = wrapped.selectionRects(0, wrapped.plainText().size());
  require(wrappedRects.size() >= 2, QStringLiteral("inline wrapped selection should span multiple rects"));
  requireValidSelectionRects(wrappedRects, QStringLiteral("inline wrapped"));
  for (qsizetype i = 1; i < wrappedRects.size(); ++i) {
    require(wrappedRects.at(i).top() > wrappedRects.at(i - 1).top(), QStringLiteral("inline wrapped selection rects should move downward"));
  }

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  const QVector<QRectF> visibleContent = active.selectionRects(7, 11);
  require(visibleContent.size() == 1, QStringLiteral("inline active visible content selection should stay single-line"));
  requireValidSelectionRects(visibleContent, QStringLiteral("inline active visible content"));
  const QRectF markerStart = active.cursorRectForSourceOffset(7);
  const QRectF markerEnd = active.cursorRectForSourceOffset(9);
  require(!markerStart.isEmpty() && !markerEnd.isEmpty(), QStringLiteral("inline active marker cursor rects should exist"));
  require(markerEnd.left() > markerStart.left(), QStringLiteral("inline active marker source rect should cover visible marker width"));
  const QVector<QRectF> openerMarker = active.selectionRectsForSourceOffsets(7, 9);
  require(openerMarker.size() == 1, QStringLiteral("inline active marker source selection should stay single-line"));
  requireValidSelectionRects(openerMarker, QStringLiteral("inline active marker source"));
  require(qAbs(openerMarker.first().left() - markerStart.left()) < 0.5,
          QStringLiteral("inline active marker source selection should start at opener marker"));
  require(openerMarker.first().right() >= markerEnd.left() - 0.5,
          QStringLiteral("inline active marker source selection should cover opener marker width"));
}

bool hasRole(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role) {
      return true;
    }
  }
  return false;
}

const BlockLayout::TableCellLayout* findTableCellLayout(const BlockLayout& tableLayout, NodeId cellId) {
  for (const BlockLayout::TableRowLayout& row : tableLayout.tableRows()) {
    for (const BlockLayout::TableCellLayout& cell : row.cells) {
      if (cell.nodeId == cellId) {
        return &cell;
      }
    }
  }
  return nullptr;
}

int changedPixelCount(const QImage& image, QColor background) {
  int changedPixels = 0;
  const QRgb backgroundRgb = background.rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (backgroundRgb & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  return changedPixels;
}

QRect imageInkBounds(const QImage& image, QColor background) {
  const bool transparentBackground = background.alpha() == 0;
  const QRgb backgroundRgb = background.rgb() & 0x00ffffff;
  int left = image.width();
  int top = image.height();
  int right = -1;
  int bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = image.pixel(x, y);
      if (transparentBackground) {
        if (qAlpha(pixel) == 0) {
          continue;
        }
      } else if ((pixel & 0x00ffffff) == backgroundRgb) {
        continue;
      }
      left = qMin(left, x);
      top = qMin(top, y);
      right = qMax(right, x);
      bottom = qMax(bottom, y);
    }
  }
  if (right < left || bottom < top) {
    return {};
  }
  return QRect(left, top, right - left + 1, bottom - top + 1);
}

void testDocumentLayoutInlineLayoutContract() {
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("alpha `code` beta gamma delta epsilon zeta eta theta\n\n| A | B |\n| --- | --- |\n| `one` | two |"),
      false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 360.0);

  const MarkdownNode* paragraph = findFirstBlock(session.document().root(), BlockType::Paragraph);
  const MarkdownNode* table = findFirstBlock(session.document().root(), BlockType::Table);
  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(paragraph != nullptr && table != nullptr && tableCell != nullptr, QStringLiteral("inline layout document fixture missing blocks"));

  const BlockLayout* paragraphLayout = layout.block(paragraph->id());
  const BlockLayout* tableLayout = layout.block(table->id());
  require(paragraphLayout != nullptr && paragraphLayout->inlineLayout() != nullptr, QStringLiteral("inline layout paragraph layout missing"));
  require(tableLayout != nullptr && tableLayout->type() == BlockType::Table, QStringLiteral("inline layout table layout missing"));
  const BlockLayout::TableCellLayout* cellLayout = findTableCellLayout(*tableLayout, tableCell->id());
  require(cellLayout != nullptr, QStringLiteral("inline layout table cell layout missing"));

  require(paragraphLayout->inlineLayout()->height() > 0.0, QStringLiteral("paragraph inline layout should have height"));
  require(paragraphLayout->inlineLayout()->size().height() == paragraphLayout->inlineLayout()->height(),
          QStringLiteral("paragraph inline layout size should expose height"));
  require(cellLayout->text.height() > 0.0, QStringLiteral("table cell inline layout should have height"));
  require(cellLayout->text.size().height() == cellLayout->text.height(), QStringLiteral("table cell inline layout size should expose height"));
  for (const BlockLayout::TableRowLayout& row : tableLayout->tableRows()) {
    for (const BlockLayout::TableCellLayout& cell : row.cells) {
      require(!cell.text.displayText().contains(QLatin1Char('|')), QStringLiteral("table cell layout should hide markdown pipe separators"));
    }
  }

  QImage image(QSize(420, qCeil(layout.totalHeight()) + 20), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  for (const auto& block : layout.blocks()) {
    block->paint(painter, theme, 0.0);
  }
  painter.end();
  require(changedPixelCount(image, theme.backgroundColor()) > 100, QStringLiteral("document inline layout paint should draw visible pixels"));

  const HitTestResult paragraphHit = layout.hitTest(paragraphLayout->rect().center(), theme);
  require(paragraphHit.isValid() && paragraphHit.zone == HitTestResult::Zone::Text,
          QStringLiteral("document inline layout paragraph hit-test should hit text"));
  require(!paragraphLayout->selectionRectsForOffsets(0, 5, theme).isEmpty(),
          QStringLiteral("document inline layout paragraph selection should be available"));

  const HitTestResult tableHit = layout.hitTest(cellLayout->rect.center(), theme);
  require(tableHit.isValid() && tableHit.zone == HitTestResult::Zone::TableCell,
          QStringLiteral("document inline layout table hit-test should hit table cell"));
  require(!tableLayout->selectionRectsForOffsets(0, 1, theme).isEmpty(),
          QStringLiteral("document inline layout table selection should be available"));
}

bool hasExactRoleSpan(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role, qsizetype start, qsizetype end) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role && span.start == start && span.end == end) {
      return true;
    }
  }
  return false;
}

struct HighlightFixture {
  QString language;
  QString code;
  QVector<CodeHighlightRole> roles;
};

void requireHighlightsFixture(const TreeSitterHighlighter& highlighter, const HighlightFixture& fixture) {
  require(highlighter.supportsLanguage(fixture.language), QStringLiteral("%1 highlighting should be registered").arg(fixture.language));
  const QVector<CodeHighlightSpan> spans = highlighter.highlight(fixture.language, fixture.code);
  require(!spans.isEmpty(), QStringLiteral("%1 should produce highlight spans").arg(fixture.language));
  for (CodeHighlightRole role : fixture.roles) {
    require(hasRole(spans, role), QStringLiteral("%1 should highlight requested role %2").arg(fixture.language).arg(static_cast<int>(role)));
  }
}

void testTreeSitterCodeHighlighting() {
  TreeSitterHighlighter highlighter;
  const QVector<HighlightFixture> fixtures{
      {QStringLiteral("python"), QStringLiteral("def greet(name):\n    return \"hello \" + name\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("cpp"), QStringLiteral("#include <QApplication>\nint main() { return 0; }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("bash"), QStringLiteral("#!/usr/bin/env bash\nif [ -n \"$HOME\" ]; then echo \"ok\"; fi\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("c"), QStringLiteral("#include <stdio.h>\nint main(void) { return 0; }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Function}},
      {QStringLiteral("csharp"), QStringLiteral("public class App { string Name => \"muffin\"; }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("css"), QStringLiteral(".note { color: red; margin: 1px; }\n"), {CodeHighlightRole::Property, CodeHighlightRole::Number}},
      {QStringLiteral("go"), QStringLiteral("package main\nfunc main() { println(\"hi\") }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("html"), QStringLiteral("<section class=\"note\">hello</section>\n"), {CodeHighlightRole::Type, CodeHighlightRole::String}},
      {QStringLiteral("ini"), QStringLiteral("[core]\nname=muffin\n"), {CodeHighlightRole::Property}},
      {QStringLiteral("java"), QStringLiteral("class App { String name() { return \"muffin\"; } }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("javascript"), QStringLiteral("function greet(name) { return `hi ${name}`; }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("json"), QStringLiteral("{\"name\": \"muffin\", \"count\": 2}\n"), {CodeHighlightRole::String, CodeHighlightRole::Number}},
      {QStringLiteral("lua"), QStringLiteral("local name = \"muffin\"\nfunction greet() return name end\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("markdown"), QStringLiteral("# Title\n\n- `code` [link](https://example.com)\n"), {CodeHighlightRole::Type, CodeHighlightRole::Punctuation}},
      {QStringLiteral("objective-c"), QStringLiteral("@interface App\n- (void)run;\n@end\n"), {CodeHighlightRole::Keyword}},
      {QStringLiteral("php"), QStringLiteral("function greet($name) { return \"hi\"; }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("powershell"), QStringLiteral("function Test { Write-Host \"hi\" }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("qml"), QStringLiteral("Item { property string title: \"Muffin\" }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Property}},
      {QStringLiteral("r"), QStringLiteral("name <- \"muffin\"\nprint(name)\n"), {CodeHighlightRole::String, CodeHighlightRole::Function}},
      {QStringLiteral("ruby"), QStringLiteral("def greet(name)\n  \"hi #{name}\"\nend\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("rust"), QStringLiteral("fn main() { let name = \"muffin\"; println!(\"{}\", name); }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("toml"), QStringLiteral("name = \"muffin\"\ncount = 2\n"), {CodeHighlightRole::String, CodeHighlightRole::Number}},
      {QStringLiteral("typescript"), QStringLiteral("function greet(name: string): string { return `hi ${name}`; }\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::Function, CodeHighlightRole::String}},
      {QStringLiteral("tsx"), QStringLiteral("export const App = () => <div className=\"note\">Hi</div>;\n"), {CodeHighlightRole::Keyword, CodeHighlightRole::String}},
      {QStringLiteral("xml"), QStringLiteral("<note priority=\"1\">hello</note>\n"), {CodeHighlightRole::Type, CodeHighlightRole::String}},
      {QStringLiteral("yaml"), QStringLiteral("name: muffin\ncount: 2\n"), {CodeHighlightRole::String, CodeHighlightRole::Number}},
  };
  for (const HighlightFixture& fixture : fixtures) {
    requireHighlightsFixture(highlighter, fixture);
  }

  const QStringList aliases{
      QStringLiteral("py"),
      QStringLiteral("hxx"),
      QStringLiteral("sh"),
      QStringLiteral("cs"),
      QStringLiteral("js"),
      QStringLiteral("ts"),
      QStringLiteral("rs"),
      QStringLiteral("rb"),
      QStringLiteral("yml"),
      QStringLiteral("md"),
      QStringLiteral("ps1"),
      QStringLiteral("objc"),
  };
  for (const QString& alias : aliases) {
    require(highlighter.supportsLanguage(alias), QStringLiteral("%1 alias should be registered").arg(alias));
  }

  const QString cppText = QStringLiteral("// 中文\nint main() { return 0; }\n");
  const QVector<CodeHighlightSpan> cppExact = highlighter.highlight(QStringLiteral("cpp"), cppText);
  const qsizetype returnStart = cppText.indexOf(QStringLiteral("return"));
  require(hasExactRoleSpan(cppExact, CodeHighlightRole::Keyword, returnStart, returnStart + 6),
          QStringLiteral("cpp keyword span should align exactly after utf-16 text"));

  require(!highlighter.supportsLanguage(QStringLiteral("text")), QStringLiteral("text should remain unhighlighted"));
  require(!highlighter.supportsLanguage(QStringLiteral("pgp")), QStringLiteral("pgp should remain unhighlighted"));
  require(highlighter.highlight(QStringLiteral("text"), QStringLiteral("plain text")).isEmpty(), QStringLiteral("text should not highlight"));
}

void testLayoutForTheme(const MarkdownDocument& document, const RenderTheme& theme, const QString& themeName) {
  DocumentLayout layout;
  layout.rebuild(document, theme, 1000.0);

  require(layout.pageWidth() > 300.0, QStringLiteral("%1 page width too small").arg(themeName));
  require(layout.totalHeight() > 700.0, QStringLiteral("%1 total height should exceed smoke viewport").arg(themeName));
  require(!layout.blocks().empty(), QStringLiteral("%1 layout produced no blocks").arg(themeName));

  const MarkdownNode* heading = findFirstBlock(document.root(), BlockType::Heading);
  const MarkdownNode* paragraph = findFirstBlock(document.root(), BlockType::Paragraph);
  const MarkdownNode* table = findFirstBlock(document.root(), BlockType::Table);
  const MarkdownNode* tableCell = findFirstTableCell(document.root());
  const MarkdownNode* code = findBlockWithLiteral(document.root(), BlockType::CodeFence, QStringLiteral("return 0"));
  const MarkdownNode* math = findBlockWithLiteral(document.root(), BlockType::MathBlock, QStringLiteral("E = mc"));

  require(heading != nullptr, QStringLiteral("Heading node missing"));
  require(paragraph != nullptr, QStringLiteral("Paragraph node missing"));
  require(table != nullptr, QStringLiteral("Table node missing"));
  require(tableCell != nullptr, QStringLiteral("Table cell node missing"));
  require(code != nullptr, QStringLiteral("Code node missing"));
  require(math != nullptr, QStringLiteral("Math node missing"));

  requireUsableRect(layout.block(heading->id())->rect(), QStringLiteral("%1 heading").arg(themeName));
  requireUsableRect(layout.block(paragraph->id())->rect(), QStringLiteral("%1 paragraph").arg(themeName));
  requireUsableRect(layout.block(table->id())->rect(), QStringLiteral("%1 table").arg(themeName));
  requireUsableRect(layout.block(code->id())->rect(), QStringLiteral("%1 code").arg(themeName));
  requireUsableRect(layout.block(math->id())->rect(), QStringLiteral("%1 math").arg(themeName));
  require(layout.block(code->id())->literal() == code->literal(),
          QStringLiteral("%1 code display literal should preserve editable source text").arg(themeName));
  require(!layout.block(code->id())->codeHighlightSpans().isEmpty(),
          QStringLiteral("%1 code block should have syntax highlight spans").arg(themeName));
  require(layout.block(math->id())->rect().height() >= 20.0,
          QStringLiteral("%1 math block height should leave room for displayed text").arg(themeName));

  const QRectF paragraphRect = layout.block(paragraph->id())->rect();
  const HitTestResult paragraphHit = layout.hitTest(paragraphRect.center(), theme);
  require(paragraphHit.isValid(), QStringLiteral("%1 paragraph hit invalid").arg(themeName));
  require(paragraphHit.zone == HitTestResult::Zone::Text, QStringLiteral("%1 paragraph hit should be text").arg(themeName));
  require(paragraphHit.blockId == paragraph->id(), QStringLiteral("%1 paragraph hit id mismatch").arg(themeName));

  const QRectF tableRect = layout.block(table->id())->rect();
  const HitTestResult tableHit = layout.hitTest(tableRect.center(), theme);
  require(tableHit.isValid(), QStringLiteral("%1 table hit invalid").arg(themeName));
  require(tableHit.zone == HitTestResult::Zone::TableCell, QStringLiteral("%1 table hit should be cell").arg(themeName));
  require(tableHit.tableRow >= 0 && tableHit.tableColumn >= 0, QStringLiteral("%1 table hit indices missing").arg(themeName));

  const QRectF codeRect = layout.block(code->id())->rect();
  const HitTestResult codeHit = layout.hitTest(codeRect.center(), theme);
  require(codeHit.isValid(), QStringLiteral("%1 code hit invalid").arg(themeName));
  require(codeHit.zone == HitTestResult::Zone::Code, QStringLiteral("%1 code hit should be code").arg(themeName));
  SelectionRange codeSelection;
  codeSelection.anchor.blockId = code->id();
  codeSelection.anchor.text.nodeId = code->id();
  codeSelection.anchor.text.textOffset = 0;
  codeSelection.focus.blockId = code->id();
  codeSelection.focus.text.nodeId = code->id();
  codeSelection.focus.text.textOffset = 5;
  require(!layout.block(code->id())->selectionRects(codeSelection, theme).isEmpty(),
          QStringLiteral("%1 code selection rects should not be empty").arg(themeName));
  SelectionRange crossSelection;
  crossSelection.anchor.blockId = paragraph->id();
  crossSelection.anchor.text.nodeId = paragraph->id();
  crossSelection.anchor.text.textOffset = 0;
  crossSelection.focus.blockId = code->id();
  crossSelection.focus.text.nodeId = code->id();
  crossSelection.focus.text.textOffset = 5;
  require(!layout.block(code->id())->selectionRectsForOffsets(0, 5, theme).isEmpty(),
          QStringLiteral("%1 code cross-block selection rects should not be empty").arg(themeName));
  require(!layout.block(table->id())->selectionRectsForOffsets(0, 1, theme).isEmpty(),
          QStringLiteral("%1 table cross-block selection rects should not be empty").arg(themeName));

  const QRectF mathRect = layout.block(math->id())->rect();
  const HitTestResult mathHit = layout.hitTest(mathRect.center(), theme);
  require(mathHit.isValid(), QStringLiteral("%1 math hit invalid").arg(themeName));
  require(mathHit.zone == HitTestResult::Zone::Math, QStringLiteral("%1 math hit should be math").arg(themeName));
}

void testThemeManagerSupportsBuiltInThemes() {
  ThemeManager manager;
  const QStringList expectedThemes{
      QStringLiteral("github"),
      QStringLiteral("newsprint"),
      QStringLiteral("night"),
      QStringLiteral("pixyll"),
      QStringLiteral("whitey"),
  };

  require(manager.availableThemes() == expectedThemes, QStringLiteral("Theme manager should expose the five built-in themes"));
  for (const QString& name : expectedThemes) {
    require(manager.setTheme(name), QStringLiteral("Theme manager should accept %1").arg(name));
    require(manager.currentThemeName() == name, QStringLiteral("Theme manager should activate %1").arg(name));
    require(manager.currentTheme().zoomPercent() == 100, QStringLiteral("%1 theme should be constructible").arg(name));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  require(argc >= 2, QStringLiteral("Fixture path argument is required"));

  const QString markdown = readFixture(QString::fromLocal8Bit(argv[1]));
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));

  runTest("testThemeCodeFontFallbackOrder", [] { testThemeCodeFontFallbackOrder(); });
  runTest("testThemeCodeHighlightPalette", [] { testThemeCodeHighlightPalette(); });
  runTest("testInlineMarkerExpansion", [] { testInlineMarkerExpansion(); });
  runTest("testInlineProjectionContract", [] { testInlineProjectionContract(); });
  runTest("testActiveLoadedImageKeepsSourceTextAndAddsPreviewSpace", [] { testActiveLoadedImageKeepsSourceTextAndAddsPreviewSpace(); });
  runTest("testEntityDisplayAfterEdit", [] { testEntityDisplayAfterEdit(); });
  runTest("testInlineCodeEndSourceHitUsesForwardBias", [] { testInlineCodeEndSourceHitUsesForwardBias(); });
  runTest("testInlineLayoutGeometryContract", [] { testInlineLayoutGeometryContract(); });
  runTest("testInlineLayoutZeroWidthTabIndentGeometry", [] { testInlineLayoutZeroWidthTabIndentGeometry(); });
  runTest("testInlineLayoutPainting", [] { testInlineLayoutPainting(); });
  runTest("testMathRenderingLayout", [] { testMathRenderingLayout(); });
  runTest("testLiteralBlockWrappedEditingGeometry", [] { testLiteralBlockWrappedEditingGeometry(); });
  runTest("testFrontMatterLayoutPreservesTrailingLiteralNewline", [] { testFrontMatterLayoutPreservesTrailingLiteralNewline(); });
  runTest("testExtendedMathFunctionRendering", [] { testExtendedMathFunctionRendering(); });
  runTest("testStrictMathGeometryFeatures", [] { testStrictMathGeometryFeatures(); });
  runTest("testMathMetricsMacrosAndState", [] { testMathMetricsMacrosAndState(); });
  const QFileInfo smokeFixtureInfo(QString::fromLocal8Bit(argv[1]));
  runTest("testMathFixtureRenderTreeDumps", [&] {
    testMathFixtureRenderTreeDumps(smokeFixtureInfo.dir().filePath(QStringLiteral("math/core.json")));
  });
  runTest("testOfficialKatexScreenshotterAudit", [&] {
    testOfficialKatexScreenshotterAudit(smokeFixtureInfo.dir().filePath(QStringLiteral("math/katex-official-screenshotter.json")));
  });
  runTest("testRemainingKatexFunctionFamilies", [] { testRemainingKatexFunctionFamilies(); });
  runTest("testInlineLayoutHitTesting", [] { testInlineLayoutHitTesting(); });
  runTest("testInlineLayoutCursorRects", [] { testInlineLayoutCursorRects(); });
  runTest("testInlineLayoutSelectionRects", [] { testInlineLayoutSelectionRects(); });
  runTest("testInlineLayoutStyleFormats", [] { testInlineLayoutStyleFormats(); });
  runTest("testInlineLayoutProjectionDisplayMappingAfterCollapsedMath", [] { testInlineLayoutProjectionDisplayMappingAfterCollapsedMath(); });
  runTest("testIncrementalBlockRebuildContract", [] { testIncrementalBlockRebuildContract(); });
  runTest("testIncrementalTopLevelRangeRebuildContract", [] { testIncrementalTopLevelRangeRebuildContract(); });
  runTest("testUnorderedListMarkerKindsByDepth", [] { testUnorderedListMarkerKindsByDepth(); });
  runTest("testDocumentLayoutInlineLayoutContract", [] { testDocumentLayoutInlineLayoutContract(); });
  runTest("testTreeSitterCodeHighlighting", [] { testTreeSitterCodeHighlighting(); });
  runTest("testThemeManagerSupportsBuiltInThemes", [] { testThemeManagerSupportsBuiltInThemes(); });
  runTest("testLayoutForTheme/github", [&] { testLayoutForTheme(document, RenderTheme::github(), QStringLiteral("github")); });
  runTest("testLayoutForTheme/newsprint", [&] { testLayoutForTheme(document, RenderTheme::newsprint(), QStringLiteral("newsprint")); });
  runTest("testLayoutForTheme/night", [&] { testLayoutForTheme(document, RenderTheme::night(), QStringLiteral("night")); });
  runTest("testLayoutForTheme/pixyll", [&] { testLayoutForTheme(document, RenderTheme::pixyll(), QStringLiteral("pixyll")); });
  runTest("testLayoutForTheme/whitey", [&] { testLayoutForTheme(document, RenderTheme::whitey(), QStringLiteral("whitey")); });
  return 0;
}
