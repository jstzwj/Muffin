// Oracle for the erDiagram parser against real Mermaid 11.16.0.
//
// tests/fixtures/mermaid/er-grammar.json records, for each source, whether
// upstream's parser accepts it, the class and line of its error when it does
// not, and the entities (with aliases, attributes and classes), relationships,
// class definitions and direction it compiles when it does. The native parser
// must answer the same way and hand the renderer the same database. Every
// difference is collected, so one run shows the whole distance to upstream.

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErDiagram.h"
#include "mermaid/erdiagram/ErLayout.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

QStringList failures;

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
QStringList entityRows(const QJsonArray& array) {
  QStringList rows;
  for (const QJsonValue& value : array) {
    const QJsonObject record = value.toObject();
    QStringList attributes;
    for (const QJsonValue& attributeValue : record.value(QStringLiteral("attributes")).toArray()) {
      const QJsonObject attribute = attributeValue.toObject();
      attributes << QStringLiteral("%1 %2|%3|%4")
                       .arg(attribute.value(QStringLiteral("type")).toString(),
                            attribute.value(QStringLiteral("name")).toString(),
                            joined(attribute.value(QStringLiteral("keys")).toArray()),
                            attribute.value(QStringLiteral("comment")).toString());
    }
    rows << QStringLiteral("%1|%2|%3|%4|%5|%6")
                .arg(record.value(QStringLiteral("id")).toString(), record.value(QStringLiteral("name")).toString(),
                     record.value(QStringLiteral("alias")).toString(), attributes.join(QLatin1Char(';')),
                     record.value(QStringLiteral("cssClasses")).toString(),
                     joined(record.value(QStringLiteral("cssStyles")).toArray()));
  }
  return rows;
}

QStringList entityRows(const QVector<er::ErEntity>& entities) {
  QStringList rows;
  for (const er::ErEntity& entity : entities) {
    QStringList attributes;
    for (const er::ErAttribute& attribute : entity.attributes)
      attributes << QStringLiteral("%1 %2|%3|%4")
                       .arg(attribute.attributeType, attribute.attributeName, attribute.keys.join(QLatin1Char(',')),
                            attribute.comment);
    // Mermaid writes every entity's class list as "default" plus its classes;
    // the port leaves it empty when there are none.
    const QString classes =
        entity.cssClasses.isEmpty() ? QStringLiteral("default") : QStringLiteral("default ") + entity.cssClasses;
    rows << QStringLiteral("%1|%2|%3|%4|%5|%6")
                .arg(entity.id, entity.name, entity.alias, attributes.join(QLatin1Char(';')), classes,
                     entity.styles.join(QLatin1Char(',')));
  }
  return rows;
}

// One line per relationship: its edge id, both ends by entity id, the
// cardinality at each end, its type, and its label.
QString relationshipRow(const QString& id, const QString& entityA, const QString& startCard, const QString& relType,
                        const QString& endCard, const QString& entityB, const QString& roleA) {
  return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
      .arg(id, entityA, startCard, relType, endCard, entityB, roleA);
}

QStringList relationshipRows(const QJsonArray& array) {
  QStringList rows;
  for (const QJsonValue& value : array) {
    const QJsonObject record = value.toObject();
    rows << relationshipRow(record.value(QStringLiteral("id")).toString(), record.value(QStringLiteral("entityA")).toString(),
                            record.value(QStringLiteral("startCard")).toString(),
                            record.value(QStringLiteral("relType")).toString(),
                            record.value(QStringLiteral("endCard")).toString(),
                            record.value(QStringLiteral("entityB")).toString(),
                            record.value(QStringLiteral("roleA")).toString());
  }
  return rows;
}

const char* cardinalityName(er::ErCardinality cardinality) {
  switch (cardinality) {
  case er::ErCardinality::ExactlyOne:
    return "ONLY_ONE";
  case er::ErCardinality::ZeroOrOne:
    return "ZERO_OR_ONE";
  case er::ErCardinality::OneOrMore:
    return "ONE_OR_MORE";
  case er::ErCardinality::ZeroOrMore:
    return "ZERO_OR_MORE";
  case er::ErCardinality::MdParent:
    return "MD_PARENT";
  }
  return "?";
}

// The edge ids are the ones the layout hands dagre.
QStringList relationshipRows(const QVector<er::ErRelationship>& relationships,
                             const QVector<er::ErLayoutRelationshipInput>& layout) {
  QStringList rows;
  for (qsizetype index = 0; index < relationships.size(); ++index) {
    const er::ErRelationship& relationship = relationships.at(index);
    rows << relationshipRow(
        layout.value(index).id, relationship.entityA, QLatin1String(cardinalityName(relationship.cardA)),
        relationship.identifying ? QStringLiteral("IDENTIFYING") : QStringLiteral("NON_IDENTIFYING"),
        QLatin1String(cardinalityName(relationship.cardB)), relationship.entityB, relationship.label);
  }
  return rows;
}

// The classDef table, keyed by class id. Mermaid's Map keeps insertion order;
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

void compareRows(QStringList native, QStringList upstream, const QString& what, const QString& id) {
  const qsizetype count = qMax(native.size(), upstream.size());
  for (qsizetype i = 0; i < count; ++i) {
    const QString mine = i < native.size() ? native.at(i) : QStringLiteral("<missing>");
    const QString theirs = i < upstream.size() ? upstream.at(i) : QStringLiteral("<missing>");
    check(mine == theirs,
          QStringLiteral("%1/%2[%3]: native '%4', upstream '%5'").arg(id, what).arg(i).arg(mine, theirs));
  }
}

void compareData(const er::ErDiagramData& data, const QJsonObject& expected, const QString& id) {
  check(data.accTitle == expected.value(QStringLiteral("accTitle")).toString(), id + QStringLiteral("/accTitle"));
  check(data.accDescription == expected.value(QStringLiteral("accDescription")).toString(),
        id + QStringLiteral("/accDescription"));
  check(data.direction == expected.value(QStringLiteral("direction")).toString(),
        QStringLiteral("%1/direction: native '%2', upstream '%3'")
            .arg(id, data.direction, expected.value(QStringLiteral("direction")).toString()));
  compareRows(entityRows(data.entities), entityRows(expected.value(QStringLiteral("entities")).toArray()),
              QStringLiteral("entities"), id);
  compareRows(relationshipRows(data.relationships, er::buildErLayoutInput(data).relationships),
              relationshipRows(expected.value(QStringLiteral("relationships")).toArray()), QStringLiteral("relationships"), id);
  compareRows(classRows(data.classDefs), classRows(expected.value(QStringLiteral("classes")).toArray()),
              QStringLiteral("classes"), id);
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  if (argc != 2) {
    std::fprintf(stderr, "Expected ER grammar fixture\n");
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
      QByteArrayLiteral("0f3efa316554366a6b1252e7e6616df337c64e49f9de9c14d2659f081fc12c92")) {
    std::fprintf(stderr, "ER grammar fixture bytes changed\n");
    return 1;
  }
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  if (root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() != QLatin1String("11.16.0") ||
      root.value(QStringLiteral("fixtureSha256")).toString() != QLatin1String("546a0f56a01597ffeaa0dc02e4f98eb86bdf4fe273aeadf7881a19e28c45c784")) {
    std::fprintf(stderr, "ER grammar fixture provenance changed\n");
    return 1;
  }
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  if (cases.size() != 317) {
    std::fprintf(stderr, "ER grammar case count drifted: %lld\n", static_cast<long long>(cases.size()));
    return 1;
  }

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool upstreamAccepts = fixture.value(QStringLiteral("accept")).toBool();
    const QJsonObject reject = fixture.value(QStringLiteral("reject")).toObject();
    const QString upstreamClass = reject.value(QStringLiteral("class")).toString();

    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(source, pre.config) == QLatin1String("er");
    } catch (const UnknownDiagramError&) {
      detected = false;
    }
    check(detected == (upstreamClass != QLatin1String("no-diagram")),
          QStringLiteral("%1/detector: native %2").arg(id, detected ? QStringLiteral("er") : QStringLiteral("none")));
    if (!detected)
      continue;

    // Mermaid names the failure by its stage: no diagram, its lexer, or its parser.
    bool accepted = false;
    try {
      const er::ErDiagramData data = er::ErDiagram::parse(pre.code).data();
      accepted = true;
      check(upstreamAccepts, id + QStringLiteral(": native accepted an upstream reject"));
      if (upstreamAccepts)
        compareData(data, fixture.value(QStringLiteral("expectedDb")).toObject(), id);
    } catch (const er::ErParseError& error) {
      const er::ErDiagnostic diagnostic = error.diagnostic();
      const QString nativeClass = diagnostic.stage == er::ErErrorStage::Lexer ? QStringLiteral("lexer")
                                                                              : QStringLiteral("parser");
      if (upstreamAccepts) {
        check(false, QStringLiteral("%1: native rejected an upstream accept: %2 (line %3)")
                         .arg(id, QString::fromUtf8(error.what())).arg(diagnostic.span.line));
      } else {
        check(nativeClass == upstreamClass,
              QStringLiteral("%1/error-class: native %2, upstream %3").arg(id, nativeClass, upstreamClass));
        // Mermaid reports the line of the last token its parser took.
        if (reject.contains(QStringLiteral("messageLine")))
          check(diagnostic.span.line == reject.value(QStringLiteral("messageLine")).toInt(),
                QStringLiteral("%1/error-line: native %2, upstream %3")
                    .arg(id).arg(diagnostic.span.line).arg(reject.value(QStringLiteral("messageLine")).toInt()));
      }
    }

    // A diagram renders only when upstream's renderer draws it.
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
    std::fprintf(stderr, "MermaidErGrammarTest: %lld divergence(s) from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::puts("MermaidErGrammarTest passed: 317 cases");
  return 0;
}
