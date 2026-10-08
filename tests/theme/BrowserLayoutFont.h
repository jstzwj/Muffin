#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QJsonObject>
#include <QHash>

// Fail on missing/different font bytes rather than comparing an OS fallback
// with browser geometry generated on another machine.
inline QHash<QString, QString> browserLayoutFont(const QJsonObject& reference) {
  const auto metadata = reference["font"].toObject();
  const auto files = metadata["files"].toObject();
  if (metadata["family"].toString().isEmpty() || files.size() != 4) qFatal("Missing browser fixture font manifest");
  QString family;
  for (auto it = files.begin(); it != files.end(); ++it) {
    QFile source(QStringLiteral(MUFFIN_SOURCE_DIR "/resources/themes/") + it.key());
    if (!source.open(QIODevice::ReadOnly) ||
        QString::fromLatin1(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256).toHex()) != it.value().toString())
      qFatal("Browser fixture font source differs; regenerate the browser reference");
    QString native = it.key();
    native.replace(".woff2", ".ttf");
    const int id = QFontDatabase::addApplicationFont(QStringLiteral(MUFFIN_BINARY_DIR "/theme-fonts/") + native);
    const auto families = QFontDatabase::applicationFontFamilies(id);
    if (id < 0 || families.isEmpty()) qFatal("Cannot load generated native browser fixture font");
    if (family.isEmpty())
      family = families.front();
    else if (family != families.front())
      qFatal("Browser fixture font variants have different families");
  }
  QFont probe(family);
  probe.setPointSizeF(12);
  if (QFontInfo(probe).family() != family || QFontInfo(probe).pixelSize() != 16)
    qFatal("Browser fixture must use its registered font at 96 logical DPI");
  return {{metadata["family"].toString().toLower(), family}};
}
