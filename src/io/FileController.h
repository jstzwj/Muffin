#pragma once

#include <QObject>
#include <QString>

class QWidget;

namespace muffin {

class DocumentSession;

// Outcome of a save attempt. SkippedBusy = an async open parse is in flight (document_ still holds
// stale pre-open text while filePath_ already points at the new file, so we refuse to persist and
// clobber it); callers may retry once the parse finishes.
enum class SaveOutcome { Saved, SkippedBusy, Failed };

class FileController final : public QObject {
  Q_OBJECT

public:
  explicit FileController(QObject* parent = nullptr);

  bool newFile(DocumentSession& session, QWidget* parent);
  bool open(DocumentSession& session, QWidget* parent, QString path = {});
  // `defaultDir` seeds the Save As dialog for an untitled document (it is ignored
  // once the document has a path). Pass MainWindow::defaultSaveDirectory() so a
  // file saved while a folder is open in the sidebar lands there by default.
  SaveOutcome save(DocumentSession& session, QWidget* parent, const QString& defaultDir = {});
  SaveOutcome saveAs(DocumentSession& session, QWidget* parent, const QString& defaultDir = {});
  bool reopenWithEncoding(DocumentSession& session, QWidget* parent, const QString& encodingName);
  bool moveTo(DocumentSession& session, QWidget* parent);
  // Re-read the file from disk, discarding unsaved edits (caller confirms). Unlike open(), this
  // skips autoSaveOnSwitch so it never overwrites the external change the user chose to reload.
  bool reload(DocumentSession& session, QWidget* parent);

signals:
  // Emitted when a document's unsaved work is resolved — either persisted by
  // save()/saveAs() or explicitly discarded in a confirm dialog — so the crash
  // recovery store can drop the now-obsolete draft for that path.
  void documentBecameClean(QString filePath);

private:
  bool confirmDiscardIfModified(DocumentSession& session, QWidget* parent);
  // Prompts before overwriting when the file drifted on disk vs. the open/save baseline. Returns
  // true to proceed (unchanged, or user accepted overwrite), false to abort the save.
  bool confirmOverwriteIfChanged(DocumentSession& session, QWidget* parent);
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
