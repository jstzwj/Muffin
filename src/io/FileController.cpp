#include "io/FileController.h"

#include "app/DocumentSession.h"

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

namespace muffin {

FileController::FileController(QObject* parent) : QObject(parent) {}

bool FileController::newFile(DocumentSession& session, QWidget* parent) {
  if (!confirmDiscardIfModified(session, parent)) {
    return false;
  }
  session.newDocument();
  return true;
}

bool FileController::open(DocumentSession& session, QWidget* parent, QString path) {
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
  session.setMarkdownText(text, false);
  return true;
}

bool FileController::save(DocumentSession& session, QWidget* parent) {
  if (session.filePath().isEmpty()) {
    return saveAs(session, parent);
  }
  if (!writeTextFile(session.filePath(), session.markdownText(), parent)) {
    return false;
  }
  session.document().setModified(false);
  return true;
}

bool FileController::saveAs(DocumentSession& session, QWidget* parent) {
  QString path = QFileDialog::getSaveFileName(
      parent,
      tr("Save As"),
      session.filePath().isEmpty() ? QStringLiteral("Untitled.md") : session.filePath(),
      tr("Markdown files (*.md);;Text files (*.txt);;All files (*.*)"));
  if (path.isEmpty()) {
    return false;
  }
  if (!writeTextFile(path, session.markdownText(), parent)) {
    return false;
  }
  session.setFilePath(path);
  session.document().setModified(false);
  return true;
}

bool FileController::confirmDiscardIfModified(DocumentSession& session, QWidget* parent) {
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
    return save(session, parent);
  }
  return true;
}

bool FileController::readTextFile(const QString& path, QString* out, QWidget* parent) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::critical(parent, tr("Open Failed"), file.errorString());
    return false;
  }

  QTextStream stream(&file);
  stream.setEncoding(QStringConverter::Utf8);
  QString text = stream.readAll();
  // Normalize line endings to LF for internal use
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  *out = text;
  return true;
}

bool FileController::writeTextFile(const QString& path, const QString& text, QWidget* parent) const {
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

bool FileController::readTextFileWithEncoding(
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

bool FileController::reopenWithEncoding(
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
    }
  }

  QString text;
  if (!readTextFileWithEncoding(session.filePath(), &text, parent, encodingName)) {
    return false;
  }

  session.setMarkdownText(text, false);
  return true;
}

bool FileController::moveTo(DocumentSession& session, QWidget* parent) {
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

}  // namespace muffin
