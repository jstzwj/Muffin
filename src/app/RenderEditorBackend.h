#pragma once

#include "app/EditorBackend.h"

namespace muffin {

class DocumentSession;
class EditorController;
class EditorView;

class RenderEditorBackend final : public EditorBackend {
public:
  RenderEditorBackend(EditorController& controller, DocumentSession& session, EditorView* view);

  void cut() override;
  void copy() override;
  void paste() override;
  void deleteForward() override;
  void selectAll() override;
  void selectLine() override;
  void selectFormatSpan() override;
  void moveLineUp() override;
  void moveLineDown() override;

  bool canUndo() const override;
  bool canRedo() const override;
  void undo() override;
  void redo() override;

  void copyAsPlainText() override;
  void copyAsMarkdown() override;
  void copyAsHtml() override;
  void pasteAsPlainText() override;

  void toggleBold() override;
  void toggleItalic() override;
  void toggleCode() override;
  void toggleStrikethrough() override;
  void toggleInlineMath() override;
  void insertLink() override;
  void insertImage() override;

  bool hasSelection() const override;
  bool isSourceMode() const override;

  void find(const QString& text, bool forward) override;
  QString selectedText() const override;
  void replaceSelection(const QString& text) override;
  QString fullText() const override;
  void setFullText(const QString& text) override;

  void centerCursor() override;
  QString cursorStatusText() const override;

private:
  EditorController& controller_;
  DocumentSession& session_;
  EditorView* view_;
};

}  // namespace muffin
