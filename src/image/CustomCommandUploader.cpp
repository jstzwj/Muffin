#include "image/CustomCommandUploader.h"

#include <QCoreApplication>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QTextStream>

namespace muffin {

QString CustomCommandUploader::resolvedCommand() {
  return QSettings().value(QStringLiteral("image/uploadCommand")).toString().trimmed();
}

bool CustomCommandUploader::isAvailable() {
  return !resolvedCommand().isEmpty();
}

CustomCommandResult CustomCommandUploader::upload(QWidget* parent, const QStringList& paths, const QString& commandOverride) {
  CustomCommandResult result;

  const QString command = !commandOverride.isEmpty() ? commandOverride.trimmed() : resolvedCommand();
  if (command.isEmpty()) {
    result.error = QStringLiteral("no upload command configured");
    return result;
  }

  // Tokenize the command line the way a shell would (honors quotes), then append
  // the image paths as trailing argv elements — the common image-uploader calling convention
  // is `<uploader> <file1> <file2> …`.
  QStringList args = QProcess::splitCommand(command);
  if (args.isEmpty()) {
    result.error = QStringLiteral("empty upload command");
    return result;
  }
  const QString program = args.takeFirst();
  for (const QString& path : paths) {
    args.append(path);
  }

  QProcess process;
  process.setProgram(program);
  process.setArguments(args);

  // Indeterminate modal progress: visible feedback for slow uploads and a Cancel
  // → kill escape if the uploader hangs. processEvents keeps the dialog repainting.
  // Mirrors PandocRunner::run exactly.
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
    result.error = process.errorString();
    return result;
  }
  process.closeWriteChannel();

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
  if (canceled) {
    return result;
  }
  if (process.state() != QProcess::NotRunning) {
    return result;
  }

  result.ran = true;
  const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
  if (process.exitCode() != 0) {
    result.error = stderrText.isEmpty() ? QStringLiteral("uploader exited with code %1").arg(process.exitCode()) : stderrText;
    return result;
  }

  // Parse one URL per non-blank stdout line, preserving order.
  QTextStream stdoutStream(process.readAllStandardOutput());
  QString line;
  while (stdoutStream.readLineInto(&line)) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty()) {
      result.urls.append(trimmed);
    }
  }
  if (result.urls.isEmpty()) {
    result.error = stderrText.isEmpty() ? QStringLiteral("uploader produced no URLs") : stderrText;
  }
  return result;
}

}  // namespace muffin
