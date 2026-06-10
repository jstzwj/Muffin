#pragma once

#include <QString>

class QTextDocument;

namespace muffin {

class EditorBackend {
public:
  virtual ~EditorBackend() = default;

  // Editing operations
  virtual void cut() = 0;
  virtual void copy() = 0;
  virtual void paste() = 0;
  virtual void deleteForward() = 0;
  virtual void selectAll() = 0;
  virtual void selectLine() = 0;
  virtual void selectFormatSpan() = 0;
  virtual void moveLineUp() = 0;
  virtual void moveLineDown() = 0;

  // Undo/Redo
  virtual bool canUndo() const = 0;
  virtual bool canRedo() const = 0;
  virtual void undo() = 0;
  virtual void redo() = 0;

  // Clipboard variants
  virtual void copyAsPlainText() = 0;
  virtual void copyAsMarkdown() = 0;
  virtual void copyAsHtml() = 0;
  virtual void pasteAsPlainText() = 0;

  // Formatting (render: WYSIWYG toggle; source: insert markers)
  virtual void toggleBold() = 0;
  virtual void toggleItalic() = 0;
  virtual void toggleCode() = 0;
  virtual void toggleStrikethrough() = 0;
  virtual void toggleInlineMath() = 0;
  virtual void toggleUnderline() = 0;
  virtual void insertLink() = 0;
  virtual void insertImage() = 0;

  // State queries
  virtual bool hasSelection() const = 0;
  virtual bool isSourceMode() const = 0;

  // Find
  virtual void find(const QString& text, bool forward) = 0;

  // Text access
  virtual QString selectedText() const = 0;
  virtual void replaceSelection(const QString& text) = 0;
  virtual QString fullText() const = 0;
  virtual void setFullText(const QString& text) = 0;

  // Typewriter mode
  virtual void centerCursor() = 0;

  // Cursor status for status bar
  virtual QString cursorStatusText() const = 0;
};

}  // namespace muffin
