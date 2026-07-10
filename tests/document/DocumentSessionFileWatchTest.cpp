#include "document/DocumentSession.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
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

void atomicWrite(const QString& path, const QByteArray& data) {
  QSaveFile file(path);
  require(file.open(QIODevice::WriteOnly), QStringLiteral("Could not open atomic writer"));
  require(file.write(data) == data.size(), QStringLiteral("Could not write replacement"));
  require(file.commit(), QStringLiteral("Could not commit replacement"));
}

bool waitForCount(int& count, int expected) {
  QElapsedTimer timer;
  timer.start();
  while (count < expected && timer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(10);
  }
  return count >= expected;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("Temp dir invalid"));
  const QString path = dir.filePath(QStringLiteral("watched.md"));
  atomicWrite(path, QByteArrayLiteral("one"));

  DocumentSession session;
  session.setFilePath(path);
  session.recordFileBaseline();
  int changes = 0;
  QObject::connect(&session, &DocumentSession::externalFileChanged, [&changes] { ++changes; });

  atomicWrite(path, QByteArrayLiteral("replacement-two"));
  require(waitForCount(changes, 1), QStringLiteral("First atomic replacement was not detected"));
  session.recordFileBaseline();

  atomicWrite(path, QByteArrayLiteral("replacement-three-longer"));
  require(waitForCount(changes, 2), QStringLiteral("File watch was not re-armed after replacement"));
  return 0;
}
