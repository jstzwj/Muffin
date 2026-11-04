#include "unicode/IcuRuntime.h"
#include "unicode/WordBoundary.h"
#include "../TestUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>
#include <atomic>
#include <thread>
#include <vector>

namespace {
int probe() {
  const auto environment = qgetenv("ICU_DATA");
  std::atomic<bool> succeeded = true;
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back([&] {
      if (!muffin::initializeIcuData())
        succeeded = false;
    });
  for (auto& thread : threads)
    thread.join();
  require(qgetenv("ICU_DATA") == environment, "ICU initialization must not change the process environment");
  QString error;
  if (!muffin::initializeIcuData(&error)) {
    QTextStream(stderr) << error << Qt::endl;
    return 3;
  }
  require(succeeded && error.isEmpty(), "concurrent initialization must use the same successful data mapping");
  const auto word = muffin::findWordSegment(QStringLiteral("你好世界"), 3);
  require(word.isWord && word.start == 2 && word.end == 4, "external ICU data must include dictionary segmentation");
  return 0;
}

void isolatedDataLoading() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "isolated runtime directory must exist");
  const QDir root(temporary.path());
  const QDir original(QCoreApplication::applicationDirPath());
  const auto executable = root.filePath(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
  require(QFile::copy(QCoreApplication::applicationFilePath(), executable), "copy runtime probe");
  for (const auto& name : original.entryList(
           {QStringLiteral("*Muffin*.dll"), QStringLiteral("*Muffin*.so"), QStringLiteral("*Muffin*.dylib")},
           QDir::Files))
    require(QFile::copy(original.filePath(name), root.filePath(name)), "copy project runtime library");
  require(root.mkpath(QStringLiteral("resources/icu")), "create isolated data directory");
  const auto dataPath =
      root.filePath(QStringLiteral("resources/icu/") + QFileInfo(muffin::icuDataFilePath()).fileName());
  const auto run = [&](bool success, const QByteArray& diagnostic) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_MACOS
    environment.remove(QStringLiteral("ICU_DATA"));
#else
    // A valid alternate directory must not rescue missing/corrupt bundled data.
    environment.insert(QStringLiteral("ICU_DATA"), QFileInfo(muffin::icuDataFilePath()).absolutePath());
#endif
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(QDir::tempPath());
    process.start(executable, {QStringLiteral("--probe")});
    require(process.waitForFinished(30000), "runtime probe must finish");
    const auto output = process.readAllStandardError();
    require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == (success ? 0 : 3),
            QString::fromUtf8(output));
    require(diagnostic.isEmpty() || output.contains(diagnostic), "runtime failure must explain the affected data file");
  };
  run(false, QFileInfo(dataPath).fileName().toUtf8());
  require(QFile::copy(muffin::icuDataFilePath(), dataPath), "copy matching ICU data");
  run(true, {});
  {
    QFile data(dataPath);
    require(data.open(QIODevice::ReadWrite) && data.seek(128), "open data for corruption test");
    const auto byte = data.read(1);
    require(byte.size() == 1 && data.seek(128), "read corruption location");
    const char changed = byte.front() ^ 0x40;
    require(data.write(&changed, 1) == 1, "change data without changing its size");
  }
  run(false, "checksum mismatch");
  {
    QFile data(dataPath);
    require(data.open(QIODevice::WriteOnly | QIODevice::Truncate) && data.write("invalid") == 7,
            "replace data with invalid version/size");
  }
  run(false, "unexpected ICU data size");
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains(QStringLiteral("--probe")))
    return probe();
  runTest("isolatedDataLoading", isolatedDataLoading);
  return 0;
}
