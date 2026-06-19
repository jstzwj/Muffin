#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace muffin {

// Outcome of a custom-command upload. `ran` distinguishes "the command executed
// and returned" from "we never started it" (unconfigured / failed to launch).
struct CustomCommandResult {
  bool ran = false;
  bool canceled = false;
  QStringList urls;  // parsed from stdout, one non-blank line per returned URL, in order.
  QString error;     // capture of stderr / launch failure, for the caller to surface.
};

// Drives a user-configured external image uploader (PicGo, PicList, uPic, a shell
// script, …) via a single command line stored under `image/uploadCommand`. This is
// Muffin's second QProcess subsystem after PandocRunner and mirrors its shape: it
// resolves the command, runs synchronously behind a cancellable modal progress
// dialog, and returns the captured output. tr()-free on purpose — callers own all
// user-facing messages, so this translation unit stays out of
// MUFFIN_TRANSLATABLE_SOURCES and may use a `namespace muffin { }` wrapper freely.
class CustomCommandUploader {
public:
  // image/uploadCommand verbatim (trimmed). Empty when unconfigured.
  static QString resolvedCommand();

  // A non-empty command counts as "available". Unlike Pandoc there is no stable
  // probe invocation (`<exe> --version` is meaningless for an arbitrary uploader),
  // so we only gate on the command being configured.
  static bool isAvailable();

  // Runs the configured upload command, splitting it with QProcess::splitCommand
  // and appending every path in `paths` as an extra argv element. Non-blank stdout
  // lines are returned as URLs (PicGo/Typora convention: one URL per input image,
  // in order). A cancellable QProgressDialog is shown over `parent`; cancelling
  // kills the process (result.canceled becomes true). Blocks until the process
  // finishes or is cancelled. Pass a non-empty `commandOverride` to run a command
  // other than the persisted one (used by the prefs "Test" button, which must test
  // the value typed into the field before the dialog applies it).
  static CustomCommandResult upload(QWidget* parent, const QStringList& paths, const QString& commandOverride = {});
};

}  // namespace muffin
