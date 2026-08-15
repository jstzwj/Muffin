#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

void require(bool value, const QString& message) {
  if (!value) fail(message);
}

QImage render(const QString& source) {
  const QString url = editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1).dataUrl;
  QImage image;
  image.loadFromData(QByteArray::fromBase64(
                         url.mid(url.indexOf(QLatin1Char(',')) + 1).toLatin1()),
                     "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}

qreal iou(const QImage& actual, const QImage& expected) {
  qint64 both = 0, either = 0;
  for (int y = 0; y < expected.height(); ++y) {
    for (int x = 0; x < expected.width(); ++x) {
      const bool a = actual.pixelColor(x, y).alpha() >= 32;
      const bool b = expected.pixelColor(x, y).alpha() >= 32;
      both += a && b;
      either += a || b;
    }
  }
  return either ? qreal(both) / either : 1.0;
}

qreal rgbaScore(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  qint64 count = 0;
  for (int y = 0; y < expected.height(); ++y) {
    for (int x = 0; x < expected.width(); ++x) {
      const QColor a = actual.pixelColor(x, y);
      const QColor b = expected.pixelColor(x, y);
      if (a.alpha() < 16 && b.alpha() < 16) continue;
      difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                    std::abs(a.blue() - b.blue()) + std::abs(a.alpha() - b.alpha());
      ++count;
    }
  }
  return count ? 1.0 - difference / (count * 1020.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Swimlane pixel manifest"));

  const QFileInfo info(QString::fromLocal8Bit(argv[1]));
  QFile file(info.absoluteFilePath());
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("3efec5ee5f79a114e6a152b3300829a1443b348b6a12cd0c75c438fbe2f8e9b6"),
          QStringLiteral("Swimlane manifest bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("b23e1c605c700bcbb9d30e479e236e6752da23ba17d2d291efbb533a8362777f"),
          QStringLiteral("Swimlane pixel provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 5, QStringLiteral("case count"));

  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    QFile reference(info.absolutePath() + QLatin1Char('/') +
                    fixture.value(QStringLiteral("file")).toString());
    require(reference.open(QIODevice::ReadOnly), id + QStringLiteral("/reference"));
    const QByteArray png = reference.readAll();
    require(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex() ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral("/sha"));
    QImage expected;
    expected.loadFromData(png, "PNG");
    const QImage actual = render(fixture.value(QStringLiteral("source")).toString());
    require(!actual.isNull(), id + QStringLiteral("/native"));
    require(actual.size() == expected.size(),
            QStringLiteral("%1/size %2x%3 != %4x%5")
                .arg(id).arg(actual.width()).arg(actual.height())
                .arg(expected.width()).arg(expected.height()));
    const qreal alphaIou = iou(actual, expected);
    const qreal score = rgbaScore(actual, expected);
    std::fprintf(stderr, "%s IoU %.5f RGBA %.5f\n", qPrintable(id), alphaIou, score);
    require(alphaIou >= 0.90, id + QStringLiteral("/IoU"));
    require(score >= 0.92, id + QStringLiteral("/RGBA"));
  }

  std::puts("MermaidSwimlanePixelTest: 5/5 passed");
  return 0;
}
