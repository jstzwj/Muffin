#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <optional>

namespace muffin {

struct UpdateRelease {
  QString version;
  QUrl page;
  QUrl installer;
  QUrl manifest;
  qint64 size = 0;
};

struct UpdatePackage {
  QString version;
  QString architecture;
  QString fileName;
  qint64 size = 0;
  QByteArray sha256;

  static QString architectureName();
  static std::optional<UpdateRelease> release(const QJsonObject& json, const QString& architecture);
  // Verify the exact signed bytes before interpreting any installation metadata.
  static std::optional<UpdatePackage> verify(const QByteArray& envelope, const UpdateRelease& release,
                                             const QString& architecture, const QByteArray& publicKey = {});
  static QByteArray trustedPublicKey();
};

} // namespace muffin
