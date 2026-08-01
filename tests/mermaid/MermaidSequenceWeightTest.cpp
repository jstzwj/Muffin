// Verifies sequence per-label font weights (sequence.actor/note/message
// FontWeight), the last live keys of the sequence font batch. Assertions are
// structural (the resolved weight reaches each prepared label's
// FlowLabelDocument::baseWeight) plus behavioral (bold changes paint).
//
// Attribution, all verified vs mermaid 11.16.0 by isolated headless-Chrome probe:
//   participant/box/menu -> actorFontWeight, note -> noteFontWeight,
//   message/fragment (the kind tag "loop" included) -> messageFontWeight.
// A truthy GLOBAL fontWeight overrides all three (setConf mirror). Math note/
// message labels render Normal regardless, because drawKatex ignores font-weight
// (analogous to noteAlign/messageAlign). Menu items share the participant code
// path (built as Participant kind), so the participant assertion covers them.
//
// Qt 6.11.1 QFont::Weight is the standard CSS 100..900 scale (Normal=400,
// Bold=700); "normal"->400, "bold"->700, numerics pass through.

#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceScene.h"

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QString>

#include <QtGlobal>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  fprintf(stderr, "WEIGHT TEST FAILED: %s\n", qPrintable(message));
  fflush(stderr);
  std::exit(1);
}
void require(bool value, const QString& message) {
  if (!value) fail(message);
}

// Returns the scene with shared ownership; a bare pointer would dangle once the
// local cache entry is destroyed.
std::shared_ptr<const sequence::SequenceScene> sequenceScene(const QString& source) {
  editor::MermaidRenderCache cache;
  editor::MermaidRenderEntry entry =
      cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  return std::dynamic_pointer_cast<const sequence::SequenceScene>(entry.scene);
}

// Wraps a sequence body in a %%{init}%% directive. initObject is a JSON object
// string, e.g. {"sequence":{"actorFontWeight":700}} or
// {"fontWeight":700,"sequence":{"actorFontWeight":900}}.
QString withInit(const QString& initObject, const QString& body) {
  return QStringLiteral("%%{init: %1}%%\n%2").arg(initObject, body);
}

bool allHaveWeight(const QVector<sequence::SequenceLabelDocument>& labels, QFont::Weight expected) {
  if (labels.isEmpty()) return false;
  for (const auto& label : labels)
    if (label.richText.baseWeight != expected) return false;
  return true;
}

QFont::Weight firstWeight(const QVector<sequence::SequenceLabelDocument>& labels) {
  require(!labels.isEmpty(), QStringLiteral("expected at least one label"));
  return labels.first().richText.baseWeight;
}

QImage renderPng(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPngDataUrl(source, 2.0);
  const int comma = dataUrl.indexOf(QLatin1Char(','));
  require(comma > 0, QStringLiteral("Mermaid PNG data URL is malformed"));
  return QImage::fromData(
      QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
}

int rgbaDiffPixels(const QImage& a, const QImage& b) {
  if (a.size() != b.size()) return -1;
  int diff = 0;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x) {
      const QColor ca = a.pixelColor(x, y);
      const QColor cb = b.pixelColor(x, y);
      const bool aa = ca.alpha() >= 16;
      const bool ab = cb.alpha() >= 16;
      if (aa != ab) { ++diff; continue; }
      if (!aa) continue;
      if (std::abs(ca.red() - cb.red()) + std::abs(ca.green() - cb.green()) +
              std::abs(ca.blue() - cb.blue()) >
          24)
        ++diff;
    }
  return diff;
}
}  // namespace

// Body exercising every kind: box title, two participants, a plain message, a
// note, and a loop fragment (kind tag + label).
const QString kBody = QStringLiteral(
    "sequenceDiagram\n"
    "box rgb(200,220,255) Group\n"
    "participant Alice\n"
    "participant Bob\n"
    "end\n"
    "Alice->>Bob: hello\n"
    "Note over Bob: anote\n"
    "loop repeat\n"
    "Bob->>Alice: reply\n"
    "end");

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  // 1. Per-kind independence + attribution. Each weight affects only its kind.
  {
    const auto scene = sequenceScene(withInit(
        QStringLiteral("{\"sequence\":{\"actorFontWeight\":700}}"), kBody));
    require(allHaveWeight(scene->participantLabels, QFont::Bold),
            QStringLiteral("actorFontWeight:700 -> participants Bold"));
    require(allHaveWeight(scene->boxLabels, QFont::Bold),
            QStringLiteral("box title tracks actorFontWeight (-> Bold)"));
    require(allHaveWeight(scene->messageLabels, QFont::Normal),
            QStringLiteral("actorFontWeight must not leak into messages"));
    require(allHaveWeight(scene->noteLabels, QFont::Normal),
            QStringLiteral("actorFontWeight must not leak into notes"));
    require(allHaveWeight(scene->fragmentKindLabels, QFont::Normal),
            QStringLiteral("fragment kind tag tracks messageFontWeight (default Normal)"));
    require(allHaveWeight(scene->fragmentLabels, QFont::Normal),
            QStringLiteral("fragment label tracks messageFontWeight (default Normal)"));
  }
  {
    const auto scene = sequenceScene(withInit(
        QStringLiteral("{\"sequence\":{\"messageFontWeight\":700}}"), kBody));
    require(allHaveWeight(scene->messageLabels, QFont::Bold),
            QStringLiteral("messageFontWeight:700 -> messages Bold"));
    require(allHaveWeight(scene->fragmentKindLabels, QFont::Bold),
            QStringLiteral("fragment kind tag tracks messageFontWeight (-> Bold)"));
    require(allHaveWeight(scene->fragmentLabels, QFont::Bold),
            QStringLiteral("fragment label tracks messageFontWeight (-> Bold)"));
    require(allHaveWeight(scene->participantLabels, QFont::Normal),
            QStringLiteral("messageFontWeight must not leak into participants"));
    require(allHaveWeight(scene->boxLabels, QFont::Normal),
            QStringLiteral("messageFontWeight must not leak into box titles"));
    require(allHaveWeight(scene->noteLabels, QFont::Normal),
            QStringLiteral("messageFontWeight must not leak into notes"));
  }
  {
    const auto scene = sequenceScene(withInit(
        QStringLiteral("{\"sequence\":{\"noteFontWeight\":700}}"), kBody));
    require(allHaveWeight(scene->noteLabels, QFont::Bold),
            QStringLiteral("noteFontWeight:700 -> notes Bold"));
    require(allHaveWeight(scene->participantLabels, QFont::Normal),
            QStringLiteral("noteFontWeight must not leak into participants"));
    require(allHaveWeight(scene->messageLabels, QFont::Normal),
            QStringLiteral("noteFontWeight must not leak into messages"));
  }

  // 2. Root fontWeight overrides all three per-kind (setConf mirror).
  {
    const auto scene = sequenceScene(withInit(
        QStringLiteral("{\"fontWeight\":700,\"sequence\":{"
                       "\"actorFontWeight\":900,\"noteFontWeight\":900,"
                       "\"messageFontWeight\":900}}"),
        kBody));
    require(allHaveWeight(scene->participantLabels, QFont::Bold),
            QStringLiteral("global fontWeight:700 overrides actorFontWeight:900"));
    require(allHaveWeight(scene->noteLabels, QFont::Bold),
            QStringLiteral("global fontWeight:700 overrides noteFontWeight:900"));
    require(allHaveWeight(scene->messageLabels, QFont::Bold),
            QStringLiteral("global fontWeight:700 overrides messageFontWeight:900"));
  }
  // Truthy semantics of the global gate: a truthy-but-invalid global (a
  // whitespace string is JS-truthy) fires the mirror and overrides per-kind to
  // Normal; 0 is JS-falsy so the mirror is skipped and per-kind stands.
  {
    const auto wsScene = sequenceScene(withInit(
        QStringLiteral("{\"fontWeight\":\"   \",\"sequence\":{"
                       "\"actorFontWeight\":900,\"messageFontWeight\":900}}"),
        kBody));
    require(allHaveWeight(wsScene->participantLabels, QFont::Normal),
            QStringLiteral("truthy whitespace global overrides per-kind to Normal"));
    require(allHaveWeight(wsScene->messageLabels, QFont::Normal),
            QStringLiteral("truthy whitespace global overrides per-kind to Normal"));
  }
  {
    const auto zeroScene = sequenceScene(withInit(
        QStringLiteral("{\"fontWeight\":0,\"sequence\":{\"actorFontWeight\":700}}"),
        kBody));
    require(allHaveWeight(zeroScene->participantLabels, QFont::Bold),
            QStringLiteral("global fontWeight:0 is falsy -> per-kind actorFontWeight stands"));
  }

  // 3. Value mapping: numeric 400/500/700/900 and "normal"/"bold".
  {
    const QString simple = QStringLiteral("sequenceDiagram\nAlice->>Bob: hi");
    const auto check = [&simple](const QString& cfg, QFont::Weight expected) {
      const auto scene =
          sequenceScene(withInit(QStringLiteral("{\"sequence\":%1}").arg(cfg), simple));
      require(firstWeight(scene->messageLabels) == expected,
              QStringLiteral("%1 -> %2 (got %3)")
                  .arg(cfg).arg(expected).arg(firstWeight(scene->messageLabels)));
    };
    check(QStringLiteral("{\"messageFontWeight\":\"bold\"}"), QFont::Bold);       // bold -> 700
    check(QStringLiteral("{\"messageFontWeight\":\"normal\"}"), QFont::Normal);   // normal -> 400
    check(QStringLiteral("{\"messageFontWeight\":400}"), QFont::Normal);          // 400 -> Normal
    check(QStringLiteral("{\"messageFontWeight\":700}"), QFont::Bold);            // 700 -> Bold
    check(QStringLiteral("{\"messageFontWeight\":500}"), QFont::Weight(500));     // numeric passthrough
    check(QStringLiteral("{\"messageFontWeight\":900}"), QFont::Weight(900));     // numeric passthrough
    check(QStringLiteral("{\"messageFontWeight\":\"bolder\"}"), QFont::Bold);     // bolder -> 700
    check(QStringLiteral("{\"messageFontWeight\":\"lighter\"}"), QFont::Weight(100));  // lighter -> 100
    check(QStringLiteral("{\"messageFontWeight\":0}"), QFont::Normal);            // 0 invalid -> Normal
    check(QStringLiteral("{\"messageFontWeight\":1001}"), QFont::Normal);         // 1001 invalid -> Normal
    check(QStringLiteral("{\"messageFontWeight\":\"0\"}"), QFont::Normal);        // "0" invalid -> Normal
    check(QStringLiteral("{\"messageFontWeight\":\"500.5\"}"), QFont::Weight(501));  // decimal string -> 501
    check(QStringLiteral("{\"messageFontWeight\":\"1e2\"}"), QFont::Weight(100));    // scientific string -> 100
    check(QStringLiteral("{\"messageFontWeight\":\"+500\"}"), QFont::Weight(500));   // signed string -> 500
    check(QStringLiteral("{\"messageFontWeight\":\"0500\"}"), QFont::Weight(500));   // leading-zero string -> 500
  }

  // 4. Math labels render Normal regardless of the per-kind weight (drawKatex
  //    ignores font-weight) — for pure and mixed math, note and message.
  {
    const QString mathBody = QStringLiteral(
        "sequenceDiagram\n"
        "Alice->>Bob: $$x^2$$\n"
        "Alice->>Bob: sum $$x^2$$\n"
        "Note over Alice: $$y$$\n"
        "Note over Alice: val $$y$$");
    const auto scene = sequenceScene(withInit(
        QStringLiteral("{\"sequence\":{\"messageFontWeight\":700,\"noteFontWeight\":700}}"),
        mathBody));
    require(allHaveWeight(scene->messageLabels, QFont::Normal),
            QStringLiteral("Math messages render Normal (drawKatex ignores weight)"));
    require(allHaveWeight(scene->noteLabels, QFont::Normal),
            QStringLiteral("Math notes render Normal (drawKatex ignores weight)"));
  }

  // 5. Default == explicit Normal.
  {
    const auto def = sequenceScene(kBody);
    const auto explicitNormal = sequenceScene(withInit(
        QStringLiteral("{\"sequence\":{\"actorFontWeight\":\"normal\","
                       "\"noteFontWeight\":\"normal\",\"messageFontWeight\":\"normal\"}}"),
        kBody));
    require(allHaveWeight(def->participantLabels, QFont::Normal),
            QStringLiteral("default participant weight is Normal"));
    require(allHaveWeight(def->messageLabels, QFont::Normal),
            QStringLiteral("default message weight is Normal"));
    require(allHaveWeight(def->noteLabels, QFont::Normal),
            QStringLiteral("default note weight is Normal"));
    require(allHaveWeight(explicitNormal->participantLabels, QFont::Normal) &&
                allHaveWeight(explicitNormal->messageLabels, QFont::Normal) &&
                allHaveWeight(explicitNormal->noteLabels, QFont::Normal),
            QStringLiteral("explicit \"normal\" resolves to Normal"));
    // Default and explicit-Normal render pixel-identically.
    require(rgbaDiffPixels(renderPng(kBody), renderPng(withInit(
        QStringLiteral("{\"sequence\":{\"actorFontWeight\":400,\"messageFontWeight\":400,"
                       "\"noteFontWeight\":400}}"), kBody))) == 0,
        QStringLiteral("default and explicit-Normal render pixel-identically"));
  }

  // 6. Bold changes paint end-to-end (synthetic bold widens the actor strokes).
  {
    const QImage normal = renderPng(kBody);
    const QImage bold = renderPng(withInit(
        QStringLiteral("{\"sequence\":{\"actorFontWeight\":700}}"), kBody));
    require(rgbaDiffPixels(normal, bold) > 0,
            QStringLiteral("actorFontWeight:700 must change the rendered output"));
  }

  // 7. Menu item attribution (direct): popup menu items are built as Participant
  //    kind, so they track actorFontWeight. (Verified directly on scene.menus,
  //    not just transitively via the participant assertion.)
  {
    const QString menuBody = QStringLiteral(
        "sequenceDiagram\nparticipant A as Browser\nparticipant B as API\n"
        "links A: {\"Documentation\": \"https://example.com/docs\"}\n"
        "A->>B: request");
    const auto scene = sequenceScene(withInit(
        QStringLiteral("{\"sequence\":{\"actorFontWeight\":700,\"forceMenus\":true}}"),
        menuBody));
    require(!scene->menus.isEmpty() && !scene->menus.first().items.isEmpty(),
            QStringLiteral("expected at least one sequence menu item"));
    const QFont::Weight menuWeight =
        scene->menus.first().items.first().labelDocument.richText.baseWeight;
    require(menuWeight == QFont::Bold,
            QStringLiteral("menu item label tracks actorFontWeight (-> Bold), got %1")
                .arg(menuWeight));
  }

  return 0;
}
