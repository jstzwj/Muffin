#pragma once

#include "app/WorkspaceFiles.h"

namespace muffin {

// A value snapshot, captured before starting a background search. Search scope
// deliberately does not share the preferences that protect opening a file.
struct MUFFIN_UI_EXPORT SearchSettings {
  WorkspaceScanOptions scan;
  QStringList extensions;
  qint64 maxFileBytes = 100 * 1024 * 1024;
  qint64 maxRegexBytes = 32 * 1024 * 1024;

  static SearchSettings load();
  static QString defaultExtensions();
  static constexpr qint64 maximumFileBytes = 512 * 1024 * 1024;
};

} // namespace muffin
