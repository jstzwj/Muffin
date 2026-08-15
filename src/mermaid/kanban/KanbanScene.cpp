#include "mermaid/kanban/KanbanScene.h"

#include "mermaid/kanban/KanbanScenePainter.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::kanban {
namespace {

constexpr qreal kColumnGap = 5.0;
constexpr qreal kPadding = 10.0;
constexpr qreal kRadius = 5.0;

bool jsTruthy(const QJsonValue& value) {
  if (value.isUndefined() || value.isNull()) return false;
  if (value.isBool()) return value.toBool();
  if (value.isDouble()) {
    const double n = value.toDouble();
    return n != 0.0 && !std::isnan(n);
  }
  if (value.isString()) return !value.toString().isEmpty();
  return true;
}

qreal usedCoordinate(qreal value) { return std::isfinite(value) ? value : 0.0; }
qreal usedDimension(qreal value) {
  return std::isfinite(value) && value > 0.0 ? value : 0.0;
}

QString jsAttributeString(const QJsonValue& value) {
  if (value.isNull() || value.isUndefined()) return {};
  if (value.isBool())
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  if (value.isDouble()) return editor::jsNumberToString(value.toDouble());
  if (value.isString()) return value.toString();
  if (value.isArray()) {
    QStringList parts;
    for (const QJsonValue& item : value.toArray())
      parts.append(jsAttributeString(item));
    return parts.join(QLatin1Char(','));
  }
  return QStringLiteral("[object Object]");
}

qreal svgDimensionUsedValue(const QJsonValue& value) {
  static const QRegularExpression number(
      QStringLiteral(R"(^[\t\n\r\f ]*[+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[\t\n\r\f ]*$)"));
  const QString text = jsAttributeString(value);
  if (!number.match(text).hasMatch()) return 0.0;
  bool ok = false;
  const double parsed = text.toDouble(&ok);
  return ok ? usedDimension(qreal(parsed)) : 0.0;
}

void includeRect(QRectF& target, bool& initialized, const QRectF& rect) {
  if (!std::isfinite(rect.x()) || !std::isfinite(rect.y()) ||
      !std::isfinite(rect.width()) || !std::isfinite(rect.height()))
    return;
  if (!initialized) {
    target = rect;
    initialized = true;
  } else {
    target = target.united(rect);
  }
}

struct LabelLayout {
  KanbanLabelGeometry geometry;
  QSizeF size{0.0, 0.0};
  QRectF localInk;
  qreal lineHeight = 0.0;
};

LabelLayout makeLabel(const QString& source, bool html, bool autoWrap,
                      qreal requestedWidth, const KanbanSceneStyle& style,
                      bool centered, const KanbanElementCss* spanCss) {
  LabelLayout result;
  result.geometry.source = source;
  result.geometry.html = html;
  result.geometry.centered = centered;
  result.geometry.fill = style.textColor;
  // The label font follows the span's cascade: htmlLabels measures the
  // foreignObject content, whose computed font themeCSS can move.
  QString family = style.fontFamily;
  qreal fontSize = style.fontSize;
  QFont::Weight weight = QFont::Normal;
  if (spanCss) {
    result.geometry.css = *spanCss;
    if (!spanCss->fontFamily.isEmpty()) family = spanCss->fontFamily;
    if (spanCss->fontSize >= 0.0) fontSize = spanCss->fontSize;
    if (!spanCss->fontWeight.isEmpty())
      weight = editor::cssFontWeightToQt(QJsonValue(spanCss->fontWeight),
                                         weight);
    weight = editor::faceAwareMetricWeight(family, weight);
  }
  result.geometry.fontFamily = family;
  result.geometry.fontSize = fontSize;
  result.geometry.fontWeight = weight;
  result.lineHeight = html ? fontSize * 1.5 : fontSize * 1.1;
  // createText/addHtmlSpan emits a zero-sized foreignObject for an empty
  // metadata field. FlowLabel's empty document still has one CSS line box,
  // so short-circuit before measuring or it adds 24px to every card.
  if (source.isEmpty()) return result;
  result.geometry.document = html
      ? flowchart::parseFlowLabel(source, QStringLiteral("markdown"), true)
      : flowchart::parseFlowSvgLabel(source, QStringLiteral("markdown"));
  if (!(fontSize > 0.0) || !std::isfinite(fontSize)) return result;

  const qreal fallbackWidth = 200.0;
  qreal wrapWidth = requestedWidth;
  if (!std::isfinite(wrapWidth)) wrapWidth = fallbackWidth;
  const QSizeF natural = flowchart::measureFlowLabel(
      result.geometry.document, family, fontSize, result.lineHeight);
  static const QRegularExpression breakableWhitespace(
      QStringLiteral(R"([\t\n\r\f ])"));
  const bool canBreak = source.contains(breakableWhitespace);
  if (autoWrap && canBreak && wrapWidth > 0.0 && natural.width() > wrapWidth) {
    result.geometry.document = flowchart::wrapFlowLabel(
        result.geometry.document, family, fontSize, wrapWidth);
  }

  if (html) {
    result.size = flowchart::measureFlowLabel(
        result.geometry.document, family, fontSize, result.lineHeight);
    // foreignObject labels are CSS line boxes; getBBox reports exactly the
    // authored 1.5em line-height rather than the glyph cell height.
    const qsizetype lineCount = !result.geometry.document.visualLines.isEmpty()
        ? result.geometry.document.visualLines.size()
        : std::max<qsizetype>(1, result.geometry.document.text.count(QLatin1Char('\n')) + 1);
    result.size.setHeight(qreal(lineCount) * result.lineHeight);
    if (autoWrap && canBreak && wrapWidth > 0.0 && natural.width() > wrapWidth)
      result.size.setWidth(wrapWidth);
    result.localInk = QRectF(QPointF(0.0, 0.0), result.size);
  } else {
    result.localInk = flowchart::measureFlowSvgTextBounds(
        result.geometry.document, family, fontSize);
    result.size = result.localInk.size();
  }
  return result;
}

QRectF translatedLabelBounds(const LabelLayout& label, qreal x, qreal y,
                             const QPointF& groupPosition = {}) {
  if (!std::isfinite(x) || !std::isfinite(y) ||
      !std::isfinite(groupPosition.x()) || !std::isfinite(groupPosition.y()))
    return label.localInk;
  return label.localInk.translated(groupPosition + QPointF(x, y));
}

QString priorityColor(const QJsonValue& value) {
  if (!value.isString()) return {};
  const QString v = value.toString();
  if (v == QLatin1String("Very High")) return QStringLiteral("red");
  if (v == QLatin1String("High")) return QStringLiteral("orange");
  if (v == QLatin1String("Low")) return QStringLiteral("blue");
  if (v == QLatin1String("Very Low")) return QStringLiteral("lightblue");
  return {};
}

QString ticketHref(const QString& base, const QString& ticket) {
  if (base.isEmpty() || ticket.isEmpty()) return {};
  QString result = base;
  const QString placeholder = QStringLiteral("#TICKET#");
  const qsizetype position = result.indexOf(placeholder);
  if (position >= 0) result.replace(position, placeholder.size(), ticket);
  return result;
}

QRectF roughBounds(const rough::Drawable& drawable) {
  QRectF result;
  bool initialized = false;
  for (const rough::OpSet& set : drawable.sets)
    includeRect(result, initialized, rough::toPainterPath(set).boundingRect());
  return result;
}

rough::Drawable makeRoughSection(const QRectF& rect, quint32 seed,
                                 const QString& fill,
                                 const QString& stroke) {
  rough::Options options;
  options.seed = seed;
  options.roughness = 0.7;
  options.strokeWidth = 1.0;
  options.fill = fill;
  options.stroke = stroke;
  options.fillStyle = QStringLiteral("solid");
  options.fillWeight = 4.0;
  QPainterPath path;
  path.addRoundedRect(rect, kRadius, kRadius);
  return rough::path(path, options);
}

QString adjustedSectionColor(const KanbanSceneStyle& style, int index) {
  if (index < 0 || index >= style.themeColorRuleCount ||
      index >= style.cScale.size() || style.cScale.at(index).isEmpty())
    return QStringLiteral("black");
  return style.darkMode ? color::darken(style.cScale.at(index), 10.0)
                        : color::lighten(style.cScale.at(index), 10.0);
}

QJsonObject rectJson(const QRectF& r) {
  return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
          {QStringLiteral("width"), r.width()},
          {QStringLiteral("height"), r.height()}};
}

}  // namespace

KanbanScene buildKanbanScene(const KanbanData& data, KanbanConfig config,
                             KanbanSceneStyle style,
                             const KanbanCssOverrides* css) {
  KanbanScene scene;
  scene.config = std::move(config);
  scene.style = std::move(style);
  scene.useMaxWidth = jsTruthy(scene.config.useMaxWidth);

  const QJsonValue effectiveSectionWidth =
      jsTruthy(scene.config.sectionWidth) ? scene.config.sectionWidth
                                          : QJsonValue(200.0);
  const qreal columnWidth =
      qreal(editor::jsNumberValue(effectiveSectionWidth));
  // Renderer arithmetic coerces sectionWidth through JS Number(), while the
  // D3 rect writes the original scalar into an SVG width attribute. For
  // example "0x50" lays columns out at 80px but is not a valid SVG length,
  // so the section rect itself has a zero used width.
  const qreal sectionShapeWidth =
      svgDimensionUsedValue(effectiveSectionWidth);
  const qreal scenePadding = scene.config.padding.isUndefined() ||
                                     scene.config.padding.isNull()
                                 ? 10.0
                                 : qreal(editor::jsNumberValue(scene.config.padding));

  struct PendingSection {
    KanbanNode node;
    qreal x = 0.0;
    LabelLayout label;
  };
  QVector<PendingSection> pending;
  pending.reserve(data.sections.size());
  qreal maxLabelHeight = 25.0;
  for (int i = 0; i < data.sections.size(); ++i) {
    const KanbanNode& section = data.sections.at(i);
    PendingSection p;
    p.node = section;
    p.x = columnWidth * qreal(i + 1) + qreal(i) * kColumnGap;
    p.label = makeLabel(section.label, scene.config.htmlLabels,
                        scene.config.markdownAutoWrap, columnWidth, scene.style,
                        true,
                        css && i < css->sections.size()
                            ? &css->sections.at(i).label
                            : nullptr);
    maxLabelHeight = std::max(maxLabelHeight, p.label.size.height());
    pending.append(std::move(p));
  }

  QRectF content;
  bool hasContent = false;
  qsizetype itemIndex = 0;
  QVector<const KanbanNode*> rendererGroups;
  for (const KanbanNode& node : data.nodes)
    if (node.isGroup) rendererGroups.append(&node);
  for (int column = 0; column < pending.size(); ++column) {
    const PendingSection& p = pending.at(column);
    const KanbanNode* rendererGroup =
        column < rendererGroups.size() ? rendererGroups.at(column) : nullptr;

    const qreal sectionTop = -1.5 * columnWidth;
    qreal cursor = sectionTop + maxLabelHeight;
    QVector<KanbanItemGeometry> columnItems;

    // kanbanRenderer filters the already-flattened getData().nodes for every
    // group. Duplicate section ids therefore replay the duplicated children
    // again (two same-id groups with A/B produce four cards in each column).
    for (const KanbanNode& node : data.nodes) {
      if (node.isGroup || node.parentId != p.node.id) continue;
      qreal passedWidth = columnWidth - 15.0;
      qreal totalWidth = std::isnan(passedWidth) || passedWidth == 0.0
                             ? 0.0
                             : passedWidth;
      const qreal labelWidth = std::isfinite(columnWidth - 25.0)
                                   ? columnWidth - 25.0
                                   : 200.0;
      const KanbanCssOverrides::Item* itemCss =
          css && itemIndex < css->items.size() ? &css->items.at(itemIndex)
                                               : nullptr;
      const auto labelCssAt = [&itemCss](int k) {
        return itemCss && k < itemCss->labels.size() ? &itemCss->labels.at(k).span
                                                     : nullptr;
      };
      LabelLayout title = makeLabel(node.label, scene.config.htmlLabels,
                                    scene.config.markdownAutoWrap, labelWidth,
                                    scene.style, false, labelCssAt(0));
      LabelLayout ticket = makeLabel(node.ticket, scene.config.htmlLabels,
                                     scene.config.markdownAutoWrap, labelWidth,
                                     scene.style, false, labelCssAt(1));
      LabelLayout assigned = makeLabel(node.assigned, scene.config.htmlLabels,
                                       scene.config.markdownAutoWrap, labelWidth,
                                       scene.style, false, labelCssAt(2));
      const qreal heightAdjustment =
          std::max(ticket.size.height(), assigned.size.height()) / 2.0;
      const qreal height = std::max(title.size.height() + 20.0, 0.0) +
                           heightAdjustment;
      const qreal titleY = -heightAdjustment - title.size.height() / 2.0;
      const qreal metadataY = -heightAdjustment + title.size.height() / 2.0;
      qreal layoutHeight = height;
      // SVGGeometryElement.getBBox() excludes a zero-width rect entirely.
      // For negative section widths the renderer therefore positions cards by
      // their label bbox (24px), even though the final rect retains height 44.
      if (usedDimension(totalWidth) == 0.0) {
        qreal minY = titleY + title.localInk.top();
        qreal maxY = titleY + title.localInk.bottom();
        if (!node.ticket.isEmpty()) {
          minY = std::min(minY, metadataY + ticket.localInk.top());
          maxY = std::max(maxY, metadataY + ticket.localInk.bottom());
        }
        if (!node.assigned.isEmpty()) {
          minY = std::min(minY, metadataY + assigned.localInk.top());
          maxY = std::max(maxY, metadataY + assigned.localInk.bottom());
        }
        layoutHeight = std::max<qreal>(0.0, maxY - minY);
      }
      const qreal centerY = cursor + layoutHeight / 2.0;
      cursor = centerY + layoutHeight / 2.0 + kColumnGap;

      KanbanItemGeometry item;
      item.id = node.id;
      item.parentId = node.parentId;
      const QRectF local(usedCoordinate(-totalWidth / 2.0), -height / 2.0,
                         usedDimension(totalWidth), height);
      item.localBounds = local;
      const QPointF usedPosition(std::isfinite(p.x) ? p.x : 0.0,
                                 std::isfinite(centerY) ? centerY : 0.0);
      item.position = usedPosition;
      item.bounds = local.translated(usedPosition);
      if (scene.style.themeColorRuleCount > 0) {
        item.fill = scene.style.background;
        item.stroke = scene.style.nodeBorder;
      } else {
        item.fill = scene.style.textColor;
        item.stroke = QStringLiteral("none");
      }
      item.strokeWidth = 1.0;
      item.radius = kRadius;
      if (itemCss) {
        item.nodeCss = itemCss->node;
        item.boxCss = itemCss->box;
        item.priorityCss = itemCss->priority;
      }

      const qreal titleX = kPadding - totalWidth / 2.0;
      title.geometry.bounds = translatedLabelBounds(title, titleX, titleY,
                                                    usedPosition);
      item.title = std::move(title.geometry);
      ticket.geometry.bounds = translatedLabelBounds(ticket, titleX, metadataY,
                                                     usedPosition);
      item.ticket = std::move(ticket.geometry);
      const qreal assignedX = totalWidth / 2.0 - assigned.size.width() - 10.0;
      assigned.geometry.bounds = translatedLabelBounds(
          assigned, assignedX, metadataY, usedPosition);
      item.assigned = std::move(assigned.geometry);
      item.href = ticketHref(scene.config.ticketBaseUrl, node.ticket);
      if (!item.href.isEmpty()) item.ticket.document.underline = true;

      item.priorityStroke = priorityColor(node.priority);
      item.priorityVisible = !item.priorityStroke.isEmpty();
      if (item.priorityVisible) {
        const qreal x = -totalWidth / 2.0 + 2.0;
        item.priorityLine = QLineF(usedPosition + QPointF(x, -height / 2.0 + 2.0),
                                   usedPosition + QPointF(x, height / 2.0 - 2.0));
      }

      // Kanban's final svg.getBBox() (after the awaited insertNode calls)
      // drops display:none geometry while visibility keeps it.
      QRectF itemInk;
      bool itemHasInk = false;
      if (item.boxCss.hasBox && local.width() > 0.0 && local.height() > 0.0)
        includeRect(itemInk, itemHasInk, local.translated(usedPosition));
      if (item.title.css.hasBox)
        includeRect(itemInk, itemHasInk, item.title.bounds);
      if (!node.ticket.isEmpty() && item.ticket.css.hasBox)
        includeRect(itemInk, itemHasInk, item.ticket.bounds);
      if (!node.assigned.isEmpty() && item.assigned.css.hasBox)
        includeRect(itemInk, itemHasInk, item.assigned.bounds);
      if (item.priorityVisible && item.priorityCss.hasBox) {
        QRectF lineBounds(item.priorityLine.p1(), item.priorityLine.p2());
        lineBounds = lineBounds.normalized().adjusted(-2.0, -2.0, 2.0, 2.0);
        includeRect(itemInk, itemHasInk, lineBounds);
      }
      item.bounds = itemHasInk ? itemInk : QRectF();
      if (!item.href.isEmpty()) {
        InteractionRegion region;
        region.bounds = item.ticket.bounds;
        region.href = item.href;
        scene.interactions.append(region);
      }
      includeRect(content, hasContent, item.bounds);
      columnItems.append(std::move(item));
      ++itemIndex;
    }

    KanbanSectionGeometry section;
    section.id = p.node.id;
    section.column = column + 1;
    // The parser's raw section collection intentionally preserves DB data;
    // `look` is attached only to getData()'s renderer-facing group node.
    section.look = rendererGroup ? rendererGroup->look : p.node.look;
    section.handDrawn = section.look == QLatin1String("handDrawn");
    section.dropShadow = section.look == QLatin1String("neo") &&
                         scene.style.dropShadowEnabled;
    const int paletteIndex = column + 2;
    if (paletteIndex < scene.style.themeColorRuleCount) {
      section.fill = adjustedSectionColor(scene.style, paletteIndex);
      section.stroke = section.fill;
    } else {
      section.fill = scene.style.textColor;
      section.stroke = QStringLiteral("none");
    }
    // The section label is an html span reached only by
    // `.cluster-label, .label { color: textColor }`; the `.section-N text`
    // palette rule targets svg <text> and never reaches kanban labels.
    section.label.fill = scene.style.textColor;
    section.strokeWidth = 1.0;
    if (css && column < css->sections.size()) {
      section.clusterCss = css->sections.at(column).cluster;
      section.boxCss = css->sections.at(column).box;
    }    const qreal finalHeight =
        std::max(cursor - (sectionTop + maxLabelHeight) + 30.0, 50.0) +
        (maxLabelHeight - 25.0);
    const QRectF finalRect(usedCoordinate(p.x - columnWidth / 2.0),
                           usedCoordinate(sectionTop),
                           sectionShapeWidth,
                           usedDimension(finalHeight));
    QRectF initialRect(usedCoordinate(p.x - columnWidth / 2.0),
                             usedCoordinate(sectionTop),
                             sectionShapeWidth,
                             usedDimension(3.0 * columnWidth));
    if (section.handDrawn) {
      // RoughOps consumes QPainterPath cubics while rough.js consumes the SVG
      // A-command path. Its bottom jitter is one CSS pixel larger for this
      // rounded 3W box; compensate the source height so the painted rough bbox
      // (the observable contract) matches Chrome.
      if (initialRect.height() > 1.0) initialRect.setHeight(initialRect.height() - 1.0);
      section.roughDrawable = makeRoughSection(
          initialRect, scene.config.handDrawnSeed, section.fill, section.stroke);
      section.shapeBounds = roughBounds(section.roughDrawable);
      section.paintedBounds = section.shapeBounds;
    } else {
      section.shapeBounds = finalRect;
      section.paintedBounds = finalRect;
    }
    LabelLayout sectionLabel = p.label;
    sectionLabel.geometry.fill = section.label.fill;
    const qreal labelX = p.x - sectionLabel.size.width() / 2.0;
    const qreal labelY = sectionTop;
    sectionLabel.geometry.bounds = translatedLabelBounds(sectionLabel, labelX, labelY);
    section.label = std::move(sectionLabel.geometry);
    if (section.boxCss.hasBox && section.paintedBounds.width() > 0.0 &&
        section.paintedBounds.height() > 0.0)
      includeRect(content, hasContent, section.paintedBounds);
    if (section.label.css.hasBox)
      includeRect(content, hasContent, section.label.bounds);
    scene.sections.append(std::move(section));
    for (KanbanItemGeometry& item : columnItems) scene.items.append(std::move(item));
  }

  scene.contentBounds = hasContent ? content : QRectF();
  if (hasContent && std::isfinite(scenePadding))
    scene.bounds = content.adjusted(-scenePadding, -scenePadding,
                                    scenePadding, scenePadding);
  else
    scene.bounds = content;
  const bool hasHandDrawn = std::any_of(
      scene.sections.cbegin(), scene.sections.cend(),
      [](const KanbanSectionGeometry& section) { return section.handDrawn; });
  if (hasHandDrawn) {
    scene.rasterBounds = QRectF(
        scene.bounds.topLeft(),
        QSizeF(std::floor(scene.bounds.width()),
               std::floor(scene.bounds.height())));
  }
  return scene;
}

void KanbanScene::paint(QPainter& painter,
                        const MermaidPaintOptions& options) const {
  paintKanbanScene(*this, painter, options);
}

QJsonObject KanbanScene::toJsonObject() const {
  QJsonArray sectionArray;
  for (const auto& section : sections) {
    sectionArray.append(QJsonObject{
        {QStringLiteral("id"), section.id},
        {QStringLiteral("column"), section.column},
        {QStringLiteral("shape"), rectJson(section.shapeBounds)},
        {QStringLiteral("label"), rectJson(section.label.bounds)},
        {QStringLiteral("fill"), section.fill},
        {QStringLiteral("stroke"), section.stroke},
        {QStringLiteral("look"), section.look}});
  }
  QJsonArray itemArray;
  for (const auto& item : items) {
    itemArray.append(QJsonObject{
        {QStringLiteral("id"), item.id},
        {QStringLiteral("parentId"), item.parentId},
        {QStringLiteral("bounds"), rectJson(item.bounds)},
        {QStringLiteral("title"), rectJson(item.title.bounds)},
        {QStringLiteral("ticket"), rectJson(item.ticket.bounds)},
        {QStringLiteral("assigned"), rectJson(item.assigned.bounds)}});
  }
  return {{QStringLiteral("family"), QStringLiteral("kanban")},
          {QStringLiteral("bounds"), rectJson(bounds)},
          {QStringLiteral("contentBounds"), rectJson(contentBounds)},
          {QStringLiteral("useMaxWidth"), useMaxWidth},
          {QStringLiteral("sections"), sectionArray},
          {QStringLiteral("items"), itemArray}};
}

}  // namespace muffin::mermaid::kanban
