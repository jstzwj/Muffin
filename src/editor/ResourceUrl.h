#pragma once

// Resolve a markdown link/image reference to a QUrl the way the document sees
// it: absolute paths and fully-qualified URLs pass through; bare paths resolve
// against the document's directory. Shared by Ctrl+Click follow (EditorView) and
// the Open Link / Open Image Location commands (context menu), so the two paths
// never disagree on what a relative reference points at.

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QUrl>

namespace muffin {

inline QUrl resolvedUrlForDocumentResource(const QString& value, const QString& documentPath) {
  const QFileInfo info(value);
  if (info.isAbsolute()) {
    return QUrl::fromLocalFile(info.absoluteFilePath());
  }

  const QUrl url(value);
  if (url.isLocalFile()) {
    return QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absoluteFilePath());
  }
  if (url.isValid() && !url.scheme().isEmpty()) {
    return url;
  }
  if (value.startsWith(QLatin1Char('#'))) {
    return url;
  }

  if (!documentPath.isEmpty()) {
    const QString baseDirectory = QFileInfo(documentPath).absolutePath();
    return QUrl::fromLocalFile(QFileInfo(QDir(baseDirectory).absoluteFilePath(value)).absoluteFilePath());
  }

  return QUrl(value);
}

}  // namespace muffin
