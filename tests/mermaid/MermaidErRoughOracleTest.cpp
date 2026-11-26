// Oracle for the port's rough.js engine against the calls erDiagram makes
// under the handDrawn look, as real Mermaid 11.16.0 made them:
// tests/fixtures/mermaid/er-rough.json is captured by
// scripts/generate_mermaid_er_rough_fixture.mjs, which wraps rough.js's svg
// renderer and records every call with its arguments, its resolved options
// and the op sets it returned. Each is made again here, and every set must
// come out the same, op for op and number for number: the shapes of the
// handDrawn look are these, and a draw off by a random number would move
// every later vertex of the shape.

#include "mermaid/erdiagram/ErRough.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>
#include <cstdio>

using namespace muffin::mermaid;
using er::ErRoughOptions;

namespace {

constexpr qreal kNumber = 1e-9;

QStringList failures;

void require(bool condition, const QString& message) {
  if (!condition)
    failures << message;
}

// The options of a call as rough.js resolved them, in the port's terms.
rough::Options portOptions(const QJsonObject& recorded) {
  rough::Options options;
  options.maxRandomnessOffset = recorded.value(QStringLiteral("maxRandomnessOffset")).toDouble();
  options.roughness = recorded.value(QStringLiteral("roughness")).toDouble();
  options.bowing = recorded.value(QStringLiteral("bowing")).toDouble();
  options.stroke = recorded.value(QStringLiteral("stroke")).toString();
  options.strokeWidth = recorded.value(QStringLiteral("strokeWidth")).toDouble();
  options.curveTightness = recorded.value(QStringLiteral("curveTightness")).toDouble();
  options.curveFitting = recorded.value(QStringLiteral("curveFitting")).toDouble();
  options.curveStepCount = recorded.value(QStringLiteral("curveStepCount")).toInt();
  options.fillStyle = recorded.value(QStringLiteral("fillStyle")).toString();
  options.fillWeight = recorded.value(QStringLiteral("fillWeight")).toDouble();
  options.hachureAngle = recorded.value(QStringLiteral("hachureAngle")).toDouble();
  options.hachureGap = recorded.value(QStringLiteral("hachureGap")).toDouble();
  options.seed = static_cast<quint32>(recorded.value(QStringLiteral("seed")).toDouble());
  options.disableMultiStroke = recorded.value(QStringLiteral("disableMultiStroke")).toBool();
  options.disableMultiStrokeFill = recorded.value(QStringLiteral("disableMultiStrokeFill")).toBool();
  options.preserveVertices = recorded.value(QStringLiteral("preserveVertices")).toBool();
  options.fillShapeRoughnessGain = recorded.value(QStringLiteral("fillShapeRoughnessGain")).toDouble();
  // A fill rough.js was not given is one the port writes as none.
  options.fill = recorded.value(QStringLiteral("fill")).toString();
  return options;
}

QVector<qreal> numbers(const QString& text) {
  QVector<qreal> result;
  static const QRegularExpression number(QStringLiteral("[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?"));
  for (const auto& match : number.globalMatch(text))
    result.append(match.captured().toDouble());
  return result;
}

// One op set as rough.js wrote it against the port's.
void compareSet(const rough::OpSet& set, const QJsonObject& recorded, const QString& path) {
  if (rough::opSetTypeName(set.type) != recorded.value(QStringLiteral("type")).toString())
    failures << path + QStringLiteral("/type: native %1, upstream %2")
                           .arg(rough::opSetTypeName(set.type), recorded.value(QStringLiteral("type")).toString());
  const QVector<qreal> mine = numbers(er::roughPathData(set));
  const QVector<qreal> theirs = numbers(recorded.value(QStringLiteral("d")).toString());
  if (mine.size() != theirs.size()) {
    failures << path + QStringLiteral(": %1 numbers, upstream %2").arg(mine.size()).arg(theirs.size());
    return;
  }
  for (int index = 0; index < mine.size(); ++index) {
    if (!(std::abs(mine.at(index) - theirs.at(index)) <= kNumber)) {
      failures << path + QStringLiteral(": number %1 is %2, upstream %3")
                             .arg(index)
                             .arg(mine.at(index), 0, 'g', 17)
                             .arg(theirs.at(index), 0, 'g', 17);
      break;
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  require(argc == 2, QStringLiteral("Expected ER rough fixture path"));
  if (argc != 2) {
    for (const QString& failure : failures)
      std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
    return 1;
  }
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  // The generator emits LF; a CRLF checkout must not change the provenance.
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("253cade0e82abc13c05731780972bdd22aa5fd5297c5b650bb708eaad0e7bec1"),
          QStringLiteral("ER rough fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("ER rough: mermaidVersion drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("93f88f720fee50192b526ce45e40855af5d7da8a6b3d346ba2cf12a19cf72a39"),
          QStringLiteral("ER rough fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 8, QStringLiteral("ER rough case count drifted: %1").arg(cases.size()));

  qsizetype calls = 0, sets = 0;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QJsonArray recorded = fixture.value(QStringLiteral("calls")).toArray();
    for (qsizetype index = 0; index < recorded.size(); ++index) {
      const QJsonObject call = recorded.at(index).toObject();
      const QString path = id + QStringLiteral("/call %1 %2(%3)")
                                    .arg(index)
                                    .arg(call.value(QStringLiteral("method")).toString(),
                                         call.value(QStringLiteral("owner")).toString());
      const rough::Options options = portOptions(call.value(QStringLiteral("options")).toObject());
      const QJsonArray args = call.value(QStringLiteral("args")).toArray();
      const QString method = call.value(QStringLiteral("method")).toString();
      rough::Drawable drawable;
      if (method == QLatin1String("rectangle") && args.size() >= 4) {
        drawable = rough::rectangle(args.at(0).toDouble(), args.at(1).toDouble(), args.at(2).toDouble(),
                                    args.at(3).toDouble(), options);
      } else if (method == QLatin1String("polygon")) {
        QVector<QPointF> points;
        for (const QJsonValue& point : args.at(0).toArray())
          points.append(QPointF(point.toArray().at(0).toDouble(), point.toArray().at(1).toDouble()));
        drawable = rough::polygon(points, options);
      } else if (method == QLatin1String("path")) {
        drawable = rough::path(er::pathCommands(args.at(0).toString()), options);
      } else {
        failures << path + QStringLiteral(": a call the port does not make");
        continue;
      }
      ++calls;
      const QJsonArray expected = call.value(QStringLiteral("sets")).toArray();
      if (drawable.sets.size() != expected.size()) {
        failures << path + QStringLiteral(": %1 sets, upstream %2").arg(drawable.sets.size()).arg(expected.size());
        continue;
      }
      for (qsizetype set = 0; set < expected.size(); ++set) {
        compareSet(drawable.sets.at(set), expected.at(set).toObject(),
                   path + QStringLiteral("/") + QString::number(set));
        ++sets;
      }
    }
  }

  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidErRoughOracleTest: %lld divergence(s) from rough.js in Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::printf("MermaidErRoughOracleTest passed: %lld calls, %lld op sets, every number within %g\n",
              static_cast<long long>(calls), static_cast<long long>(sets), kNumber);
  return 0;
}
