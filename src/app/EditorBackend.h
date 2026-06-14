#pragma once

#include <QString>

class QTextDocument;

namespace muffin {

// Unified delete API. `Forward` preserves the legacy "delete selection or one
// char forward" semantics used by the standalone Delete menu item; the other
// targets power the Delete Range submenu (delete the current word / format
// span / line / block). Implementations select the target unit and delete it.
enum class DeleteTarget {
  Forward,     // delete selection, else one char forward
  Backward,    // delete selection, else one char backward
  Word,        // delete the word at the cursor
  FormatSpan,  // delete the inline format span at the cursor
  Line,        // clear the current line/block content, keep the container
  Block,       // remove the whole current block
};

class EditorBackend {
public:
  virtual ~EditorBackend() = default;

  // Editing operations
  virtual void cut() = 0;
  virtual void copy() = 0;
  virtual void paste() = 0;
  virtual void deleteRange(DeleteTarget target) = 0;
  // Legacy single-forward-delete; delegates to deleteRange(Forward). Kept as a
  // non-virtual inline so existing callers (and tests) are unaffected.
  void deleteForward() { deleteRange(DeleteTarget::Forward); }
  virtual void selectAll() = 0;
  virtual void selectLine() = 0;
  virtual void selectBlock() = 0;        // whole current paragraph / block
  virtual void selectWord() = 0;         // word at the caret
  virtual void selectFormatSpan() = 0;
  virtual void moveLineUp() = 0;
  virtual void moveLineDown() = 0;

  // Caret navigation for the Edit ▸ Select ▸ "Jump to" group.
  virtual void moveDocumentStart() = 0;  // caret to start of document
  virtual void moveDocumentEnd() = 0;    // caret to end of document
  virtual void moveLineStart() = 0;      // caret to start of current line/block
  virtual void moveLineEnd() = 0;        // caret to end of current line/block
  virtual void selectNextOccurrence() = 0;  // select the next occurrence of the current selection/word

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
