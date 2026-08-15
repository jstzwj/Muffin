#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QDir>
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
void require(bool value, const QString& message) { if (!value) fail(message); }
QByteArray sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}
QImage nativePng(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1).dataUrl;
  QImage image;
  image.loadFromData(QByteArray::fromBase64(
      dataUrl.mid(dataUrl.indexOf(QLatin1Char(',')) + 1).toLatin1()), "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}
qreal alphaIou(const QImage& actual, const QImage& expected) {
  qint64 intersection = 0;
  qint64 unionArea = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const bool lhs = actual.pixelColor(x, y).alpha() >= 32;
      const bool rhs = expected.pixelColor(x, y).alpha() >= 32;
      intersection += lhs && rhs;
      unionArea += lhs || rhs;
    }
  return unionArea ? qreal(intersection) / unionArea : 1.0;
}
qreal rgbaScore(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  qint64 count = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const QColor lhs = actual.pixelColor(x, y);
      const QColor rhs = expected.pixelColor(x, y);
      if (lhs.alpha() < 16 && rhs.alpha() < 16) continue;
      difference += std::abs(lhs.red() * lhs.alpha() / 255 -
                             rhs.red() * rhs.alpha() / 255) +
                    std::abs(lhs.green() * lhs.alpha() / 255 -
                             rhs.green() * rhs.alpha() / 255) +
                    std::abs(lhs.blue() * lhs.alpha() / 255 -
                             rhs.blue() * rhs.alpha() / 255) +
                    std::abs(lhs.alpha() - rhs.alpha());
      ++count;
    }
  return count ? 1.0 - difference / (count * 1020.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Architecture pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("43831ab5ef735b1fd5ef0c90984122fa36d6d706af6d99728048eecd2daad0d8"),
          QStringLiteral("Architecture pixel manifest bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("63a292d35ae2fb97b1d9d552c04ce10f8a46e43bec4d82717240febd8ddfe238"),
          QStringLiteral("Architecture pixel provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 4, QStringLiteral("Architecture pixel case count"));
  const QDir directory = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString referencePath =
        directory.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(referencePath) ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral("/png-sha"));
    const QImage expected(referencePath);
    const QImage actual =
        nativePng(fixture.value(QStringLiteral("source")).toString());
    require(!actual.isNull(), id + QStringLiteral("/native-png"));
    require(actual.size() == expected.size(),
            QStringLiteral("%1/size %2x%3 != %4x%5")
                .arg(id).arg(actual.width()).arg(actual.height())
                .arg(expected.width()).arg(expected.height()));
    const qreal iou = alphaIou(actual, expected);
    {  // TEMP DEBUG
      QDir temp(qEnvironmentVariable("TEMP"));
      actual.save(temp.filePath(id + QStringLiteral("-native.png")));
      expected.save(temp.filePath(id + QStringLiteral("-browser.png")));
    }
    const qreal rgba = rgbaScore(actual, expected);
    std::fprintf(stderr, "%s IoU %.5f RGBA %.5f\n", qPrintable(id), iou, rgba);
    require(iou >= .88, id + QStringLiteral("/alpha-IoU"));
    require(rgba >= .90, id + QStringLiteral("/RGBA"));
  }
  std::puts("MermaidArchitecturePixelTest: 4/4 passed");
  return 0;
}
