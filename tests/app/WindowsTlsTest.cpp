#include "../TestUtils.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSslCertificate>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>

namespace {
QByteArray fixture(const QString& name) {
  QFile file(QStringLiteral(MUFFIN_TLS_FIXTURES) + '/' + name);
  require(file.open(QIODevice::ReadOnly), "open loopback TLS fixture");
  return file.readAll();
}

void loopbackHandshake(QSsl::SslProtocol protocol) {
  const QSslCertificate certificate(fixture("localhost-cert.pem"));
  const QSslKey key(fixture("localhost-key.pem"), QSsl::Rsa);
  require(!certificate.isNull() && !key.isNull(), "load loopback certificate and test-only key");
  QSslConfiguration serverConfig;
  serverConfig.setProtocol(protocol);
  serverConfig.setLocalCertificate(certificate);
  serverConfig.setPrivateKey(key);
  serverConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
  QByteArray request;
  QSslServer server;
  server.setSslConfiguration(serverConfig);
  require(server.listen(QHostAddress::LocalHost), "listen on loopback TLS port");
  QObject::connect(&server, &QSslServer::pendingConnectionAvailable, &server, [&] {
    auto* peer = static_cast<QSslSocket*>(server.nextPendingConnection());
    require(peer && peer->isEncrypted(), "server completes an encrypted handshake");
    auto respond = [&, peer] {
      request += peer->readAll();
      if (request == "ping")
        peer->write("pong");
    };
    QObject::connect(peer, &QSslSocket::readyRead, peer, respond);
    respond();
  });
  QSslConfiguration clientConfig;
  clientConfig.setProtocol(protocol);
  clientConfig.setCaCertificates({certificate});
  clientConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
  QSslSocket client;
  client.setSslConfiguration(clientConfig);
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(&client, &QSslSocket::encrypted, &client, [&] { client.write("ping"); });
  QByteArray response;
  QObject::connect(&client, &QSslSocket::readyRead, &loop, [&] {
    response += client.readAll();
    if (response == "pong")
      loop.quit();
  });
  QObject::connect(&client, &QSslSocket::errorOccurred, &loop, &QEventLoop::quit);
  timeout.start(10000);
  client.connectToHostEncrypted("127.0.0.1", server.serverPort());
  loop.exec();
  require(response == "pong" && client.isEncrypted() && client.peerCertificate() == certificate,
          "verified loopback TLS handshake and round trip: " + client.errorString());
  require(client.sessionProtocol() == protocol, "negotiated the requested TLS version");
  client.abort();
}
} // namespace

// Also run against the deployed plugin directory before publishing. Optional
// HTTPS probes exercise peer verification without making ctest network-dependent.
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  const auto plugin = args.indexOf("--plugins");
  if (plugin >= 0) {
    require(plugin + 1 < args.size(), "plugin directory argument");
    QCoreApplication::setLibraryPaths({QDir(args[plugin + 1]).absolutePath()});
    const auto backends = QSslSocket::availableBackends();
    require(backends.contains("openssl") && backends.contains("schannel"),
            "deployed distribution preserves both OpenSSL and Schannel backends");
  }
  const auto backend = args.indexOf("--backend");
  if (backend >= 0) {
    require(backend + 1 < args.size(), "TLS backend argument");
    require(QSslSocket::setActiveBackend(args[backend + 1]), "select the requested TLS backend");
  }
  require(QSslSocket::supportsSsl(), "Qt selects a functional TLS backend");
  require(!QSslConfiguration::supportedCiphers().isEmpty(), "active TLS backend provides ciphers");
  runTest("verified loopback TLS 1.2", +[] { loopbackHandshake(QSsl::TlsV1_2); });
  if (QSslSocket::isProtocolSupported(QSsl::TlsV1_3))
    runTest("verified loopback TLS 1.3", +[] { loopbackHandshake(QSsl::TlsV1_3); });
  for (int i = 1; i < args.size(); ++i) {
    if (args[i] != "--url")
      continue;
    require(++i < args.size(), "HTTPS URL argument");
    const QUrl url(args[i]);
    require(url.scheme() == "https" && !url.host().isEmpty(), "HTTPS probe URL");
    QSslSocket socket;
    socket.connectToHostEncrypted(url.host(), quint16(url.port(443)));
    const bool encrypted = socket.waitForEncrypted(15000);
    require(encrypted, "verified TLS handshake: " + socket.errorString());
    auto target = url.path(QUrl::FullyEncoded).toUtf8();
    if (target.isEmpty())
      target = "/";
    socket.write("HEAD " + target + " HTTP/1.1\r\nHost: " + url.host().toUtf8() + "\r\nConnection: close\r\n\r\n");
    const bool response = socket.waitForReadyRead(15000) && socket.readLine().startsWith("HTTP/");
    require(response, "HTTPS response: " + socket.errorString());
    qInfo().noquote() << "Verified HTTPS via" << QSslSocket::activeBackend() << url.host();
  }
  return 0;
}
