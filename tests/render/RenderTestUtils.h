#pragma once

#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>

#include "../TestUtils.h"

#include <cstdio>
#include <cstdlib>
#include <functional>

// ---- fixture I/O ----

inline QString readFixture(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("Could not open fixture: %1").arg(path));
  return QString::fromUtf8(file.readAll());
}

inline QJsonArray readJsonArrayFixture(const QString& path) {
  const QJsonDocument document = QJsonDocument::fromJson(readFixture(path).toUtf8());
  require(document.isArray(), QStringLiteral("Fixture should be a JSON array: %1").arg(path));
  return document.array();
}

inline QJsonArray readOptionalJsonArrayFixture(const QString& path) {
  if (!QFileInfo::exists(path)) {
    return {};
  }
  return readJsonArrayFixture(path);
}

inline QMap<QString, QJsonObject> readObjectArrayById(const QString& path) {
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

inline QString katexGoldenPathForFixture(const QString& fixturePath, const QString& goldenName, const QString& fileName) {
  QDir testsDir(QFileInfo(fixturePath).absoluteDir());
  require(testsDir.cdUp() && testsDir.cdUp(), QStringLiteral("Could not resolve tests directory from %1").arg(fixturePath));
  return testsDir.filePath(QStringLiteral("golden/%1/%2").arg(goldenName, fileName));
}

inline QString katexMetricsPathForFixture(const QString& fixturePath, const QString& goldenName = QStringLiteral("katex")) {
  return katexGoldenPathForFixture(fixturePath, goldenName, QStringLiteral("metrics.json"));
}

inline QString katexBBoxPathForFixture(const QString& fixturePath, const QString& goldenName = QStringLiteral("katex")) {
  return katexGoldenPathForFixture(fixturePath, goldenName, QStringLiteral("bbox.json"));
}

inline QString katexGlyphsPathForFixture(const QString& fixturePath, const QString& goldenName = QStringLiteral("katex")) {
  return katexGoldenPathForFixture(fixturePath, goldenName, QStringLiteral("glyphs.json"));
}

// ---- AST navigation ----

inline const muffin::MarkdownNode* findFirstBlock(const muffin::MarkdownNode& node, muffin::BlockType type) {
  if (node.type() == type) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const muffin::MarkdownNode* found = findFirstBlock(*child, type)) {
      return found;
    }
  }
  return nullptr;
}

inline const muffin::MarkdownNode* findBlockWithLiteral(const muffin::MarkdownNode& node, muffin::BlockType type, const QString& literalPart) {
  if (node.type() == type && node.literal().contains(literalPart)) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const muffin::MarkdownNode* found = findBlockWithLiteral(*child, type, literalPart)) {
      return found;
    }
  }
  return nullptr;
}

inline const muffin::MarkdownNode* findFirstTableCell(const muffin::MarkdownNode& node) {
  if (node.type() == muffin::BlockType::TableCell) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const muffin::MarkdownNode* found = findFirstTableCell(*child)) {
      return found;
    }
  }
  return nullptr;
}

inline const muffin::MarkdownNode* findListItemWithText(const muffin::MarkdownNode& node, const QString& text) {
  if (node.type() == muffin::BlockType::ListItem) {
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
    if (const muffin::MarkdownNode* found = findListItemWithText(*child, text)) {
      return found;
    }
  }
  return nullptr;
}

inline void collectListItems(const muffin::MarkdownNode& node, QVector<const muffin::MarkdownNode*>& items) {
  if (node.type() == muffin::BlockType::ListItem) {
    items.push_back(&node);
  }
  for (const auto& child : node.children()) {
    collectListItems(*child, items);
  }
}

// ---- JSON utilities ----

inline bool jsonHasNumber(const QJsonObject& object, const QString& key) {
  return object.contains(key) && object.value(key).isDouble();
}

inline qreal jsonNumber(const QJsonObject& object, const QString& key) {
  require(jsonHasNumber(object, key), QStringLiteral("Expected numeric JSON metric: %1").arg(key));
  return object.value(key).toDouble();
}

inline QRectF jsonRect(const QJsonObject& object, const QString& key) {
  const QJsonObject rect = object.value(key).toObject();
  require(!rect.isEmpty(), QStringLiteral("Expected JSON rect: %1").arg(key));
  return QRectF(jsonNumber(rect, QStringLiteral("x")),
                jsonNumber(rect, QStringLiteral("y")),
                jsonNumber(rect, QStringLiteral("width")),
                jsonNumber(rect, QStringLiteral("height")));
}

inline void requireCloseMetric(qreal actual, qreal expected, qreal tolerance, const QString& label) {
  require(qAbs(actual - expected) <= tolerance,
          QStringLiteral("%1 mismatch: actual=%2 expected=%3 tolerance=%4")
              .arg(label)
              .arg(actual, 0, 'f', 6)
              .arg(expected, 0, 'f', 6)
              .arg(tolerance, 0, 'f', 6));
}

inline bool renderTreeContainsValue(const QJsonObject& node, const QString& value) {
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

inline bool renderTreeContainsClass(const QJsonObject& node, const QString& className) {
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

inline bool findRenderTreeText(const QJsonObject& node, const QString& text, QJsonObject& out) {
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

inline bool findRenderTreeClass(const QJsonObject& node, const QString& className, QJsonObject& out) {
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

// ---- document manipulation ----

inline muffin::MarkdownNode* mutableBlockAt(muffin::MarkdownDocument& document, qsizetype index) {
  auto& children = document.root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), QStringLiteral("mutable block index out of range"));
  return children.at(static_cast<size_t>(index)).get();
}

inline void requireUsableRect(const QRectF& rect, const QString& label) {
  require(rect.isValid(), QStringLiteral("%1 rect is invalid").arg(label));
  require(rect.width() > 20.0, QStringLiteral("%1 rect width too small").arg(label));
  require(rect.height() > 10.0, QStringLiteral("%1 rect height too small").arg(label));
}

// ---- text layout helpers ----

inline void requireTextLayoutCursorRoundTrip(const muffin::InlineLayout& layout, qsizetype offset, const QString& label) {
  const QRectF cursor = layout.cursorRect(offset);
  require(!cursor.isEmpty(), label + QStringLiteral(" cursor rect should exist"));
  require(layout.hitTestTextOffset(QPointF(cursor.left(), cursor.center().y())) == offset,
          label + QStringLiteral(" cursor-left hit-test should round-trip"));
}

inline void requireTextLayoutCharacterBias(const muffin::InlineLayout& layout, qsizetype offset, const QString& label) {
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

// ---- selection rect validation ----

inline void requireValidSelectionRects(const QVector<QRectF>& rects, const QString& label) {
  require(!rects.isEmpty(), label + QStringLiteral(" selection rects should not be empty"));
  for (const QRectF& rect : rects) {
    require(rect.width() > 0.0, label + QStringLiteral(" selection rect width should be positive"));
    require(rect.height() > 0.0, label + QStringLiteral(" selection rect height should be positive"));
  }
}

// ---- table cell, pixel, HTML helpers ----

inline bool hasRole(const QVector<muffin::CodeHighlightSpan>& spans, muffin::CodeHighlightRole role) {
  for (const muffin::CodeHighlightSpan& span : spans) {
    if (span.role == role) {
      return true;
    }
  }
  return false;
}

inline const muffin::BlockLayout::TableCellLayout* findTableCellLayout(const muffin::BlockLayout& tableLayout, muffin::NodeId cellId) {
  for (const muffin::BlockLayout::TableRowLayout& row : tableLayout.tableRows()) {
    for (const muffin::BlockLayout::TableCellLayout& cell : row.cells) {
      if (cell.nodeId == cellId) {
        return &cell;
      }
    }
  }
  return nullptr;
}

inline int changedPixelCount(const QImage& image, QColor background) {
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

inline QRect imageInkBounds(const QImage& image, QColor background) {
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
