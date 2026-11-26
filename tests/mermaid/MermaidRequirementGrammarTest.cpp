// Oracle for the requirementDiagram parser against real Mermaid 11.16.0.
//
// tests/fixtures/mermaid/requirement-grammar.json records, for each source,
// whether upstream's parser accepts it, the class and line of its error when
// it does not, and the requirements, elements and relationships it compiles
// when it does. The native parser must answer the same way and hand the
// renderer the same database. Every divergence is collected and reported, so
// one run shows the whole distance to upstream.

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/requirement/RequirementDiagram.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

QStringList failures;

// Upstream accepts or rejects these exactly as the port does, but reports the
// error on a later line: its parser runs on past the valueless directive and
// trips over the following content. The port names the directive's own line,
// which points the user at the right place; the difference is recorded here
// rather than reproduced.
const QSet<QString> kLineReportedDifferently = {QStringLiteral("reject-acc-title-empty"),
                                                QStringLiteral("reject-acc-title-spaces-only"),
                                                QStringLiteral("reject-acc-descr-empty")};

void check(bool condition, const QString& message) {
  if (!condition)
    failures << message;
}

QString joined(const QJsonArray& array) {
  QStringList values;
  for (const QJsonValue& value : array)
    values << value.toString();
  return values.join(QLatin1Char(','));
}

// One line per record, so a difference names the field it is in.
QStringList requirementRows(const QJsonArray& array) {
  QStringList rows;
  for (const QJsonValue& value : array) {
    const QJsonObject record = value.toObject();
    rows << QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                .arg(record.value(QStringLiteral("name")).toString(), record.value(QStringLiteral("type")).toString(),
                     record.value(QStringLiteral("requirementId")).toString(),
                     record.value(QStringLiteral("text")).toString(), record.value(QStringLiteral("risk")).toString(),
                     record.value(QStringLiteral("verifyMethod")).toString(),
                     joined(record.value(QStringLiteral("classes")).toArray()),
                     joined(record.value(QStringLiteral("cssStyles")).toArray()));
  }
  return rows;
}

QStringList requirementRows(const QVector<requirement::RequirementNode>& nodes) {
  QStringList rows;
  for (const requirement::RequirementNode& node : nodes)
    rows << QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                .arg(node.name, node.type, node.requirementId, node.text, node.risk, node.verifyMethod,
                     node.cssClasses.join(QLatin1Char(',')), node.cssStyles.join(QLatin1Char(',')));
  return rows;
}

QStringList elementRows(const QJsonArray& array) {
  QStringList rows;
  for (const QJsonValue& value : array) {
    const QJsonObject record = value.toObject();
    rows << QStringLiteral("%1|%2|%3|%4|%5")
                .arg(record.value(QStringLiteral("name")).toString(), record.value(QStringLiteral("type")).toString(),
                     record.value(QStringLiteral("docRef")).toString(),
                     joined(record.value(QStringLiteral("classes")).toArray()),
                     joined(record.value(QStringLiteral("cssStyles")).toArray()));
  }
  return rows;
}

QStringList elementRows(const QVector<requirement::ElementNode>& nodes) {
  QStringList rows;
  for (const requirement::ElementNode& node : nodes)
    rows << QStringLiteral("%1|%2|%3|%4|%5")
                .arg(node.name, node.type, node.docRef, node.cssClasses.join(QLatin1Char(',')),
                     node.cssStyles.join(QLatin1Char(',')));
  return rows;
}

QStringList relationshipRows(const QJsonArray& array) {
  QStringList rows;
  for (const QJsonValue& value : array) {
    const QJsonObject record = value.toObject();
    rows << QStringLiteral("%1|%2|%3")
                .arg(record.value(QStringLiteral("type")).toString(), record.value(QStringLiteral("src")).toString(),
                     record.value(QStringLiteral("dst")).toString());
  }
  return rows;
}

QStringList relationshipRows(const QVector<requirement::Relationship>& relations) {
  QStringList rows;
  for (const requirement::Relationship& relation : relations)
    rows << QStringLiteral("%1|%2|%3").arg(relation.type, relation.src, relation.dst);
  return rows;
}

// The classDef table, keyed by class id. Upstream's Map keeps insertion order;
// the port's hash does not, and nothing it renders depends on the order.
QStringList classRows(const QJsonArray& array) {
  QStringList rows;
  for (const QJsonValue& value : array) {
    const QJsonObject record = value.toObject();
    rows << record.value(QStringLiteral("id")).toString() + QLatin1Char('|') +
                joined(record.value(QStringLiteral("styles")).toArray());
  }
  rows.sort();
  return rows;
}

QStringList classRows(const QHash<QString, QStringList>& classDefs) {
  QStringList rows;
  for (auto it = classDefs.cbegin(); it != classDefs.cend(); ++it)
    rows << it.key() + QLatin1Char('|') + it.value().join(QLatin1Char(','));
  rows.sort();
  return rows;
}

void compareRows(const QStringList& native, const QStringList& upstream, const QString& what, const QString& id) {
  if (native == upstream)
    return;
  const qsizetype count = qMax(native.size(), upstream.size());
  for (qsizetype i = 0; i < count; ++i) {
    const QString mine = i < native.size() ? native.at(i) : QStringLiteral("<missing>");
    const QString theirs = i < upstream.size() ? upstream.at(i) : QStringLiteral("<missing>");
    if (mine != theirs) {
      check(false, QStringLiteral("%1/%2[%3]: native '%4', upstream '%5'").arg(id, what).arg(i).arg(mine, theirs));
      return;
    }
  }
}

void compareData(const requirement::RequirementDiagramData& data, const QJsonObject& expected, const QString& id) {
  check(data.accTitle == expected.value(QStringLiteral("accTitle")).toString(), id + QStringLiteral("/accTitle"));
  check(data.accDescription == expected.value(QStringLiteral("accDescription")).toString(),
        id + QStringLiteral("/accDescription"));
  check(data.direction == expected.value(QStringLiteral("direction")).toString(),
        QStringLiteral("%1/direction: native '%2', upstream '%3'")
            .arg(id, data.direction, expected.value(QStringLiteral("direction")).toString()));
  compareRows(requirementRows(data.requirements),
              requirementRows(expected.value(QStringLiteral("requirements")).toArray()), QStringLiteral("requirements"),
              id);
  compareRows(elementRows(data.elements), elementRows(expected.value(QStringLiteral("elements")).toArray()),
              QStringLiteral("elements"), id);
  compareRows(relationshipRows(data.relations),
              relationshipRows(expected.value(QStringLiteral("relationships")).toArray()),
              QStringLiteral("relationships"), id);
  compareRows(classRows(data.classDefs), classRows(expected.value(QStringLiteral("classes")).toArray()),
              QStringLiteral("classes"), id);
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  if (argc != 2) {
    std::fprintf(stderr, "Expected requirement grammar fixture\n");
    return 1;
  }
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) {
    std::fprintf(stderr, "%s\n", qPrintable(file.errorString()));
    return 1;
  }
  QByteArray bytes = file.readAll();
  // The generator emits LF; a CRLF checkout must not change the provenance.
  bytes.replace("\r\n", "\n");
  if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
      QByteArrayLiteral("a16edcac680845e43fe2de3f1474567260b5e6ebb59060ff0f70452e27c7e0f8")) {
    std::fprintf(stderr, "Requirement grammar fixture bytes changed\n");
    return 1;
  }
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  if (root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() !=
          QLatin1String("11.16.0") ||
      root.value(QStringLiteral("fixtureSha256")).toString() !=
          QLatin1String("bfeef636b6a84166c5268c6cb22072ebb8cec0d25202bce9f4d50442e5a1b486")) {
    std::fprintf(stderr, "Requirement grammar fixture provenance changed\n");
    return 1;
  }
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  check(cases.size() == 173, QStringLiteral("Requirement grammar case count drifted: %1").arg(cases.size()));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool upstreamAccepts = fixture.value(QStringLiteral("accept")).toBool();
    const QJsonObject reject = fixture.value(QStringLiteral("reject")).toObject();

    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(source, pre.config) == QLatin1String("requirement");
    } catch (const UnknownDiagramError&) {
      detected = false;
    }
    check(detected == (reject.value(QStringLiteral("class")).toString() != QLatin1String("no-diagram")),
          QStringLiteral("%1/detector: native %2")
              .arg(id, detected ? QStringLiteral("requirement") : QStringLiteral("none")));
    if (!detected)
      continue;

    bool accepted = false;
    try {
      const requirement::RequirementDiagramData data = requirement::RequirementDiagram::parse(pre.code).data();
      accepted = true;
      check(upstreamAccepts, id + QStringLiteral(": native accepted an upstream reject"));
      if (upstreamAccepts)
        compareData(data, fixture.value(QStringLiteral("expectedDb")).toObject(), id);
    } catch (const requirement::RequirementParseError& error) {
      check(!upstreamAccepts, QStringLiteral("%1: native rejected an upstream accept: %2 (line %3)")
                                  .arg(id, QString::fromUtf8(error.what()))
                                  .arg(error.line));
      // The diagnostic names the line upstream shows the user.
      if (!upstreamAccepts && reject.contains(QStringLiteral("messageLine"))) {
        const int upstreamLine = reject.value(QStringLiteral("messageLine")).toInt();
        if (error.line != upstreamLine && kLineReportedDifferently.contains(id))
          std::fprintf(stderr, "[known] %s: native line %d, upstream line %d\n", qPrintable(id), error.line,
                       upstreamLine);
        else
          check(error.line == upstreamLine,
                QStringLiteral("%1/error-line: native %2, upstream %3").arg(id).arg(error.line).arg(upstreamLine));
      }
    }

    // A diagram renders only when upstream's renderer draws it: a source whose
    // relationship names an undeclared node parses upstream but fails at render.
    const bool upstreamRenders = upstreamAccepts && fixture.value(QStringLiteral("renderAccept")).toBool();
    const editor::MermaidRenderEntry entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    const bool nativeRenders = entry.status == editor::MermaidRenderStatus::Ready;
    check(nativeRenders == upstreamRenders,
          QStringLiteral("%1/render: native %2, upstream %3%4")
              .arg(id, nativeRenders ? QStringLiteral("Ready") : QStringLiteral("not Ready"),
                   upstreamRenders ? QStringLiteral("Ready") : QStringLiteral("not Ready"),
                   nativeRenders ? QString() : QStringLiteral(" (") + entry.errorMessage + QStringLiteral(")")));
  }

  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidRequirementGrammarTest: %lld divergence(s) from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::puts("MermaidRequirementGrammarTest passed: 173 cases");
  return 0;
}
