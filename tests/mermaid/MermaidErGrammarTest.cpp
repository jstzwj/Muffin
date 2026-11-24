// Oracle for the erDiagram parser against real Mermaid 11.16.0.
//
// tests/fixtures/mermaid/er-grammar.json records, for each source, whether
// upstream's parser accepts it, the class and line of its error when it does
// not, and the entities (with aliases, attributes and classes), relationships
// and class definitions it compiles when it does. The native parser must
// answer the same way and hand the renderer the same database.
//
// The port is not there yet: of the 304 cases, 142 still differ. kKnownDivergences
// names every one of them by group; the list may only shrink. An unlisted
// difference fails, and a listed case that no longer differs fails too, so a
// fix must also remove its entry. Nothing else in the suite shows how far the
// parser is from upstream. Set MUFFIN_ORACLE_VERBOSE=1 to print how each
// listed case differs.

#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErDiagram.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
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
const bool verbose = qEnvironmentVariableIntValue("MUFFIN_ORACLE_VERBOSE") == 1;
// Cases whose native answer still differs from upstream. The port's erDiagram
// parser covers about half of upstream's grammar; each group below is a part
// that is missing or reads differently, and a diagram using it renders
// differently or not at all. Updating a group means fixing the parser and
// removing its entries; the test fails on an entry that no longer diverges.
const QSet<QString> kKnownDivergences = {
    // No `a[Alias]` syntax: `[` is not a token, so the line is a lexical error.  (31)
    QStringLiteral("alias-accented"),
    QStringLiteral("alias-after-plain-declaration"),
    QStringLiteral("alias-after-relationship"),
    QStringLiteral("alias-before-plain-declaration"),
    QStringLiteral("alias-cardinality-word-two"),
    QStringLiteral("alias-decimal"),
    QStringLiteral("alias-digits"),
    QStringLiteral("alias-dots"),
    QStringLiteral("alias-hyphen"),
    QStringLiteral("alias-on-dotted-id"),
    QStringLiteral("alias-on-hyphen-id"),
    QStringLiteral("alias-on-quoted-id"),
    QStringLiteral("alias-on-quoted-id-with-space"),
    QStringLiteral("alias-one"),
    QStringLiteral("alias-plain"),
    QStringLiteral("alias-quoted-brackets"),
    QStringLiteral("alias-quoted-spaces"),
    QStringLiteral("alias-space-before-bracket"),
    QStringLiteral("alias-star"),
    QStringLiteral("alias-star-suffix"),
    QStringLiteral("alias-then-quoted"),
    QStringLiteral("alias-then-word-no-space"),
    QStringLiteral("alias-then-word-then-alias"),
    QStringLiteral("alias-underscore"),
    QStringLiteral("alias-unicode"),
    QStringLiteral("alias-used-in-relationship"),
    QStringLiteral("alias-with-block"),
    QStringLiteral("alias-with-block-no-space"),
    QStringLiteral("alias-with-class-and-block"),
    QStringLiteral("alias-with-class-no-block"),
    QStringLiteral("alias-with-two-classes"),
    // Cardinalities written as words or numbers (`one or more`, `many(0)`, `1+`), and an
    // entity named `1` or `1.5` next to one.  (19)
    QStringLiteral("card-decimal-entity-on-left"),
    QStringLiteral("card-entity-one-on-left"),
    QStringLiteral("card-entity-one-on-right"),
    QStringLiteral("card-mixed-glyph-word"),
    QStringLiteral("card-mixed-word-glyph"),
    QStringLiteral("card-word-one"),
    QStringLiteral("card-word-uppercase"),
    QStringLiteral("card-words-many"),
    QStringLiteral("card-words-many-paren-one"),
    QStringLiteral("card-words-many-paren-zero"),
    QStringLiteral("card-words-one-or-many"),
    QStringLiteral("card-words-one-or-more"),
    QStringLiteral("card-words-one-or-zero"),
    QStringLiteral("card-words-one-plus"),
    QStringLiteral("card-words-only-one"),
    QStringLiteral("card-words-zero-or-many"),
    QStringLiteral("card-words-zero-or-more"),
    QStringLiteral("card-words-zero-or-one"),
    QStringLiteral("card-words-zero-plus"),
    // The relationship types `.-`, `-.`, `to` and `optionally to`.  (4)
    QStringLiteral("rel-type-dash-dot"),
    QStringLiteral("rel-type-dot-dash"),
    QStringLiteral("rel-type-optionally-to"),
    QStringLiteral("rel-type-to"),
    // Attribute key lists (`PK, FK`), lower-case keys, four-word types, a key in the
    // type position, and names with dots or commas.  (14)
    QStringLiteral("attr-four-word-type"),
    QStringLiteral("attr-key-alone"),
    QStringLiteral("attr-key-as-type"),
    QStringLiteral("attr-key-duplicate"),
    QStringLiteral("attr-key-list"),
    QStringLiteral("attr-key-list-spaces"),
    QStringLiteral("attr-key-list-three"),
    QStringLiteral("attr-key-lowercase"),
    QStringLiteral("attr-key-mixed-case"),
    QStringLiteral("attr-key-order-reversed"),
    QStringLiteral("attr-name-with-comma"),
    QStringLiteral("attr-name-with-dots"),
    QStringLiteral("attr-numeric-type"),
    QStringLiteral("attr-unquoted-words-after-key"),
    // Entity names with digits, dots, a leading hyphen or backticks, quoted names, and
    // `ENTITY "Display Name"`: upstream reads two entities where the port makes an alias.  (15)
    QStringLiteral("entity-name-backticks"),
    QStringLiteral("entity-name-decimal"),
    QStringLiteral("entity-name-digits"),
    QStringLiteral("entity-name-dots"),
    QStringLiteral("entity-name-hyphen-leading"),
    QStringLiteral("entity-name-leading-digit-word"),
    QStringLiteral("entity-name-one"),
    QStringLiteral("entity-name-quoted"),
    QStringLiteral("entity-name-quoted-with-block"),
    QStringLiteral("entity-name-star"),
    QStringLiteral("entity-name-star-prefix"),
    QStringLiteral("entity-quoted-after-name"),
    QStringLiteral("entity-quoted-after-name-with-block"),
    QStringLiteral("entity-then-relationship-on-one-line"),
    QStringLiteral("entity-two-on-one-line"),
    // `classDef`, `class` and `:::` class lists.  (20)
    QStringLiteral("class-def"),
    QStringLiteral("class-def-bracket-name"),
    QStringLiteral("class-def-color-text-style"),
    QStringLiteral("class-def-empty-styles"),
    QStringLiteral("class-def-quoted-name"),
    QStringLiteral("class-def-redefined-appends"),
    QStringLiteral("class-def-semicolon"),
    QStringLiteral("class-def-space-after-colon"),
    QStringLiteral("class-def-two-targets"),
    QStringLiteral("class-inline"),
    QStringLiteral("class-inline-on-undefined-entity"),
    QStringLiteral("class-inline-two"),
    QStringLiteral("class-inline-with-block"),
    QStringLiteral("class-statement"),
    QStringLiteral("class-statement-before-entity"),
    QStringLiteral("class-statement-duplicate"),
    QStringLiteral("class-statement-no-entity"),
    QStringLiteral("class-statement-two-classes"),
    QStringLiteral("class-statement-two-entities"),
    QStringLiteral("class-statement-undefined-entity"),
    // `style` statements.  (6)
    QStringLiteral("style-and-class"),
    QStringLiteral("style-statement-before-entity"),
    QStringLiteral("style-statement-no-entity"),
    QStringLiteral("style-statement-no-styles"),
    QStringLiteral("style-statement-semicolon"),
    QStringLiteral("style-statement-two-entities"),
    // The `direction` statement: upstream stores it, the port rejects the line.  (8)
    QStringLiteral("direction-bt"),
    QStringLiteral("direction-embedded-in-text"),
    QStringLiteral("direction-lowercase-ignored"),
    QStringLiteral("direction-lr"),
    QStringLiteral("direction-lr-on-line-with-entity"),
    QStringLiteral("direction-rl"),
    QStringLiteral("direction-tb"),
    QStringLiteral("direction-unknown-is-entities"),
    // Upper-case `accTitle`, a one-line `accDescr { }` block, and an empty accTitle
    // swallowing the next line.  (3)
    QStringLiteral("acc-descr-block-one-line"),
    QStringLiteral("acc-title-empty-swallows-next-line"),
    QStringLiteral("acc-title-uppercase-keyword"),
    // A relationship on the same line as a closing brace; the error line of a nested brace.  (2)
    QStringLiteral("block-close-then-relationship"),
    QStringLiteral("block-nested-open"),
    // A `%%` comment after a relationship: upstream rejects it.  (1)
    QStringLiteral("comment-trailing-after-relationship"),
    // A `title` line: upstream reads the words as entities.  (2)
    QStringLiteral("title-line-is-entities"),
    QStringLiteral("title-with-colon"),
    // Several statements on one line: upstream reads bare words as entities.  (1)
    QStringLiteral("unknown-words-are-entities"),
    // Labels: the port accepts an unquoted multi-word label and no label at all, keeps
    // the quotes of a quoted label, and reads a trailing word as part of the label.  (10)
    QStringLiteral("rel-label-backtick"),
    QStringLiteral("rel-label-empty"),
    QStringLiteral("rel-label-multiword-unquoted"),
    QStringLiteral("rel-label-quoted-hash"),
    QStringLiteral("rel-label-quoted-spaces"),
    QStringLiteral("rel-label-then-brace"),
    QStringLiteral("rel-label-then-class"),
    QStringLiteral("rel-label-three-words"),
    QStringLiteral("rel-unlabeled"),
    QStringLiteral("rel-label-word-then-quoted"),
    // Class lists on a relationship.  (4)
    QStringLiteral("rel-class-both-sides"),
    QStringLiteral("rel-class-left"),
    QStringLiteral("rel-class-right"),
    QStringLiteral("rel-class-undefined"),
    // A trailing word after the label, and a second relationship on the same line.  (2)
    QStringLiteral("rel-trailing-word-is-entity"),
    QStringLiteral("rel-two-on-one-line"),
};

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
    rows << QStringLiteral("%1|%2|%3|%4|%5")
                .arg(record.value(QStringLiteral("name")).toString(), record.value(QStringLiteral("alias")).toString(),
                     attributes.join(QLatin1Char(';')), record.value(QStringLiteral("cssClasses")).toString(),
                     joined(record.value(QStringLiteral("cssStyles")).toArray()));
  }
  return rows;
}

QStringList entityRows(const QVector<er::ErEntity>& entities) {
  QStringList rows;
  for (const er::ErEntity& entity : entities) {
    QStringList attributes;
    for (const er::ErAttribute& attribute : entity.attributes) {
      QString key;
      switch (attribute.keyType) {
      case er::ErAttributeKeyType::PrimaryKey:
        key = QStringLiteral("PK");
        break;
      case er::ErAttributeKeyType::ForeignKey:
        key = QStringLiteral("FK");
        break;
      case er::ErAttributeKeyType::UniqueKey:
        key = QStringLiteral("UK");
        break;
      case er::ErAttributeKeyType::None:
        break;
      }
      attributes << QStringLiteral("%1 %2|%3|%4")
                       .arg(attribute.attributeType, attribute.attributeName, key, attribute.comment);
    }
    // The port leaves an unclassed entity's class list empty where upstream
    // writes "default"; both mean "no class".
    rows << QStringLiteral("%1|%2|%3|%4|%5")
                .arg(entity.id, entity.name == entity.id ? QString() : entity.name, attributes.join(QLatin1Char(';')),
                     entity.cssClasses.isEmpty() ? QStringLiteral("default") : entity.cssClasses,
                     entity.styles.join(QLatin1Char(',')));
  }
  return rows;
}

QStringList relationshipRows(const QJsonArray& array) {
  QStringList rows;
  for (const QJsonValue& value : array) {
    const QJsonObject record = value.toObject();
    rows << QStringLiteral("%1|%2|%3|%4|%5")
                .arg(record.value(QStringLiteral("entityA")).toString(), record.value(QStringLiteral("startCard")).toString(),
                     record.value(QStringLiteral("relType")).toString(), record.value(QStringLiteral("endCard")).toString(),
                     record.value(QStringLiteral("entityB")).toString() + QLatin1Char('|') +
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
  }
  return "?";
}

QStringList relationshipRows(const QVector<er::ErRelationship>& relationships) {
  QStringList rows;
  for (const er::ErRelationship& relationship : relationships)
    rows << QStringLiteral("%1|%2|%3|%4|%5")
                .arg(relationship.entityA, QLatin1String(cardinalityName(relationship.cardA)),
                     relationship.identifying ? QStringLiteral("IDENTIFYING") : QStringLiteral("NON_IDENTIFYING"),
                     QLatin1String(cardinalityName(relationship.cardB)),
                     relationship.entityB + QLatin1Char('|') + relationship.label +
                         (relationship.roleA.isEmpty() && relationship.roleB.isEmpty() && relationship.comment.isEmpty()
                              ? QString()
                              : QStringLiteral(" (native roles '%1' '%2', comment '%3')")
                                    .arg(relationship.roleA, relationship.roleB, relationship.comment)));
  return rows;
}

// The classDef table, keyed by class id. Upstream's Map keeps insertion order;
// the port's hash does not, and nothing it renders depends on the order.
QStringList classRows(const QJsonArray& array, bool withTextStyles) {
  QStringList rows;
  for (const QJsonValue& value : array) {
    const QJsonObject record = value.toObject();
    rows << record.value(QStringLiteral("id")).toString() + QLatin1Char('|') +
          joined(record.value(QStringLiteral("styles")).toArray()) +
          (withTextStyles ? QStringLiteral("|") + joined(record.value(QStringLiteral("textStyles")).toArray())
                          : QString());
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

void compareRows(QStringList native, QStringList upstream, const QString& what, const QString& id, QStringList& out) {
  // Class definitions are a table: order carries no meaning.
  if (what == QLatin1String("classes")) {
    native.sort();
    upstream.sort();
  }
  const qsizetype count = qMax(native.size(), upstream.size());
  for (qsizetype i = 0; i < count; ++i) {
    const QString mine = i < native.size() ? native.at(i) : QStringLiteral("<missing>");
    const QString theirs = i < upstream.size() ? upstream.at(i) : QStringLiteral("<missing>");
    if (mine != theirs)
      out << QStringLiteral("%1/%2[%3]: native '%4', upstream '%5'").arg(id, what).arg(i).arg(mine, theirs);
  }
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
      QByteArrayLiteral("3ef35d8df14d94005ed15b77fee9af99110626ff185a72c626441daa9f09e0ed")) {
    std::fprintf(stderr, "ER grammar fixture bytes changed\n");
    return 1;
  }
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  if (root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() != QLatin1String("11.16.0") ||
      root.value(QStringLiteral("fixtureSha256")).toString() != QLatin1String("f6a2d783c1f5f8cd0f7737c5f63e88ebd8a9d6fd8da234c9257342d38734a135")) {
    std::fprintf(stderr, "ER grammar fixture provenance changed\n");
    return 1;
  }
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  if (cases.size() != 304) {
    std::fprintf(stderr, "ER grammar case count drifted: %lld\n", static_cast<long long>(cases.size()));
    return 1;
  }

  QSet<QString> stillDiverging;
  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool upstreamAccepts = fixture.value(QStringLiteral("accept")).toBool();
    const QJsonObject reject = fixture.value(QStringLiteral("reject")).toObject();
    QStringList divergences;

    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(source, pre.config) == QLatin1String("er");
    } catch (const UnknownDiagramError&) {
      detected = false;
    }
    if (detected == (reject.value(QStringLiteral("class")).toString() == QLatin1String("no-diagram")))
      divergences << QStringLiteral("detector: native %1")
                        .arg(detected ? QStringLiteral("er") : QStringLiteral("none"));
    if (detected) {
      bool accepted = false;
      er::ErDiagramData data;
      try {
        data = er::ErDiagram::parse(pre.code).data();
        accepted = true;
      } catch (const er::ErParseError& error) {
        const int nativeLine = error.diagnostic().span.line;
        if (upstreamAccepts) {
          divergences << QStringLiteral("native rejected an upstream accept: %1 (line %2)")
                             .arg(QString::fromUtf8(error.what())).arg(nativeLine);
        } else if (reject.contains(QStringLiteral("messageLine")) && nativeLine != reject.value(QStringLiteral("messageLine")).toInt()) {
          divergences << QStringLiteral("error-line: native %1, upstream %2")
                             .arg(nativeLine)
                             .arg(reject.value(QStringLiteral("messageLine")).toInt());
        }
      }
      if (accepted && !upstreamAccepts) {
        divergences << QStringLiteral("native accepted an upstream reject (%1)")
                          .arg(reject.value(QStringLiteral("message")).toString().section(QLatin1Char('\n'), 0, 0));
      } else if (accepted) {
        const QJsonObject expected = fixture.value(QStringLiteral("expectedDb")).toObject();
        if (data.accTitle != expected.value(QStringLiteral("accTitle")).toString())
          divergences << QStringLiteral("accTitle: native '%1', upstream '%2'")
                             .arg(data.accTitle, expected.value(QStringLiteral("accTitle")).toString());
        if (data.accDescription != expected.value(QStringLiteral("accDescription")).toString())
          divergences << QStringLiteral("accDescription: native '%1', upstream '%2'")
                             .arg(data.accDescription, expected.value(QStringLiteral("accDescription")).toString());
        compareRows(entityRows(data.entities), entityRows(expected.value(QStringLiteral("entities")).toArray()),
                    QStringLiteral("entities"), id, divergences);
        compareRows(relationshipRows(data.relationships),
                    relationshipRows(expected.value(QStringLiteral("relationships")).toArray()), QStringLiteral("relationships"),
                    id, divergences);
        compareRows(classRows(data.classDefs), classRows(expected.value(QStringLiteral("classes")).toArray(), false),
                    QStringLiteral("classes"), id, divergences);
      }
    }

    // A diagram renders only when upstream's renderer draws it.
    const bool upstreamRenders = upstreamAccepts && fixture.value(QStringLiteral("renderAccept")).toBool();
    const editor::MermaidRenderEntry entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    const bool nativeRenders = entry.status == editor::MermaidRenderStatus::Ready;
    if (nativeRenders != upstreamRenders)
      divergences << QStringLiteral("render: native %1, upstream %2%3")
                        .arg(nativeRenders ? QStringLiteral("Ready") : QStringLiteral("not Ready"),
                             upstreamRenders ? QStringLiteral("Ready") : QStringLiteral("not Ready"),
                             nativeRenders ? QString() : QStringLiteral(" (") + entry.errorMessage + QStringLiteral(")"));

    if (divergences.isEmpty()) {
      if (kKnownDivergences.contains(id))
        failures << QStringLiteral("%1 no longer diverges; remove it from kKnownDivergences").arg(id);
      continue;
    }
    if (!kKnownDivergences.contains(id)) {
      for (const QString& divergence : divergences)
        failures << QStringLiteral("%1: %2").arg(id, divergence);
    } else {
      stillDiverging << id;
      // Set MUFFIN_ORACLE_VERBOSE=1 to see how each known case differs.
      if (verbose)
        for (const QString& divergence : divergences)
          std::fprintf(stderr, "[known] %s: %s\n", qPrintable(id), qPrintable(divergence));
    }
  }
  for (const QString& id : kKnownDivergences)
    if (!stillDiverging.contains(id))
      failures << QStringLiteral("%1 is listed in kKnownDivergences but is not in the fixture").arg(id);

  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidErGrammarTest: %lld divergence(s) from Mermaid 11.16.0 beyond the %lld known\n",
                 static_cast<long long>(failures.size()), static_cast<long long>(kKnownDivergences.size()));
    return 1;
  }
  std::printf("MermaidErGrammarTest passed: %lld cases agree or are listed, %lld of them still differ from Mermaid "
              "11.16.0\n",
              static_cast<long long>(cases.size()), static_cast<long long>(kKnownDivergences.size()));
  return 0;
}
