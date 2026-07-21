#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdlib>

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition)
    fail(message);
}

QJsonObject load(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot open %1").arg(path));
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError && document.isObject(),
          QStringLiteral("Invalid JSON in %1: %2").arg(path, error.errorString()));
  return document.object();
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

void requireVersion(const QJsonObject& fixture, const QString& name) {
  const QString upstreamVersion = fixture.value(QStringLiteral("upstream")).toObject()
                                      .value(QStringLiteral("version")).toString();
  const QString version = upstreamVersion.isEmpty()
                              ? fixture.value(QStringLiteral("mermaidVersion")).toString()
                              : upstreamVersion;
  require(version == QLatin1String("11.16.0"),
          QStringLiteral("%1 Mermaid version drifted from 11.16.0").arg(name));
}

QSet<int> integerSet(const QJsonArray& values) {
  QSet<int> result;
  for (const QJsonValue& value : values)
    result.insert(value.toInt());
  return result;
}

QByteArray sha256(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot hash %1").arg(path));
  QCryptographicHash hash(QCryptographicHash::Sha256);
  require(hash.addData(&file), QStringLiteral("Cannot read %1 while hashing").arg(path));
  return hash.result().toHex();
}

QByteArray combinedFileDigest(const QString& directory, const QSet<QString>& files) {
  QStringList ordered(files.begin(), files.end());
  ordered.sort();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const QByteArray separator(1, '\0');
  for (const QString& fileName : ordered) {
    QFile file(directory + QLatin1Char('/') + fileName);
    require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot digest %1").arg(fileName));
    hash.addData(fileName.toUtf8());
    hash.addData(separator);
    require(hash.addData(&file), QStringLiteral("Cannot read %1 while digesting").arg(fileName));
  }
  return hash.result().toHex();
}

void requireCaseIds(const QSet<QString>& ids, const QStringList& required,
                    const QString& axis) {
  for (const QString& id : required)
    require(ids.contains(id), QStringLiteral("Flowchart %1 axis missing: %2").arg(axis, id));
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, QStringLiteral("Expected flowchart DB fixture path"));
  const QFileInfo dbInfo(QString::fromLocal8Bit(argv[1]));
  const QString dir = dbInfo.absolutePath();
  const QJsonObject db = load(dbInfo.absoluteFilePath());
  const QJsonObject fuzz = load(dir + QStringLiteral("/flowchart-differential-fuzz.json"));
  const QJsonObject errors = load(dir + QStringLiteral("/flowchart-errors.json"));
  const QJsonObject geometry = load(dir + QStringLiteral("/flowchart-geometry.json"));
  const QJsonObject labels = load(dir + QStringLiteral("/flowchart-label.json"));
  const QJsonObject rough = load(dir + QStringLiteral("/rough-ops.json"));
  const QString pixelDir = dir + QStringLiteral("/golden-pixel");
  const QJsonObject pixels = load(pixelDir + QStringLiteral("/manifest.json"));

  for (const auto& fixture : {qMakePair(&db, QStringLiteral("flowchart-db")),
                              qMakePair(&fuzz, QStringLiteral("flowchart-differential-fuzz")),
                              qMakePair(&errors, QStringLiteral("flowchart-errors")),
                              qMakePair(&geometry, QStringLiteral("flowchart-geometry")),
                              qMakePair(&labels, QStringLiteral("flowchart-label")),
                              qMakePair(&pixels, QStringLiteral("golden-pixel"))})
    requireVersion(*fixture.first, fixture.second);
  require(rough.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() == QLatin1String("4.6.6"),
          QStringLiteral("RoughJS fixture version drifted from 4.6.6"));

  const QJsonArray dbCases = db.value(QStringLiteral("cases")).toArray();
  const QJsonArray productions = db.value(QStringLiteral("productions")).toArray();
  const QJsonArray positiveCases = fuzz.value(QStringLiteral("cases")).toArray();
  const QJsonArray negativeCases = fuzz.value(QStringLiteral("negativeCases")).toArray();
  const QJsonArray errorCases = errors.value(QStringLiteral("cases")).toArray();
  const QJsonArray geometryCases = geometry.value(QStringLiteral("cases")).toArray();
  const QJsonArray labelCases = labels.value(QStringLiteral("cases")).toArray();
  const QJsonArray roughCases = rough.value(QStringLiteral("cases")).toArray();
  const QJsonArray pixelCases = pixels.value(QStringLiteral("cases")).toArray();
  const QSet<QString> dbIds = uniqueIds(dbCases, QStringLiteral("flowchart-db"));
  uniqueIds(positiveCases, QStringLiteral("flowchart-differential-fuzz positive"));
  uniqueIds(negativeCases, QStringLiteral("flowchart-differential-fuzz negative"));
  uniqueIds(errorCases, QStringLiteral("flowchart-errors"));
  const QSet<QString> geometryIds = uniqueIds(geometryCases, QStringLiteral("flowchart-geometry"));
  const QSet<QString> labelIds = uniqueIds(labelCases, QStringLiteral("flowchart-label"));
  uniqueIds(roughCases, QStringLiteral("rough-ops"));
  const QSet<QString> pixelIds = uniqueIds(pixelCases, QStringLiteral("golden-pixel"));

  QSet<int> coveredProductions;
  for (const QJsonValue& value : dbCases)
    coveredProductions.unite(integerSet(value.toObject().value(QStringLiteral("productions")).toArray()));
  QSet<int> declaredProductions;
  int covered = 0;
  int unreachable = 0;
  int upstreamError = 0;
  for (const QJsonValue& value : productions) {
    const QJsonObject production = value.toObject();
    const int id = production.value(QStringLiteral("id")).toInt();
    require(id >= 1 && id <= 189 && !declaredProductions.contains(id),
            QStringLiteral("Invalid/duplicate flowchart production %1").arg(id));
    declaredProductions.insert(id);
    const QString status = production.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("covered")) {
      ++covered;
      require(coveredProductions.contains(id),
              QStringLiteral("Covered production %1 has no DB case").arg(id));
      require(!production.value(QStringLiteral("native")).toString().isEmpty(),
              QStringLiteral("Covered production %1 has no native parser mapping").arg(id));
      for (const QJsonValue& fixture : production.value(QStringLiteral("fixtures")).toArray())
        require(dbIds.contains(fixture.toString()),
                QStringLiteral("Production %1 references unknown fixture %2")
                    .arg(id).arg(fixture.toString()));
    } else {
      require(!production.value(QStringLiteral("reason")).toString().isEmpty(),
              QStringLiteral("Non-covered production %1 lacks a reason").arg(id));
      unreachable += status == QLatin1String("unreachable");
      upstreamError += status == QLatin1String("upstream-error");
      require(status == QLatin1String("unreachable") || status == QLatin1String("upstream-error"),
              QStringLiteral("Unknown production status for %1: %2").arg(id).arg(status));
    }
  }
  require(productions.size() == 189 && declaredProductions.size() == 189 && covered == 161 &&
              unreachable == 27 && upstreamError == 1 && coveredProductions.size() == 161,
          QStringLiteral("Flowchart 189-production coverage matrix regressed"));

  QSet<int> fuzzProductions;
  for (const QJsonValue& value : positiveCases)
    fuzzProductions.unite(integerSet(value.toObject().value(QStringLiteral("productions")).toArray()));
  QSet<int> negativeOrigins;
  QSet<QString> mutationOperators;
  QSet<QString> upstreamStages;
  QSet<QString> upstreamClasses;
  int comparableLines = 0;
  int comparableColumns = 0;
  for (const QJsonValue& value : negativeCases) {
    const QJsonObject item = value.toObject();
    negativeOrigins.unite(integerSet(item.value(QStringLiteral("originProductions")).toArray()));
    mutationOperators.insert(item.value(QStringLiteral("operator")).toString());
    const QJsonObject error = item.value(QStringLiteral("upstreamError")).toObject();
    upstreamStages.insert(error.value(QStringLiteral("stage")).toString());
    upstreamClasses.insert(error.value(QStringLiteral("class")).toString());
    comparableLines += error.value(QStringLiteral("compareLine")).toBool();
    comparableColumns += error.value(QStringLiteral("compareColumn")).toBool();
  }
  require(positiveCases.size() >= 256 && negativeCases.size() >= 192 &&
              fuzzProductions == coveredProductions && negativeOrigins == coveredProductions &&
              mutationOperators.size() >= 10 && comparableLines >= 189 && comparableColumns >= 150 &&
              upstreamStages == QSet<QString>{QStringLiteral("detector"), QStringLiteral("lexer"),
                                              QStringLiteral("parser"), QStringLiteral("semantic")} &&
              upstreamClasses == QSet<QString>{QStringLiteral("detection"),
                                               QStringLiteral("semantic"), QStringLiteral("syntax")},
          QStringLiteral("Flowchart production-aware diagnostic fuzz coverage regressed"));

  QSet<QString> diagnosticCodes;
  QSet<QString> diagnosticStages;
  for (const QJsonValue& value : errorCases) {
    const QJsonObject item = value.toObject();
    diagnosticCodes.insert(item.value(QStringLiteral("expectedCode")).toString());
    const QString stage = item.value(QStringLiteral("expectedStage")).toString();
    diagnosticStages.insert(stage);
    if (stage != QLatin1String("resource"))
      require(item.value(QStringLiteral("expectedLine")).toInt() >= 1 &&
                  item.value(QStringLiteral("expectedColumn")).toInt() >= 1,
              QStringLiteral("Diagnostic %1 lacks a stable native span")
                  .arg(item.value(QStringLiteral("id")).toString()));
  }
  require(diagnosticCodes == QSet<QString>{QStringLiteral("limit-exceeded"),
                                           QStringLiteral("link-style-bounds"),
                                           QStringLiteral("missing-header"),
                                           QStringLiteral("missing-token"),
                                           QStringLiteral("unclosed-subgraph"),
                                           QStringLiteral("unexpected-end")} &&
              diagnosticStages == QSet<QString>{QStringLiteral("detector"),
                                                QStringLiteral("parser"),
                                                QStringLiteral("resource"),
                                                QStringLiteral("semantic")},
          QStringLiteral("Flowchart structured diagnostic coverage regressed"));

  requireCaseIds(geometryIds,
                 {QStringLiteral("curve-linear"), QStringLiteral("curve-step"),
                  QStringLiteral("curve-cardinal"), QStringLiteral("curve-stepBefore"),
                  QStringLiteral("curve-stepAfter"), QStringLiteral("curve-monotoneX"),
                  QStringLiteral("curve-monotoneY"), QStringLiteral("curve-bumpX"),
                  QStringLiteral("curve-bumpY"), QStringLiteral("curve-catmullRom"),
                  QStringLiteral("curve-natural"), QStringLiteral("compound-self-parallel"),
                  QStringLiteral("recursive-cluster-three-level"),
                  QStringLiteral("cluster-cross-layer-explicit-direction")},
                 QStringLiteral("edge/cluster geometry"));
  require(geometryCases.size() >= 68, QStringLiteral("Flowchart geometry case coverage regressed"));

  requireCaseIds(labelIds,
                 {QStringLiteral("fraction"), QStringLiteral("radical"),
                  QStringLiteral("root-index"), QStringLiteral("supsub"),
                  QStringLiteral("array"), QStringLiteral("html-math"),
                  QStringLiteral("markdown-html-math"), QStringLiteral("cjk-no-space"),
                  QStringLiteral("arabic-only"), QStringLiteral("hebrew-only"),
                  QStringLiteral("bidi-numeric"), QStringLiteral("mixed-format-math")},
                 QStringLiteral("label layout"));
  require(labelCases.size() >= 21, QStringLiteral("Flowchart label oracle coverage regressed"));

  QSet<QString> themes;
  QSet<QString> looks;
  QSet<QString> dprs;
  QSet<QString> structuralKinds;
  QSet<QString> files;
  QSet<QString> cropKinds;
  int notoCases = 0;
  int structuralCases = 0;
  int cropCases = 0;
  for (const QJsonValue& value : pixelCases) {
    const QJsonObject item = value.toObject();
    themes.insert(item.value(QStringLiteral("theme")).toString());
    looks.insert(item.value(QStringLiteral("look")).toString());
    dprs.insert(QString::number(item.value(QStringLiteral("dpr")).toDouble(), 'g', 3));
    notoCases += item.value(QStringLiteral("fontMode")).toString() == QLatin1String("noto");
    const QString structural = item.value(QStringLiteral("svgStructural")).toString();
    if (!structural.isEmpty()) {
      structuralKinds.insert(structural);
      ++structuralCases;
      require(!item.value(QStringLiteral("content")).toObject()
                   .value(QStringLiteral("svgStructure")).toObject().isEmpty(),
              QStringLiteral("Structural case %1 has no SVG oracle")
                  .arg(item.value(QStringLiteral("id")).toString()));
    }
    const QString fileName = item.value(QStringLiteral("file")).toString();
    require(!fileName.isEmpty() && !files.contains(fileName),
            QStringLiteral("Golden pixel file is empty/duplicated: %1").arg(fileName));
    files.insert(fileName);
    require(QFileInfo::exists(pixelDir + QLatin1Char('/') + fileName),
            QStringLiteral("Golden pixel file is missing: %1").arg(fileName));
    for (const auto& crop : {qMakePair(QStringLiteral("mathCropFile"),
                                       QStringLiteral("mathCropSha256")),
                             qMakePair(QStringLiteral("labelCropFile"),
                                       QStringLiteral("labelCropSha256"))}) {
      const QString cropFile = item.value(crop.first).toString();
      if (cropFile.isEmpty())
        continue;
      ++cropCases;
      const QString kindKey = crop.first.startsWith(QLatin1String("math"))
                                  ? QStringLiteral("mathCropKind")
                                  : QStringLiteral("labelCropKind");
      cropKinds.insert(item.value(kindKey).toString());
      require(!files.contains(cropFile),
              QStringLiteral("Golden crop file is duplicated: %1").arg(cropFile));
      files.insert(cropFile);
      const QString path = pixelDir + QLatin1Char('/') + cropFile;
      require(QFileInfo::exists(path), QStringLiteral("Golden crop file is missing: %1").arg(cropFile));
      require(sha256(path) == item.value(crop.second).toString().toLatin1(),
              QStringLiteral("Golden crop hash drifted: %1").arg(cropFile));
    }
  }
  require(themes == QSet<QString>{QStringLiteral("base"), QStringLiteral("dark"),
                                  QStringLiteral("default"), QStringLiteral("forest"),
                                  QStringLiteral("neutral"), QStringLiteral("neo"),
                                  QStringLiteral("neo-dark"), QStringLiteral("redux"),
                                  QStringLiteral("redux-color"), QStringLiteral("redux-dark"),
                                  QStringLiteral("redux-dark-color")} &&
              looks == QSet<QString>{QStringLiteral("classic"), QStringLiteral("handDrawn"),
                                     QStringLiteral("neo")} &&
              dprs == QSet<QString>{QStringLiteral("1"), QStringLiteral("1.25"),
                                    QStringLiteral("1.5"), QStringLiteral("2")} &&
              structuralKinds == QSet<QString>{QStringLiteral("animated"), QStringLiteral("aria"),
                  QStringLiteral("cluster"), QStringLiteral("cluster-direction"),
                  QStringLiteral("combined"), QStringLiteral("compound-self-parallel"),
                  QStringLiteral("edge"), QStringLiteral("hand-drawn"),
                  QStringLiteral("markers"), QStringLiteral("neo-markers"),
                  QStringLiteral("redux")} &&
              pixelCases.size() >= 102 && notoCases >= 58 && structuralCases >= 17 &&
              cropCases >= 21 && cropKinds.size() >= 20,
          QStringLiteral("Flowchart theme/look/DPR/SVG/crop coverage regressed"));

  requireCaseIds(pixelIds,
                 {QStringLiteral("look-neo-shape-matrix"),
                  QStringLiteral("look-neo-shapes-01"),
                  QStringLiteral("look-neo-shapes-02"),
                  QStringLiteral("look-neo-shapes-03"),
                  QStringLiteral("look-neo-shapes-04"),
                  QStringLiteral("look-neo-shapes-05"),
                  QStringLiteral("look-neo-shapes-06"),
                  QStringLiteral("look-neo-shapes-07"),
                  QStringLiteral("look-neo-dark-shapes-01-1x"),
                  QStringLiteral("look-neo-dark-shapes-02-1_25x"),
                  QStringLiteral("look-neo-dark-shapes-03-1_5x"),
                  QStringLiteral("look-neo-dark-shapes-04-2x"),
                  QStringLiteral("look-neo-dark-shapes-05-1_25x"),
                  QStringLiteral("look-neo-dark-shapes-06-1_5x"),
                  QStringLiteral("look-neo-dark-shapes-07-2x"),
                  QStringLiteral("look-hand-drawn-shapes-01-TB-1x"),
                  QStringLiteral("look-hand-drawn-shapes-02-BT-1_5x"),
                  QStringLiteral("look-hand-drawn-shapes-03-LR-2x"),
                  QStringLiteral("look-hand-drawn-shapes-04-RL-1x"),
                  QStringLiteral("look-hand-drawn-shapes-05-TB-1_5x"),
                  QStringLiteral("look-hand-drawn-shapes-06-BT-2x"),
                  QStringLiteral("look-hand-drawn-shapes-07-LR-1x"),
                  QStringLiteral("look-hand-drawn-cluster-self-marker-cjk-bidi-2x"),
                  QStringLiteral("flow-edge-label-crop-wrap-three-lines"),
                  QStringLiteral("flow-cluster-label-crop-markdown-neo-dark-2x"),
                  QStringLiteral("compound-self-parallel"),
                  QStringLiteral("cluster-cross-layer-explicit-direction"),
                 QStringLiteral("animated-edge-static-initial")},
                 QStringLiteral("shape/render integration"));

  const QByteArray pixelDigest = combinedFileDigest(pixelDir, files);
  require(pixelDigest == QByteArrayLiteral("939c7eb71632eb649e66e7aab594630ff126f2a7c61afa9f113696aaca9a4fb4"),
          QStringLiteral("Flowchart pixel fixture digest drifted: %1")
              .arg(QString::fromLatin1(pixelDigest)));

  QSet<QString> roughKinds;
  QSet<QString> roughOperations;
  for (const QJsonValue& value : roughCases) {
    const QJsonObject item = value.toObject();
    roughKinds.insert(item.value(QStringLiteral("kind")).toString());
    for (const QJsonValue& set : item.value(QStringLiteral("drawable")).toObject()
                                     .value(QStringLiteral("sets")).toArray())
      for (const QJsonValue& operation : set.toObject().value(QStringLiteral("ops")).toArray())
        roughOperations.insert(operation.toObject().value(QStringLiteral("op")).toString());
  }
  require(roughCases.size() >= 16 && roughKinds.size() >= 6 &&
              roughOperations.contains(QStringLiteral("move")) &&
              roughOperations.contains(QStringLiteral("lineTo")) &&
              roughOperations.contains(QStringLiteral("bcurveTo")) &&
              rough.value(QStringLiteral("operationDigest")).toString() ==
                  QLatin1String("9ebea1ab20ba9f281012cc32fd2077bb8e808c7b5cc77c3116d440e9b772661f"),
          QStringLiteral("Flowchart RoughJS operation coverage/digest regressed"));

  qDebug() << "MermaidFlowchartCoverageMatrixTest:" << productions.size() << "productions,"
           << mutationOperators.size() << "mutation operators," << diagnosticCodes.size()
           << "diagnostics," << geometryCases.size() << "geometry cases," << pixelCases.size()
           << "pixel cases and" << structuralKinds.size() << "SVG structural kinds passed";
  return 0;
}
