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

namespace resource_url_detail {
inline QUrl resolve(const QString& value, const QUrl& base) {
  const QUrl url(value);
#ifdef Q_OS_WIN
  // A native drive/UNC path is not a URL scheme (QUrl parses C: as scheme "c").
  if (QFileInfo(value).isAbsolute() && !value.startsWith(QLatin1Char('/'))) {
    const QString path = QDir::fromNativeSeparators(value);
    return QUrl((path.startsWith(QStringLiteral("//")) ? QStringLiteral("file:")
                                                       : QStringLiteral("file:///")) + path);
  }
#endif
  if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isAbsolute()) {
    return url;
  }
  if (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile()) {
    return url;
  }
  if (value.startsWith(QLatin1Char('#'))) {
    return url;
  }

  // Resolve URLs, not encoded filename strings: a%20b.png names "a b.png",
  // and query/fragment components must not become part of a local filename.
  return base.resolved(url.isLocalFile() ? QUrl(url.toLocalFile()) : url);
}
}  // namespace resource_url_detail

inline QUrl resolvedUrlForDocumentResource(const QString& value, const QString& documentPath) {
  const QUrl base = documentPath.isEmpty()
      ? QUrl::fromLocalFile(QDir::currentPath() + QLatin1Char('/'))
      : QUrl::fromLocalFile(QFileInfo(documentPath).absoluteFilePath());
  return resource_url_detail::resolve(value, base);
}

inline QUrl resolvedUrlForDirectoryResource(const QString& value, const QString& documentDir) {
  const QUrl base = QUrl::fromLocalFile(QDir(documentDir).absolutePath() + QLatin1Char('/'));
  return resource_url_detail::resolve(value, base);
}

}  // namespace muffin
