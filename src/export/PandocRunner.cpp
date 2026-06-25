#include "export/PandocRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QSettings>

namespace muffin {

namespace {

// Per-platform well-known Pandoc install locations. These are checked before
// falling back to a bare "pandoc" PATH lookup so that installs which did not
// (or could not, in the current process) add Pandoc to PATH are still found —
// the common failure mode on Windows per-user installs.
QStringList wellKnownPandocCandidates() {
  QStringList paths;
#ifdef Q_OS_WIN
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  // Per-user MSI install (default location, e.g. C:\Users\<u>\AppData\Local\Pandoc).
  const QString localAppData = env.value(QStringLiteral("LOCALAPPDATA"));
  if (!localAppData.isEmpty()) {
    paths << QDir(localAppData).filePath(QStringLiteral("Pandoc/pandoc.exe"));
  }
  // Machine-wide MSI install.
  const QString programFiles = env.value(QStringLiteral("ProgramFiles"));
  if (!programFiles.isEmpty()) {
    paths << QDir(programFiles).filePath(QStringLiteral("Pandoc/pandoc.exe"));
  }
  const QString programFilesX86 = env.value(QStringLiteral("ProgramFiles(x86)"));
  if (!programFilesX86.isEmpty()) {
    paths << QDir(programFilesX86).filePath(QStringLiteral("Pandoc/pandoc.exe"));
  }
  // Package managers.
  const QString programData = env.value(QStringLiteral("ProgramData"));
  if (!programData.isEmpty()) {
    paths << QDir(programData).filePath(QStringLiteral("chocolatey/bin/pandoc.exe"));
  }
  const QString userProfile = env.value(QStringLiteral("USERPROFILE"));
  if (!userProfile.isEmpty()) {
    paths << QDir(userProfile).filePath(QStringLiteral("scoop/shims/pandoc.exe"));
  }
#elif defined(Q_OS_MACOS)
  paths << QStringLiteral("/opt/homebrew/bin/pandoc")   // Apple Silicon Homebrew
        << QStringLiteral("/usr/local/bin/pandoc")      // Intel Homebrew
        << QStringLiteral("/opt/local/bin/pandoc")      // MacPorts
        << QStringLiteral("/Library/TeX/texbin/pandoc");  // MacTeX bundle
#else
  paths << QStringLiteral("/usr/bin/pandoc") << QStringLiteral("/usr/local/bin/pandoc")
        << QStringLiteral("/bin/pandoc") << QStringLiteral("/snap/bin/pandoc")
        << QStringLiteral("/opt/pandoc/bin/pandoc")
        << QDir::home().filePath(QStringLiteral(".local/bin/pandoc"));
#endif
  return paths;
}

}  // namespace

QString PandocRunner::resolvedExecutable() {
  QSettings settings;
  const QString configured = settings.value(QStringLiteral("export/pandocPath")).toString().trimmed();
  if (!configured.isEmpty() && QFileInfo(configured).isExecutable()) {
    return configured;
  }
  // Probe well-known install locations before relying on PATH, so installs that
  // are present but not on the current PATH are still found.
  const QString found = searchSystem();
  if (!found.isEmpty()) {
    return found;
  }
  // Last resort: let QProcess resolve "pandoc" via PATH at run time.
  return QStringLiteral("pandoc");
}

QString PandocRunner::findFirstExistingExecutable(const QStringList& candidates) {
  for (const QString& candidate : candidates) {
    if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()) {
      return candidate;
    }
  }
  return QString();
}

QString PandocRunner::searchSystem() {
  return findFirstExistingExecutable(wellKnownPandocCandidates());
}

QString PandocRunner::resolveOnPath() {
  QProcess lookup;
#ifdef Q_OS_WIN
  lookup.setProgram(QStringLiteral("where"));
  lookup.setArguments({QStringLiteral("pandoc")});
#else
  lookup.setProgram(QStringLiteral("which"));
  lookup.setArguments({QStringLiteral("pandoc")});
#endif
  lookup.start();
  if (!lookup.waitForStarted(2000) || !lookup.waitForFinished(2000)) {
    if (lookup.state() != QProcess::NotRunning) {
      lookup.kill();
      lookup.waitForFinished(500);
    }
    return QString();
  }
  if (lookup.exitCode() != 0) {
    return QString();
  }
  // `where` may emit several lines; take the first non-empty one that exists.
  for (const QByteArray& line : lookup.readAllStandardOutput().split('\n')) {
    const QString path = QString::fromUtf8(line).trimmed();
    if (!path.isEmpty() && QFileInfo(path).isExecutable()) {
      return path;
    }
  }
  return QString();
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
                               const QString& workDir, const QString& progressLabel) {
  PandocResult result;

  QProcess process;
  process.setProgram(resolvedExecutable());
  process.setArguments(args);
  if (!workDir.isEmpty()) {
    process.setWorkingDirectory(workDir);
  }

  // Indeterminate modal progress: gives visible feedback for large documents
  // and a way out (Cancel → kill) if Pandoc hangs. processEvents in the wait
  // loop keeps the dialog repainting. Pandoc emits no conversion progress, so
  // the bar stays indeterminate — `progressLabel` is the textual feedback.
  QProgressDialog progress(parent);
  progress.setWindowModality(Qt::WindowModal);
  progress.setRange(0, 0);
  progress.setMinimumDuration(0);
  progress.setValue(0);
  if (!progressLabel.isEmpty()) {
    progress.setLabelText(progressLabel);
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
