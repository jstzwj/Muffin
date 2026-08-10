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
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }
QByteArray sha256(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()
             : QByteArray();
}
QImage decode(const QString& url) {
  QImage image;
  const qsizetype comma = url.indexOf(QLatin1Char(','));
  if (comma >= 0)
    image.loadFromData(QByteArray::fromBase64(url.mid(comma + 1).toLatin1()), "PNG");
  return image;
}
qreal alphaIou(const QImage& a, const QImage& b) {
  int intersection = 0, united = 0;
  for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width(); ++x) {
      const bool aa = a.pixelColor(x, y).alpha() >= 32;
      const bool bb = b.pixelColor(x, y).alpha() >= 32;
      intersection += aa && bb;
      united += aa || bb;
    }
  return united ? qreal(intersection) / united : 1.0;
}
qreal rgbaSimilarity(const QImage& a, const QImage& b) {
  qreal difference = 0.0;
  int count = 0;
  for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width(); ++x) {
      const QColor x1 = a.pixelColor(x, y), x2 = b.pixelColor(x, y);
      if (x1.alpha() < 32 && x2.alpha() < 32) continue;
      ++count;
      const auto premul = [](int c, int alpha) { return c * alpha / 255; };
      difference += std::abs(premul(x1.red(), x1.alpha()) - premul(x2.red(), x2.alpha()));
      difference += std::abs(premul(x1.green(), x1.alpha()) - premul(x2.green(), x2.alpha()));
      difference += std::abs(premul(x1.blue(), x1.alpha()) - premul(x2.blue(), x2.alpha()));
      difference += std::abs(x1.alpha() - x2.alpha());
    }
  return count ? 1.0 - difference / (count * 1020.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Gantt pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("461864a1105aa8d855e0b73d8c357c9201e111500e7150494260818fa9479ec2"),
          QStringLiteral("Gantt pixel fixture changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 4, QStringLiteral("Expected four Gantt pixel cases"));
  const QDir dir = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString path = dir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(path) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(" browser PNG drifted"));
    const QImage reference(path);
    const QImage native = decode(editor::MermaidRenderCache::renderMermaidSourceToPng(
                                     fixture.value(QStringLiteral("source")).toString(), 1.0)
                                     .dataUrl);
    require(!native.isNull() && !reference.isNull(), id + QStringLiteral(" PNG decode"));
    require(native.size() == reference.size(),
            QStringLiteral("%1: native %2x%3 != browser %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(reference.width()).arg(reference.height()));
    const qreal iou = alphaIou(native, reference);
    const qreal rgba = rgbaSimilarity(native, reference);
    std::fprintf(stderr, "%s iou=%.6f rgba=%.6f\n", qPrintable(id), iou, rgba);
    require(iou >= 0.78, id + QStringLiteral(" alpha IoU"));
    require(rgba >= 0.76, id + QStringLiteral(" RGBA similarity"));
  }
  return 0;
}
