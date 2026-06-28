#include "io/FileController.h"

#include "document/DocumentSession.h"

#include <QFile>
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
  session.openDocumentAsync(text);  // async parse keeps the UI responsive on huge files
  return true;
}

bool muffin::FileController::save(DocumentSession& session, QWidget* parent) {
  if (session.filePath().isEmpty()) {
    return saveAs(session, parent);
  }
  if (!writeTextFile(session.filePath(), session.markdownText().toString(), parent)) {
    return false;
  }
  session.document().setModified(false);
  emit documentBecameClean(session.filePath());
  return true;
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

bool muffin::FileController::saveAs(DocumentSession& session, QWidget* parent) {
  QString path = QFileDialog::getSaveFileName(
      parent,
      tr("Save As"),
      session.filePath().isEmpty() ? defaultUntitledName() : session.filePath(),
      tr("Markdown files (*.md *.markdown);;Text files (*.txt);;All files (*.*)"));
  if (path.isEmpty()) {
    return false;
  }
  if (!writeTextFile(path, session.markdownText().toString(), parent)) {
    return false;
  }
  // The previous path's draft (if any) is now obsolete: the content lives at `path`.
  emit documentBecameClean(session.filePath());
  session.setFilePath(path);
  session.document().setModified(false);
  return true;
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
    return save(session, parent);  // save() emits documentBecameClean on success.
  }
  emit documentBecameClean(session.filePath());
  return true;
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
      if (!save(session, parent)) {
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
    if (!save(session, parent)) {
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
  return true;
}
