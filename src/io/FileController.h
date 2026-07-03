#pragma once

#include <QObject>
#include <QString>

class QWidget;

namespace muffin {

class DocumentSession;

class FileController final : public QObject {
  Q_OBJECT

public:
  explicit FileController(QObject* parent = nullptr);

  bool newFile(DocumentSession& session, QWidget* parent);
  bool open(DocumentSession& session, QWidget* parent, QString path = {});
  // `defaultDir` seeds the Save As dialog for an untitled document (it is ignored
  // once the document has a path). Pass MainWindow::defaultSaveDirectory() so a
  // file saved while a folder is open in the sidebar lands there by default.
  bool save(DocumentSession& session, QWidget* parent, const QString& defaultDir = {});
  bool saveAs(DocumentSession& session, QWidget* parent, const QString& defaultDir = {});
  bool reopenWithEncoding(DocumentSession& session, QWidget* parent, const QString& encodingName);
  bool moveTo(DocumentSession& session, QWidget* parent);

signals:
  // Emitted when a document's unsaved work is resolved — either persisted by
  // save()/saveAs() or explicitly discarded in a confirm dialog — so the crash
  // recovery store can drop the now-obsolete draft for that path.
  void documentBecameClean(QString filePath);

private:
  bool confirmDiscardIfModified(DocumentSession& session, QWidget* parent);
  // Default filename offered by Save As for an untitled document, driven by the
  // files/defaultExtension setting (0=.md, 1=.markdown, 2=.txt).
  QString defaultUntitledName() const;
  // Silently persist a pathed, modified document before switching to another, so
  // the confirm-discard prompt is skipped. No-op unless files/autoSaveOnSwitch is on.
  void autoSaveOnSwitchIfEnabled(DocumentSession& session, QWidget* parent);
  bool readTextFile(const QString& path, QString* out, QWidget* parent) const;
  bool readTextFileWithEncoding(const QString& path, QString* out, QWidget* parent, const QString& encodingName) const;
  bool writeTextFile(const QString& path, const QString& text, QWidget* parent) const;
};

}  // namespace muffin
