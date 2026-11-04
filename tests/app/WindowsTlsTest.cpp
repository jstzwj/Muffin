#include "../TestUtils.h"
#include <QCoreApplication>
#include <QDir>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>

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
  require(QSslSocket::supportsSsl(), "Qt selects a functional TLS backend");
  require(!QSslConfiguration::supportedCiphers().isEmpty(), "active TLS backend provides ciphers");
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
