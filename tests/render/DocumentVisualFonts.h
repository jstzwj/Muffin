#pragma once

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>

// Windows' offscreen plugin sees application fonts but does not enumerate the
// desktop font database. Register the same installed symbol faces used by the
// real Windows UI; do not bundle platform fonts or change the document font.
inline void prepareDocumentVisualFonts() {
#if defined(Q_OS_WIN)
  const QDir folder(qEnvironmentVariable("WINDIR") + QStringLiteral("/Fonts"));
  for (const auto* name :
       {"seguiemj.ttf", "seguisym.ttf", "consola.ttf", "consolab.ttf", "consolai.ttf", "consolaz.ttf"}) {
    const auto path = folder.filePath(QLatin1String(name));
    if (QFileInfo::exists(path))
      QFontDatabase::addApplicationFont(path);
  }
#endif
}
