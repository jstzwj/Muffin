#pragma once

#include "RenderTestUtils.h"
#include "math/MathBuilder.h"
#include "math/MathFontMetrics.h"
#include "math/MathFunctionRegistry.h"
#include "math/MathLayoutTree.h"
#include "math/MathRenderer.h"
#include "math/MathSvgGeometry.h"

// ---- test layout box factory ----

inline std::unique_ptr<muffin::math::MathLayoutNode> makeTestLayoutBox(qreal width, qreal height, qreal depth) {
  auto node = std::make_unique<muffin::math::MathLayoutNode>();
  node->kind = muffin::math::MathLayoutKind::Span;
  node->renderKind = muffin::math::MathRenderKind::Span;
  node->width = width;
  node->height = height;
  node->depth = depth;
  return node;
}

// ---- metric flatteners ----

inline qreal maxAbsJsonMetric(const QJsonObject& node, const QString& key) {
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

inline qreal maxAbsFlatMetric(const QJsonArray& flat, const QString& key) {
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

inline QJsonArray flattenNativeMetrics(const QJsonObject& node, const QString& path = QString()) {
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

inline QMap<QString, QJsonObject> flatMetricsByPath(const QJsonArray& flat) {
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

inline void dumpBuilderMetricDiffs(const QString& id, const QJsonObject& nativeRoot, const QJsonObject& golden, qreal em) {
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

// ---- BBoxAuditEntry / GlyphAuditItem ----

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

// ---- math native ink root ----

inline const muffin::math::MathRenderNode& nativeInkRoot(const muffin::math::MathRenderNode& root) {
  if (root.text == QStringLiteral("katex") && !root.children.empty()) {
    const muffin::math::MathRenderNode* html = root.children.front().get();
    if (html != nullptr && html->text == QStringLiteral("katex-html") && !html->children.empty()) {
      return *html;
    }
  }
  return root;
}

// ---- glyph/bbox comparison helpers ----

inline QRectF nativeGlyphInkRect(const muffin::math::MathRenderNode& node, QPointF origin, qreal em) {
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

inline void collectNativeGlyphItems(const muffin::math::MathRenderNode& node,
                                    QPointF origin,
                                    qreal em,
                                    QVector<GlyphAuditItem>& out) {
  const QRectF domRect(origin.x() / em, (origin.y() - node.height) / em, node.width / em, (node.height + node.depth) / em);
  if (node.kind == muffin::math::MathRenderKind::Symbol || node.kind == muffin::math::MathRenderKind::Error) {
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
    case muffin::math::MathRenderKind::Span: {
      qreal x = origin.x();
      for (const auto& child : node.children) {
        collectNativeGlyphItems(*child, QPointF(x + child->xOffset, origin.y() + child->yOffset + child->shift), em, out);
        x += child->width;
      }
      return;
    }
    case muffin::math::MathRenderKind::SupSub: {
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
    case muffin::math::MathRenderKind::Fraction: {
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

inline QVector<GlyphAuditItem> nativeGlyphItems(const muffin::math::MathRenderNode& root, qreal em) {
  const muffin::math::MathRenderNode& htmlRoot = nativeInkRoot(root);
  QVector<GlyphAuditItem> out;
  collectNativeGlyphItems(htmlRoot, QPointF(0.0, htmlRoot.height), em, out);
  return out;
}

inline QVector<GlyphAuditItem> nativeVisibleGlyphItems(const muffin::math::MathRenderNode& root, qreal em) {
  const QVector<GlyphAuditItem> all = nativeGlyphItems(root, em);
  QVector<GlyphAuditItem> glyphs;
  for (const GlyphAuditItem& item : all) {
    if (item.kind == QStringLiteral("glyph") && !item.text.isEmpty() &&
        item.text != QString::fromUtf8("\xE2\x80\x8B")) {
      glyphs.push_back(item);
    }
  }
  return glyphs;
}

inline QRectF glyphJsonRect(const QJsonObject& item, const QString& key) {
  const QJsonObject rect = item.value(key).toObject();
  return QRectF(rect.value(QStringLiteral("x")).toDouble(),
                rect.value(QStringLiteral("y")).toDouble(),
                rect.value(QStringLiteral("width")).toDouble(),
                rect.value(QStringLiteral("height")).toDouble());
}

inline QVector<QJsonObject> katexVisibleGlyphItems(const QJsonObject& golden) {
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

inline void compareKatexGlyphs(const QString& id,
                               const muffin::math::MathRenderNode& root,
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

inline void dumpGlyphDiffs(const QString& id, const muffin::math::MathRenderNode& root, const QJsonObject& golden, qreal em) {
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

inline QRect nativeInkBounds(const muffin::math::MathRenderNode& root) {
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

inline BBoxAuditEntry makeBBoxAuditEntry(const QString& id,
                                         const muffin::math::MathRenderNode& root,
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

inline void compareKatexBrowserBBox(const QString& id,
                                    const muffin::math::MathRenderNode& root,
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

inline void compareKatexInkBBox(const QString& id,
                                const muffin::math::MathRenderNode& root,
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

inline void compareKatexBuilderRootMetrics(const QString& id,
                                           const muffin::math::MathRenderNode& root,
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
