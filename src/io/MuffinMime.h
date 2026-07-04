#pragma once

#include <QLatin1StringView>

namespace muffin {

// Custom MIME flavour attached to drags that originate from Muffin's sidebar file tree.
// Drop targets (the rendered EditorView and the source-mode QPlainTextEdit) check for it to
// distinguish an in-app file-tree drop — which inserts a markdown link at the caret — from an
// external file:// drag, which keeps the existing open-as-document / open-as-folder routing.
// The payload is empty: presence of the format is the only signal, so it composes cleanly
// with the standard text/uri-list URLs the QFileSystemModel already produces.
inline constexpr QLatin1StringView kMuffinFileTreeDragMime{"application/x-muffin-filetree"};

}  // namespace muffin
