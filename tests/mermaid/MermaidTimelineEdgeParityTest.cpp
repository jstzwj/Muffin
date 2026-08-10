#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/timeline/TimelineScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
double number(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  QString text = value.toString();
  if (text.endsWith(QStringLiteral("px"))) text.chop(2);
  bool ok = false;
  const double result = text.toDouble(&ok);
  require(ok, QStringLiteral("Invalid oracle number: ") + text);
  return result;
}
void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 0.25) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual).arg(expected));
}
QPointF translate(const QString& value) {
  static const QRegularExpression re(
      QStringLiteral(R"(^translate\(\s*([^,\s]+)[,\s]+([^\)\s]+)\s*\)$)"));
  const auto match = re.match(value);
  require(match.hasMatch(), QStringLiteral("Invalid translate: ") + value);
  return QPointF(number(match.captured(1)), number(match.captured(2)));
}
QRectF viewBox(const QString& value) {
  const QStringList f = value.split(QRegularExpression(QStringLiteral("\\s+")),
                                    Qt::SkipEmptyParts);
  require(f.size() == 4, QStringLiteral("Invalid viewBox"));
  return QRectF(number(f[0]), number(f[1]), number(f[2]), number(f[3]));
}
struct Rendered {
  editor::MermaidRenderEntry entry;
  std::shared_ptr<const timeline::TimelineScene> scene;
};
Rendered render(const QString& source) {
  static editor::MermaidRenderCache cache;
  const auto key = editor::MermaidRenderCache::makeKey(source);
  editor::MermaidRenderEntry entry = cache.getSync(key, source);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("Timeline must render: ") + entry.errorMessage);
  auto scene = std::dynamic_pointer_cast<const timeline::TimelineScene>(entry.scene);
  require(bool(scene), QStringLiteral("Expected TimelineScene"));
  return {std::move(entry), std::move(scene)};
}
editor::MermaidRenderEntry renderEntry(const QString& source) {
  static editor::MermaidRenderCache cache;
  const auto key = editor::MermaidRenderCache::makeKey(source);
  return cache.getSync(key, source);
}
QImage paintScene(const timeline::TimelineScene& scene, QSize size) {
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.translate(-scene.bounds.topLeft());
  scene.paint(painter, {});
  painter.end();
  return image;
}
int opaquePixels(const QImage& image, const QRect& area) {
  int count = 0;
  const QRect clipped = area.intersected(image.rect());
  for (int y = clipped.top(); y <= clipped.bottom(); ++y)
    for (int x = clipped.left(); x <= clipped.right(); ++x)
      count += image.pixelColor(x, y).alpha() > 0;
  return count;
}
int differingPixels(const QImage& a, const QImage& b) {
  require(a.size() == b.size(), QStringLiteral("Image size mismatch"));
  int count = 0;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x)
      count += a.pixel(x, y) != b.pixel(x, y);
  return count;
}
timeline::TimelineScene isolatedLineScene(const timeline::TimelineSceneStyle& style,
                                          const QString& stroke,
                                          qreal strokeWidth = 2.0,
                                          bool marker = false) {
  timeline::TimelineScene scene;
  scene.bounds = QRectF(0.0, 0.0, 120.0, 80.0);
  scene.style = style;
  timeline::TimelineLineGeometry line;
  line.start = QPointF(20.0, 40.0);
  line.end = QPointF(100.0, 40.0);
  line.stroke = stroke;
  line.strokeWidth = strokeWidth;
  line.markerEnd = marker;
  line.markerResolved = marker;
  scene.lines.append(line);
  return scene;
}
void compareColor(const QString& actual, const QString& expected,
                  const QString& path) {
  const QColor a = color::toQColor(actual);
  const QColor b = color::toQColor(expected);
  require(a.isValid() && b.isValid() && a.rgba() == b.rgba(),
          QStringLiteral("%1: %2 != %3").arg(path, actual, expected));
}
QString computed(const QJsonObject& element, const char* field) {
  return element.value(QStringLiteral("computed")).toObject()
      .value(QLatin1String(field)).toString();
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected timeline config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("8a7fb9bf306bcd62be6a15d84560ced1f8fb2dd105e0f149137cf76787389fdd"),
          QStringLiteral("Timeline config fixture changed; audit its digest"));
  const QJsonObject fixtureRoot = QJsonDocument::fromJson(bytes).object();
  const QJsonArray cases = fixtureRoot.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 40, QStringLiteral("Timeline config fixture must have 40 cases"));

  QJsonObject baseline;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const Rendered rendered = render(fixture.value(QStringLiteral("source")).toString());
    const timeline::TimelineScene& scene = *rendered.scene;
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
    const QJsonObject attrs = root.value(QStringLiteral("attrs")).toObject();
    const QRectF expectedView = viewBox(attrs.value(QStringLiteral("viewBox")).toString());
    const bool portableGeometry = id != QStringLiteral("fontFamily");
    if (portableGeometry) {
      near(scene.bounds.x(), expectedView.x(), id + QStringLiteral("/x"));
      near(scene.bounds.y(), expectedView.y(), id + QStringLiteral("/y"));
      near(scene.bounds.width(), expectedView.width(), id + QStringLiteral("/w"));
      near(scene.bounds.height(), expectedView.height(), id + QStringLiteral("/h"));
    } else {
      // Qt's offscreen QPA has no system font database on CI and resolves the
      // Windows-only Courier New oracle to the bundled Noto Sans. Preserve the
      // CSS family value, but do not compare font-coupled geometry across two
      // different physical fonts.
      require(scene.style.fontFamily == QStringLiteral("Courier New"),
              QStringLiteral("fontFamily source value did not reach the scene"));
    }
    require(scene.config.useMaxWidth ==
                (attrs.value(QStringLiteral("width")).toString() == QStringLiteral("100%")),
            id + QStringLiteral(": useMaxWidth mismatch"));

    const QJsonArray nodes = expected.value(QStringLiteral("nodes")).toArray();
    require(scene.nodes.size() == nodes.size(), id + QStringLiteral(": node count"));
    if (!nodes.isEmpty()) {
      for (qsizetype index : {qsizetype(0), nodes.size() - 1}) {
        const QJsonObject oracle = nodes.at(index).toObject();
        const auto& native = scene.nodes.at(index);
        const QPointF expectedPosition =
            translate(oracle.value(QStringLiteral("outerTransform")).toString());
        if (portableGeometry) {
          near(native.position.x(), expectedPosition.x(),
               id + QStringLiteral("/node/x"));
          near(native.position.y(), expectedPosition.y(),
               id + QStringLiteral("/node/y"));
        }
        const QJsonObject bbox = oracle.value(QStringLiteral("bbox")).toObject();
        if (portableGeometry) {
          near(native.width, number(bbox.value(QStringLiteral("width"))),
               id + QStringLiteral("/node/w"));
          near(native.height, number(bbox.value(QStringLiteral("height"))),
               id + QStringLiteral("/node/h"));
        }
        const QJsonObject path = oracle.value(QStringLiteral("background"))
                                     .toArray().first().toObject();
        const QString expectedFill = computed(path, "fill");
        if (color::isParsableColor(native.fill) && color::isParsableColor(expectedFill))
          compareColor(native.fill, expectedFill, id + QStringLiteral("/fill"));
        const QString expectedText = computed(
            oracle.value(QStringLiteral("text")).toObject(), "fill");
        if (color::isParsableColor(native.textFill) && color::isParsableColor(expectedText))
          compareColor(native.textFill, expectedText, id + QStringLiteral("/text"));
      }
    }

    if (id == QStringLiteral("default")) baseline = scene.toJsonObject();
    if (id == QStringLiteral("legacy-fields") ||
        id.startsWith(QStringLiteral("legacy-")))
      require(scene.toJsonObject() == baseline,
              id + QStringLiteral(": upstream-inert setting changed the scene"));
    if (id == QStringLiteral("disableMulticolor")) {
      require(scene.nodes.size() >= 2 && scene.nodes[0].sectionClass ==
                  scene.nodes[1].sectionClass,
              QStringLiteral("disableMulticolor did not freeze the class"));
    }
    if (id == QStringLiteral("themeColorLimit-0")) {
      require(!scene.nodes.isEmpty() && scene.nodes.first().sectionClass ==
                  QStringLiteral("section-NaN"),
              QStringLiteral("TCL=0 did not preserve section-NaN"));
    }
    if (id == QStringLiteral("look-neo-gradient")) {
      require(!scene.nodes.isEmpty() && scene.nodes.first().gradientStroke,
              QStringLiteral("neo gradient did not reach the scene"));
    }
    if (id == QStringLiteral("look-neo-no-gradient")) {
      require(!scene.nodes.isEmpty() && !scene.nodes.first().gradientStroke,
              QStringLiteral("useGradient:false was ignored"));
    }
  }

  // Fractional limits use the raw JS remainder for class names, while the
  // generated stylesheet contains ceil(limit) integer rules only.
  const Rendered fractional = render(QStringLiteral(
      "%%{init: {\"themeVariables\":{\"THEME_COLOR_LIMIT\":2.5}}}%%\n"
      "timeline\nA\nB\nC\nD\nE"));
  require(fractional.scene->nodes.size() == 5,
          QStringLiteral("fractional TCL node count"));
  const QStringList fractionalClasses = {
      QStringLiteral("section--1"), QStringLiteral("section-0"),
      QStringLiteral("section-1"), QStringLiteral("section--0.5"),
      QStringLiteral("section-0.5")};
  for (qsizetype i = 0; i < fractionalClasses.size(); ++i) {
    const auto& node = fractional.scene->nodes.at(i);
    require(node.sectionClass == fractionalClasses.at(i),
            QStringLiteral("fractional TCL class %1").arg(i));
    require(node.paletteIndex == (i < 3 ? int(i) : -1),
            QStringLiteral("fractional TCL rule %1").arg(i));
    require(node.dividerVisible == (i < 3),
            QStringLiteral("fractional TCL divider %1").arg(i));
  }

  auto withLimit = [](const QString& theme, const QString& limit) {
    return QStringLiteral(
               "%%{init: {\"theme\":\"%1\",\"themeVariables\":{"
               "\"THEME_COLOR_LIMIT\":%2}}}%%\ntimeline\nA")
        .arg(theme, limit);
  };
  auto ready = [&](const QString& theme, const QString& limit) {
    return renderEntry(withLimit(theme, limit)).status ==
           editor::MermaidRenderStatus::Ready;
  };
  const QJsonObject gate = fixtureRoot.value(
      QStringLiteral("themeColorLimitGate")).toObject();
  const QJsonArray gateCases = gate.value(QStringLiteral("cases")).toArray();
  require(gateCases.size() == 22,
          QStringLiteral("TCL upstream gate must cover 11 themes x 2 limits"));
  QSet<QString> gateKeys;
  for (const QJsonValue& value : gateCases) {
    const QJsonObject oracle = value.toObject();
    const QString theme = oracle.value(QStringLiteral("theme")).toString();
    const int limit = oracle.value(QStringLiteral("limit")).toInt();
    const QString key = theme + QLatin1Char('/') + QString::number(limit);
    require(!theme.isEmpty() && (limit == 13 || limit == 14) &&
                !gateKeys.contains(key),
            QStringLiteral("Invalid or duplicate TCL oracle: ") + key);
    gateKeys.insert(key);
    const editor::MermaidRenderEntry entry =
        renderEntry(withLimit(theme, QString::number(limit)));
    if (oracle.value(QStringLiteral("accept")).toBool()) {
      require(entry.status == editor::MermaidRenderStatus::Ready,
              key + QStringLiteral(": native rejected upstream acceptance"));
      continue;
    }
    const QString message = oracle.value(QStringLiteral("error")).toObject()
                                .value(QStringLiteral("message")).toString();
    require(entry.status == editor::MermaidRenderStatus::Error &&
                entry.diagnostic.stage == QLatin1String("render") &&
                entry.diagnostic.code == QLatin1String("native-render-failed") &&
                entry.diagnostic.message == message,
            key + QStringLiteral(": theme-construction diagnostic drifted: ") +
                entry.errorMessage);
  }
  require(gateKeys.size() == 22,
          QStringLiteral("TCL upstream gate coverage drifted"));

  const QJsonArray nativePolicies = gate.value(
      QStringLiteral("nativeOnlyPolicies")).toArray();
  require(nativePolicies.size() == 1 &&
              nativePolicies.first().toObject()
                      .value(QStringLiteral("value")).toString() ==
                  QLatin1String("Infinity") &&
              nativePolicies.first().toObject()
                      .value(QStringLiteral("result")).toString() ==
                  QLatin1String("reject"),
          QStringLiteral("Infinity resource policy provenance drifted"));
  for (const QString& theme : {QStringLiteral("default"),
                               QStringLiteral("redux")}) {
    const editor::MermaidRenderEntry entry =
        renderEntry(withLimit(theme, QStringLiteral("\"Infinity\"")));
    require(entry.status == editor::MermaidRenderStatus::Error &&
                entry.diagnostic.stage == QLatin1String("render") &&
                entry.diagnostic.code == QLatin1String("native-render-failed") &&
                entry.diagnostic.message ==
                    QLatin1String("Cannot read properties of undefined (reading 'l')"),
            theme + QStringLiteral(": infinite TCL resource policy drifted"));
  }
  for (const QString& theme : {QStringLiteral("default"), QStringLiteral("dark"),
                               QStringLiteral("redux-color")})
    require(ready(theme, QStringLiteral("2.5")),
            theme + QStringLiteral(": fractional TCL must render"));

  const Rendered stringMargin = render(QStringLiteral(
      "%%{init: {\"timeline\":{\"leftMargin\":\"7\"}}}%%\n"
      "timeline\nA : event\nB"));
  require(stringMargin.scene->nodes.size() == 3,
          QStringLiteral("string leftMargin node count"));
  near(stringMargin.scene->nodes.at(0).position.x(), 507.0,
       QStringLiteral("string leftMargin first task"), 0.001);
  near(stringMargin.scene->nodes.at(1).position.x(), 507.0,
       QStringLiteral("string leftMargin event"), 0.001);
  near(stringMargin.scene->nodes.at(2).position.x(), 507200.0,
       QStringLiteral("string leftMargin repeated concat"), 0.001);
  const auto stringConnector = std::find_if(
      stringMargin.scene->lines.cbegin(), stringMargin.scene->lines.cend(),
      [](const auto& line) { return !line.axis; });
  const auto stringAxis = std::find_if(
      stringMargin.scene->lines.cbegin(), stringMargin.scene->lines.cend(),
      [](const auto& line) { return line.axis; });
  require(stringConnector != stringMargin.scene->lines.cend() &&
              stringAxis != stringMargin.scene->lines.cend(),
          QStringLiteral("string leftMargin lines missing"));
  near(stringConnector->start.x(), 50795.0,
       QStringLiteral("string leftMargin connector concat"), 0.001);
  near(stringAxis->start.x(), 7.0,
       QStringLiteral("string leftMargin raw axis attr"), 0.001);

  const Rendered boolMargin = render(QStringLiteral(
      "%%{init: {\"timeline\":{\"leftMargin\":true}}}%%\n"
      "timeline\nA : event"));
  near(boolMargin.scene->nodes.first().position.x(), 51.0,
       QStringLiteral("boolean leftMargin arithmetic"), 0.001);
  const auto boolAxis = std::find_if(
      boolMargin.scene->lines.cbegin(), boolMargin.scene->lines.cend(),
      [](const auto& line) { return line.axis; });
  require(boolAxis != boolMargin.scene->lines.cend(),
          QStringLiteral("boolean leftMargin axis missing"));
  near(boolAxis->start.x(), 0.0,
       QStringLiteral("boolean leftMargin SVG attr"), 0.001);
  const Rendered invalidStringMargin = render(QStringLiteral(
      "%%{init: {\"timeline\":{\"leftMargin\":\"abc\"},"
      "\"fontFamily\":\"Noto Sans\","
      "\"themeVariables\":{\"fontFamily\":\"Noto Sans\"}}}%%\n"
      "timeline\nA : event"));
  require(invalidStringMargin.scene->nodes.size() == 2 &&
              invalidStringMargin.scene->nodes.at(0).position == QPointF() &&
              invalidStringMargin.scene->nodes.at(1).position == QPointF(),
          QStringLiteral("invalid translate did not discard the whole transform"));
  near(invalidStringMargin.scene->bounds.x(), -50.0,
       QStringLiteral("invalid leftMargin viewBox x"), 0.001);
  near(invalidStringMargin.scene->bounds.y(), -50.0,
       QStringLiteral("invalid leftMargin viewBox y"), 0.001);
  near(invalidStringMargin.scene->bounds.width(), 290.0,
       QStringLiteral("invalid leftMargin viewBox width"), 0.001);
  near(invalidStringMargin.scene->bounds.height(), 471.600006,
       QStringLiteral("invalid leftMargin Noto viewBox height"), 0.01);

  auto paddingScene = [](const QString& jsonValue) {
    return render(QStringLiteral(
                      "%%{init: {\"timeline\":{\"padding\":%1}}}%%\n"
                      "timeline\nA : event")
                      .arg(jsonValue));
  };
  const Rendered arrayPadding = paddingScene(QStringLiteral("[7]"));
  near(arrayPadding.scene->config.padding, 7.0,
       QStringLiteral("array padding Number"), 0.001);
  near(arrayPadding.scene->bounds.x(),
       arrayPadding.scene->contentBounds.x() - 7.0,
       QStringLiteral("array padding x"), 0.001);
  near(arrayPadding.scene->bounds.width(),
       arrayPadding.scene->contentBounds.width() + 14.0,
       QStringLiteral("array padding width"), 0.001);
  const Rendered emptyArrayPadding = paddingScene(QStringLiteral("[]"));
  near(emptyArrayPadding.scene->config.padding, 0.0,
       QStringLiteral("empty-array padding Number"), 0.001);
  require(emptyArrayPadding.scene->bounds == emptyArrayPadding.scene->contentBounds,
          QStringLiteral("empty-array padding did not preserve content bounds"));
  const Rendered nullPadding = paddingScene(QStringLiteral("null"));
  near(nullPadding.scene->config.padding, 50.0,
       QStringLiteral("null padding fallback"), 0.001);
  for (const QString& invalid : {QStringLiteral("{}"), QStringLiteral("[1,2]"),
                                 QStringLiteral("\"abc\"")}) {
    const Rendered invalidPadding = paddingScene(invalid);
    require(invalidPadding.scene->config.invalidPadding &&
                invalidPadding.scene->bounds == QRectF(0.0, 0.0, 784.0, 150.0),
            invalid + QStringLiteral(": invalid viewBox used-value"));
  }

  const Rendered plainTitle = render(QStringLiteral(
      "timeline\ntitle A B\nTask"));
  const Rendered spacedTitle = render(QStringLiteral(
      "timeline\ntitle     A\t   B   \nTask"));
  require(plainTitle.scene->title != spacedTitle.scene->title &&
              plainTitle.scene->titleGeometry.text == QStringLiteral("A B") &&
              spacedTitle.scene->titleGeometry.text == QStringLiteral("A B") &&
              plainTitle.scene->titleGeometry.logicalBounds ==
                  spacedTitle.scene->titleGeometry.logicalBounds,
          QStringLiteral("title white-space normal collapse diverged"));
  const Rendered bearingTitle = render(QStringLiteral(
      "timeline\ntitle j\nTask"));
  require(bearingTitle.scene->titleGeometry.logicalBounds.left() <
              bearingTitle.scene->titleGeometry.baseline.x(),
          QStringLiteral("title negative side-bearing was clipped"));
  const Rendered unicodeSpace = render(QStringLiteral(
      "timeline\nA\u00a0\u00a0B"));
  require(!unicodeSpace.scene->nodes.isEmpty() &&
              !unicodeSpace.scene->nodes.first().textLines.isEmpty() &&
              unicodeSpace.scene->nodes.first().textLines.first().visibleText
                  .contains(QChar(0x00a0)) &&
              unicodeSpace.scene->nodes.first().textLines.first().visibleText !=
                  QStringLiteral("A B"),
          QStringLiteral("CSS incorrectly collapsed visible NBSP glyphs"));
  const Rendered nbspTitle = render(QStringLiteral(
      "timeline\ntitle A\u00a0B\nTask"));
  require(nbspTitle.scene->titleGeometry.text.contains(QChar(0x00a0)),
          QStringLiteral("title incorrectly collapsed visible NBSP glyph"));
  const Rendered hugeTitle = render(QStringLiteral(
      "%%{init: {\"themeVariables\":{\"fontSize\":\"1e9px\"}}}%%\n"
      "timeline\ntitle Huge\nTask"));
  near(hugeTitle.scene->titleGeometry.fontSize, 10000.0,
       QStringLiteral("title 4ex used-value clamp"), 0.001);

  auto gradientScene = [](const QString& raw) {
    return render(QStringLiteral(
                      "%%{init: {\"theme\":\"neo\",\"look\":\"neo\","
                      "\"themeVariables\":{\"useGradient\":%1}}}%%\n"
                      "timeline\nA")
                      .arg(raw));
  };
  for (const QString& truthy : {QStringLiteral("1"), QStringLiteral("\"false\"")})
    require(gradientScene(truthy).scene->nodes.first().gradientStroke,
            truthy + QStringLiteral(": truthy useGradient disabled"));
  for (const QString& falsy : {QStringLiteral("0"), QStringLiteral("false"),
                               QStringLiteral("\"\"")})
    require(!gradientScene(falsy).scene->nodes.first().gradientStroke,
            falsy + QStringLiteral(": falsy useGradient enabled"));
  require(gradientScene(QStringLiteral("null")).scene->nodes.first().gradientStroke,
          QStringLiteral("null useGradient did not retain the theme default"));
  require(!render(QStringLiteral(
                      "%%{init: {\"theme\":\"neo\",\"look\":\"Neo\"}}%%\n"
                      "timeline\nA"))
               .scene->nodes.first().gradientStroke,
          QStringLiteral("look matching must be case-sensitive"));
  const Rendered wrongCaseRedux = render(QStringLiteral(
      "%%{init: {\"theme\":\"Redux\"}}%%\ntimeline\nA"));
  require(wrongCaseRedux.scene->nodes.first().rounded &&
              wrongCaseRedux.scene->nodes.first().strokeWidth == 1.0,
          QStringLiteral("renderer treated unknown Redux casing as redux"));
  const Rendered wrongCaseNeutral = render(QStringLiteral(
      "%%{init: {\"theme\":\"Neutral\",\"look\":\"neo\","
      "\"themeVariables\":{\"useGradient\":true}}}%%\n"
      "timeline\nA"));
  require(wrongCaseNeutral.scene->nodes.first().gradientStroke,
          QStringLiteral("renderer treated unknown Neutral casing as neutral"));

  auto reduxStroke = [](const QString& jsonValue) {
    return render(QStringLiteral(
                      "%%{init: {\"theme\":\"redux-color\","
                      "\"themeVariables\":{\"strokeWidth\":%1}}}%%\n"
                      "timeline\nA : event")
                      .arg(jsonValue));
  };
  auto checkWidths = [](const Rendered& rendered, qreal node,
                        qreal connector, qreal axis, const QString& id) {
    for (const auto& item : rendered.scene->nodes)
      near(item.strokeWidth, node, id + QStringLiteral("/node"), 0.001);
    for (const auto& line : rendered.scene->lines)
      near(line.strokeWidth, line.axis ? axis : connector,
           id + (line.axis ? QStringLiteral("/axis")
                           : QStringLiteral("/connector")),
           0.001);
  };
  checkWidths(reduxStroke(QStringLiteral("\"7px\"")), 7.0, 7.0, 7.0,
              QStringLiteral("stroke-7"));
  checkWidths(reduxStroke(QStringLiteral("[7]")), 7.0, 7.0, 7.0,
              QStringLiteral("stroke-array-7"));
  checkWidths(reduxStroke(QStringLiteral("\"0\"")), 0.0, 0.0, 0.0,
              QStringLiteral("stroke-zero"));
  checkWidths(reduxStroke(QStringLiteral("[0]")), 0.0, 0.0, 0.0,
              QStringLiteral("stroke-array-zero"));
  for (const QString& inherited : {QStringLiteral("\"inherit\""),
                                   QStringLiteral("\"initial\""),
                                   QStringLiteral("\"unset\""),
                                   QStringLiteral("\"revert\"")})
    checkWidths(reduxStroke(inherited), 1.0, 1.0, 1.0,
                QStringLiteral("stroke-global-") + inherited);
  for (const QString& invalid : {QStringLiteral("\"none\""),
                                 QStringLiteral("\"bogus\""),
                                 QStringLiteral("\"revert-layer\""),
                                 QStringLiteral("true"), QStringLiteral("[]"),
                                 QStringLiteral("{}")})
    checkWidths(reduxStroke(invalid), 1.0, 2.0, 4.0,
                QStringLiteral("stroke-invalid-") + invalid);
  const Rendered percentStroke = reduxStroke(QStringLiteral("\"10%\""));
  const qreal percentWidth =
      std::hypot(percentStroke.scene->bounds.width(),
                 percentStroke.scene->bounds.height()) /
      std::sqrt(2.0) * 0.1;
  checkWidths(percentStroke, percentWidth, percentWidth, percentWidth,
              QStringLiteral("stroke-percent"));

  // A complete CSS family list must be used by measurement and paint. A
  // missing first family therefore falls through to the same generic face as
  // the single-family source.
  const QString fontBody = QStringLiteral(
      "timeline\ntitle Font fallback\nA very long task label to measure : event");
  const Rendered fallbackFont = render(
      QStringLiteral("%%{init: {\"themeVariables\":{\"fontFamily\":"
                     "\"DefinitelyMissing,monospace\"}}}%%\n") +
      fontBody);
  const Rendered monoFont = render(
      QStringLiteral("%%{init: {\"themeVariables\":{\"fontFamily\":"
                     "\"monospace\"}}}%%\n") +
      fontBody);
  require(fallbackFont.scene->style.fontFamily ==
              QStringLiteral("DefinitelyMissing,monospace"),
          QStringLiteral("CSS family list was truncated"));
  near(fallbackFont.scene->bounds.width(), monoFont.scene->bounds.width(),
       QStringLiteral("font fallback width"), 0.001);
  near(fallbackFont.scene->bounds.height(), monoFont.scene->bounds.height(),
       QStringLiteral("font fallback height"), 0.001);
  const QSize fallbackSize(qCeil(fallbackFont.scene->bounds.width()),
                           qCeil(fallbackFont.scene->bounds.height()));
  require(fallbackSize == QSize(qCeil(monoFont.scene->bounds.width()),
                                qCeil(monoFont.scene->bounds.height())) &&
              paintScene(*fallbackFont.scene, fallbackSize) ==
                  paintScene(*monoFont.scene, fallbackSize),
          QStringLiteral("font fallback measurement and paint diverged"));

  auto rawThemeFont = [](const QString& key, const QString& raw) {
    return QStringLiteral(
               "%%{init: {\"theme\":\"redux\",\"themeVariables\":{"
               "\"%1\":%2}}}%%\ntimeline\nA")
        .arg(key, raw);
  };
  require(render(rawThemeFont(QStringLiteral("fontFamily"),
                              QStringLiteral("[\"monospace\"]")))
                  .scene->style.fontFamily == QLatin1String("monospace") &&
              render(rawThemeFont(
                         QStringLiteral("fontFamily"),
                         QStringLiteral("[\"DefinitelyMissing\",\"monospace\"]")))
                      .scene->style.fontFamily ==
                  QLatin1String("DefinitelyMissing,monospace") &&
              render(rawThemeFont(QStringLiteral("fontFamily"),
                                  QStringLiteral("[20]")))
                      .scene->style.fontFamily == QLatin1String("Times New Roman") &&
              render(rawThemeFont(QStringLiteral("fontFamily"),
                                  QStringLiteral("{}")))
                      .scene->style.fontFamily == QLatin1String("Times New Roman"),
          QStringLiteral("raw fontFamily source coercion drifted"));
  for (const QString& invalidFamily : {QStringLiteral("20"),
                                       QStringLiteral("true"),
                                       QStringLiteral("false")}) {
    const auto entry = renderEntry(rawThemeFont(QStringLiteral("fontFamily"),
                                                invalidFamily));
    require(entry.status == editor::MermaidRenderStatus::Error &&
                entry.errorMessage.contains(QStringLiteral("str is not iterable")),
            invalidFamily + QStringLiteral(": fontFamily must fail"));
  }
  for (const QString& inheritedSize : {QStringLiteral("[20]"),
                                       QStringLiteral("[]"),
                                       QStringLiteral("{}"),
                                       QStringLiteral("true")})
    near(render(rawThemeFont(QStringLiteral("fontSize"), inheritedSize))
             .scene->style.fontSize,
         16.0, QStringLiteral("fontSize-") + inheritedSize, 0.001);
  near(render(rawThemeFont(QStringLiteral("fontSize"), QStringLiteral("[0]")))
           .scene->style.fontSize,
       0.0, QStringLiteral("fontSize-array-zero"), 0.001);
  near(render(rawThemeFont(QStringLiteral("fontSize"), QStringLiteral("null")))
           .scene->style.fontSize,
       14.0, QStringLiteral("fontSize-null-default"), 0.001);

  const auto rootStyle = [](const QString& textColor) {
    return render(QStringLiteral(
                      "%%{init: {\"themeVariables\":{\"textColor\":%1}}}%%\n"
                      "timeline\nA : event")
                      .arg(textColor))
        .scene->style;
  };
  const QImage markerNone = paintScene(
      isolatedLineScene(rootStyle(QStringLiteral("\"none\"")),
                        QStringLiteral("#ff0000"), 2.0, true),
      QSize(120, 80));
  const QImage markerEmpty = paintScene(
      isolatedLineScene(rootStyle(QStringLiteral("\"\"")),
                        QStringLiteral("#ff0000"), 2.0, true),
      QSize(120, 80));
  const QRect markerWing(90, 34, 12, 5);
  require(opaquePixels(markerNone, markerWing) == 0 &&
              opaquePixels(markerEmpty, markerWing) > 0,
          QStringLiteral("root SVG fill did not control marker paint"));
  const QImage markerOnly = paintScene(
      isolatedLineScene(rootStyle(QStringLiteral("\"#333\"")),
                        QStringLiteral("none"), 2.0, true),
      QSize(120, 80));
  require(opaquePixels(markerOnly, markerWing) > 0 &&
              markerOnly.pixelColor(60, 40).alpha() == 0,
          QStringLiteral("stroke:none incorrectly suppressed the marker"));

  auto isolatedTitle = [&](const QString& textColor) {
    timeline::TimelineScene scene;
    scene.bounds = QRectF(0.0, 0.0, 120.0, 60.0);
    scene.style = rootStyle(textColor);
    scene.titleGeometry.visible = true;
    scene.titleGeometry.text = QStringLiteral("Title");
    scene.titleGeometry.baseline = QPointF(10.0, 30.0);
    scene.titleGeometry.fontSize = 16.0;
    scene.titleGeometry.fill = scene.style.textColor;
    return paintScene(scene, QSize(120, 60));
  };
  require(opaquePixels(isolatedTitle(QStringLiteral("\"none\"")),
                       QRect(0, 0, 120, 60)) == 0 &&
              opaquePixels(isolatedTitle(QStringLiteral("\"\"")),
                           QRect(0, 0, 120, 60)) > 0,
          QStringLiteral("root empty/none title inheritance diverged"));
  require(rootStyle(QStringLiteral("[\"none\"]")).textColor ==
              QStringLiteral("none") &&
              opaquePixels(isolatedTitle(QStringLiteral("[\"none\"]")),
                           QRect(0, 0, 120, 60)) == 0,
          QStringLiteral("singleton array textColor did not stringify"));
  for (const QString& invalid : {
           QStringLiteral("[\"#f00\",\"#0f0\"]"), QStringLiteral("{}")}) {
    const QImage image = isolatedTitle(invalid);
    require(opaquePixels(image, QRect(0, 0, 120, 60)) > 0,
            invalid + QStringLiteral(": invalid root fill did not fall back black"));
  }

  auto paintedLine = [&](const QString& stroke) {
    return paintScene(isolatedLineScene(rootStyle(QStringLiteral("\"#333\"")),
                                        stroke, 2.0, false),
                      QSize(120, 80));
  };
  for (const QString& fallback : {QString(), QStringLiteral("bogus"),
                                  QStringLiteral("revert-layer")}) {
    const QColor pixel = paintedLine(fallback).pixelColor(60, 40);
    require(pixel.alpha() > 0 && pixel.red() < 10 && pixel.green() < 10 &&
                pixel.blue() < 10,
            QStringLiteral("invalid generated line color did not fall back black"));
  }
  for (const QString& noStroke : {QStringLiteral("none"),
                                  QStringLiteral("inherit")})
    require(paintedLine(noStroke).pixelColor(60, 40).alpha() == 0,
            noStroke + QStringLiteral(": generated line should not paint"));

  auto generatedLine = [&](const QString& cssValue) {
    const Rendered produced = render(QStringLiteral(
        "%%{init: {\"themeVariables\":{\"cScaleLabel11\":\"%1\"}}}%%\n"
        "timeline\nA : event").arg(cssValue));
    require(!produced.scene->lines.isEmpty(),
            QStringLiteral("generated-line source emitted no line"));
    return produced.scene->lines.first().stroke;
  };
  require(generatedLine(QStringLiteral("bogus")) == QStringLiteral("bogus"),
          QStringLiteral("invalid generated line declaration was not preserved"));
  require(paintedLine(generatedLine(QStringLiteral("bogus")))
                  .pixelColor(60, 40).alpha() > 0,
          QStringLiteral("source-entry invalid generated line did not paint black"));
  require(paintedLine(generatedLine(QStringLiteral("revert-layer")))
                  .pixelColor(60, 40).alpha() > 0,
          QStringLiteral("source-entry revert-layer did not restore presentation black"));
  for (const QString& noStroke : {QStringLiteral("none"),
                                  QStringLiteral("inherit")})
    require(paintedLine(generatedLine(noStroke)).pixelColor(60, 40).alpha() == 0,
            noStroke + QStringLiteral(": source-entry line should not paint"));

  // CSS brightness applies to the complete event wrapper, including the Neo
  // gradient stroke. Use a uniform gradient so the sampled used colour is
  // deterministic.
  timeline::TimelineScene gradient;
  gradient.bounds = QRectF(0.0, 0.0, 120.0, 80.0);
  gradient.style = rootStyle(QStringLiteral("\"#333\""));
  gradient.style.gradientStart = QStringLiteral("#102030");
  gradient.style.gradientStop = QStringLiteral("#102030");
  timeline::TimelineNodeGeometry event;
  event.kind = timeline::TimelineNodeKind::Event;
  event.position = QPointF(10.0, 10.0);
  event.width = 100.0;
  event.height = 50.0;
  event.rounded = true;
  event.fill = QStringLiteral("none");
  event.stroke = QStringLiteral("none");
  event.strokeWidth = 4.0;
  event.gradientStroke = true;
  event.eventBrightness = true;
  event.dividerVisible = false;
  gradient.nodes.append(event);
  const QColor brightGradient = paintScene(gradient, QSize(120, 80)).pixelColor(50, 10);
  require(brightGradient.alpha() > 0 && brightGradient.red() == 19 &&
              brightGradient.green() == 38 && brightGradient.blue() == 58,
          QStringLiteral("event brightness did not cover the gradient"));

  timeline::TimelineScene transparentShadow;
  transparentShadow.bounds = QRectF(0.0, 0.0, 120.0, 80.0);
  transparentShadow.style = rootStyle(QStringLiteral("\"#333\""));
  transparentShadow.style.themeName = QStringLiteral("redux-color");
  timeline::TimelineNodeGeometry transparentNode;
  transparentNode.position = QPointF(10.0, 10.0);
  transparentNode.width = 100.0;
  transparentNode.height = 50.0;
  transparentNode.rounded = false;
  transparentNode.fill = QStringLiteral("transparent");
  transparentNode.stroke = QStringLiteral("transparent");
  transparentNode.strokeWidth = 2.0;
  transparentNode.dropShadow = true;
  transparentNode.dividerVisible = false;
  transparentShadow.nodes.append(transparentNode);
  require(opaquePixels(paintScene(transparentShadow, QSize(120, 80)),
                       QRect(0, 0, 120, 80)) == 0,
          QStringLiteral("transparent source path emitted a Redux shadow"));

  const Rendered classicReduxShadow = render(QStringLiteral(
      "%%{init: {\"theme\":\"redux\",\"look\":\"classic\"}}%%\n"
      "timeline\nA"));
  require(!classicReduxShadow.scene->nodes.isEmpty() &&
              std::all_of(classicReduxShadow.scene->nodes.cbegin(),
                          classicReduxShadow.scene->nodes.cend(),
                          [](const auto& node) { return node.dropShadow; }),
          QStringLiteral("classic Redux did not attach drop-shadow"));
  const QSize classicSize(qCeil(classicReduxShadow.scene->bounds.width()),
                          qCeil(classicReduxShadow.scene->bounds.height()));
  timeline::TimelineScene classicWithoutShadow = *classicReduxShadow.scene;
  for (auto& node : classicWithoutShadow.nodes) node.dropShadow = false;
  require(differingPixels(paintScene(*classicReduxShadow.scene, classicSize),
                          paintScene(classicWithoutShadow, classicSize)) > 0,
          QStringLiteral("classic Redux drop-shadow produced no pixels"));

  auto shadowAlpha = [&](const QString& fill, const QString& stroke,
                         qreal strokeWidth, QPoint sample) {
    timeline::TimelineScene shadowScene = transparentShadow;
    auto& node = shadowScene.nodes.first();
    node.fill = fill;
    node.stroke = stroke;
    node.strokeWidth = strokeWidth;
    return paintScene(shadowScene, QSize(120, 80))
        .pixelColor(sample)
        .alpha();
  };
  const int opaqueFillShadow =
      shadowAlpha(QStringLiteral("#ff0000"), QStringLiteral("none"), 0.0,
                  QPoint(112, 30));
  const int halfFillShadow = shadowAlpha(
      QStringLiteral("rgba(255,0,0,0.5)"), QStringLiteral("none"), 0.0,
      QPoint(112, 30));
  require(opaqueFillShadow > 0 && halfFillShadow > 0 &&
              std::abs(halfFillShadow * 2 - opaqueFillShadow) <= 2,
          QStringLiteral("fill SourceAlpha did not attenuate drop-shadow"));
  const int opaqueStrokeShadow =
      shadowAlpha(QStringLiteral("none"), QStringLiteral("#ff0000"), 4.0,
                  QPoint(114, 30));
  const int halfStrokeShadow = shadowAlpha(
      QStringLiteral("none"), QStringLiteral("rgba(255,0,0,0.5)"), 4.0,
      QPoint(114, 30));
  require(opaqueStrokeShadow > 0 && halfStrokeShadow > 0 &&
              std::abs(halfStrokeShadow * 2 - opaqueStrokeShadow) <= 2 &&
              shadowAlpha(QStringLiteral("none"),
                          QStringLiteral("rgba(255,0,0,0.5)"), 4.0,
                          QPoint(50, 40)) == 0,
          QStringLiteral("stroke SourceAlpha shadow geometry/intensity drifted"));

  auto reduxWeight = [](const QString& raw) {
    return render(QStringLiteral(
                      "%%{init: {\"theme\":\"redux\","
                      "\"themeVariables\":{\"fontWeight\":%1}}}%%\n"
                      "timeline\nA")
        .arg(raw)).scene->style.nodeFontWeight;
  };
  for (const QString& value : {QStringLiteral("700"), QStringLiteral("[700]"),
                               QStringLiteral("[[700]]"),
                               QStringLiteral("[\"bolder\"]")})
    require(reduxWeight(value) == QFont::Weight(700),
            value + QStringLiteral(": Redux fontWeight stringify"));
  for (const QString& value : {QStringLiteral("[]"), QStringLiteral("[1,2]"),
                               QStringLiteral("{}"), QStringLiteral("true")})
    require(reduxWeight(value) == QFont::Normal,
            value + QStringLiteral(": invalid Redux fontWeight fallback"));
  require(reduxWeight(QStringLiteral("null")) == QFont::Weight(600),
          QStringLiteral("null Redux fontWeight did not retain theme default"));
  require(render(QStringLiteral(
                     "%%{init: {\"themeVariables\":{\"fontWeight\":700}}}%%\n"
                     "timeline\nA"))
              .scene->style.nodeFontWeight == QFont::Normal,
          QStringLiteral("classic Timeline consumed Redux fontWeight"));

  // Timeline's stylesheet calls theme?.includes(). JSON numbers and booleans
  // therefore fail at the renderer boundary instead of falling back default.
  for (const QString& rawTheme : {QStringLiteral("20"), QStringLiteral("true"),
                                  QStringLiteral("false")}) {
    const editor::MermaidRenderEntry entry = renderEntry(
        QStringLiteral("%%{init: {\"theme\":%1}}%%\ntimeline\nA")
            .arg(rawTheme));
    require(entry.status == editor::MermaidRenderStatus::Error &&
                entry.diagnostic.code == QLatin1String("native-render-failed") &&
                entry.diagnostic.message ==
                    QLatin1String("theme?.includes is not a function"),
            rawTheme + QStringLiteral(": raw theme runtime contract"));
  }

  const auto reduxMainBkg = [](const QString& raw) {
    return QStringLiteral(
               "%%{init: {\"theme\":\"redux\",\"look\":\"neo\","
               "\"themeVariables\":{\"mainBkg\":%1}}}%%\n"
               "timeline\nA")
        .arg(raw);
  };
  const Rendered reduxBase = render(QStringLiteral(
      "%%{init: {\"theme\":\"redux\",\"look\":\"neo\"}}%%\n"
      "timeline\nA"));
  for (const QString& falsy : {QStringLiteral("false"), QStringLiteral("0"),
                               QStringLiteral("\"\""), QStringLiteral("null")}) {
    const Rendered value = render(reduxMainBkg(falsy));
    require(value.scene->style.mainBkg == reduxBase.scene->style.mainBkg &&
                value.scene->nodes.first().fill ==
                    reduxBase.scene->nodes.first().fill,
            falsy + QStringLiteral(": falsy mainBkg did not retain default"));
  }
  struct RuntimeFailure {
    QString raw;
    QString message;
  };
  const QVector<RuntimeFailure> mainBkgFailures = {
      {QStringLiteral("[\"#f00\"]"),
       QStringLiteral("Cannot read properties of undefined (reading 'is')")},
      {QStringLiteral("{}"),
       QStringLiteral("Cannot read properties of undefined (reading 'is')")},
      {QStringLiteral("true"),
       QStringLiteral("Cannot create property 'l' on boolean 'true'")},
      {QStringLiteral("20"),
       QStringLiteral("Cannot create property 'l' on number '20'")},
      {QStringLiteral("\"bogus\""),
       QStringLiteral("Unsupported color format: \"bogus\"")},
      {QStringLiteral("\"none\""),
       QStringLiteral("Unsupported color format: \"none\"")},
  };
  for (const RuntimeFailure& expected : mainBkgFailures) {
    const editor::MermaidRenderEntry entry =
        renderEntry(reduxMainBkg(expected.raw));
    require(entry.status == editor::MermaidRenderStatus::Error &&
                entry.diagnostic.code == QLatin1String("native-render-failed") &&
                entry.diagnostic.message == expected.message,
            expected.raw + QStringLiteral(": Redux mainBkg runtime contract"));
  }

  const Rendered borderArray = render(QStringLiteral(
      "%%{init: {\"theme\":\"redux\",\"look\":\"neo\","
      "\"themeVariables\":{\"nodeBorder\":[\"#f00\"]}}}%%\n"
      "timeline\nA"));
  const Rendered borderMulti = render(QStringLiteral(
      "%%{init: {\"theme\":\"redux\",\"look\":\"neo\","
      "\"themeVariables\":{\"nodeBorder\":[\"#f00\",\"#0f0\"]}}}%%\n"
      "timeline\nA"));
  require(borderArray.scene->style.nodeBorder == QLatin1String("#f00") &&
              borderArray.scene->nodes.first().stroke == QLatin1String("#f00") &&
              borderMulti.scene->style.nodeBorder ==
                  QLatin1String("#f00,#0f0") &&
              borderMulti.scene->nodes.first().stroke ==
                  QLatin1String("#f00,#0f0"),
          QStringLiteral("nodeBorder did not use JS CSS stringification"));
  const Rendered gradientArrays = render(QStringLiteral(
      "%%{init: {\"theme\":\"neo\",\"look\":\"neo\","
      "\"themeVariables\":{\"gradientStart\":[\"#f00\"],"
      "\"gradientStop\":[\"#0f0\"]}}}%%\n"
      "timeline\nA"));
  const Rendered gradientInvalid = render(QStringLiteral(
      "%%{init: {\"theme\":\"neo\",\"look\":\"neo\","
      "\"themeVariables\":{\"gradientStart\":[\"#f00\",\"#00f\"],"
      "\"gradientStop\":{}}}}%%\ntimeline\nA"));
  require(gradientArrays.scene->style.gradientStart == QLatin1String("#f00") &&
              gradientArrays.scene->style.gradientStop == QLatin1String("#0f0") &&
              gradientInvalid.scene->style.gradientStart ==
                  QLatin1String("#f00,#00f") &&
              gradientInvalid.scene->style.gradientStop ==
                  QLatin1String("[object Object]"),
          QStringLiteral("gradient endpoints did not use JS CSS stringification"));

  // A NaN top-level config.fontSize poisons SVG path attributes, but invalid
  // paths do not enter getBBox. The first label remains valid and determines a
  // finite root viewBox.
  const Rendered nanLayout = render(QStringLiteral(
      "%%{init: {\"fontSize\":\"abc\","
      "\"themeVariables\":{\"fontFamily\":\"Noto Sans\"}}}%%\n"
      "timeline\nA"));
  require(std::isnan(nanLayout.scene->style.layoutFontSize) &&
              !nanLayout.scene->nodes.isEmpty() &&
              std::isnan(nanLayout.scene->nodes.first().height) &&
              std::isfinite(nanLayout.scene->nodes.first().textBounds.left()) &&
              std::isfinite(nanLayout.scene->bounds.left()) &&
              std::isfinite(nanLayout.scene->bounds.top()) &&
              std::isfinite(nanLayout.scene->bounds.width()) &&
              std::isfinite(nanLayout.scene->bounds.height()) &&
              nanLayout.scene->bounds.width() > 0.0 &&
              nanLayout.scene->bounds.height() > 0.0,
          QStringLiteral("NaN layout font poisoned surviving SVG text bounds"));
  near(nanLayout.scene->contentBounds.x(), 150.0,
       QStringLiteral("NaN layout content x"), 0.001);
  near(nanLayout.scene->contentBounds.y(), 0.0,
       QStringLiteral("NaN layout content y"), 0.001);
  near(nanLayout.scene->contentBounds.width(), 490.0,
       QStringLiteral("NaN layout content width"), 0.001);
  near(nanLayout.scene->contentBounds.height(), 85.28125,
       QStringLiteral("NaN layout content height"), 0.001);
  near(nanLayout.scene->bounds.x(), 100.0,
       QStringLiteral("NaN layout viewBox x"), 0.001);
  near(nanLayout.scene->bounds.y(), -50.0,
       QStringLiteral("NaN layout viewBox y"), 0.001);
  near(nanLayout.scene->bounds.width(), 590.0,
       QStringLiteral("NaN layout viewBox width"), 0.001);
  near(nanLayout.scene->bounds.height(), 185.28125,
       QStringLiteral("NaN layout viewBox height"), 0.001);
  const QSize nanSize(qMax(1, qCeil(nanLayout.scene->bounds.width())),
                      qMax(1, qCeil(nanLayout.scene->bounds.height())));
  require(opaquePixels(paintScene(*nanLayout.scene, nanSize),
                       QRect(QPoint(), nanSize)) > 0,
          QStringLiteral("NaN layout font suppressed surviving text paint"));

  // CSS drop-shadow uses SourceAlpha. A stroke-only Redux-Neo node must cast
  // only an outline shadow, never a filled rectangular interior.
  timeline::TimelineScene strokeShadow = transparentShadow;
  strokeShadow.nodes.first().stroke = QStringLiteral("#ff0000");
  const QImage strokeShadowImage =
      paintScene(strokeShadow, QSize(120, 80));
  require(opaquePixels(strokeShadowImage, QRect(30, 35, 60, 15)) == 0 &&
              opaquePixels(strokeShadowImage, QRect(8, 8, 110, 58)) > 0,
          QStringLiteral("stroke-only Redux shadow filled the node interior"));

  std::fprintf(stderr, "Timeline config parity: 40/40 passed\n");
  return 0;
}
