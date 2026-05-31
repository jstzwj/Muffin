#include "io/FileController.h"

#include "app/DocumentSession.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QTextStream>
#include <QWidget>

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
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::critical(parent, tr("Open Failed"), file.errorString());
    return false;
  }

  QTextStream stream(&file);
  stream.setEncoding(QStringConverter::Utf8);
  *out = stream.readAll();
  return true;
}

bool FileController::writeTextFile(const QString& path, const QString& text, QWidget* parent) const {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::critical(parent, tr("Save Failed"), file.errorString());
    return false;
  }

  QTextStream stream(&file);
  stream.setEncoding(QStringConverter::Utf8);
  stream << text;
  if (!file.commit()) {
    QMessageBox::critical(parent, tr("Save Failed"), file.errorString());
    return false;
  }
  return true;
}

}  // namespace muffin
