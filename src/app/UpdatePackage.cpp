#include "app/UpdatePackage.h"
#include "UpdateTrust.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QJsonArray>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
constexpr qint64 kMaxInstallerSize = 1024LL * 1024 * 1024;

bool validVersion(const QString& version) {
  static const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"));
  return version.size() <= 32 && pattern.match(version).hasMatch();
}

bool releaseUrl(const QUrl& url, const QString& path) {
  return url.scheme() == QStringLiteral("https") && url.host() == QStringLiteral("github.com") && url.port(-1) == -1 &&
         url.userInfo().isEmpty() && url.query().isEmpty() && url.fragment().isEmpty() && url.path() == path;
}

bool verifySignature(const QByteArray& payload, const QByteArray& signature, const QByteArray& publicKey) {
#ifdef Q_OS_WIN
  if (publicKey.size() != 72 || signature.size() != 64)
    return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0)
    return false;
  BCRYPT_KEY_HANDLE key = nullptr;
  const auto imported = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                                            reinterpret_cast<PUCHAR>(const_cast<char*>(publicKey.constData())),
                                            ULONG(publicKey.size()), 0);
  bool valid = false;
  if (imported >= 0) {
    const auto digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    valid = BCryptVerifySignature(
                key, nullptr, reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())), ULONG(digest.size()),
                reinterpret_cast<PUCHAR>(const_cast<char*>(signature.constData())), ULONG(signature.size()), 0) >= 0;
    BCryptDestroyKey(key);
  }
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return valid;
#else
  Q_UNUSED(payload);
  Q_UNUSED(signature);
  Q_UNUSED(publicKey);
  return false;
#endif
}
} // namespace

QString muffin::UpdatePackage::architectureName() {
#if defined(Q_PROCESSOR_ARM_64)
  return QStringLiteral("arm64");
#elif defined(Q_PROCESSOR_X86_64)
  return QStringLiteral("x64");
#else
  return {};
#endif
}

QByteArray muffin::UpdatePackage::trustedPublicKey() {
  return QByteArray::fromHex(MUFFIN_UPDATE_PUBLIC_KEY_HEX);
}

std::optional<muffin::UpdateRelease> muffin::UpdatePackage::release(const QJsonObject& json,
                                                                    const QString& architecture) {
  const QString tag = json.value(QStringLiteral("tag_name")).toString();
  const QString version = tag.startsWith(u'v') ? tag.mid(1) : QString();
  if (!validVersion(version) || json.value(QStringLiteral("draft")).toBool() ||
      json.value(QStringLiteral("prerelease")).toBool() ||
      (architecture != QStringLiteral("x64") && architecture != QStringLiteral("arm64")))
    return std::nullopt;
  UpdateRelease result;
  result.version = version;
  result.page = QUrl(json.value(QStringLiteral("html_url")).toString());
  if (!releaseUrl(result.page, QStringLiteral("/jstzwj/Muffin/releases/tag/") + tag))
    return std::nullopt;
  const auto stem = QStringLiteral("Muffin-Setup-%1-windows-%2").arg(version, architecture);
  const QString prefix = QStringLiteral("/jstzwj/Muffin/releases/download/%1/").arg(tag);
  for (const auto& value : json.value(QStringLiteral("assets")).toArray()) {
    const auto asset = value.toObject();
    const QString name = asset.value(QStringLiteral("name")).toString();
    const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
    if (!releaseUrl(url, prefix + name))
      continue;
    if (name == stem + QStringLiteral(".exe")) {
      if (!result.installer.isEmpty())
        return std::nullopt;
      result.installer = url;
      result.size = asset.value(QStringLiteral("size")).toInteger();
    } else if (name == stem + QStringLiteral(".update.json")) {
      if (!result.manifest.isEmpty())
        return std::nullopt;
      result.manifest = url;
    }
  }
  if (result.installer.isEmpty() || result.manifest.isEmpty() || result.size <= 0 || result.size > kMaxInstallerSize)
    return std::nullopt;
  return result;
}

std::optional<muffin::UpdatePackage> muffin::UpdatePackage::verify(const QByteArray& envelope,
                                                                   const UpdateRelease& release,
                                                                   const QString& architecture,
                                                                   const QByteArray& publicKey) {
  if (envelope.size() > 16384)
    return std::nullopt;
  const auto object = QJsonDocument::fromJson(envelope).object();
  const auto payload = QByteArray::fromBase64Encoding(object.value(QStringLiteral("payload")).toString().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
  const auto signature = QByteArray::fromBase64Encoding(object.value(QStringLiteral("signature")).toString().toLatin1(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
  if (!payload || !signature ||
      !verifySignature(payload.decoded, signature.decoded, publicKey.isEmpty() ? trustedPublicKey() : publicKey))
    return std::nullopt;
  const auto metadata = QJsonDocument::fromJson(payload.decoded).object();
  UpdatePackage result;
  result.version = metadata.value(QStringLiteral("version")).toString();
  result.architecture = metadata.value(QStringLiteral("architecture")).toString();
  result.fileName = metadata.value(QStringLiteral("fileName")).toString();
  result.size = metadata.value(QStringLiteral("size")).toInteger();
  const QByteArray hash = metadata.value(QStringLiteral("sha256")).toString().toLatin1();
  result.sha256 = QByteArray::fromHex(hash);
  if (metadata.value(QStringLiteral("schema")).toInt() != 1 || !validVersion(result.version) ||
      result.version != release.version || result.architecture != architecture || result.size != release.size ||
      result.size <= 0 || result.size > kMaxInstallerSize || hash.size() != 64 || result.sha256.toHex() != hash ||
      result.fileName != QStringLiteral("Muffin-Setup-%1-windows-%2.exe").arg(result.version, architecture))
    return std::nullopt;
  return result;
}
