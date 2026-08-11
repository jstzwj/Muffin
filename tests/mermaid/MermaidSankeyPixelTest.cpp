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
[[noreturn]] void fail(const QString &s) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(s));
  std::exit(1);
}
void req(bool v, const QString &s) {
  if (!v)
    fail(s);
}
QByteArray sha(const QString &p) {
  QFile f(p);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256)
      .toHex();
}
QImage decode(const QString &u) {
  QImage i;
  i.loadFromData(QByteArray::fromBase64(u.mid(u.indexOf(',') + 1).toLatin1()),
                 "PNG");
  return i.convertToFormat(QImage::Format_RGBA8888);
}
double iou(const QImage &a, const QImage &b) {
  qint64 n = 0, d = 0;
  for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width(); ++x) {
      bool p = a.pixelColor(x, y).alpha() >= 32,
           q = b.pixelColor(x, y).alpha() >= 32;
      n += p && q;
      d += p || q;
    }
  return d ? double(n) / d : 1;
}
double rgba(const QImage &a, const QImage &b) {
  double d = 0;
  qint64 n = 0;
  for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width(); ++x) {
      auto p = a.pixelColor(x, y), q = b.pixelColor(x, y);
      if (p.alpha() < 16 && q.alpha() < 16)
        continue;
      d += std::abs(p.red() * p.alpha() / 255 - q.red() * q.alpha() / 255) +
           std::abs(p.green() * p.alpha() / 255 - q.green() * q.alpha() / 255) +
           std::abs(p.blue() * p.alpha() / 255 - q.blue() * q.alpha() / 255) +
           std::abs(p.alpha() - q.alpha());
      ++n;
    }
  return n ? 1 - d / (n * 1020) : 1;
}
} // namespace
int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  req(argc == 2, "manifest");
  QString mp = QString::fromLocal8Bit(argv[1]);
  QFile f(mp);
  req(f.open(QIODevice::ReadOnly), f.errorString());
  QByteArray bytes = f.readAll();
  req(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
          QByteArrayLiteral("2eeeb9791cb2d4d58c697a522e0bf82ab0a9de7ea6023cfa9c"
                            "79ad04559abb2a"),
      "bytes");
  auto root = QJsonDocument::fromJson(bytes).object();
  QDir dir = QFileInfo(mp).dir();
  for (const auto &v : root.value("cases").toArray()) {
    auto c = v.toObject();
    QString id = c.value("id").toString(),
            p = dir.filePath(c.value("file").toString());
    req(sha(p) == c.value("sha256").toString().toLatin1(), id + " sha");
    QImage ref(p),
        nat = decode(editor::MermaidRenderCache::renderMermaidSourceToPng(
                         c.value("source").toString(), 1)
                         .dataUrl);
    req(ref.size() == nat.size(), QStringLiteral("%1 size %2x%3 != %4x%5")
                                      .arg(id)
                                      .arg(nat.width())
                                      .arg(nat.height())
                                      .arg(ref.width())
                                      .arg(ref.height()));
    double ai = iou(nat, ref), rs = rgba(nat, ref);
    std::fprintf(stderr, "%s iou=%.5f rgba=%.5f\n", qPrintable(id), ai, rs);
    req(ai >= .91, id + " iou");
    req(rs >= .94, id + " rgba");
  }
}
