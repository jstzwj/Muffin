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
  bool save(DocumentSession& session, QWidget* parent);
  bool saveAs(DocumentSession& session, QWidget* parent);

private:
  bool confirmDiscardIfModified(DocumentSession& session, QWidget* parent);
  bool readTextFile(const QString& path, QString* out, QWidget* parent) const;
  bool writeTextFile(const QString& path, const QString& text, QWidget* parent) const;
};

}  // namespace muffin
