#pragma once

#include "document/PieceTable.h"
#include "editor/SourceLineHeightIndex.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTextLayout>
#include <QVector>

#include <memory>
#include <utility>

class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QFocusEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QTimer;
class QVariant;

namespace muffin {

class DocumentSession;
class RenderTheme;

struct SourceEditorColors {
  QColor background;
  QColor gutterBackground;
  QColor text;
  QColor currentLine;
  QColor lineNumber;
  QColor selection;
  QColor heading;
  QColor listMarker;
  QColor linkLabel;
  QColor linkTarget;
  QColor inlineCodeText;
  QColor inlineCodeBackground;
  QColor fence;
  QColor quote;
  QColor emphasis;
  QColor table;
  QColor zeroWidthText;
  QColor zeroWidthBackground;
  QColor spell;

  static SourceEditorColors fromTheme(const RenderTheme& theme);
};

// Source editor whose text remains in DocumentSession::PieceTable. It owns no
// QTextDocument and creates QTextLayout objects only for visible logical lines.
class VirtualSourceEdit final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit VirtualSourceEdit(QWidget* parent = nullptr);

  void bindSession(DocumentSession* session);
  void syncFromSession(bool preserveCursor = true);
  void notifyDocumentChanged();
  void setStandaloneText(QString text);
  QString text() const;

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
  bool wordWrapEnabled() const;
  void setSourceFont(QFont font);
  void setColors(SourceEditorColors colors);
  void setPlaceholderText(QString text);
  void setDocumentPath(QString path);
  void setReadOnly(bool readOnly);
  bool isReadOnly() const;
  // Full IME context for the source editor (cursor rect/position, selection, surrounding text).
  // Public like the rendered editor's so tests query it directly.
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
  void cursorPositionChanged(int line, int column);
  void editApplied();

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  struct EditRecord {
    qsizetype start = 0;
    QString removed;
    QString inserted;
    qsizetype beforeAnchor = 0;
    qsizetype beforeCursor = 0;
    qsizetype afterAnchor = 0;
    qsizetype afterCursor = 0;
  };

  struct LineLayout;

  const PieceTable& source() const;
  PieceTable& standaloneSource();
  bool applyEdit(qsizetype start, qsizetype end, const QString& inserted, bool recordUndo);
  bool applyEditRecord(const EditRecord& record, bool reverse);
  void resetGeometryIndex(bool preserveScroll = true);
  void updateScrollBars();
  void applyScrollBarStyle();
  int baseLineHeight() const;
  int textAreaWidth() const;
  qsizetype boundedOffset(qsizetype offset) const;
  qsizetype previousCharacterOffset(qsizetype offset) const;
  qsizetype nextCharacterOffset(qsizetype offset) const;
  qsizetype previousWordOffset(qsizetype offset) const;
  qsizetype nextWordOffset(qsizetype offset) const;
  qsizetype lineStart(int zeroBasedLine) const;
  qsizetype lineEnd(int zeroBasedLine) const;
  int lineForOffset(qsizetype offset) const;
  QString lineText(int zeroBasedLine) const;
  std::shared_ptr<LineLayout> buildLineLayout(int zeroBasedLine) const;
  int measuredLineHeight(int zeroBasedLine) const;
  void invalidateLayoutCache();
  QPoint contentPoint(const QPoint& viewportPoint) const;
  qsizetype offsetForPoint(const QPoint& viewportPoint) const;
  QRect cursorRectForOffset(qsizetype offset) const;
  void moveCursorTo(qsizetype position, bool keepAnchor);
  std::pair<qsizetype, qsizetype> wordRangeAt(qsizetype position) const;
  void resetCursorBlink();
  void emitCursorPosition();
  void scrollContentsBy(int dx, int dy) override;

  DocumentSession* session_ = nullptr;
  PieceTable standalone_;
  SourceLineHeightIndex heights_;
  SourceEditorColors colors_;
  QFont sourceFont_;
  QFont lineNumberFont_;
  QString placeholder_;
  QString documentPath_;
  QString preedit_;
  QVector<QTextLayout::FormatRange> preeditFormats_;
  int preeditCursor_ = -1;
  qsizetype cursor_ = 0;
  qsizetype anchor_ = 0;
  int preferredColumn_ = -1;
  bool wordWrap_ = true;
  bool draggingSelection_ = false;
  bool applyingEdit_ = false;
  bool readOnly_ = false;
  bool cursorVisible_ = true;
  QTimer* cursorTimer_ = nullptr;
  QVector<EditRecord> undoStack_;
  QVector<EditRecord> redoStack_;
  mutable QHash<int, std::shared_ptr<LineLayout>> layoutCache_;
  mutable QList<int> layoutLru_;
};

}  // namespace muffin
