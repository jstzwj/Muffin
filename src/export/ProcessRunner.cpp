#include "export/ProcessRunner.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QProcess>
#include <QProgressDialog>

namespace muffin {

ProcessResult ProcessRunner::run(QWidget* parent, const QString& program, const QStringList& args,
                                 const ProcessRunOptions& options) {
  ProcessResult result;

  QProcess process;
  // Keep diagnostics bounded even for a noisy custom converter. Draining both
  // channels as they arrive also prevents the child from blocking on output.
  const auto drain = [&](QByteArray& target, const QByteArray& bytes) {
    target += options.outputLimit < 0 ? bytes : bytes.left(qMax<qsizetype>(0, options.outputLimit - target.size()));
  };
  QObject::connect(&process, &QProcess::readyReadStandardOutput, &process,
                   [&] { drain(result.out, process.readAllStandardOutput()); });
  QObject::connect(&process, &QProcess::readyReadStandardError, &process,
                   [&] { drain(result.err, process.readAllStandardError()); });
  process.setProgram(program);
  process.setArguments(args);
  if (!options.workDir.isEmpty()) {
    process.setWorkingDirectory(options.workDir);
  }

  // Indeterminate modal progress: gives visible feedback for large documents
  // and a way out (Cancel → kill) if Pandoc hangs. processEvents in the wait
  // loop keeps the dialog repainting. Pandoc emits no conversion progress, so
  // the bar stays indeterminate — `options.progressLabel` is the textual feedback.
  QProgressDialog progress(parent);
  progress.setWindowModality(Qt::WindowModal);
  progress.setRange(0, 0);
  progress.setMinimumDuration(0);
  progress.setValue(0);
  if (!options.progressLabel.isEmpty()) {
    progress.setLabelText(options.progressLabel);
  }
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
  if (!options.stdinData.isNull()) {
    process.write(options.stdinData);
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
  if (!canceled && process.state() == QProcess::NotRunning) {
    result.ran = true;
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    drain(result.out, process.readAllStandardOutput());
    drain(result.err, process.readAllStandardError());
  }
  return result;
}

} // namespace muffin
