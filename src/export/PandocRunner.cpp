#include "export/PandocRunner.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>

namespace muffin {

QString PandocRunner::resolvedExecutable() {
  QSettings settings;
  const QString configured = settings.value(QStringLiteral("export/pandocPath")).toString().trimmed();
  if (!configured.isEmpty() && QFileInfo(configured).isExecutable()) {
    return configured;
  }
  // Let QProcess resolve "pandoc" via PATH at run time.
  return QStringLiteral("pandoc");
}

bool PandocRunner::isAvailable() {
  QProcess probe;
  probe.setProgram(resolvedExecutable());
  probe.setArguments({QStringLiteral("--version")});
  probe.start();
  if (!probe.waitForStarted(3000)) {
    return false;
  }
  probe.closeWriteChannel();
  if (!probe.waitForFinished(3000)) {
    probe.kill();
    probe.waitForFinished(1000);
    return false;
  }
  return probe.exitCode() == 0;
}

PandocResult PandocRunner::run(QWidget* parent, const QStringList& args, const QByteArray& stdinData,
                               const QString& workDir) {
  PandocResult result;

  QProcess process;
  process.setProgram(resolvedExecutable());
  process.setArguments(args);
  if (!workDir.isEmpty()) {
    process.setWorkingDirectory(workDir);
  }

  // Indeterminate modal progress: gives visible feedback for large documents
  // and a way out (Cancel → kill) if Pandoc hangs. processEvents in the wait
  // loop keeps the dialog repainting.
  QProgressDialog progress(parent);
  progress.setWindowModality(Qt::WindowModal);
  progress.setRange(0, 0);
  progress.setMinimumDuration(0);
  progress.setValue(0);
  progress.show();

  bool canceled = false;
  QObject::connect(&progress, &QProgressDialog::canceled, &progress, [&canceled, &process]() {
    canceled = true;
    process.kill();
  });

  process.start();
  if (!process.waitForStarted(5000)) {
    result.err = process.errorString().toUtf8();
    return result;
  }
  if (!stdinData.isNull()) {
    process.write(stdinData);
    process.closeWriteChannel();
  }

  while (process.state() != QProcess::NotRunning) {
    if (process.waitForFinished(100)) {
      break;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (canceled) {
      process.waitForFinished(2000);
      break;
    }
  }

  result.canceled = canceled;
  if (!canceled && process.state() == QProcess::NotRunning) {
    result.ran = true;
    result.exitCode = process.exitCode();
    result.out = process.readAllStandardOutput();
    result.err = process.readAllStandardError();
  }
  return result;
}

}  // namespace muffin
