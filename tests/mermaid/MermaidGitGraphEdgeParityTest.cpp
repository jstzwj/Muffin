#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/gitgraph/GitGraphScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool value, const QString& message) {
  if (!value) fail(message);
}
void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 1.0) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3 (tol %4)")
              .arg(path).arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17).arg(tolerance));
}
qreal number(QJsonValue value) {
  if (value.isDouble()) return value.toDouble();
  QString text = value.toString();
  if (text.endsWith(QLatin1String("px"))) text.chop(2);
  bool ok = false;
  const qreal result = text.toDouble(&ok);
  require(ok, QStringLiteral("Invalid oracle number: ") + text);
  return result;
}
QRectF viewBox(const QString& value) {
  const QStringList fields = value.split(
      QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  require(fields.size() == 4, QStringLiteral("Invalid viewBox: ") + value);
  return QRectF(number(fields[0]), number(fields[1]), number(fields[2]),
                number(fields[3]));
}

QString oracleRole(const QJsonObject& primitive) {
  const QString tag = primitive.value(QStringLiteral("tag")).toString();
  const QJsonObject attrs = primitive.value(QStringLiteral("attrs")).toObject();
  const QString css = attrs.value(QStringLiteral("class")).toString();
  const QString parent = primitive.value(QStringLiteral("parentClass")).toString();
  if (css.startsWith(QLatin1String("branch "))) return QStringLiteral("branch");
  if (css.startsWith(QLatin1String("branchLabelBkg")))
    return QStringLiteral("branch-label-background");
  if (parent.startsWith(QLatin1String("label branch-label")))
    return QStringLiteral("branch-label");
  if (css.startsWith(QLatin1String("arrow "))) return QStringLiteral("arrow");
  if (css.contains(QLatin1String("commit-highlight-outer")))
    return QStringLiteral("commit-highlight-outer");
  if (css.contains(QLatin1String("commit-highlight-inner")))
    return QStringLiteral("commit-highlight-inner");
  if (css.contains(QLatin1String("commit-merge")))
    return QStringLiteral("commit-merge");
  if (css.contains(QLatin1String("commit-reverse")))
    return QStringLiteral("commit-reverse");
  if (css.contains(QLatin1String("commit-cherry-pick"))) {
    if (tag == QLatin1String("line")) return QStringLiteral("cherry-stem");
    if (tag == QLatin1String("circle"))
      return attrs.value(QStringLiteral("r")).toString().toDouble() > 5.0
                 ? QStringLiteral("commit")
                 : QStringLiteral("cherry-dot");
  }
  if (parent == QLatin1String("commit-bullets")) return QStringLiteral("commit");
  if (css == QLatin1String("commit-label-bkg"))
    return QStringLiteral("commit-label-background");
  if (css == QLatin1String("commit-label")) return QStringLiteral("commit-label");
  if (css == QLatin1String("tag-label-bkg")) return QStringLiteral("tag-background");
  if (css == QLatin1String("tag-hole")) return QStringLiteral("tag-hole");
  if (css == QLatin1String("tag-label")) return QStringLiteral("tag-label");
  if (css == QLatin1String("gitTitleText")) return QStringLiteral("title");
  return QStringLiteral("unknown:") + tag + QLatin1Char(':') + css;
}

void samePaint(const QString& actual, const QString& expected,
               const QString& path) {
  if (actual.compare(expected, Qt::CaseInsensitive) == 0) return;
  const QColor a = color::toQColor(actual);
  const QColor b = color::toQColor(expected);
  require(a.isValid() && b.isValid() && a.rgba() == b.rgba(),
          QStringLiteral("%1: %2 != %3").arg(path, actual, expected));
}

void compareStyle(const gitgraph::GitGraphSceneStyle& actual,
                  const QJsonObject& expected, const QString& id) {
  require(actual.themeName == expected.value(QStringLiteral("theme")).toString(),
          id + QStringLiteral("/theme"));
  const auto paint = [&](const QString& value, const char* key) {
    const QJsonValue oracle = expected.value(QLatin1String(key));
    if (oracle.isNull())
      require(value.isEmpty(), id + QLatin1Char('/') + QLatin1String(key));
    else
      samePaint(value, oracle.toString(),
                id + QLatin1Char('/') + QLatin1String(key));
  };
  paint(actual.textColor, "textColor");
  paint(actual.lineColor, "lineColor");
  paint(actual.commitLineColor, "commitLineColor");
  paint(actual.nodeBorder, "nodeBorder");
  paint(actual.mainBkg, "mainBkg");
  paint(actual.primaryColor, "primaryColor");
  paint(actual.commitLabelColor, "commitLabelColor");
  paint(actual.commitLabelBackground, "commitLabelBackground");
  paint(actual.tagLabelColor, "tagLabelColor");
  paint(actual.tagLabelBackground, "tagLabelBackground");
  paint(actual.tagLabelBorder, "tagLabelBorder");
  near(actual.commitLabelFontSize,
       number(expected.value(QStringLiteral("commitLabelFontSize"))),
       id + QStringLiteral("/commitLabelFontSize"), 0.001);
  near(actual.tagLabelFontSize,
       number(expected.value(QStringLiteral("tagLabelFontSize"))),
       id + QStringLiteral("/tagLabelFontSize"), 0.001);
  const auto colors = [&](const QVector<QString>& values, const char* key) {
    const QJsonArray oracle = expected.value(QLatin1String(key)).toArray();
    require(values.size() == oracle.size(),
            id + QLatin1Char('/') + QLatin1String(key) + QStringLiteral(" size"));
    for (qsizetype i = 0; i < values.size(); ++i)
      samePaint(values.at(i), oracle.at(i).toString(),
                id + QLatin1Char('/') + QLatin1String(key) +
                    QStringLiteral("/%1").arg(i));
  };
  colors(actual.gitColors, "git");
  colors(actual.gitInvColors, "gitInv");
  colors(actual.branchLabelColors, "gitBranchLabel");
}

void comparePrimitivePaint(const gitgraph::GitGraphPrimitive& actual,
                           const QJsonObject& expected, const QString& path) {
  require(actual.role == oracleRole(expected), path + QStringLiteral("/role"));
  require(actual.text == expected.value(QStringLiteral("text")).toString(),
          path + QStringLiteral("/text"));
  const QJsonObject computed =
      expected.value(QStringLiteral("computed")).toObject();
  const QString fill = computed.value(QStringLiteral("fill")).toString();
  const QString stroke = computed.value(QStringLiteral("stroke")).toString();
  if (actual.kind == gitgraph::PrimitiveKind::Text) {
    samePaint(actual.fill, fill, path + QStringLiteral("/fill"));
    near(actual.fontSize, number(computed.value(QStringLiteral("fontSize"))),
         path + QStringLiteral("/fontSize"), 0.001);
    const int expectedWeight = computed.value(QStringLiteral("fontWeight"))
                                   .toString().toInt();
    require((actual.bold ? 600 : 400) == expectedWeight,
            path + QStringLiteral("/fontWeight"));
  } else if (actual.kind == gitgraph::PrimitiveKind::Line ||
             actual.kind == gitgraph::PrimitiveKind::Path) {
    samePaint(actual.stroke, stroke, path + QStringLiteral("/stroke"));
    near(actual.strokeWidth,
         number(computed.value(QStringLiteral("strokeWidth"))),
         path + QStringLiteral("/strokeWidth"), 0.001);
  } else {
    samePaint(actual.fill, fill, path + QStringLiteral("/fill"));
    if (actual.gradientStroke)
      require(stroke.startsWith(QLatin1String("url(")),
              path + QStringLiteral("/gradient stroke"));
    else if (stroke != QLatin1String("none") || actual.stroke != QLatin1String("none"))
      samePaint(actual.stroke, stroke, path + QStringLiteral("/stroke"));
  }
  near(actual.opacity, number(computed.value(QStringLiteral("opacity"))),
       path + QStringLiteral("/opacity"), 0.001);
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected GitGraph config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("115723042f467e050ff49357ae5fc7a30351c2fb69e577b039a0773bffd8165c"),
          QStringLiteral("GitGraph config fixture bytes drifted"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("0addd071354d39faf58ff725f050ffa438d521ce00d018d7a24050c852adb2bf"),
          QStringLiteral("GitGraph config fixture provenance drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 27, QStringLiteral("GitGraph config case count"));

  editor::MermaidRenderCache cache;
  QJsonObject baseline;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source),
                                     source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": ") + entry.errorMessage);
    const auto scene =
        std::dynamic_pointer_cast<const gitgraph::GitGraphScene>(entry.scene);
    require(bool(scene), id + QStringLiteral(": expected GitGraphScene"));
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    compareStyle(scene->style,
                 expected.value(QStringLiteral("effectiveStyle")).toObject(), id);
    const QJsonObject attrs = expected.value(QStringLiteral("root")).toObject()
                                  .value(QStringLiteral("attrs")).toObject();
    require(scene->config.useMaxWidth ==
                (attrs.value(QStringLiteral("width")).toString() == QLatin1String("100%")),
            id + QStringLiteral("/useMaxWidth"));
    if (id != QLatin1String("font-family")) {
      const QRectF expectedView = viewBox(attrs.value(QStringLiteral("viewBox")).toString());
      near(scene->bounds.x(), expectedView.x(), id + QStringLiteral("/viewBox/x"));
      near(scene->bounds.y(), expectedView.y(), id + QStringLiteral("/viewBox/y"));
      near(scene->bounds.width(), expectedView.width(), id + QStringLiteral("/viewBox/w"));
      near(scene->bounds.height(), expectedView.height(), id + QStringLiteral("/viewBox/h"));
    } else {
      require(scene->style.fontFamily == QLatin1String("monospace"),
              QStringLiteral("font-family override did not reach the scene"));
    }
    const QJsonArray primitives = expected.value(QStringLiteral("primitives")).toArray();
    require(scene->primitives.size() == primitives.size(),
            id + QStringLiteral("/primitive count"));
    for (qsizetype i = 0; i < scene->primitives.size(); ++i)
      comparePrimitivePaint(scene->primitives.at(i), primitives.at(i).toObject(),
                            id + QStringLiteral("/%1").arg(i));

    if (id == QLatin1String("defaults")) {
      baseline = scene->toJsonObject();
      require(scene->config.titleTopMargin == 25.0 &&
                  scene->config.diagramPadding == 8.0 &&
                  scene->config.showCommitLabel && scene->config.showBranches &&
                  scene->config.rotateCommitLabel && !scene->config.parallelCommits,
              QStringLiteral("GitGraph defaults"));
    } else if (id == QLatin1String("use-width-inert")) {
      require(scene->toJsonObject() == baseline,
              QStringLiteral("gitGraph.useWidth must remain upstream-inert"));
    } else if (id == QLatin1String("title-margin")) {
      require(scene->config.titleTopMargin == 50.0,
              QStringLiteral("titleTopMargin did not reach the scene"));
    } else if (id == QLatin1String("padding")) {
      require(scene->config.diagramPadding == 30.0,
              QStringLiteral("diagramPadding did not reach the scene"));
    } else if (id == QLatin1String("parallel")) {
      require(scene->config.parallelCommits,
              QStringLiteral("parallelCommits did not reach the scene"));
    }
  }
  std::puts("MermaidGitGraphEdgeParityTest: 27 cases passed");
  return 0;
}
