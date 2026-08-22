#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/venn/VennDiagram.h"
#include "mermaid/venn/VennLayout.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

void near(double actual, double expected, double tolerance,
          const QString& message) {
  require(std::abs(actual - expected) <= tolerance,
          message + QStringLiteral(" actual=") + QString::number(actual, 'g', 17) +
              QStringLiteral(" expected=") + QString::number(expected, 'g', 17));
}

QVector<venn::VennSubset> ensurePairwise(QVector<venn::VennSubset> subsets) {
  QSet<QString> keys;
  QMap<QString, double> singletonSizes;
  for (const auto& subset : subsets) {
    keys.insert(subset.sets.join(QLatin1Char('|')));
    if (subset.sets.size() == 1)
      singletonSizes.insert(subset.sets.front(), subset.size);
  }
  QVector<venn::VennSubset> synthetic;
  for (const auto& subset : std::as_const(subsets)) {
    if (subset.sets.size() < 3) continue;
    QStringList members = subset.sets;
    std::sort(members.begin(), members.end());
    for (int i = 0; i < members.size() - 1; ++i) {
      for (int j = i + 1; j < members.size(); ++j) {
        const QStringList pair{members.at(i), members.at(j)};
        const QString key = pair.join(QLatin1Char('|'));
        if (keys.contains(key)) continue;
        keys.insert(key);
        const bool complete = singletonSizes.contains(pair.at(0)) &&
                              singletonSizes.contains(pair.at(1));
        synthetic.append({pair,
                          complete ? std::min(singletonSizes.value(pair.at(0)),
                                              singletonSizes.value(pair.at(1))) /
                                         4.0
                                   : 2.5,
                          QString(), false});
      }
    }
  }
  subsets += synthetic;
  return subsets;
}

QVector<venn::VennSubset> filterZeroSets(
    const QVector<venn::VennSubset>& subsets) {
  QSet<QString> removed;
  for (const auto& subset : subsets)
    if (subset.sets.size() == 1 && subset.size == 0.0)
      removed.insert(subset.sets.front());
  QVector<venn::VennSubset> result;
  for (const auto& subset : subsets) {
    bool remove = false;
    for (const QString& set : subset.sets)
      if (removed.contains(set)) remove = true;
    if (!remove) result.append(subset);
  }
  return result;
}

struct ParsedCircle {
  double x = 0.0;
  double y = 0.0;
  double radius = 0.0;
};

ParsedCircle circleFromPath(const QString& path) {
  static const QRegularExpression expression(
      QStringLiteral(R"(^\s*M\s+([^\s]+)\s+([^\s]+)\s+m\s+([^\s]+)\s+0)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = expression.match(path);
  require(match.hasMatch(), QStringLiteral("Unable to parse Venn circle path: ") + path);
  return {match.captured(1).toDouble(), match.captured(2).toDouble(),
          -match.captured(3).toDouble()};
}

}  // namespace

int main(int argc, char** argv) {
#if defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; macOS
  // (SF/Helvetica) resolves different faces with different metrics.
  // Bundled-font goldens are the eventual closure.
  qWarning("skipped on macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Venn geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "898a3845d1786af23b3a137f494a2bef75b9444d699239d4d0a282427be3c5ff"),
          QStringLiteral("Venn geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "b15e69bd29441a18d2da2c81df95243196b5db6d56937f569abc27bd419fe6ec"),
          QStringLiteral("Venn geometry fixture provenance changed"));

  int compared = 0;
  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    if (id == QLatin1String("hand-drawn")) continue;
    const MermaidPreprocessResult pre =
        preprocessDiagram(fixture.value(QStringLiteral("source")).toString());
    venn::VennData data = venn::VennDiagram::parse(pre.code);
    if (!data.hasTitleDirective && !pre.title.isEmpty()) data.title = pre.title;
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QStringList viewBox = expected.value(QStringLiteral("root")).toObject()
                                    .value(QStringLiteral("attrs")).toObject()
                                    .value(QStringLiteral("viewBox")).toString()
                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const double width = viewBox.at(2).toDouble();
    const double height = viewBox.at(3).toDouble();
    const double scale = width / 1600.0;
    const double titleHeight = data.title.isEmpty() ? 0.0 : 48.0 * scale;
    const QVector<venn::VennSubset> renderSets = ensurePairwise(data.subsets);
    const auto result = venn::layout::compute(filterZeroSets(renderSets), width,
                                               height - titleHeight, 15.0);

    QMap<QString, venn::layout::Circle> actual;
    for (const auto& circle : result.circles) actual.insert(circle.set, circle);
    int expectedCircles = 0;
    for (const QJsonValue& areaValue :
         expected.value(QStringLiteral("areas")).toArray()) {
      const QJsonObject area = areaValue.toObject();
      const QStringList classes =
          area.value(QStringLiteral("attrs")).toObject()
              .value(QStringLiteral("class")).toString().split(QLatin1Char(' '));
      if (!classes.contains(QStringLiteral("venn-circle"))) continue;
      ++expectedCircles;
      const QString set = area.value(QStringLiteral("attrs")).toObject()
                              .value(QStringLiteral("data-venn-sets")).toString();
      require(actual.contains(set), id + QStringLiteral(": missing circle ") + set);
      const ParsedCircle expectedCircle = circleFromPath(
          area.value(QStringLiteral("path")).toObject()
              .value(QStringLiteral("attrs")).toObject()
              .value(QStringLiteral("d")).toString());
      const auto& native = actual.value(set);
      near(native.x, expectedCircle.x, 1e-8,
           id + QLatin1Char('/') + set + QStringLiteral(" x"));
      near(native.y, expectedCircle.y, 1e-8,
           id + QLatin1Char('/') + set + QStringLiteral(" y"));
      near(native.radius, expectedCircle.radius, 1e-8,
           id + QLatin1Char('/') + set + QStringLiteral(" radius"));
      ++compared;
    }
    require(actual.size() == expectedCircles,
            id + QStringLiteral(": circle count mismatch"));
  }
  std::printf("MermaidVennLayoutTest: %d browser circles matched\n", compared);
  return 0;
}
