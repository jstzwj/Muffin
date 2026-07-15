#include "mermaid/rough/RoughOps.h"

#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

rough::Options optionsFromJson(const QJsonObject& json) {
  rough::Options options;
  if (json.contains(QStringLiteral("maxRandomnessOffset")))
    options.maxRandomnessOffset = json.value(QStringLiteral("maxRandomnessOffset")).toDouble();
  if (json.contains(QStringLiteral("roughness"))) options.roughness = json.value(QStringLiteral("roughness")).toDouble();
  if (json.contains(QStringLiteral("bowing"))) options.bowing = json.value(QStringLiteral("bowing")).toDouble();
  if (json.contains(QStringLiteral("stroke"))) options.stroke = json.value(QStringLiteral("stroke")).toString();
  if (json.contains(QStringLiteral("strokeWidth"))) options.strokeWidth = json.value(QStringLiteral("strokeWidth")).toDouble();
  if (json.contains(QStringLiteral("fill"))) options.fill = json.value(QStringLiteral("fill")).toString();
  if (json.contains(QStringLiteral("fillStyle"))) options.fillStyle = json.value(QStringLiteral("fillStyle")).toString();
  if (json.contains(QStringLiteral("fillWeight"))) options.fillWeight = json.value(QStringLiteral("fillWeight")).toDouble();
  if (json.contains(QStringLiteral("hachureAngle"))) options.hachureAngle = json.value(QStringLiteral("hachureAngle")).toDouble();
  if (json.contains(QStringLiteral("hachureGap"))) options.hachureGap = json.value(QStringLiteral("hachureGap")).toDouble();
  if (json.contains(QStringLiteral("curveTightness"))) options.curveTightness = json.value(QStringLiteral("curveTightness")).toDouble();
  if (json.contains(QStringLiteral("curveFitting"))) options.curveFitting = json.value(QStringLiteral("curveFitting")).toDouble();
  if (json.contains(QStringLiteral("curveStepCount"))) options.curveStepCount = json.value(QStringLiteral("curveStepCount")).toInt();
  if (json.contains(QStringLiteral("seed"))) options.seed = static_cast<quint32>(json.value(QStringLiteral("seed")).toInt());
  if (json.contains(QStringLiteral("disableMultiStroke"))) options.disableMultiStroke = json.value(QStringLiteral("disableMultiStroke")).toBool();
  if (json.contains(QStringLiteral("disableMultiStrokeFill"))) options.disableMultiStrokeFill = json.value(QStringLiteral("disableMultiStrokeFill")).toBool();
  if (json.contains(QStringLiteral("preserveVertices"))) options.preserveVertices = json.value(QStringLiteral("preserveVertices")).toBool();
  if (json.contains(QStringLiteral("fillShapeRoughnessGain"))) options.fillShapeRoughnessGain = json.value(QStringLiteral("fillShapeRoughnessGain")).toDouble();
  return options;
}

QVector<QPointF> pointsFromJson(const QJsonArray& values) {
  QVector<QPointF> points;
  for (const QJsonValue& value : values) {
    const QJsonArray point = value.toArray();
    points.append(QPointF(point.at(0).toDouble(), point.at(1).toDouble()));
  }
  return points;
}

rough::Drawable generate(const QJsonObject& fixture) {
  const QString kind = fixture.value(QStringLiteral("kind")).toString();
  const QJsonArray args = fixture.value(QStringLiteral("args")).toArray();
  const rough::Options options = optionsFromJson(fixture.value(QStringLiteral("options")).toObject());
  if (kind == QLatin1String("line"))
    return rough::line(args[0].toDouble(), args[1].toDouble(), args[2].toDouble(), args[3].toDouble(), options);
  if (kind == QLatin1String("rectangle"))
    return rough::rectangle(args[0].toDouble(), args[1].toDouble(), args[2].toDouble(), args[3].toDouble(), options);
  if (kind == QLatin1String("polygon"))
    return rough::polygon(pointsFromJson(args[0].toArray()), options);
  if (kind == QLatin1String("ellipse"))
    return rough::ellipse(args[0].toDouble(), args[1].toDouble(), args[2].toDouble(), args[3].toDouble(), options);
  if (kind == QLatin1String("arc"))
    return rough::arc(args[0].toDouble(), args[1].toDouble(), args[2].toDouble(), args[3].toDouble(),
                      args[4].toDouble(), args[5].toDouble(), args[6].toBool(), options);
  if (kind == QLatin1String("path")) {
    QPainterPath path(QPointF(0, 0));
    path.lineTo(80, 0);
    path.cubicTo(90, 10, 90, 50, 80, 60);
    path.lineTo(0, 60);
    path.closeSubpath();
    return rough::path(path, options);
  }
  fail(QStringLiteral("Unknown RoughJS fixture kind: %1").arg(kind));
}

bool sameOperations(const rough::Drawable& left, const rough::Drawable& right) {
  if (left.sets.size() != right.sets.size()) return false;
  for (qsizetype i = 0; i < left.sets.size(); ++i) {
    if (left.sets[i].type != right.sets[i].type ||
        left.sets[i].ops.size() != right.sets[i].ops.size()) return false;
    for (qsizetype j = 0; j < left.sets[i].ops.size(); ++j)
      if (left.sets[i].ops[j].type != right.sets[i].ops[j].type ||
          left.sets[i].ops[j].data != right.sets[i].ops[j].data) return false;
  }
  return true;
}

void compareDrawable(const QString& id, const rough::Drawable& actual,
                     const QJsonObject& expected) {
  require(actual.shape == expected.value(QStringLiteral("shape")).toString(),
          QStringLiteral("%1: drawable shape differs").arg(id));
  const QJsonArray expectedSets = expected.value(QStringLiteral("sets")).toArray();
  require(actual.sets.size() == expectedSets.size(),
          QStringLiteral("%1: set count %2 != %3").arg(id).arg(actual.sets.size()).arg(expectedSets.size()));
  for (qsizetype setIndex = 0; setIndex < actual.sets.size(); ++setIndex) {
    const rough::OpSet& actualSet = actual.sets[setIndex];
    const QJsonObject expectedSet = expectedSets[setIndex].toObject();
    require(rough::opSetTypeName(actualSet.type) == expectedSet.value(QStringLiteral("type")).toString(),
            QStringLiteral("%1 set %2: type differs").arg(id).arg(setIndex));
    const QJsonArray expectedOps = expectedSet.value(QStringLiteral("ops")).toArray();
    require(actualSet.ops.size() == expectedOps.size(),
            QStringLiteral("%1 set %2: op count %3 != %4")
                .arg(id).arg(setIndex).arg(actualSet.ops.size()).arg(expectedOps.size()));
    for (qsizetype opIndex = 0; opIndex < actualSet.ops.size(); ++opIndex) {
      const rough::Op& actualOp = actualSet.ops[opIndex];
      const QJsonObject expectedOp = expectedOps[opIndex].toObject();
      require(rough::opTypeName(actualOp.type) == expectedOp.value(QStringLiteral("op")).toString(),
              QStringLiteral("%1 set %2 op %3: type differs").arg(id).arg(setIndex).arg(opIndex));
      const QJsonArray expectedData = expectedOp.value(QStringLiteral("data")).toArray();
      require(actualOp.data.size() == expectedData.size(),
              QStringLiteral("%1 set %2 op %3: arity differs").arg(id).arg(setIndex).arg(opIndex));
      for (qsizetype dataIndex = 0; dataIndex < actualOp.data.size(); ++dataIndex)
        require(std::abs(actualOp.data[dataIndex] - expectedData[dataIndex].toDouble()) <= 1e-9,
                QStringLiteral("%1 set %2 op %3 data %4: %5 != %6")
                    .arg(id).arg(setIndex).arg(opIndex).arg(dataIndex)
                    .arg(actualOp.data[dataIndex], 0, 'g', 16)
                    .arg(expectedData[dataIndex].toDouble(), 0, 'g', 16));
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, QStringLiteral("Expected rough operation fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open rough operation fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("4.6.6"),
          QStringLiteral("RoughJS fixture version drifted"));
  require(root.value(QStringLiteral("operationDigest")).toString() ==
              QLatin1String("9ebea1ab20ba9f281012cc32fd2077bb8e808c7b5cc77c3116d440e9b772661f"),
          QStringLiteral("RoughJS operation fixture changed; audit and update its digest"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  QSet<QString> ids, kinds, opTypes, setTypes;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!ids.contains(id), QStringLiteral("Duplicate RoughJS operation case: %1").arg(id));
    ids.insert(id);
    kinds.insert(fixture.value(QStringLiteral("kind")).toString());
    const rough::Drawable actual = generate(fixture);
    compareDrawable(id, actual, fixture.value(QStringLiteral("drawable")).toObject());
    require(sameOperations(actual, generate(fixture)),
            QStringLiteral("%1: same seed is not operation deterministic").arg(id));
    QJsonObject alternate = fixture;
    QJsonObject alternateOptions = alternate.value(QStringLiteral("options")).toObject();
    alternateOptions[QStringLiteral("seed")] = alternateOptions.value(QStringLiteral("seed")).toInt() + 1;
    alternate[QStringLiteral("options")] = alternateOptions;
    require(!sameOperations(actual, generate(alternate)),
            QStringLiteral("%1: changing seed did not change operations").arg(id));
    for (const rough::OpSet& set : actual.sets) {
      setTypes.insert(rough::opSetTypeName(set.type));
      for (const rough::Op& op : set.ops) opTypes.insert(rough::opTypeName(op.type));
    }
  }
  require(cases.size() == 16 && kinds.size() == 6 && setTypes.size() == 3 && opTypes.size() == 3,
          QStringLiteral("RoughJS primitive/operation coverage matrix regressed"));
  qDebug() << "MermaidRoughOpsTest:" << cases.size() << "RoughJS operation cases passed";
  return 0;
}
