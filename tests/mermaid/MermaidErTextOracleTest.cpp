// Oracle for the widths of an erDiagram text against real Mermaid 11.16.0.
//
// tests/fixtures/mermaid/er-text.json is captured by
// scripts/generate_mermaid_er_text_fixture.mjs in the bundled Noto fonts,
// which is also what the port measures text in, so nothing here depends on
// the machine's fonts. Each case gives one string and the three widths
// Chrome reports for it at 16px: the HTML inline box erBox lays an HTML label
// out by, and SVGTextElement.getBBox() as erBox measures the text right after
// it is appended and again after the layout. Every number is asserted, for
// er::measureErText (HTML and SVG) and er::measureErSvgTextDrawnBox.

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/erdiagram/ErLayout.h"
#include "mermaid/scene/ParityDiff.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

constexpr qreal kGeometry = 0.05; // every coordinate and size (px)
constexpr parity::Tier kTier{kGeometry};

QStringList failures;

void require(bool condition, const QString& message) {
  if (!condition)
    failures << message;
}

void compareBox(QStringList& out, const QRectF& native, const QJsonObject& reference, const QString& path) {
  static const char* const keys[4] = {"x", "y", "width", "height"};
  const qreal values[4] = {native.x(), native.y(), native.width(), native.height()};
  for (int i = 0; i < 4; ++i)
    out += parity::compareNumber(values[i], reference.value(QLatin1String(keys[i])).toDouble(), kTier,
                                 path + QLatin1Char('/') + QLatin1String(keys[i]));
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected ER text fixture path"));
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
              QByteArrayLiteral("53a6a24542a42f4a260591069728ea3685011863505844d45a70a48ac9e4a1af"),
          QStringLiteral("ER text fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("ER text: mermaidVersion drifted"));
  require(root.value(QStringLiteral("fontSize")).toInt() == 16, QStringLiteral("ER text: fontSize drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("ffa17db0e257437a331b43c93a2791a59c70c7516b587d733b1a10de86c32c3d"),
          QStringLiteral("ER text fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 118, QStringLiteral("ER text case count drifted: %1").arg(cases.size()));

  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString text = fixture.value(QStringLiteral("text")).toString();
    // The stylesheet's font: the theme's family at 16px.
    const er::ErTextStyle style;
    const er::ErTextMetrics html = er::measureErText(text, style, true, 0.0);
    failures += parity::compareNumber(html.size().width(), fixture.value(QStringLiteral("html")).toDouble(), kTier,
                                      text + QStringLiteral("/html/width"));
    const er::ErTextMetrics svg = er::measureErText(text, style, false, 0.0);
    compareBox(failures, svg.box, fixture.value(QStringLiteral("insert")).toObject(), text + QStringLiteral("/insert"));
    compareBox(failures, er::measureErSvgTextDrawnBox(svg.document, style),
               fixture.value(QStringLiteral("drawn")).toObject(), text + QStringLiteral("/drawn"));
  }

  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidErTextOracleTest: %lld divergence(s) from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::printf("MermaidErTextOracleTest passed: %lld cases, every HTML, SVG and drawn box within %0.2f px\n",
              static_cast<long long>(cases.size()), kGeometry);
  return 0;
}
