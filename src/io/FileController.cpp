#include "io/FileController.h"

#include "document/DocumentSession.h"

#include <QFile>
#include <QDir>
#include <QSettings>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>
#include <QWidget>

#include <unicode/ucnv.h>
#include <unicode/ustring.h>

muffin::FileController::FileController(QObject* parent) : QObject(parent) {}

bool muffin::FileController::newFile(DocumentSession& session, QWidget* parent) {
  autoSaveOnSwitchIfEnabled(session, parent);
  if (!confirmDiscardIfModified(session, parent)) {
    return false;
  }
  session.newDocument();
  return true;
}

bool muffin::FileController::open(DocumentSession& session, QWidget* parent, QString path) {
  autoSaveOnSwitchIfEnabled(session, parent);
  if (!confirmDiscardIfModified(session, parent)) {
    return false;
  }

  if (path.isEmpty()) {
    path = QFileDialog::getOpenFileName(
        parent,
        tr("Open"),
        QString(),
        tr("Markdown and text files (*.md *.markdown *.mdown *.txt);;All files (*.*)"));
  }
  if (path.isEmpty()) {
    return false;
  }

  QString text;
  if (!readTextFile(path, &text, parent)) {
    return false;
  }

  session.setFilePath(path);
  session.recordFileBaseline();  // baseline the file as it was at open (external-change detection)
  session.openDocumentAsync(text);  // async parse keeps the UI responsive on huge files
  return true;
}

muffin::SaveOutcome muffin::FileController::save(DocumentSession& session, QWidget* parent, const QString& defaultDir) {
  // Refuse to persist while an async open parse is in flight: document_ still holds the pre-open
  // text, but filePath_ already points at the new file — writing would clobber it with stale content.
  if (session.isAsyncParseInProgress()) {
    return SaveOutcome::SkippedBusy;
  }
  if (session.filePath().isEmpty()) {
    return saveAs(session, parent, defaultDir);
  }
  if (!confirmOverwriteIfChanged(session, parent)) {
    return SaveOutcome::Failed;  // user declined to overwrite the externally-modified file
  }
  DocumentSession::SelfWriteGuard guard(session);  // absorb our own commit's fileChanged signal
  if (!writeTextFile(session.filePath(), session.markdownText().toString(), parent)) {
    return SaveOutcome::Failed;
  }
  session.document().setModified(false);
  session.recordFileBaseline();  // re-baseline after our write (2nd line of self-trigger defense)
  emit documentBecameClean(session.filePath());
  return SaveOutcome::Saved;
}

QString muffin::FileController::defaultUntitledName() const {
  // files/defaultExtension: 0 = .md (default), 1 = .markdown, 2 = .txt.
  QSettings settings;
  switch (settings.value(QStringLiteral("files/defaultExtension"), 0).toInt()) {
    case 1:
      return QStringLiteral("Untitled.markdown");
    case 2:
      return QStringLiteral("Untitled.txt");
    default:
      return QStringLiteral("Untitled.md");
  }
}

void muffin::FileController::autoSaveOnSwitchIfEnabled(DocumentSession& session, QWidget* parent) {
  // The internal Save paths below intentionally keep defaultDir = {} — only the
  // explicit Save / Save As commands (which know the sidebar folder) seed it.
  // Silently persist a pathed, modified document before switching away, so the
  // confirm-discard prompt below is skipped. No-op unless files/autoSaveOnSwitch is on.
  QSettings settings;
  if (!settings.value(QStringLiteral("files/autoSaveOnSwitch"), false).toBool()) {
    return;
  }
  if (session.filePath().isEmpty() || !session.document().isModified()) {
    return;
  }
  save(session, parent);
}

muffin::SaveOutcome muffin::FileController::saveAs(DocumentSession& session, QWidget* parent, const QString& defaultDir) {
  if (session.isAsyncParseInProgress()) {
    return SaveOutcome::SkippedBusy;
  }
  // For an untitled document, anchor the dialog in the requested directory
  // (typically the sidebar's open folder) rather than the working directory.
  QString startingPath;
  if (!session.filePath().isEmpty()) {
    startingPath = session.filePath();
  } else {
    const QString name = defaultUntitledName();
    startingPath = defaultDir.isEmpty() ? name : QDir(defaultDir).filePath(name);
  }
  QString path = QFileDialog::getSaveFileName(
      parent,
      tr("Save As"),
      startingPath,
      tr("Markdown files (*.md *.markdown);;Text files (*.txt);;All files (*.*)"));
  if (path.isEmpty()) {
    return SaveOutcome::Failed;
  }
  DocumentSession::SelfWriteGuard guard(session);
  if (!writeTextFile(path, session.markdownText().toString(), parent)) {
    return SaveOutcome::Failed;
  }
  // The previous path's draft (if any) is now obsolete: the content lives at `path`.
  emit documentBecameClean(session.filePath());
  session.setFilePath(path);  // also re-points the QFileSystemWatcher to the new path
  session.document().setModified(false);
  session.recordFileBaseline();  // baseline the new path
  return SaveOutcome::Saved;
}

bool muffin::FileController::confirmDiscardIfModified(DocumentSession& session, QWidget* parent) {
  if (!session.document().isModified()) {
    return true;
  }

  const QMessageBox::StandardButton choice = QMessageBox::warning(
      parent,
      tr("Muffin"),
      tr("The current document has unsaved changes."),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  if (choice == QMessageBox::Cancel) {
    return false;
  }
  if (choice == QMessageBox::Save) {
    return save(session, parent) == SaveOutcome::Saved;  // save() emits documentBecameClean on success.
  }
  emit documentBecameClean(session.filePath());
  return true;
}

bool muffin::FileController::confirmOverwriteIfChanged(DocumentSession& session, QWidget* parent) {
  if (!session.hasFileBaseline()) {
    return true;  // no baseline (first save / freshly opened before baseline recorded) → just write
  }
  const QFileInfo info(session.filePath());
  if (!info.exists()) {
    QMessageBox::warning(parent, tr("File Missing"),
                         tr("The file \"%1\" no longer exists on disk. Use Save As to write it to a new location.")
                             .arg(session.filePath()));
    return false;
  }
  if (info.lastModified() == session.fileBaselineMtime() && info.size() == session.fileBaselineSize()) {
    return true;  // unchanged since the open/last-save baseline
  }
  const QMessageBox::StandardButton choice = QMessageBox::warning(
      parent, tr("File Changed on Disk"),
      tr("The file \"%1\" has been changed outside Muffin. Overwrite it?").arg(info.fileName()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  return choice == QMessageBox::Yes;
}

bool muffin::FileController::readTextFile(const QString& path, QString* out, QWidget* parent) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::critical(parent, tr("Open Failed"), file.errorString());
    return false;
  }

  // Read the whole file as bytes in one shot and decode once. QTextStream::readAll() accumulates
  // its result in small chunks, which is O(n^2)-ish in the buffer size and catastrophically slow on
  // large files (~160s for 100MB vs <1s here). QFile::readAll() + QString::fromUtf8 is O(n) and is
  // the Qt-recommended way to slurp a file. Line-ending normalization is unchanged.
  const QByteArray raw = file.readAll();
  QString text = QString::fromUtf8(raw);
  // Normalize line endings to LF for internal use
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  *out = text;
  return true;
}

bool muffin::FileController::writeTextFile(const QString& path, const QString& text, QWidget* parent) const {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    QMessageBox::critical(parent, tr("Save Failed"), file.errorString());
    return false;
  }

  QSettings settings;
  QString content = text;

  // Ensure trailing newline
  if (settings.value(QStringLiteral("editor/trailingNewline"), true).toBool()) {
    if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n'))) {
      content += QLatin1Char('\n');
    }
  }

  // Apply line endings (internal text is always LF)
  const int lb = settings.value(QStringLiteral("editor/defaultLineBreak"), 1).toInt();
  if (lb == 1) {
    content.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
  }

  QTextStream stream(&file);
  stream.setEncoding(QStringConverter::Utf8);
  stream << content;
  if (!file.commit()) {
    QMessageBox::critical(parent, tr("Save Failed"), file.errorString());
    return false;
  }
  return true;
}

bool muffin::FileController::readTextFileWithEncoding(
    const QString& path, QString* out, QWidget* parent,
    const QString& encodingName) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::critical(parent, tr("Open Failed"), file.errorString());
    return false;
  }

  const QByteArray raw = file.readAll();

  UErrorCode status = U_ZERO_ERROR;
  UConverter* conv = ucnv_open(encodingName.toUtf8().constData(), &status);
  if (U_FAILURE(status)) {
    QMessageBox::critical(parent, tr("Encoding Error"),
                          tr("Unsupported encoding: %1").arg(encodingName));
    return false;
  }

  // Required destination buffer length (in UChar, NOT including NUL)
  const int32_t destCapacity = ucnv_toUChars(conv, nullptr, 0,
      raw.constData(), raw.size(), &status);
  if (status == U_BUFFER_OVERFLOW_ERROR) {
    status = U_ZERO_ERROR;
  }
  if (U_FAILURE(status)) {
    QMessageBox::critical(parent, tr("Encoding Error"),
                          tr("Failed to decode file with encoding: %1").arg(encodingName));
    ucnv_close(conv);
    return false;
  }

  QByteArray utf16(sizeof(char16_t) * (destCapacity + 1), Qt::Uninitialized);
  auto* dest = reinterpret_cast<UChar*>(utf16.data());
  ucnv_toUChars(conv, dest, destCapacity + 1,
      raw.constData(), raw.size(), &status);
  ucnv_close(conv);

  if (U_FAILURE(status)) {
    QMessageBox::critical(parent, tr("Encoding Error"),
                          tr("Failed to decode file with encoding: %1").arg(encodingName));
    return false;
  }

  *out = QString(reinterpret_cast<const QChar*>(dest), destCapacity);
  return true;
}

bool muffin::FileController::reopenWithEncoding(
    DocumentSession& session, QWidget* parent,
    const QString& encodingName) {
  if (session.filePath().isEmpty()) {
    return false;
  }

  if (session.document().isModified()) {
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        parent, tr("Muffin"),
        tr("The document has unsaved changes. Save before reopening with a new encoding?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
      return false;
    }
    if (choice == QMessageBox::Save) {
      if (save(session, parent) != SaveOutcome::Saved) {
        return false;
      }
    } else {
      emit documentBecameClean(session.filePath());
    }
  }

  QString text;
  if (!readTextFileWithEncoding(session.filePath(), &text, parent, encodingName)) {
    return false;
  }

  session.setMarkdownText(text, false);
  return true;
}

bool muffin::FileController::moveTo(DocumentSession& session, QWidget* parent) {
  if (session.filePath().isEmpty()) {
    return false;
  }

  if (session.document().isModified()) {
    if (save(session, parent) != SaveOutcome::Saved) {
      return false;
    }
  }

  const QString newPath = QFileDialog::getSaveFileName(
      parent, tr("Move To"),
      session.filePath(),
      tr("Markdown files (*.md);;Text files (*.txt);;All files (*.*)"));
  if (newPath.isEmpty() || newPath == session.filePath()) {
    return false;
  }

  if (!QFile::rename(session.filePath(), newPath)) {
    QMessageBox::critical(parent, tr("Move Failed"),
                          tr("Could not move file to:\n%1").arg(newPath));
    return false;
  }

  session.setFilePath(newPath);
  session.document().setModified(false);
  session.recordFileBaseline();  // baseline the moved file
  return true;
}

bool muffin::FileController::reload(DocumentSession& session, QWidget* parent) {
  if (session.filePath().isEmpty()) {
    return false;
  }
  QString text;
  if (!readTextFile(session.filePath(), &text, parent)) {
    return false;
  }
  // Discards unsaved edits (caller already confirmed). Does NOT route through open(), which would
  // trigger autoSaveOnSwitch and overwrite the very external change being reloaded. Re-baseline so
  // the watcher treats the reloaded content as the new reference.
  session.setMarkdownText(text, false);
  session.recordFileBaseline();
  return true;
}
