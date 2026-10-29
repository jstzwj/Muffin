#include "app/UpdatePackage.h"
#include "app/WindowsUpdater.h"
#include "../TestUtils.h"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#ifdef Q_OS_WIN
#include <bcrypt.h>
#endif

namespace {
void waitUntil(const std::function<bool()>& condition) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!condition() && elapsed.elapsed() < 10000) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
  }
  require(condition(), "asynchronous update operation completes");
}

QJsonObject releaseJson() {
  const QString name = QStringLiteral("Muffin-Setup-9.8.7-windows-x64");
  const auto asset = [&](const QString& suffix) {
    return QJsonObject{{QStringLiteral("name"), name + suffix},
                       {QStringLiteral("size"), 1024},
                       {QStringLiteral("browser_download_url"),
                        QStringLiteral("https://github.com/jstzwj/Muffin/releases/download/v9.8.7/") + name + suffix}};
  };
  return {{QStringLiteral("tag_name"), QStringLiteral("v9.8.7")},
          {QStringLiteral("html_url"), QStringLiteral("https://github.com/jstzwj/Muffin/releases/tag/v9.8.7")},
          {QStringLiteral("assets"), QJsonArray{asset(QStringLiteral(".exe")), asset(QStringLiteral(".update.json"))}}};
}

void assetSelection() {
  auto json = releaseJson();
  require(muffin::UpdatePackage::release(json, QStringLiteral("x64")).has_value(),
          "exact version and architecture selected");
  require(!muffin::UpdatePackage::release(json, QStringLiteral("arm64")),
          "never fall back to an incompatible architecture");
  auto duplicate = json.value(QStringLiteral("assets")).toArray();
  duplicate.append(duplicate.first());
  json.insert(QStringLiteral("assets"), duplicate);
  require(!muffin::UpdatePackage::release(json, QStringLiteral("x64")), "ambiguous update assets rejected");
  for (const auto& bad : {QStringLiteral("v9.8.7-beta"), QStringLiteral("v9.8.7junk"), QStringLiteral("v09.8.7")}) {
    json = releaseJson();
    json.insert(QStringLiteral("tag_name"), bad);
    require(!muffin::UpdatePackage::release(json, QStringLiteral("x64")), "nonstable and malformed versions rejected");
  }
  json = releaseJson();
  json.insert(QStringLiteral("html_url"), QStringLiteral("https://github.com/attacker/Muffin/releases/tag/v9.8.7"));
  require(!muffin::UpdatePackage::release(json, QStringLiteral("x64")),
          "release must belong to the official repository");
}

#ifdef Q_OS_WIN
void signatureValidation() {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) >= 0, "open CNG signer");
  BCRYPT_KEY_HANDLE key = nullptr;
  require(BCryptGenerateKeyPair(algorithm, &key, 256, 0) >= 0 && BCryptFinalizeKeyPair(key, 0) >= 0,
          "generate ephemeral test key, separate from release signing");
  ULONG size = 0;
  QByteArray publicKey(72, '\0');
  require(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<PUCHAR>(publicKey.data()),
                          ULONG(publicKey.size()), &size, 0) >= 0,
          "export test public key");
  const auto release = *muffin::UpdatePackage::release(releaseJson(), QStringLiteral("x64"));
  const QJsonObject metadata{{QStringLiteral("schema"), 1},
                             {QStringLiteral("version"), release.version},
                             {QStringLiteral("architecture"), QStringLiteral("x64")},
                             {QStringLiteral("fileName"), QStringLiteral("Muffin-Setup-9.8.7-windows-x64.exe")},
                             {QStringLiteral("size"), 1024},
                             {QStringLiteral("sha256"), QString(64, u'a')}};
  const auto envelope = [&](const QJsonObject& object, bool tamper = false) {
    auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    auto digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    QByteArray signature(64, '\0');
    require(BCryptSignHash(key, nullptr, reinterpret_cast<PUCHAR>(digest.data()), ULONG(digest.size()),
                           reinterpret_cast<PUCHAR>(signature.data()), ULONG(signature.size()), &size, 0) >= 0,
            "sign fixture payload");
    if (tamper)
      payload.append(' ');
    return QJsonDocument(QJsonObject{{QStringLiteral("payload"), QString::fromLatin1(payload.toBase64())},
                                     {QStringLiteral("signature"), QString::fromLatin1(signature.toBase64())}})
        .toJson(QJsonDocument::Compact);
  };
  require(muffin::UpdatePackage::verify(envelope(metadata), release, QStringLiteral("x64"), publicKey).has_value(),
          "valid CNG P1363 signature accepted");
  require(!muffin::UpdatePackage::verify(envelope(metadata, true), release, QStringLiteral("x64"), publicKey),
          "even a whitespace mutation of signed bytes rejected");
  require(!muffin::UpdatePackage::verify(envelope(metadata), release, QStringLiteral("x64")),
          "a valid signature from a different publisher is rejected");
  for (const auto& field : {QStringLiteral("version"), QStringLiteral("architecture"), QStringLiteral("fileName"),
                            QStringLiteral("sha256"), QStringLiteral("size"), QStringLiteral("schema")}) {
    auto bad = metadata;
    bad.insert(field, field == QStringLiteral("size") || field == QStringLiteral("schema")
                          ? QJsonValue(2)
                          : QJsonValue(QStringLiteral("wrong")));
    require(!muffin::UpdatePackage::verify(envelope(bad), release, QStringLiteral("x64"), publicKey),
            "correctly signed but inconsistent metadata rejected");
  }
  BCryptDestroyKey(key);
  BCryptCloseAlgorithmProvider(algorithm, 0);
}
#endif

class HttpFixture {
public:
  QTcpServer server;
  QByteArray response;
  explicit HttpFixture(QByteArray body, int status = 200, int declared = -1) {
    require(server.listen(QHostAddress::LocalHost), "local download fixture listens");
    response = "HTTP/1.1 " + QByteArray::number(status) +
               " Test\r\nContent-Length: " + QByteArray::number(declared < 0 ? body.size() : declared) +
               "\r\nConnection: close\r\n\r\n" + body;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
      auto* socket = server.nextPendingConnection();
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
        socket->readAll();
        socket->write(response);
        socket->disconnectFromHost();
      });
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
  }
  QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/update").arg(server.serverPort())); }
};
} // namespace

namespace muffin {
struct WindowsUpdaterTestAccess {
  static void downloads() {
    const QByteArray body(128 * 1024, 'x');
    for (int scenario = 0; scenario != 4; ++scenario) {
      HttpFixture server(body, scenario == 3 ? 404 : 200, scenario == 2 ? body.size() * 2 : -1);
      WindowsUpdater updater;
      updater.release_ = {QStringLiteral("9.8.7"), {}, server.url(), {}, body.size()};
      updater.package_ = UpdatePackage{
          QStringLiteral("9.8.7"),
          QStringLiteral("x64"),
          {},
          body.size(),
          QCryptographicHash::hash(scenario == 1 ? QByteArray("wrong") : body, QCryptographicHash::Sha256)};
      updater.state_ = WindowsUpdater::State::Downloading;
      updater.downloadInstaller();
      waitUntil([&] { return updater.state_ != WindowsUpdater::State::Downloading; });
      require(updater.state_ == (scenario == 0 ? WindowsUpdater::State::Ready : WindowsUpdater::State::Failed),
              "only a complete, matching HTTP 200 download becomes installable");
      if (updater.dialog_)
        delete updater.dialog_.data();
    }
    WindowsUpdater updater;
    HttpFixture oversized(QByteArray(20000, 'a'));
    updater.state_ = WindowsUpdater::State::Downloading;
    bool completed = false;
    updater.get(oversized.url(), 16384, [&](const QByteArray&) { completed = true; });
    waitUntil([&] { return updater.state_ == WindowsUpdater::State::Failed; });
    require(!completed, "oversized manifest never reaches verification");
    updater.state_ = WindowsUpdater::State::Downloading;
    updater.get(oversized.url(), 16384, [&](const QByteArray&) { completed = true; });
    updater.cancel();
    QCoreApplication::processEvents();
    require(!completed && updater.state_ == WindowsUpdater::State::Available, "cancel cannot race into installation");
  }
};
} // namespace muffin

void runWindowsUpdateTests() {
  runTest("assetSelection", assetSelection);
#ifdef Q_OS_WIN
  runTest("signatureValidation", signatureValidation);
#endif
  runTest("downloads", muffin::WindowsUpdaterTestAccess::downloads);
}
