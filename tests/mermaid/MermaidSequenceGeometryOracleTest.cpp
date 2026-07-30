// Sequence geometry oracle: compares Muffin's native sequence::SequenceScene
// against real mermaid 11.16.0 sequenceDiagram structure captured by
// scripts/generate_mermaid_sequence_geometry_fixture.mjs (headless Chrome).
//
// Sequence layout (the legacy flat renderer) is positionally font-coupled —
// actor anchorX, message lineY, and lifeline Y all depend on per-text widths and
// heights. So the font-INDEPENDENT parity is the conversation STRUCTURE, and this
// oracle is purely structural (no geometry fields are compared):
//
// ASSERTS (font-independent):
//   - participant count + participant id multiset
//   - message count
//   - the ORDERED list of message tuples (from, to, dashed, markerEnd). Order
//     matters for sequence (it IS the conversation flow). This proves Muffin
//     reproduces mermaid's exact message sequence: who signals whom, in what
//     order, solid vs dashed, and arrow kind (arrowhead/crosshead/none for
//     ->>/-->>, --x/-x, ->).
//
// mermaid emits each message as <line data-from/data-to/data-id/marker-end/
// style> — read directly, no coordinate matching. Participant id == label (the
// corpus is alias-free; aliased participants would need an id↔label map).
// Self-loops (A->>A) render as a loop path, not a messageLine, so are excluded
// from the corpus.
//
// editor::MermaidRenderCache::getSync -> entry.scene (dynamic_cast<SequenceScene>) -> SequenceScene::toJsonObject().

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceScene.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

// Normalize a marker field: Muffin omits markerEnd when none; mermaid emits null.
QString normMarker(const QJsonValue& v) {
  const QString s = v.toString();
  return s;
}

// Ordered message identity: from | to | dashed | markerEnd-kind.
QString messageTuple(const QJsonObject& m) {
  return m.value(QStringLiteral("from")).toString() + QStringLiteral(" | ") +
         m.value(QStringLiteral("to")).toString() + QStringLiteral(" | dashed=") +
         (m.value(QStringLiteral("dashed")).toBool() ? QStringLiteral("Y") : QStringLiteral("N")) +
         QStringLiteral(" | ") + normMarker(m.value(QStringLiteral("markerEnd")));
}

QStringList participantIds(const QJsonArray& participants) {
  QStringList ids;
  for (const QJsonValue& p : participants)
    ids << p.toObject().value(QStringLiteral("id")).toString();
  ids.sort();
  return ids;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected sequence geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Sequence geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(
              QLatin1String("sequenceDiagram.render")),
          QStringLiteral("Sequence geometry: oracle contract drifted"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* sequenceScene = dynamic_cast<const sequence::SequenceScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && sequenceScene != nullptr,
            id + QStringLiteral(": native sequence render failed: ") + entry.errorMessage);
    const QJsonObject actual = sequenceScene->toJsonObject();

    QStringList assertErrors;

    const QJsonArray expectedParticipants = expected.value(QStringLiteral("participants")).toArray();
    const QJsonArray actualParticipants = actual.value(QStringLiteral("participants")).toArray();
    const QJsonArray expectedMessages = expected.value(QStringLiteral("messages")).toArray();
    const QJsonArray actualMessages = actual.value(QStringLiteral("messages")).toArray();

    // Participant multiset.
    const QStringList expectedParts = participantIds(expectedParticipants);
    const QStringList actualParts = participantIds(actualParticipants);
    if (actualParts != expectedParts) {
      assertErrors << id + QStringLiteral(": participant multiset diverged");
      assertErrors << QStringLiteral("  mermaid: %1").arg(expectedParts.join(QStringLiteral(", ")));
      assertErrors << QStringLiteral("  muffin : %1").arg(actualParts.join(QStringLiteral(", ")));
    }

    // Message count.
    if (actualMessages.size() != expectedMessages.size())
      assertErrors << id + QStringLiteral(": message count %1 != mermaid %2")
                          .arg(actualMessages.size()).arg(expectedMessages.size());

    // Ordered message tuples (order IS the conversation).
    QStringList expectedTuples;
    for (const QJsonValue& m : expectedMessages) expectedTuples << messageTuple(m.toObject());
    QStringList actualTuples;
    for (const QJsonValue& m : actualMessages) actualTuples << messageTuple(m.toObject());
    if (actualTuples != expectedTuples) {
      assertErrors << id + QStringLiteral(": ordered message tuples diverged");
      const int n = std::max(expectedTuples.size(), actualTuples.size());
      for (int i = 0; i < n; ++i) {
        if (i >= actualTuples.size() || i >= expectedTuples.size() ||
            actualTuples[i] != expectedTuples[i])
          assertErrors << QStringLiteral("  #%1  mermaid[%2]  muffin[%3]")
                              .arg(i)
                              .arg(i < expectedTuples.size() ? expectedTuples[i] : QStringLiteral("<none>"))
                              .arg(i < actualTuples.size() ? actualTuples[i] : QStringLiteral("<none>"));
      }
    }

    if (!assertErrors.isEmpty()) {
      for (const QString& e : assertErrors) std::fprintf(stderr, "%s\n", qPrintable(e));
      fail(id + QStringLiteral(": sequence geometry parity regression (participants/message order)"));
    }
  }
  return 0;
}
