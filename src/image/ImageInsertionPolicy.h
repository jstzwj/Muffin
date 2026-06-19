#pragma once

#include "image/ImageInsertAction.h"

#include <QImage>
#include <QString>

class QSettings;
class QWidget;

namespace muffin {

// One image being brought into the document. Exactly one of `sourcePath` /
// `pastedImage` carries the payload: file picks and drops set `sourcePath` (a
// local path or an http(s) URL); clipboard image data sets `pastedImage`.
struct ImageInsertRequest {
  QString sourcePath;
  QImage pastedImage;
  QString documentPath;  // absolute path of the .md file; empty for an untitled doc.
  QString documentText;  // raw markdown, scanned for a frontmatter upload directive.
  QString alt;           // desired alt text; empty → caller/insert site picks a default.
};

// The href (and alt) to splice into `![alt](href)`, plus status. `uploaded` tells
// the caller the href is a remote URL produced by the upload service (vs. a local
// path), which matters for actions like "reload all images".
struct ImageInsertResult {
  bool ok = false;
  QString href;
  QString alt;
  QString error;    // non-fatal warning (e.g. upload failed, fell back to local path); empty when clean.
  bool uploaded = false;
};

// Centralizes the "what happens when an image is inserted" decision so every entry
// point (clipboard paste, Insert-Local-Image dialog, drag-drop) applies the same
// rules: the configured ImageInsertAction, the applyToLocal/applyToNetwork gates,
// the frontmatter upload override, and the path-formatting checkboxes. Performs
// the file-copy / network-download / uploader side effects. tr()-free — callers
// own user-facing messages, so this unit stays out of MUFFIN_TRANSLATABLE_SOURCES.
class ImageInsertionPolicy {
public:
  static ImageInsertResult resolveHref(const ImageInsertRequest& request, QSettings& settings, QWidget* parent);
};

}  // namespace muffin
