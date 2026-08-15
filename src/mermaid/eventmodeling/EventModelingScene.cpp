#include "mermaid/eventmodeling/EventModelingScene.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/eventmodeling/EventModelingScenePainter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QFontMetricsF>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

namespace muffin::mermaid::eventmodeling {
namespace {

constexpr qreal kSwimlaneMinHeight = 70.0;
constexpr qreal kSwimlanePadding = 15.0;
constexpr qreal kSwimlaneGap = 10.0;
constexpr qreal kBoxPadding = 10.0;
constexpr qreal kBoxOverlap = 90.0;
constexpr qreal kBoxMinWidth = 80.0;
constexpr qreal kBoxMaxWidth = 450.0;
constexpr qreal kBoxMinHeight = 80.0;
constexpr qreal kBoxMaxHeight = 750.0;
constexpr qreal kContentStartX = 250.0;
constexpr qreal kTextMaxWidth = 430.0;
constexpr qreal kTextFontSize = 16.0;

bool jsTruthy(const QJsonValue& value) {
  if (value.isUndefined() || value.isNull()) return false;
  if (value.isBool()) return value.toBool();
  if (value.isDouble())
    return value.toDouble() != 0.0 && !std::isnan(value.toDouble());
  if (value.isString()) return !value.toString().isEmpty();
  return true;
}

QJsonValue scalar(const QJsonValue& value, const QJsonValue& fallback) {
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

QString extractNamespace(const QString& identifier) {
  const QStringList parts = identifier.split(QLatin1Char('.'));
  return parts.size() == 2 ? parts.front() : QString();
}

QString extractName(const QString& identifier) {
  const QStringList parts = identifier.split(QLatin1Char('.'));
  return parts.size() == 2 ? parts.back() : identifier;
}

QFont literalFont() {
  QFont font;
  font.setFamilies({QStringLiteral("Trebuchet MS"), QStringLiteral("Verdana"),
                    QStringLiteral("Arial"), QStringLiteral("sans-serif")});
  font.setPixelSize(int(kTextFontSize));
  font.setHintingPreference(QFont::PreferNoHinting);
  font.setWeight(QFont::Bold);
  return font;
}

qreal literalWidth(const QString& text) {
  QString visible = text;
  static const QRegularExpression collapsibleWhitespace(
      QStringLiteral(R"([\x09\x0a\x0d\x20]+)"));
  visible.replace(collapsibleWhitespace, QStringLiteral(" "));
  while (visible.startsWith(QLatin1Char(' '))) visible.removeFirst();
  while (visible.endsWith(QLatin1Char(' '))) visible.chop(1);
  flowchart::FlowLabelDocument document;
  document.text = visible;
  document.baseWeight = QFont::Bold;
  const auto designWidth = flowchart::measureOpenTypeDesignAdvance(
      document, QStringLiteral("Trebuchet MS"), kTextFontSize);
  return qRound(designWidth.value_or(
      QFontMetricsF(literalFont()).horizontalAdvance(visible)));
}

qreal literalLineHeight() {
  return qRound(QFontMetricsF(literalFont()).height());
}

QStringList splitRenderedLines(const QString& text) {
  static const QRegularExpression breaks(
      QStringLiteral(R"(<br\s*/?>)"),
      QRegularExpression::CaseInsensitiveOption);
  return text.split(breaks, Qt::KeepEmptyParts);
}

QSizeF calculateLiteralDimensions(const QString& text) {
  if (text.isEmpty()) return {};
  qreal width = 0.0;
  qreal height = 0.0;
  for (const QString& line : splitRenderedLines(text)) {
    width = qMax(width, literalWidth(line.isEmpty() ? QString(QChar(0x200b))
                                                    : line));
    height += literalLineHeight();
  }
  return {width, height};
}

QString breakLongWord(const QString& word) {
  QStringList lines;
  QString current;
  const QVector<uint> codePoints = word.toUcs4();
  for (qsizetype i = 0; i < codePoints.size(); ++i) {
    const char32_t codePoint = static_cast<char32_t>(codePoints.at(i));
    const QString next = current + QString::fromUcs4(&codePoint, 1);
    if (literalWidth(next) >= kTextMaxWidth) {
      lines.append(i + 1 == codePoints.size() ? next
                                              : next + QLatin1Char('-'));
      current.clear();
    } else {
      current = next;
    }
  }
  if (!current.isEmpty()) lines.append(current);
  return lines.join(QStringLiteral("<br/>"));
}

QString wrapLabel(const QString& label) {
  static const QRegularExpression breaks(
      QStringLiteral(R"(<br\s*/?>)"),
      QRegularExpression::CaseInsensitiveOption);
  if (label.isEmpty() || label.contains(breaks)) return label;
  const QStringList words = label.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  QStringList completed;
  QString nextLine;
  for (qsizetype i = 0; i < words.size(); ++i) {
    const QString& word = words.at(i);
    const qreal wordLength = literalWidth(word + QLatin1Char(' '));
    const qreal nextLength = literalWidth(nextLine);
    if (wordLength > kTextMaxWidth) {
      if (!nextLine.isEmpty()) completed.append(nextLine);
      completed.append(breakLongWord(word).split(QStringLiteral("<br/>")));
      nextLine.clear();
    } else if (nextLength + wordLength >= kTextMaxWidth) {
      if (!nextLine.isEmpty()) completed.append(nextLine);
      nextLine = word;
    } else {
      nextLine = nextLine.isEmpty() ? word : nextLine + QLatin1Char(' ') + word;
    }
    if (i + 1 == words.size() && !nextLine.isEmpty()) completed.append(nextLine);
  }
  return completed.join(QStringLiteral("<br/>"));
}

QString jsSubstring(const QString& source, int start, int end) {
  const int length = source.size();
  start = std::clamp(start, 0, length);
  end = std::clamp(end, 0, length);
  if (start > end) std::swap(start, end);
  return source.mid(start, end - start);
}

struct TextProps {
  QString html;
  qreal width = 0.0;
  qreal height = 0.0;
};

TextProps calculateTextProps(const EventModelingFrame& frame,
                             const QVector<EventModelingDataEntity>& entities) {
  const HtmlSanitizer sanitizer;
  const QString name =
      sanitizer.sanitizedMermaidText(extractName(frame.entityIdentifier));
  const QString wrappedName = wrapLabel(name);
  QString html = QStringLiteral("<b>%1</b>").arg(wrappedName);
  QString renderedData;
  bool hasData = false;
  if (!frame.dataInlineValue.isEmpty()) {
    renderedData = frame.dataInlineValue;
    renderedData = renderedData.mid(renderedData.indexOf(QLatin1Char('{')) + 1);
    renderedData =
        jsSubstring(renderedData, 0, renderedData.lastIndexOf(QLatin1Char('}')) - 1);
    hasData = true;
  }
  if (!frame.dataReference.isEmpty()) {
    const auto it = std::find_if(entities.cbegin(), entities.cend(),
                                 [&](const EventModelingDataEntity& entity) {
                                   return entity.name == frame.dataReference;
                                 });
    if (it != entities.cend()) {
      renderedData = it->dataBlockValue;
      renderedData =
          renderedData.mid(renderedData.indexOf(QStringLiteral("{\n")) + 2);
      renderedData = jsSubstring(
          renderedData, 0, renderedData.lastIndexOf(QLatin1Char('}')) - 1);
      hasData = true;
    }
  }
  if (hasData) {
    renderedData = sanitizer.sanitizedMermaidText(renderedData);
    renderedData = wrapLabel(renderedData);
    renderedData.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
    if (!frame.dataReference.isEmpty()) renderedData += QStringLiteral("<br/>");
    html += QStringLiteral("<br/><br/><code style=\"text-align: left; display: block;max-width:430px\">%1</code>")
                .arg(renderedData);
  }
  const QSizeF dimensions = calculateLiteralDimensions(html);
  return {html, hasData ? dimensions.width() / 3.0 : dimensions.width(),
          dimensions.height()};
}

struct LaneProps {
  int index = 0;
  QString label;
};

int nextIndex(const QVector<EventModelingSwimlaneGeometry>& lanes, int low,
              int high) {
  int value = low;
  for (const auto& lane : lanes)
    if (lane.index > low && lane.index < high) value = qMax(value, lane.index);
  return value + 1;
}

LaneProps laneProps(const EventModelingFrame& frame,
                    const QVector<EventModelingSwimlaneGeometry>& lanes) {
  // Upstream tries to find lane.namespace, but never writes that property.
  // Therefore every qualified identifier creates a fresh lane.
  const QString nameSpace = extractNamespace(frame.entityIdentifier);
  const QString type = frame.modelEntityType;
  if (type == QLatin1String("ui") || type == QLatin1String("pcr") ||
      type == QLatin1String("processor")) {
    if (!nameSpace.isEmpty())
      return {nextIndex(lanes, 0, 100), QStringLiteral("UI/A: ") + nameSpace};
    return {0, QStringLiteral("UI/Automation")};
  }
  if (type == QLatin1String("rmo") || type == QLatin1String("readmodel") ||
      type == QLatin1String("cmd") || type == QLatin1String("command")) {
    if (!nameSpace.isEmpty())
      return {nextIndex(lanes, 100, 200), QStringLiteral("C/RM: ") + nameSpace};
    return {100, QStringLiteral("Command/Read Model")};
  }
  if (!nameSpace.isEmpty())
    return {nextIndex(lanes, 200, 300), QStringLiteral("Stream: ") + nameSpace};
  return {200, QStringLiteral("Events")};
}

QJsonObject rectJson(const QRectF& rect) {
  return {{QStringLiteral("x"), rect.x()},
          {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

}  // namespace

void EventModelingScene::paint(QPainter& painter,
                               const MermaidPaintOptions& options) const {
  paintEventModelingScene(*this, painter, options);
}

QPair<QString, QString> eventModelingBoxPaint(
    const EventModelingFrame& frame, const EventModelingSceneStyle& style) {
  const QString& type = frame.modelEntityType;
  if (type == QLatin1String("ui")) return {style.uiFill, style.uiStroke};
  if (type == QLatin1String("pcr") || type == QLatin1String("processor"))
    return {style.processorFill, style.processorStroke};
  if (type == QLatin1String("rmo") || type == QLatin1String("readmodel"))
    return {style.readModelFill, style.readModelStroke};
  if (type == QLatin1String("cmd") || type == QLatin1String("command"))
    return {style.commandFill, style.commandStroke};
  return {style.eventFill, style.eventStroke};
}

EventModelingScene buildEventModelingScene(const EventModelingData& data,
                                            EventModelingConfig config,
                                            EventModelingSceneStyle style,
                                            const EventModelingCssOverrides* css) {
  EventModelingScene scene;
  scene.config = config;
  scene.style = std::move(style);
  scene.useMaxWidth = jsTruthy(scalar(config.useMaxWidth, true));
  scene.padding = editor::jsNumberValue(scalar(config.padding, 30.0));
  if (!std::isfinite(scene.padding)) scene.padding = 30.0;

  int previousLaneIndex = 0;
  bool hasPreviousLane = false;
  qreal maxRight = 0.0;
  for (qsizetype frameIndex = 0; frameIndex < data.frames.size(); ++frameIndex) {
    const EventModelingFrame& frame = data.frames.at(frameIndex);
    const TextProps text = calculateTextProps(frame, data.dataEntities);
    const LaneProps wanted = laneProps(frame, scene.swimlanes);
    auto laneIt = std::find_if(scene.swimlanes.begin(), scene.swimlanes.end(),
                               [&](const auto& lane) { return lane.index == wanted.index; });
    if (laneIt == scene.swimlanes.end()) {
      EventModelingSwimlaneGeometry lane;
      lane.index = wanted.index;
      lane.label = wanted.label;
      lane.y = wanted.index * kSwimlaneMinHeight + kSwimlaneGap;
      scene.swimlanes.append(lane);
      laneIt = scene.swimlanes.end() - 1;
    }
    const int laneVectorIndex = int(laneIt - scene.swimlanes.begin());
    const qreal width =
        std::max(kBoxMinWidth, std::min(kBoxMaxWidth, text.width + 20.0)) +
        2.0 * kBoxPadding;
    const qreal height =
        std::max(kBoxMinHeight, std::min(kBoxMaxHeight, text.height + 20.0)) +
        2.0 * kBoxPadding;
    qreal x = kContentStartX;
    if (hasPreviousLane) {
      const auto previousIt =
          std::find_if(scene.swimlanes.cbegin(), scene.swimlanes.cend(),
                       [&](const auto& lane) { return lane.index == previousLaneIndex; });
      if (previousLaneIndex == wanted.index && previousIt != scene.swimlanes.cend() &&
          previousIt->right != 0.0) {
        x = previousIt->right + kBoxPadding;
      } else if (!scene.boxes.isEmpty()) {
        x = scene.boxes.back().rightWithPadding - kBoxOverlap + kBoxPadding;
      }
    }
    laneIt = scene.swimlanes.begin() + laneVectorIndex;
    const qreal rightWithPadding = x + width + kBoxPadding;
    laneIt->right = x + width;
    laneIt->maxHeight = qMax(laneIt->maxHeight, height);
    laneIt->height = qMax(kSwimlaneMinHeight, laneIt->maxHeight) +
                     2.0 * kSwimlanePadding;
    maxRight = qMax(maxRight, rightWithPadding);

    EventModelingBoxGeometry box;
    box.frameIndex = int(frameIndex);
    box.swimlaneIndex = wanted.index;
    box.frameName = frame.name;
    box.modelEntityType = frame.modelEntityType;
    box.entityIdentifier = frame.entityIdentifier;
    box.rect = QRectF(x, 0.0, width, height);
    box.foreignObjectRect = QRectF(x + kBoxPadding, 0.0,
                                   width - 2.0 * kBoxPadding,
                                   height - 2.0 * kBoxPadding);
    const auto paints = eventModelingBoxPaint(frame, scene.style);
    box.fill = paints.first;
    box.stroke = paints.second;
    box.contentHtml = text.html;
    box.contentHtml.replace(QStringLiteral("<br/>"), QStringLiteral("<br>"));
    box.label = flowchart::parseFlowLabel(text.html, QStringLiteral("text"), false);
    box.rightWithPadding = rightWithPadding;
    scene.boxes.append(std::move(box));

    previousLaneIndex = wanted.index;
    hasPreviousLane = true;

    std::sort(scene.swimlanes.begin(), scene.swimlanes.end(),
              [](const auto& a, const auto& b) { return a.index < b.index; });
    if (!scene.swimlanes.isEmpty()) scene.swimlanes[0].y = 0.0;
    for (qsizetype i = 1; i < scene.swimlanes.size(); ++i)
      scene.swimlanes[i].y = scene.swimlanes[i - 1].y +
                             scene.swimlanes[i - 1].height + kSwimlaneGap;

    if (frame.reset || (frameIndex == 0 && frame.sourceFrames.isEmpty())) continue;
    int target = -1;
    for (qsizetype i = 0; i < scene.boxes.size(); ++i)
      if (scene.boxes.at(i).frameName == frame.name) {
        target = int(i);
        break;
      }
    if (target < 0) continue;
    QVector<int> sources;
    if (!frame.sourceFrames.isEmpty()) {
      for (const QString& name : frame.sourceFrames)
        for (qsizetype i = 0; i < scene.boxes.size(); ++i)
          if (scene.boxes.at(i).frameName == name) sources.append(int(i));
    } else {
      for (int i = int(frameIndex) - 1; i >= 0 && i < scene.boxes.size(); --i)
        if (scene.boxes.at(i).swimlaneIndex !=
            scene.boxes.at(target).swimlaneIndex) {
          sources.append(i);
          break;
        }
    }
    for (int source : sources) {
      EventModelingRelationGeometry relation;
      relation.sourceBox = source;
      relation.targetBox = target;
      relation.stroke = scene.style.relationStroke;
      scene.relations.append(std::move(relation));
    }
  }

  const auto laneFor = [&](int index) -> const EventModelingSwimlaneGeometry* {
    const auto it = std::find_if(scene.swimlanes.cbegin(), scene.swimlanes.cend(),
                                 [&](const auto& lane) { return lane.index == index; });
    return it == scene.swimlanes.cend() ? nullptr : &*it;
  };
  for (qsizetype laneIndex = 0; laneIndex < scene.swimlanes.size();
       ++laneIndex) {
    auto& lane = scene.swimlanes[laneIndex];
    lane.rect = QRectF(0.0, lane.y, maxRight + kSwimlanePadding, lane.height);
    lane.labelPosition = QPointF(30.0, lane.y + 30.0);
    if (css && laneIndex < css->swimlanes.size()) {
      lane.rectCss = css->swimlanes.at(laneIndex).rect;
      lane.textCss = css->swimlanes.at(laneIndex).text;
    }
  }
  for (qsizetype boxIndex = 0; boxIndex < scene.boxes.size(); ++boxIndex) {
    auto& box = scene.boxes[boxIndex];
    const auto* lane = laneFor(box.swimlaneIndex);
    const qreal y = (lane ? lane->y : 0.0) + kSwimlanePadding;
    box.rect.moveTop(y);
    box.foreignObjectRect.moveTop(y + 10.0);
    if (css && boxIndex < css->boxes.size()) {
      box.rectCss = css->boxes.at(boxIndex).rect;
      box.labelCss = css->boxes.at(boxIndex).label;
    }
  }
  for (qsizetype relationIndex = 0; relationIndex < scene.relations.size();
       ++relationIndex) {
    auto& relation = scene.relations[relationIndex];
    const auto& source = scene.boxes.at(relation.sourceBox);
    const auto& target = scene.boxes.at(relation.targetBox);
    const auto* sourceLane = laneFor(source.swimlaneIndex);
    const auto* targetLane = laneFor(target.swimlaneIndex);
    const qreal sourceLaneY = (sourceLane ? sourceLane->y : 0.0) + kSwimlanePadding;
    const qreal targetLaneY = (targetLane ? targetLane->y : 0.0) + kSwimlanePadding;
    const bool upwards = sourceLaneY > targetLaneY;
    const QPointF start(source.rect.x() + source.rect.width() * 2.0 / 3.0,
                        upwards ? sourceLaneY : sourceLaneY + source.rect.height());
    const QPointF end(target.rect.x() + target.rect.width() / 3.0,
                      upwards ? targetLaneY + target.rect.height() : targetLaneY);
    relation.line = QLineF(start, end);
    if (css && relationIndex < css->relations.size())
      relation.css = css->relations.at(relationIndex);
    relation.pathData = QStringLiteral("M%1 %2 L%3 %4")
                            .arg(editor::jsNumberToString(start.x()),
                                 editor::jsNumberToString(start.y()),
                                 editor::jsNumberToString(end.x()),
                                 editor::jsNumberToString(end.y()));
  }

  if (css) scene.markerCss = css->marker;
  if (scene.swimlanes.isEmpty()) {
    scene.contentBounds = QRectF();
  } else {
    qreal bottom = 0.0;
    for (const auto& lane : scene.swimlanes) bottom = qMax(bottom, lane.rect.bottom());
    scene.contentBounds = QRectF(0.0, 0.0, maxRight + kSwimlanePadding, bottom);
  }
  scene.bounds = scene.contentBounds.adjusted(-scene.padding, -scene.padding,
                                               scene.padding, scene.padding);
  if (scene.contentBounds.isNull())
    scene.bounds = QRectF(-scene.padding, -scene.padding,
                         scene.padding * 2.0, scene.padding * 2.0);
  return scene;
}

QJsonObject EventModelingScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("bounds")] = rectJson(bounds);
  root[QStringLiteral("contentBounds")] = rectJson(contentBounds);
  root[QStringLiteral("useMaxWidth")] = useMaxWidth;
  root[QStringLiteral("padding")] = padding;
  QJsonArray laneArray;
  for (const auto& lane : swimlanes)
    laneArray.append(QJsonObject{
        {QStringLiteral("index"), lane.index},
        {QStringLiteral("label"), lane.label},
        {QStringLiteral("rect"), rectJson(lane.rect)}});
  root[QStringLiteral("swimlanes")] = laneArray;
  QJsonArray boxArray;
  for (const auto& box : boxes)
    boxArray.append(QJsonObject{
        {QStringLiteral("frame"), box.frameName},
        {QStringLiteral("type"), box.modelEntityType},
        {QStringLiteral("rect"), rectJson(box.rect)},
        {QStringLiteral("html"), box.contentHtml},
        {QStringLiteral("fill"), box.fill},
        {QStringLiteral("stroke"), box.stroke}});
  root[QStringLiteral("boxes")] = boxArray;
  QJsonArray relationArray;
  for (const auto& relation : relations)
    relationArray.append(QJsonObject{
        {QStringLiteral("source"), relation.sourceBox},
        {QStringLiteral("target"), relation.targetBox},
        {QStringLiteral("path"), relation.pathData},
        {QStringLiteral("stroke"), relation.stroke}});
  root[QStringLiteral("relations")] = relationArray;
  return root;
}

}  // namespace muffin::mermaid::eventmodeling
