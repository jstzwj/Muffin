#include "editor/EditorAccessibility.h"

#include "document/DocumentSession.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "editor/VirtualSourceEdit.h"
#include "editor/WordBoundary.h"

#include <QAccessibleWidget>
#include <QHash>
#include <QMutex>
#include <QPointer>
#include <QRect>

#include <QAccessible>
#include <QString>
#include <QStringView>

// Screen-reader adapters for the two editor canvases. Neither canvas is a QTextEdit — they are
// custom-painted QAbstractScrollAreas — so both get a QAccessibleWidget-derived editable-text
// adapter exposing the markdown source as the accessible text with UTF-16 source offsets as
// the character positions. Source offsets (not the projected visible text) are the stable,
// round-trippable coordinate space; screen readers will therefore read the raw markdown syntax
// ("**bold**"), which matches source mode and is honest about what editing does.

namespace {

using namespace muffin;

QMutex& registryMutex() {
  static QMutex mutex;
  return mutex;
}

QHash<const EditorView*, QPointer<EditorController>>& controllerRegistry() {
  static QHash<const EditorView*, QPointer<EditorController>> registry;
  return registry;
}

// --- shared editable-text helpers ----------------------------------------------------------

// Word/line boundary resolution over a cached flat text. Line boundaries are '\n' scans;
// words use the shared programmer-word semantics from WordBoundary.h.
struct TextBoundaries {
  const QString* text;

  void lineAt(qsizetype offset, qsizetype& start, qsizetype& end) const {
    start = text->lastIndexOf(QLatin1Char('\n'), offset - 1) + 1;
    const qsizetype nl = text->indexOf(QLatin1Char('\n'), offset);
    end = nl < 0 ? text->size() : nl;
    if (start < 0) start = 0;
  }

  void wordAt(qsizetype offset, qsizetype& start, qsizetype& end) const {
    const qsizetype lineStart = text->lastIndexOf(QLatin1Char('\n'), offset - 1) + 1;
    const qsizetype nl = text->indexOf(QLatin1Char('\n'), offset);
    const qsizetype lineEnd = nl < 0 ? text->size() : nl;
    start = lineStart;
    end = lineEnd;
    if (offset < lineStart || offset > lineEnd) {
      return;
    }
    const auto range = words::wordRangeAt(QStringView(*text), offset, lineStart, lineEnd);
    start = range.first;
    end = range.second;
  }
};

QString boundaryText(const QString& text, QAccessible::TextBoundaryType boundary, int offset,
                     int* startOffset, int* endOffset) {
  const TextBoundaries bounds{&text};
  const qsizetype clamped = offset < 0 ? qsizetype(0) : (offset > text.size() ? text.size() : qsizetype(offset));
  qsizetype start = 0;
  qsizetype end = 0;
  if (boundary == QAccessible::CharBoundary) {
    // Step a full grapheme-ish unit: at least one UTF-16 code unit, surrogate pairs stay whole.
    if (clamped < text.size() && text.at(clamped).isHighSurrogate() && clamped + 1 < text.size() &&
        text.at(clamped + 1).isLowSurrogate()) {
      end = clamped + 2;
    } else {
      end = qMin<qsizetype>(text.size(), clamped + 1);
    }
    start = clamped;
  } else if (boundary == QAccessible::LineBoundary) {
    bounds.lineAt(clamped, start, end);
  } else if (boundary == QAccessible::WordBoundary) {
    bounds.wordAt(clamped, start, end);
  } else {  // ParagraphBoundary / SentenceBoundary / NoBoundary
    bounds.lineAt(clamped, start, end);
  }
  *startOffset = static_cast<int>(start);
  *endOffset = static_cast<int>(end);
  return text.mid(start, end - start);
}

// --- rendered-mode adapter ------------------------------------------------------------------

class EditorViewAccessible final : public QAccessibleWidget, public QAccessibleTextInterface {
public:
  explicit EditorViewAccessible(EditorView* view)
      : QAccessibleWidget(view, QAccessible::EditableText), view_(view) {}

  // QAccessibleInterface
  QAccessible::Role role() const override { return QAccessible::EditableText; }
  QAccessible::State state() const override {
    QAccessible::State s = QAccessibleWidget::state();
    s.editable = true;
    s.multiLine = true;
    s.selectableText = true;
    return s;
  }
  QString text(QAccessible::Text t) const override {
    if (t == QAccessible::Value) {
      return flatText();
    }
    return QAccessibleWidget::text(t);
  }
  void* interface_cast(QAccessible::InterfaceType type) override {
    if (type == QAccessible::TextInterface) {
      return static_cast<QAccessibleTextInterface*>(this);
    }
    return QAccessibleWidget::interface_cast(type);
  }

  // QAccessibleTextInterface
  void addSelection(int startOffset, int endOffset) override {
    if (EditorController* controller = editorController()) {
      controller->selectSourceRange(startOffset, endOffset);
    }
  }
  QString attributes(int offset, int* startOffset, int* endOffset) const override {
    *startOffset = offset;
    *endOffset = offset + 1;
    return QString();
  }
  int cursorPosition() const override {
    if (EditorController* controller = editorController()) {
      const CursorPosition cursor = controller->selection().cursorPosition();
      if (cursor.isValid() && cursor.text.sourceOffset >= 0) {
        return static_cast<int>(cursor.text.sourceOffset);
      }
    }
    return 0;
  }
  QRect characterRect(int offset) const override {
    // Full caret-quality rects per offset would need a document-wide mapping per query; give the
    // caret rect at/near the offset via the hit machinery, falling back to the widget rect.
    if (EditorController* c = editorController()) {
      const CursorPosition pos = c->cursorForSourceOffset(offset);
      if (pos.isValid() && view_) {
        const HitTestResult hit = view_->hitForCursorPosition(pos);
        if (!hit.cursorRect.isEmpty()) {
          const QRect caret = hit.cursorRect.toAlignedRect();
          return QRect(view_->mapToGlobal(caret.topLeft()), caret.size());
        }
      }
    }
    return QAccessibleWidget::rect();
  }
  int offsetAtPoint(const QPoint& point) const override {
    // Screen-reader hit testing: without a full offset↔geometry map, answer with the caret only
    // when the point is inside the widget.
    if (view_ && view_->rect().contains(view_->mapFromGlobal(point))) {
      return cursorPosition();
    }
    return -1;
  }
  void removeSelection(int selectionNumber) override {
    if (selectionNumber != 0) {
      return;
    }
    if (EditorController* controller = editorController()) {
      const CursorPosition cursor = controller->selection().cursorPosition();
      if (cursor.isValid()) {
        controller->selection().setCursorPosition(cursor);
      }
    }
  }
  void scrollToSubstring(int startIndex, int endIndex) override {
    if (EditorController* controller = editorController()) {
      controller->setCursorForSourceOffset(startIndex);
      if (view_) {
        view_->scrollToCursorCentered();
      }
    }
    Q_UNUSED(endIndex);
  }
  int selectionCount() const override {
    qsizetype start = 0;
    qsizetype end = 0;
    const EditorController* controller = editorController();
    return controller && controller->selectionSourceRange(start, end) && end > start ? 1 : 0;
  }
  void selection(int selectionNumber, int* startOffset, int* endOffset) const override {
    if (selectionNumber != 0) {
      *startOffset = 0;
      *endOffset = 0;
      return;
    }
    qsizetype start = 0;
    qsizetype end = 0;
    const EditorController* controller = editorController();
    if (!controller || !controller->selectionSourceRange(start, end) || end <= start) {
      *startOffset = 0;
      *endOffset = 0;
      return;
    }
    *startOffset = static_cast<int>(start);
    *endOffset = static_cast<int>(end);
  }
  void setSelection(int selectionNumber, int startOffset, int endOffset) override {
    if (selectionNumber == 0) {
      if (EditorController* controller = editorController()) {
        controller->selectSourceRange(startOffset, endOffset);
      }
    }
  }
  int characterCount() const override {
    return static_cast<int>(flatText().size());
  }
  void setCursorPosition(int position) override {
    if (EditorController* controller = editorController()) {
      controller->setCursorForSourceOffset(position);
    }
  }
  QString text(int startOffset, int endOffset) const override {
    const QString flat = flatText();
    return flat.mid(qBound(0, startOffset, static_cast<int>(flat.size())),
                    qMax(0, endOffset - startOffset));
  }
  QString textBeforeOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset,
                           int* endOffset) const override {
    const QString flat = flatText();
    QString piece = boundaryText(flat, boundaryType, offset, startOffset, endOffset);
    if (*startOffset > 0) {
      piece = boundaryText(flat, boundaryType, *startOffset - 1, startOffset, endOffset);
    }
    return piece;
  }
  QString textAfterOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset,
                          int* endOffset) const override {
    const QString flat = flatText();
    QString piece = boundaryText(flat, boundaryType, offset, startOffset, endOffset);
    if (*endOffset < static_cast<int>(flat.size())) {
      piece = boundaryText(flat, boundaryType, *endOffset, startOffset, endOffset);
    }
    return piece;
  }
  QString textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset,
                       int* endOffset) const override {
    return boundaryText(flatText(), boundaryType, offset, startOffset, endOffset);
  }

private:
  EditorController* editorController() const { return a11y::controllerFor(view_); }

  // Flattened document text cached on the document revision — never flatten per query.
  const QString& flatText() const {
    EditorController* c = editorController();
    const DocumentSession* session = c ? c->session() : nullptr;
    if (!session) {
      static const QString empty;
      return empty;
    }
    const quint64 revision = session->document().revision();
    if (revision != cachedRevision_) {
      cachedText_ = session->markdownText().toString();
      cachedRevision_ = revision;
    }
    return cachedText_;
  }

  EditorView* view_ = nullptr;
  mutable QString cachedText_;
  mutable quint64 cachedRevision_ = ~static_cast<quint64>(0);
};

// --- source-mode adapter --------------------------------------------------------------------

class VirtualSourceEditAccessible final : public QAccessibleWidget, public QAccessibleTextInterface {
public:
  explicit VirtualSourceEditAccessible(VirtualSourceEdit* edit)
      : QAccessibleWidget(edit, QAccessible::EditableText), edit_(edit) {}

  QAccessible::Role role() const override { return QAccessible::EditableText; }
  QAccessible::State state() const override {
    QAccessible::State s = QAccessibleWidget::state();
    s.editable = !edit_->isReadOnly();
    s.multiLine = true;
    s.selectableText = true;
    return s;
  }
  QString text(QAccessible::Text t) const override {
    if (t == QAccessible::Value) {
      return edit_->text();
    }
    return QAccessibleWidget::text(t);
  }
  void* interface_cast(QAccessible::InterfaceType type) override {
    if (type == QAccessible::TextInterface) {
      return static_cast<QAccessibleTextInterface*>(this);
    }
    return QAccessibleWidget::interface_cast(type);
  }

  void addSelection(int startOffset, int endOffset) override {
    edit_->setSelection(startOffset, endOffset);
  }
  QString attributes(int offset, int* startOffset, int* endOffset) const override {
    *startOffset = offset;
    *endOffset = offset + 1;
    return QString();
  }
  int cursorPosition() const override { return static_cast<int>(edit_->cursorPosition()); }
  QRect characterRect(int offset) const override {
    Q_UNUSED(offset);
    return QAccessibleWidget::rect();
  }
  int offsetAtPoint(const QPoint& point) const override {
    if (edit_->rect().contains(edit_->mapFromGlobal(point))) {
      return cursorPosition();
    }
    return -1;
  }
  void removeSelection(int selectionNumber) override {
    if (selectionNumber == 0) {
      edit_->setCursorPosition(edit_->cursorPosition(), false);
    }
  }
  void scrollToSubstring(int startIndex, int endIndex) override {
    edit_->setCursorPosition(startIndex, false);
    edit_->ensureCursorVisible();
    Q_UNUSED(endIndex);
  }
  int selectionCount() const override { return edit_->hasSelection() ? 1 : 0; }
  void selection(int selectionNumber, int* startOffset, int* endOffset) const override {
    if (selectionNumber != 0 || !edit_->hasSelection()) {
      *startOffset = 0;
      *endOffset = 0;
      return;
    }
    *startOffset = static_cast<int>(edit_->selectionStart());
    *endOffset = static_cast<int>(edit_->selectionEnd());
  }
  void setSelection(int selectionNumber, int startOffset, int endOffset) override {
    if (selectionNumber == 0) {
      edit_->setSelection(startOffset, endOffset);
    }
  }
  int characterCount() const override { return static_cast<int>(edit_->text().size()); }
  void setCursorPosition(int position) override { edit_->setCursorPosition(position, false); }
  QString text(int startOffset, int endOffset) const override {
    const QString flat = edit_->text();
    return flat.mid(qBound(0, startOffset, static_cast<int>(flat.size())),
                    qMax(0, endOffset - startOffset));
  }
  QString textBeforeOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset,
                           int* endOffset) const override {
    const QString flat = edit_->text();
    QString piece = boundaryText(flat, boundaryType, offset, startOffset, endOffset);
    if (*startOffset > 0) {
      piece = boundaryText(flat, boundaryType, *startOffset - 1, startOffset, endOffset);
    }
    return piece;
  }
  QString textAfterOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset,
                          int* endOffset) const override {
    const QString flat = edit_->text();
    QString piece = boundaryText(flat, boundaryType, offset, startOffset, endOffset);
    if (*endOffset < static_cast<int>(flat.size())) {
      piece = boundaryText(flat, boundaryType, *endOffset, startOffset, endOffset);
    }
    return piece;
  }
  QString textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset,
                       int* endOffset) const override {
    return boundaryText(edit_->text(), boundaryType, offset, startOffset, endOffset);
  }

private:
  VirtualSourceEdit* edit_ = nullptr;
};

QAccessibleInterface* editorAccessibleFactory(const QString& classname, QObject* object) {
  if (!object || !object->isWidgetType()) {
    return nullptr;
  }
  if (classname == QLatin1String("muffin::EditorView")) {
    return new EditorViewAccessible(static_cast<EditorView*>(object));
  }
  if (classname == QLatin1String("muffin::VirtualSourceEdit")) {
    return new VirtualSourceEditAccessible(static_cast<VirtualSourceEdit*>(object));
  }
  return nullptr;
}

}  // namespace

namespace muffin {
namespace a11y {

void registerController(EditorView* view, EditorController* controller) {
  if (!view) {
    return;
  }
  QMutexLocker lock(&registryMutex());
  controllerRegistry()[view] = controller;
}

void unregisterController(EditorView* view) {
  if (!view) {
    return;
  }
  QMutexLocker lock(&registryMutex());
  controllerRegistry().remove(view);
}

EditorController* controllerFor(const EditorView* view) {
  if (!view) {
    return nullptr;
  }
  QMutexLocker lock(&registryMutex());
  return controllerRegistry().value(view, nullptr);
}

}  // namespace a11y

void installEditorAccessibility() {
  QAccessible::installFactory(&editorAccessibleFactory);
}

}  // namespace muffin
