#include "mermaid/sequence/SequenceScene.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <stdexcept>

namespace muffin::mermaid::sequence {

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout,
                                 SequenceSceneStyle style,
                                 const SequencePreparedLabels& prepared,
                                 bool requirePrepared) {
  SequenceScene scene;
  scene.bounds = layout.bounds;
  scene.logicalBounds = layout.logicalBounds;
  scene.boxes = layout.boxes;
  scene.participants = layout.participants;
  scene.messages = layout.messages;
  scene.activations = layout.activations;
  scene.notes = layout.notes;
  scene.fragments = layout.fragments;
  scene.sequenceNumbers = layout.sequenceNumbers;
  scene.forceMenus = layout.forceMenus;
  scene.style = std::move(style);
  const auto fallbackLabel = [&](const QString& text,
                                 SequenceLabelKind kind) {
    return prepareSequenceLabel(
        parseSequenceLabel(text, kind), scene.style.fontSize);
  };
  const auto labelFor = [&](const auto& labels, const auto& key,
                            const QString& text, SequenceLabelKind kind) {
    const auto found = labels.constFind(key);
    if (found != labels.cend()) return found.value();
    if (requirePrepared)
      throw std::logic_error(
          QStringLiteral("Missing prepared sequence label for key '%1'")
              .arg(key).toStdString());
    return fallbackLabel(text, kind);
  };
  for (const auto& box : scene.boxes)
    scene.boxLabels.append(labelFor(prepared.boxesByIndex, box.boxIndex,
                                    box.label, SequenceLabelKind::Box));
  for (const auto& actor : scene.participants)
    scene.participantLabels.append(labelFor(
        prepared.participantsById, actor.id, actor.label,
        SequenceLabelKind::Participant));
  for (const auto& message : scene.messages)
    scene.messageLabels.append(labelFor(
        prepared.messagesByIndex, message.messageIndex, message.label,
        SequenceLabelKind::Message));
  for (const auto& note : scene.notes)
    scene.noteLabels.append(labelFor(
        prepared.notesByIndex, note.messageIndex, note.label,
        SequenceLabelKind::Note));
  for (const auto& fragment : scene.fragments) {
    scene.fragmentKindLabels.append(labelFor(
        prepared.fragmentKindsByIndex, fragment.messageIndex, fragment.kind,
        SequenceLabelKind::Box));
    scene.fragmentLabels.append(labelFor(
        prepared.fragmentsByIndex, fragment.messageIndex, fragment.label,
        SequenceLabelKind::Fragment));
  }
  for (const auto& sourceMenu : layout.menus) {
    SequenceSceneMenu menu;
    menu.actorId = sourceMenu.actorId;
    menu.panelRect = sourceMenu.panelRect;
    for (const auto& sourceItem : sourceMenu.items) {
      SequenceSceneMenuItem item;
      item.label = sourceItem.label;
      item.link = sourceItem.link;
      item.hitRect = sourceItem.hitRect;
      item.labelRect = sourceItem.labelRect;
      item.labelDocument = labelFor(
          prepared.menuItemsByKey,
          sequenceMenuLabelKey(sourceMenu.actorId, sourceItem.label),
          sourceItem.label, SequenceLabelKind::Participant);
      menu.items.append(std::move(item));
    }
    scene.menus.append(std::move(menu));
  }
  // Interaction regions: actors first, then items (reverse iteration in the
  // editor gives items priority). Precomputed once at build, returned by ref.
  for (const SequenceSceneMenu& menu : scene.menus) {
    for (const SequenceLayoutParticipant& p : scene.participants) {
      if (p.id != menu.actorId) continue;
      InteractionRegion actor;
      actor.bounds = p.topPaintedBounds
          .united(p.topLabelRect)
          .united(QRectF(p.logicalRect.x(), p.topY,
                         p.logicalRect.width(), p.logicalRect.height()));
      actor.togglesMenu = menu.actorId;
      scene.interactionRegions_.append(actor);
      break;
    }
  }
  for (const SequenceSceneMenu& menu : scene.menus) {
    for (const SequenceSceneMenuItem& item : menu.items) {
      InteractionRegion region;
      region.bounds = item.hitRect;
      region.href = item.link;
      region.accessibleLabel = item.label;
      region.requiresOpenMenu = menu.actorId;
      scene.interactionRegions_.append(region);
    }
  }
  return scene;
}

namespace {

qreal r3(qreal v) { return std::round(v * 1000.0) / 1000.0; }

QJsonObject rectJson(const QRectF& r) {
  return {{QStringLiteral("x"), r3(r.x())},
          {QStringLiteral("y"), r3(r.y())},
          {QStringLiteral("width"), r3(r.width())},
          {QStringLiteral("height"), r3(r.height())}};
}

}  // namespace

QJsonObject SequenceScene::toJsonObject() const {
  QJsonObject o;
  o[QStringLiteral("bounds")] = rectJson(bounds);
  o[QStringLiteral("logicalBounds")] = rectJson(logicalBounds);

  QJsonArray boxesArray;
  for (const SequenceLayoutBox& box : boxes) {
    QJsonObject b;
    b[QStringLiteral("boxIndex")] = box.boxIndex;
    if (!box.label.isEmpty())
      b[QStringLiteral("label")] = box.label;
    if (!box.fill.isEmpty())
      b[QStringLiteral("fill")] = box.fill;
    b[QStringLiteral("rect")] = rectJson(box.rect);
    boxesArray.append(b);
  }
  o[QStringLiteral("boxes")] = boxesArray;

  QJsonArray participantsArray;
  for (const SequenceLayoutParticipant& participant : participants) {
    QJsonObject p;
    p[QStringLiteral("id")] = participant.id;
    if (participant.actorIndex >= 0)
      p[QStringLiteral("actorIndex")] = participant.actorIndex;
    if (!participant.type.isEmpty())
      p[QStringLiteral("type")] = participant.type;
    p[QStringLiteral("label")] = participant.label;
    p[QStringLiteral("anchorX")] = r3(participant.anchorX);
    p[QStringLiteral("lifelineStartY")] = r3(participant.lifelineStartY);
    p[QStringLiteral("lifelineStopY")] = r3(participant.lifelineStopY);
    p[QStringLiteral("topY")] = r3(participant.topY);
    p[QStringLiteral("bottomY")] = r3(participant.bottomY);
    participantsArray.append(p);
  }
  o[QStringLiteral("participants")] = participantsArray;

  QJsonArray messagesArray;
  for (const SequenceLayoutMessage& message : messages) {
    QJsonObject m;
    m[QStringLiteral("messageIndex")] = message.messageIndex;
    // Stable identity: prefer the upstream id; synthesize msg#<index> when absent.
    if (!message.id.isEmpty())
      m[QStringLiteral("id")] = message.id;
    else
      m[QStringLiteral("key")] = QStringLiteral("msg#%1").arg(message.messageIndex);
    m[QStringLiteral("from")] = message.from;
    m[QStringLiteral("to")] = message.to;
    if (!message.label.isEmpty())
      m[QStringLiteral("label")] = message.label;
    m[QStringLiteral("type")] = message.type;
    m[QStringLiteral("startX")] = r3(message.startX);
    m[QStringLiteral("stopX")] = r3(message.stopX);
    m[QStringLiteral("lineY")] = r3(message.lineY);
    m[QStringLiteral("dashed")] = message.dashed;
    if (!message.path.isEmpty())
      m[QStringLiteral("path")] = message.path;
    if (!message.markerStart.isEmpty())
      m[QStringLiteral("markerStart")] = message.markerStart;
    if (!message.markerEnd.isEmpty())
      m[QStringLiteral("markerEnd")] = message.markerEnd;
    messagesArray.append(m);
  }
  o[QStringLiteral("messages")] = messagesArray;

  QJsonArray activationsArray;
  for (const SequenceLayoutActivation& activation : activations) {
    QJsonObject a;
    a[QStringLiteral("messageIndex")] = activation.messageIndex;
    a[QStringLiteral("actor")] = activation.actor;
    a[QStringLiteral("depth")] = activation.depth;
    a[QStringLiteral("rect")] = rectJson(activation.rect);
    if (activation.actorIndex >= 0)
      a[QStringLiteral("actorIndex")] = activation.actorIndex;
    activationsArray.append(a);
  }
  o[QStringLiteral("activations")] = activationsArray;

  QJsonArray notesArray;
  for (const SequenceLayoutNote& note : notes) {
    QJsonObject n;
    n[QStringLiteral("messageIndex")] = note.messageIndex;
    n[QStringLiteral("from")] = note.from;
    n[QStringLiteral("to")] = note.to;
    n[QStringLiteral("placement")] = note.placement;
    if (!note.label.isEmpty())
      n[QStringLiteral("label")] = note.label;
    n[QStringLiteral("rect")] = rectJson(note.rect);
    notesArray.append(n);
  }
  o[QStringLiteral("notes")] = notesArray;

  QJsonArray fragmentsArray;
  for (const SequenceLayoutFragment& fragment : fragments) {
    QJsonObject f;
    f[QStringLiteral("messageIndex")] = fragment.messageIndex;
    f[QStringLiteral("kind")] = fragment.kind;
    if (!fragment.label.isEmpty())
      f[QStringLiteral("label")] = fragment.label;
    f[QStringLiteral("depth")] = fragment.depth;
    f[QStringLiteral("rect")] = rectJson(fragment.rect);
    fragmentsArray.append(f);
  }
  o[QStringLiteral("fragments")] = fragmentsArray;

  QJsonArray sequenceNumbersArray;
  for (const SequenceLayoutNumber& number : sequenceNumbers) {
    QJsonObject s;
    s[QStringLiteral("messageIndex")] = number.messageIndex;
    s[QStringLiteral("text")] = number.text;
    s[QStringLiteral("x")] = r3(number.position.x());
    s[QStringLiteral("y")] = r3(number.position.y());
    sequenceNumbersArray.append(s);
  }
  o[QStringLiteral("sequenceNumbers")] = sequenceNumbersArray;

  return o;
}

SvgMarkerProjection SequenceScene::svgMarkerProjection() const {
  SvgMarkerProjection projection;
  const auto addPath = [&](QString key, qreal refX, qreal refY,
                           qreal width, qreal height, QString path,
                           QString units = {},
                           QString orient = QStringLiteral("auto-start-reverse"),
                           QString stroke = {}, QString fill = {}) {
    SvgMarkerDefinition definition;
    definition.key = key;
    definition.idSuffix = QStringLiteral("-") + key;
    definition.refX = refX; definition.refY = refY;
    definition.markerWidth = width; definition.markerHeight = height;
    definition.markerUnits = std::move(units);
    definition.orient = std::move(orient);
    SvgMarkerChild child;
    child.tag = QStringLiteral("path"); child.path = std::move(path);
    child.stroke = stroke.isEmpty() ? style.signalColor : std::move(stroke);
    child.fill = fill.isEmpty() ? style.signalColor : std::move(fill);
    definition.children.append(child);
    projection.definitions.append(definition);
  };
  addPath(QStringLiteral("arrowhead"), 7.9, 5, 12, 12,
          QStringLiteral("M -1 0 L 10 5 L 0 10 z"),
          QStringLiteral("userSpaceOnUse"));
  addPath(QStringLiteral("crosshead"), 4, 4.5, 15, 8,
          QStringLiteral("M 1,2 L 6,7 M 6,2 L 1,7"), {},
          QStringLiteral("auto"), QStringLiteral("#000000"),
          QStringLiteral("none"));
  projection.definitions.last().children.last().strokeWidth = QStringLiteral("1pt");
  projection.definitions.last().children.last().style = QStringLiteral("stroke-dasharray: 0, 0;");
  addPath(QStringLiteral("filled-head"), 15.5, 7, 20, 28,
          QStringLiteral("M 18,7 L9,13 L14,7 L9,1 Z"), {},
          QStringLiteral("auto"));
  addPath(QStringLiteral("solidTopArrowHead"), 7.9, 7.25, 12, 12,
          QStringLiteral("M 0 0 L 10 8 L 0 8 z"),
          QStringLiteral("userSpaceOnUse"));
  addPath(QStringLiteral("solidBottomArrowHead"), 7.9, 0.75, 12, 12,
          QStringLiteral("M 0 0 L 10 0 L 0 8 z"),
          QStringLiteral("userSpaceOnUse"));
  addPath(QStringLiteral("stickTopArrowHead"), 7.5, 7, 12, 12,
          QStringLiteral("M 0 0 L 7 7"), QStringLiteral("userSpaceOnUse"),
          QStringLiteral("auto-start-reverse"), QStringLiteral("black"),
          QStringLiteral("none"));
  projection.definitions.last().children.last().strokeWidth = QStringLiteral("1.5");
  addPath(QStringLiteral("stickBottomArrowHead"), 7.5, 0, 12, 12,
          QStringLiteral("M 0 7 L 7 0"), QStringLiteral("userSpaceOnUse"),
          QStringLiteral("auto-start-reverse"), QStringLiteral("black"),
          QStringLiteral("none"));
  projection.definitions.last().children.last().strokeWidth = QStringLiteral("1.5");
  SvgMarkerDefinition number;
  number.key = QStringLiteral("sequencenumber");
  number.idSuffix = QStringLiteral("-sequencenumber");
  number.refX = 15; number.refY = 15;
  number.markerWidth = 60; number.markerHeight = 40;
  SvgMarkerChild circle;
  circle.tag = QStringLiteral("circle"); circle.cx = 15; circle.cy = 15;
  circle.radius = 6; circle.fill = style.actorFill;
  number.children.append(circle);
  projection.definitions.append(number);

  for (const SequenceLayoutMessage& message : messages) {
    if (message.markerStart.isEmpty() && message.markerEnd.isEmpty()) continue;
    SvgMarkerEdge edge;
    edge.tag = message.painterPath.isEmpty() ? QStringLiteral("line")
                                             : QStringLiteral("path");
    edge.cssClass = message.dashed ? QStringLiteral("messageLine1")
                                   : QStringLiteral("messageLine0");
    edge.start = QPointF(message.startX, message.lineY);
    edge.end = QPointF(message.stopX, message.lineY);
    edge.path = message.path;
    edge.markerStart = message.markerStart;
    edge.markerEnd = message.markerEnd;
    edge.stroke = style.signalColor;
    edge.strokeWidth = QStringLiteral("2");
    if (message.dashed) edge.strokeDasharray = QStringLiteral("3,3");
    projection.edges.append(edge);
  }
  return projection;
}

}  // namespace muffin::mermaid::sequence
