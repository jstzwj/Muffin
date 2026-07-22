#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cstdlib>

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
QJsonObject load(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot open %1").arg(path));
  return QJsonDocument::fromJson(file.readAll()).object();
}
QSet<QString> uniqueIds(const QJsonArray& cases, const QString& fixture) {
  QSet<QString> ids;
  for (const QJsonValue& value : cases) {
    const QString id = value.toObject().value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !ids.contains(id),
            QStringLiteral("%1 duplicate/empty case id: %2").arg(fixture, id));
    ids.insert(id);
  }
  return ids;
}
void collectPngReferences(const QJsonValue& value, QSet<QString>* files) {
  if (value.isString()) {
    const QString name = value.toString();
    if (name.endsWith(QLatin1String(".png"))) files->insert(name);
  } else if (value.isArray()) {
    for (const QJsonValue& child : value.toArray()) collectPngReferences(child, files);
  } else if (value.isObject()) {
    for (const QJsonValue& child : value.toObject()) collectPngReferences(child, files);
  }
}
QString sha256(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot hash %1").arg(path));
  return QString::fromLatin1(
      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, QStringLiteral("Expected class DB fixture path"));
  const QFileInfo dbInfo(QString::fromLocal8Bit(argv[1]));
  const QString dir = dbInfo.absolutePath();
  const QString pixelDir = dir + QStringLiteral("/class-pixel");
  const QJsonObject db = load(dbInfo.absoluteFilePath());
  const QJsonObject fuzz = load(dir + QStringLiteral("/class-differential-fuzz.json"));
  const QJsonObject layout = load(dir + QStringLiteral("/class-layout.json"));
  const QJsonObject pixel = load(pixelDir + QStringLiteral("/manifest.json"));

  require(db.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() == QLatin1String("11.16.0") &&
              fuzz.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() == QLatin1String("11.16.0") &&
              layout.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0") &&
              pixel.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Class fixture Mermaid versions drifted"));

  const QJsonArray dbCases = db.value(QStringLiteral("cases")).toArray();
  const QJsonArray coverageCases = db.value(QStringLiteral("coverageOnly")).toArray();
  const QJsonArray invalidCases = db.value(QStringLiteral("invalidCases")).toArray();
  const QJsonArray negativeCases = fuzz.value(QStringLiteral("negativeCases")).toArray();
  const QJsonArray layoutCases = layout.value(QStringLiteral("cases")).toArray();
  const QJsonArray pixelCases = pixel.value(QStringLiteral("cases")).toArray();
  uniqueIds(dbCases, QStringLiteral("class-db"));
  uniqueIds(coverageCases, QStringLiteral("class-db coverageOnly"));
  uniqueIds(invalidCases, QStringLiteral("class-db invalidCases"));
  uniqueIds(negativeCases, QStringLiteral("class-differential-fuzz"));
  const QSet<QString> layoutIds = uniqueIds(layoutCases, QStringLiteral("class-layout"));
  const QSet<QString> pixelIds = uniqueIds(pixelCases, QStringLiteral("class-pixel"));

  QSet<int> reducedProductions;
  for (const QJsonArray cases : {dbCases, coverageCases})
    for (const QJsonValue& value : cases)
      for (const QJsonValue& reduction :
           value.toObject().value(QStringLiteral("reductions")).toArray())
        reducedProductions.insert(reduction.toInt());
  QSet<int> coveredProductions;
  int unreachableProductions = 0;
  const QJsonArray productionTable = db.value(QStringLiteral("productions")).toArray();
  for (const QJsonValue& value : productionTable) {
    const QJsonObject production = value.toObject();
    const QString status = production.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("covered")) {
      coveredProductions.insert(production.value(QStringLiteral("id")).toInt());
      require(!production.value(QStringLiteral("native")).toString().isEmpty() &&
                  !production.value(QStringLiteral("fixtures")).toArray().isEmpty(),
              QStringLiteral("Covered class production lacks native mapping or fixture"));
    } else if (status == QLatin1String("unreachable")) {
      ++unreachableProductions;
    } else {
      fail(QStringLiteral("Unknown class production status: %1").arg(status));
    }
  }
  require(productionTable.size() == 138 && coveredProductions.size() == 116 &&
              unreachableProductions == 22 && reducedProductions == coveredProductions,
          QStringLiteral("Class grammar production coverage regressed"));

  QSet<QString> operators, codes, stages;
  for (const QJsonValue& value : negativeCases) {
    const QJsonObject item = value.toObject();
    operators.insert(item.value(QStringLiteral("operator")).toString());
    codes.insert(item.value(QStringLiteral("expectedNativeCode")).toString());
    stages.insert(item.value(QStringLiteral("upstreamError")).toObject()
                      .value(QStringLiteral("stage")).toString());
    require(item.value(QStringLiteral("targetProduction")).toInt() > 0 &&
                item.value(QStringLiteral("diagnosticPosition")).toObject()
                    .contains(QStringLiteral("column")),
            QStringLiteral("Class mutation lacks production or diagnostic position"));
  }
  require(negativeCases.size() >= 10 && operators.size() == 6 && codes.size() >= 5 &&
              stages == QSet<QString>{QStringLiteral("detector"), QStringLiteral("lexer"),
                                      QStringLiteral("parser")},
          QStringLiteral("Class diagnostic mutation coverage regressed"));

  require(layoutCases.size() >= 20, QStringLiteral("Class layout corpus regressed"));
  for (const QString& id : {QStringLiteral("class-compartments"),
                            QStringLiteral("relation-marker-label-matrix"),
                            QStringLiteral("lollipop-and-note-edge"),
                            QStringLiteral("generic-member-compartments"),
                            QStringLiteral("note-label-matrix"),
                            QStringLiteral("nested-namespace-label-matrix"),
                            QStringLiteral("inert-spacing-rl"),
                            QStringLiteral("inert-spacing-bt"),
                            QStringLiteral("svg-label-compartments"),
                            QStringLiteral("svg-multiline-cjk-rtl"),
                            QStringLiteral("svg-classifier-styles"),
                            QStringLiteral("compound-self-parallel-relations")})
    require(layoutIds.contains(id), QStringLiteral("Class layout axis missing: %1").arg(id));
  for (const QJsonValue& value : layoutCases) {
    const QJsonObject item = value.toObject();
    if (!item.value(QStringLiteral("id")).toString().startsWith(
            QLatin1String("inert-spacing-")))
      continue;
    const QJsonObject expected = item.value(QStringLiteral("expected")).toObject();
    require(item.value(QStringLiteral("nodeSpacing")).toDouble() != 50.0 &&
                item.value(QStringLiteral("rankSpacing")).toDouble() != 50.0 &&
                expected.value(QStringLiteral("nodeSpacing")).toDouble() == 50.0 &&
                expected.value(QStringLiteral("rankSpacing")).toDouble() == 50.0,
            QStringLiteral("Class spacing inertness oracle drifted"));
  }

  QSet<QString> themes, dprs, cropTargets, cropKinds;
  int cropCases = 0, sceneCases = 0, mathCases = 0, bidiCases = 0, cjkCases = 0;
  int markerDefinitions = 0, labelContainers = 0, domEntries = 0, ariaCases = 0;
  QSet<QString> referencedPngs;
  collectPngReferences(pixel, &referencedPngs);
  for (const QJsonValue& value : pixelCases) {
    const QJsonObject item = value.toObject();
    themes.insert(item.value(QStringLiteral("theme")).toString(QStringLiteral("default")));
    dprs.insert(QString::number(item.value(QStringLiteral("dpr")).toDouble(1.0), 'g', 3));
    const QString source = item.value(QStringLiteral("source")).toString();
    mathCases += source.contains(QStringLiteral("$$"));
    bidiCases += source.contains(QChar(0x0645)) || source.contains(QChar(0x05e9));
    cjkCases += std::any_of(source.cbegin(), source.cend(), [](QChar ch) {
      return ch.unicode() >= 0x3400 && ch.unicode() <= 0x9fff;
    });
    if (item.value(QStringLiteral("cropOnly")).toBool()) {
      ++cropCases;
      cropTargets.insert(item.value(QStringLiteral("cropTarget")).toString());
      cropKinds.insert(item.value(QStringLiteral("cropKind")).toString());
    } else {
      ++sceneCases;
    }
    const QJsonObject svg = item.value(QStringLiteral("svgStructure")).toObject();
    markerDefinitions += svg.value(QStringLiteral("markers")).toArray().size();
    labelContainers += svg.value(QStringLiteral("labelContainers")).toArray().size();
    domEntries += svg.value(QStringLiteral("domOrder")).toArray().size();
    ariaCases += !svg.value(QStringLiteral("ariaTitle")).toString().isEmpty();

    const QString file = item.value(QStringLiteral("file")).toString();
    const QString cropFile = item.value(QStringLiteral("cropFile")).toString();
    const QString image = file.isEmpty() ? cropFile : file;
    const QString expectedHash = file.isEmpty()
        ? item.value(QStringLiteral("cropSha256")).toString()
        : item.value(QStringLiteral("sha256")).toString();
    require(!image.isEmpty() && sha256(pixelDir + QLatin1Char('/') + image) == expectedHash,
            QStringLiteral("Class PNG hash drifted: %1").arg(image));
  }
  const QStringList diskPngList = QDir(pixelDir).entryList(
      {QStringLiteral("*.png")}, QDir::Files, QDir::Name);
  const QSet<QString> diskPngs(diskPngList.cbegin(), diskPngList.cend());
  require(referencedPngs == diskPngs && diskPngs.size() == 19,
          QStringLiteral("Class PNG fixture references/orphans regressed"));
  require(pixelCases.size() >= 17 && cropCases >= 8 && sceneCases >= 9 &&
              themes == QSet<QString>{QStringLiteral("default"), QStringLiteral("dark")} &&
              dprs == QSet<QString>{QStringLiteral("1"), QStringLiteral("1.25"),
                                    QStringLiteral("1.5"), QStringLiteral("2")} &&
              cropTargets == QSet<QString>{QStringLiteral("node"), QStringLiteral("edge"),
                                           QStringLiteral("cluster")} &&
              cropKinds.size() == 8 && mathCases >= 2 && bidiCases >= 5 && cjkCases >= 5 &&
              markerDefinitions >= 323 && labelContainers >= 65 && domEntries >= 900 &&
              ariaCases >= 1,
          QStringLiteral("Class theme/DPR/label/SVG coverage regressed: themes=%1 dprs=%2 "
                         "crops=%3 scenes=%4 targets=%5 kinds=%6 math=%7 bidi=%8 cjk=%9 "
                         "markers=%10 labels=%11 dom=%12 aria=%13")
              .arg(QStringList(themes.values()).join(QLatin1Char(',')))
              .arg(QStringList(dprs.values()).join(QLatin1Char(',')))
              .arg(cropCases).arg(sceneCases).arg(cropTargets.size()).arg(cropKinds.size())
              .arg(mathCases).arg(bidiCases).arg(cjkCases).arg(markerDefinitions)
              .arg(labelContainers).arg(domEntries).arg(ariaCases));
  for (const QString& id : {QStringLiteral("compartments"), QStringLiteral("marker-matrix"),
                            QStringLiteral("note"), QStringLiteral("nested-namespaces"),
                            QStringLiteral("styled-members"), QStringLiteral("cjk-rtl"),
                            QStringLiteral("dark-compound"), QStringLiteral("dark-note-markers")})
    require(pixelIds.contains(id), QStringLiteral("Class pixel axis missing: %1").arg(id));

  qDebug() << "MermaidClassCoverageMatrixTest:" << coveredProductions.size()
           << "productions," << codes.size() << "diagnostics," << layoutCases.size()
           << "layouts and" << pixelCases.size() << "pixel/SVG cases passed";
  return 0;
}
