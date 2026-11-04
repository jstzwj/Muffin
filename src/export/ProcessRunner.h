#pragma once
#include "Export.h"
#include <QByteArray>
#include <QStringList>
class QWidget;
namespace muffin {
struct ProcessResult {
  bool ran = false;
  bool canceled = false;
  int exitCode = -1;
  QByteArray out;
  QByteArray err;
};
struct ProcessRunOptions {
  QByteArray stdinData;
  QString workDir;
  QString progressLabel;
  qsizetype outputLimit = -1; // Per channel; negative preserves complete import output.
};
// Shared cancellable execution for Pandoc and user-configured exporters.
// Arguments are passed directly, without an implicit command shell.
class MUFFIN_UI_EXPORT ProcessRunner {
public:
  static ProcessResult run(QWidget* parent, const QString& program, const QStringList& arguments,
                           const ProcessRunOptions& options = {});
};
}
