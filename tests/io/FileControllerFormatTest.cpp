#include "document/DocumentSession.h"
#include "io/FileController.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>

using namespace muffin;

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

void writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), QStringLiteral("Could not create fixture"));
  require(file.write(bytes) == bytes.size(), QStringLiteral("Could not write fixture"));
}

QByteArray readBytes(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not read fixture"));
  return file.readAll();
}

void waitForParse(DocumentSession& session) {
  QElapsedTimer timer;
  timer.start();
  while (session.isAsyncParseInProgress() && timer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(5);
  }
  require(!session.isAsyncParseInProgress(), QStringLiteral("Async parse timed out"));
}

void testExistingLfFileIgnoresNewFileDefault(const QString& path) {
  writeBytes(path, QByteArrayLiteral("alpha\nbeta"));
  DocumentSession session;
  FileController controller;
  require(controller.open(session, nullptr, path), QStringLiteral("Could not open LF file"));
  waitForParse(session);
  require(session.fileFormat().lineEnding == TextLineEnding::Lf, QStringLiteral("LF was not detected"));
  session.setMarkdownText(QStringLiteral("alpha\nchanged"), true);
  require(controller.save(session, nullptr) == SaveOutcome::Saved, QStringLiteral("Could not save LF file"));
  const QByteArray saved = readBytes(path);
  require(!saved.contains("\r\n"), QStringLiteral("Existing LF file was converted to CRLF"));
  require(!saved.endsWith('\n'), QStringLiteral("Existing no-newline file gained a trailing newline"));
}

void testLegacyEncodingRoundTrips(const QString& path) {
  writeBytes(path, QByteArray("caf\xe9\r\nfin", 9));
  DocumentSession session;
  FileController controller;
  require(controller.open(session, nullptr, path), QStringLiteral("Could not open legacy file"));
  waitForParse(session);
  const QString decoded = session.markdownText().toString();
  const QString encoding = session.fileFormat().encodingName;
  require(decoded.startsWith(QStringLiteral("caf\u00e9")),
          QStringLiteral("Legacy text was not decoded: encoding=%1, utf8=%2")
              .arg(encoding, QString::fromLatin1(decoded.toUtf8().toHex())));
  require(encoding.compare(QStringLiteral("windows-1252"), Qt::CaseInsensitive) == 0,
          QStringLiteral("Legacy file was classified as %1 instead of windows-1252").arg(encoding));
  session.setMarkdownText(QStringLiteral("caf\u00e9\nfin"), true);
  require(controller.save(session, nullptr) == SaveOutcome::Saved,
          QStringLiteral("Could not save legacy file"));
  const QByteArray saved = readBytes(path);
  require(saved.contains(QByteArray("\xe9", 1)), QStringLiteral("Legacy encoding was not preserved"));
  require(!saved.contains(QByteArray::fromHex("c3a9")), QStringLiteral("Legacy file was silently converted to UTF-8"));
  require(saved.contains("\r\n"), QStringLiteral("CRLF was not preserved"));
}

void testWindows1252PunctuationRoundTrips(const QString& path) {
  QByteArray raw("say ", 4);
  raw.append(QByteArray::fromHex("93"));
  raw.append("hello");
  raw.append(QByteArray::fromHex("94"));
  writeBytes(path, raw);

  DocumentSession session;
  FileController controller;
  require(controller.open(session, nullptr, path), QStringLiteral("Could not open Windows-1252 file"));
  waitForParse(session);
  require(session.markdownText().toString() == QStringLiteral("say \u201chello\u201d"),
          QStringLiteral("Windows-1252 punctuation was not decoded deterministically"));
  require(controller.reopenWithEncoding(session, nullptr, QStringLiteral("windows-1252")),
          QStringLiteral("Could not reopen Windows-1252 file explicitly"));
  waitForParse(session);
  require(session.markdownText().toString() == QStringLiteral("say \u201chello\u201d"),
          QStringLiteral("Explicit Windows-1252 decoding was not deterministic"));
  session.setMarkdownText(session.markdownText().toString(), true);
  require(controller.save(session, nullptr) == SaveOutcome::Saved,
          QStringLiteral("Could not save Windows-1252 file"));
  require(readBytes(path) == raw, QStringLiteral("Windows-1252 punctuation did not round-trip"));
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTests"));
  QCoreApplication::setApplicationName(QStringLiteral("FileControllerFormatTest"));
  QSettings().clear();
  QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 1);
  QSettings().setValue(QStringLiteral("editor/trailingNewline"), true);

  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("Temp dir invalid"));
  testExistingLfFileIgnoresNewFileDefault(dir.filePath(QStringLiteral("lf.md")));
  testLegacyEncodingRoundTrips(dir.filePath(QStringLiteral("legacy.md")));
  testWindows1252PunctuationRoundTrips(dir.filePath(QStringLiteral("windows1252.md")));
  return 0;
}
