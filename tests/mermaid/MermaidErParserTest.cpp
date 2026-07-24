#include "mermaid/erdiagram/ErDiagram.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cstdio>
#include <cstdlib>

// erDiagram parser DB fixture test.
//
// Mirrors tests/mermaid/MermaidFlowchartParserTest.cpp (fail()/require() shape,
// argv[1] fixture path) but asserts against er::ErDiagram::parse(source).data()
// instead of toJson() so the test pins the parsed struct fields directly:
// entity/relationship counts, per-entity attribute counts + key/comment detail,
// per-relationship cardinality / label / identifying / roles, and accTitle.
//
// Parser-only: no Qt GUI, so QCoreApplication is enough and QT_QPA_PLATFORM
// does not need to be set to offscreen.

using muffin::mermaid::er::ErAttribute;
using muffin::mermaid::er::ErAttributeKeyType;
using muffin::mermaid::er::ErCardinality;
using muffin::mermaid::er::ErDiagram;
using muffin::mermaid::er::ErEntity;
using muffin::mermaid::er::ErParseError;
using muffin::mermaid::er::ErRelationship;

namespace {

[[noreturn]] void fail(const QString& message) {
  // qFatal() -> abort() does not flush buffered stderr on Windows (same trap as
  // MermaidStateSceneTest.cpp), so flush before the abort surfaces as an
  // unprintable 0xc0000409.
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  qFatal("%s", qPrintable(message));
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

ErCardinality cardinalityFromName(const QString& name) {
  if (name == QLatin1String("ExactlyOne")) return ErCardinality::ExactlyOne;
  if (name == QLatin1String("ZeroOrOne")) return ErCardinality::ZeroOrOne;
  if (name == QLatin1String("OneOrMore")) return ErCardinality::OneOrMore;
  if (name == QLatin1String("ZeroOrMore")) return ErCardinality::ZeroOrMore;
  fail(QStringLiteral("Unknown cardinality in fixture: %1").arg(name));
}

ErAttributeKeyType keyTypeFromName(const QString& name) {
  if (name == QLatin1String("None")) return ErAttributeKeyType::None;
  if (name == QLatin1String("PrimaryKey")) return ErAttributeKeyType::PrimaryKey;
  if (name == QLatin1String("ForeignKey")) return ErAttributeKeyType::ForeignKey;
  if (name == QLatin1String("UniqueKey")) return ErAttributeKeyType::UniqueKey;
  fail(QStringLiteral("Unknown keyType in fixture: %1").arg(name));
}

const ErEntity* findEntity(const QVector<ErEntity>& entities, const QString& id, const QString& caseId) {
  for (const ErEntity& entity : entities)
    if (entity.id == id) return &entity;
  fail(QStringLiteral("%1: expected entity '%2' not present").arg(caseId, id));
}

const ErRelationship* findRelationship(const QVector<ErRelationship>& relationships,
                                       const QString& entityA, const QString& entityB,
                                       const QString& caseId) {
  for (const ErRelationship& rel : relationships)
    if (rel.entityA == entityA && rel.entityB == entityB) return &rel;
  fail(QStringLiteral("%1: expected relationship %2--%3 not present").arg(caseId, entityA, entityB));
}

const ErAttribute* findAttribute(const QVector<ErAttribute>& attributes, const QString& name,
                                 const QString& caseId, const QString& entityId) {
  for (const ErAttribute& attr : attributes)
    if (attr.attributeName == name) return &attr;
  fail(QStringLiteral("%1: entity '%2' missing attribute '%3'").arg(caseId, entityId, name));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected erDiagram DB fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Could not open %1").arg(file.fileName()));
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  require(parseError.error == QJsonParseError::NoError && document.isObject(),
          QStringLiteral("Invalid er-db fixture: %1").arg(parseError.errorString()));
  const QJsonArray cases = document.object().value(QStringLiteral("cases")).toArray();
  require(cases.size() >= 4, QStringLiteral("er-db fixture is unexpectedly small"));

  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString caseId = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    require(!caseId.isEmpty(), QStringLiteral("er-db case missing id"));
    require(!source.isEmpty(), QStringLiteral("er-db case %1 missing source").arg(caseId));

    ErDiagram diagram;
    try {
      diagram = ErDiagram::parse(source);
    } catch (const ErParseError& error) {
      fail(QStringLiteral("%1: parse threw ErParseError: %2").arg(caseId, QString::fromUtf8(error.what())));
    } catch (const std::exception& error) {
      fail(QStringLiteral("%1: parse threw: %2").arg(caseId, QString::fromUtf8(error.what())));
    }

    const auto& data = diagram.data();

    // accTitle (optional). When present the parser must capture the rest of the
    // accTitle line verbatim (trimmed).
    if (fixture.contains(QStringLiteral("accTitle"))) {
      const QString expectedAccTitle = fixture.value(QStringLiteral("accTitle")).toString();
      require(data.accTitle == expectedAccTitle,
              QStringLiteral("%1: accTitle '%2' != '%3'").arg(caseId, data.accTitle, expectedAccTitle));
    }

    // Entity surface: count + per-entity id/name/attribute detail.
    const QJsonArray expectedEntities = fixture.value(QStringLiteral("entities")).toArray();
    require(data.entities.size() == expectedEntities.size(),
            QStringLiteral("%1: entity count %2 != %3")
                .arg(caseId).arg(data.entities.size()).arg(expectedEntities.size()));
    for (const QJsonValue& entityValue : expectedEntities) {
      const QJsonObject expectedEntity = entityValue.toObject();
      const QString id = expectedEntity.value(QStringLiteral("id")).toString();
      const ErEntity* entity = findEntity(data.entities, id, caseId);
      const QString expectedName =
          expectedEntity.contains(QStringLiteral("name")) ? expectedEntity.value(QStringLiteral("name")).toString() : id;
      require(entity->name == expectedName,
              QStringLiteral("%1: entity '%2' name '%3' != '%4'").arg(caseId, id, entity->name, expectedName));
      const int expectedAttrCount = expectedEntity.value(QStringLiteral("attributes")).toInt(0);
      require(entity->attributes.size() == expectedAttrCount,
              QStringLiteral("%1: entity '%2' attribute count %3 != %4")
                  .arg(caseId, id).arg(entity->attributes.size()).arg(expectedAttrCount));

      // Per-attribute detail (type/name/keyType/comment), matched by attribute name.
      const QJsonArray attributeDetails = expectedEntity.value(QStringLiteral("attributeDetails")).toArray();
      for (const QJsonValue& detailValue : attributeDetails) {
        const QJsonObject detail = detailValue.toObject();
        const QString name = detail.value(QStringLiteral("name")).toString();
        const ErAttribute* attr = findAttribute(entity->attributes, name, caseId, id);
        if (detail.contains(QStringLiteral("type"))) {
          const QString expectedType = detail.value(QStringLiteral("type")).toString();
          require(attr->attributeType == expectedType,
                  QStringLiteral("%1: entity '%2' attr '%3' type '%4' != '%5'")
                      .arg(caseId, id, name, attr->attributeType, expectedType));
        }
        if (detail.contains(QStringLiteral("keyType"))) {
          const ErAttributeKeyType expectedKey = keyTypeFromName(detail.value(QStringLiteral("keyType")).toString());
          require(attr->keyType == expectedKey,
                  QStringLiteral("%1: entity '%2' attr '%3' keyType mismatch").arg(caseId, id, name));
        }
        if (detail.contains(QStringLiteral("comment"))) {
          const QString expectedComment = detail.value(QStringLiteral("comment")).toString();
          require(attr->comment == expectedComment,
                  QStringLiteral("%1: entity '%2' attr '%3' comment '%4' != '%5'")
                      .arg(caseId, id, name, attr->comment, expectedComment));
        }
      }
    }

    // Relationship surface: count + per-relationship endpoints/cardinality/label/identifying/roles.
    const QJsonArray expectedRelationships = fixture.value(QStringLiteral("relationships")).toArray();
    require(data.relationships.size() == expectedRelationships.size(),
            QStringLiteral("%1: relationship count %2 != %3")
                .arg(caseId).arg(data.relationships.size()).arg(expectedRelationships.size()));
    for (const QJsonValue& relValue : expectedRelationships) {
      const QJsonObject expectedRel = relValue.toObject();
      const QString entityA = expectedRel.value(QStringLiteral("entityA")).toString();
      const QString entityB = expectedRel.value(QStringLiteral("entityB")).toString();
      const ErRelationship* rel = findRelationship(data.relationships, entityA, entityB, caseId);
      if (expectedRel.contains(QStringLiteral("cardA")))
        require(rel->cardA == cardinalityFromName(expectedRel.value(QStringLiteral("cardA")).toString()),
                QStringLiteral("%1: %2--%3 cardA mismatch").arg(caseId, entityA, entityB));
      if (expectedRel.contains(QStringLiteral("cardB")))
        require(rel->cardB == cardinalityFromName(expectedRel.value(QStringLiteral("cardB")).toString()),
                QStringLiteral("%1: %2--%3 cardB mismatch").arg(caseId, entityA, entityB));
      if (expectedRel.contains(QStringLiteral("identifying"))) {
        const bool expectedIdentifying = expectedRel.value(QStringLiteral("identifying")).toBool();
        require(rel->identifying == expectedIdentifying,
                QStringLiteral("%1: %2--%3 identifying %4 != %5")
                    .arg(caseId, entityA, entityB).arg(rel->identifying).arg(expectedIdentifying));
      }
      // label defaults to "" when omitted.
      const QString expectedLabel =
          expectedRel.contains(QStringLiteral("label")) ? expectedRel.value(QStringLiteral("label")).toString() : QString();
      require(rel->label == expectedLabel,
              QStringLiteral("%1: %2--%3 label '%4' != '%5'")
                  .arg(caseId, entityA, entityB, rel->label, expectedLabel));
      if (expectedRel.contains(QStringLiteral("roleA"))) {
        const QString expectedRoleA = expectedRel.value(QStringLiteral("roleA")).toString();
        require(rel->roleA == expectedRoleA,
                QStringLiteral("%1: %2--%3 roleA '%4' != '%5'")
                    .arg(caseId, entityA, entityB, rel->roleA, expectedRoleA));
      }
      if (expectedRel.contains(QStringLiteral("roleB"))) {
        const QString expectedRoleB = expectedRel.value(QStringLiteral("roleB")).toString();
        require(rel->roleB == expectedRoleB,
                QStringLiteral("%1: %2--%3 roleB '%4' != '%5'")
                    .arg(caseId, entityA, entityB, rel->roleB, expectedRoleB));
      }
    }
  }

  return 0;
}
