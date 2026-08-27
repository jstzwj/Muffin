#pragma once

#include <QWidget>
#include <QStringView>

#include "Export.h"

class QEvent;
class QResizeEvent;

namespace muffin {

class DocumentSession;
class RenderTheme;
class VirtualSourceEdit;

// Stable facade for source-mode commands. Positions are UTF-16 source offsets
// into DocumentSession::PieceTable; no QTextDocument is exposed or retained.
class MUFFIN_UI_EXPORT SourceEditorWidget final : public QWidget {
  Q_OBJECT

public:
  explicit SourceEditorWidget(QWidget* parent = nullptr);

  void bindSession(DocumentSession* session);
  void syncFromSession(bool preserveCursor = true);
  void notifyDocumentChanged();

  QString text() const;
  void setText(const QString& text);
  qsizetype cursorPosition() const;
  qsizetype anchorPosition() const;
  qsizetype selectionStart() const;
  qsizetype selectionEnd() const;
  bool hasSelection() const;
  QString selectedText() const;
  void setCursorPosition(qsizetype position, bool keepAnchor = false);
  void setSelection(qsizetype start, qsizetype end);
  void selectAll();
  void selectLine();
  void selectWord();

  void insertText(const QString& text);
  void replaceSelection(const QString& text);
  void deleteForward();
  void deleteBackward();
  void deleteWord();
  void deleteLineContent();
  void deleteWholeLine();
  void moveCurrentLineUp();
  void moveCurrentLineDown();

  bool canUndo() const;
  bool canRedo() const;
  void undo();
  void redo();

  void moveDocumentStart();
  void moveDocumentEnd();
  void moveLineStart();
  void moveLineEnd();
  void moveLineVertical(int delta);
  void selectNextOccurrence();
  qsizetype findText(QStringView text, qsizetype from = 0) const;
  qsizetype findTextBackward(QStringView text, qsizetype from = -1) const;
  void ensureCursorVisible();
  void centerCursor();
  int cursorLine() const;
  int cursorColumn() const;

  void setWordWrapEnabled(bool enabled);
  void setZoomPercent(int percent);
  void setFontSizePx(int px);
  void setContentWidthPx(int px);
  void setTheme(const RenderTheme& theme);
  void setDocumentPath(const QString& path);
  void setReadOnly(bool readOnly);
  bool isReadOnly() const;

signals:
  void cursorPositionChanged(int line, int column);
  void editApplied();

protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  void setupStyle();
  void retranslateUi();
  void updateEditorWidth();
  void applyFontSize();

  VirtualSourceEdit* editor_ = nullptr;
  int zoomPercent_ = 100;
  int fontSizePx_ = 16;
  int contentWidthPx_ = 0;
};

}  // namespace muffin
