#pragma once

#include <QAbstractScrollArea>
#include <QEvent>
#include <QGuiApplication>
#include <QHash>
#include <QInputMethod>
#include <QInputMethodQueryEvent>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <QVariant>
#include <utility>

namespace muffin {

// Custom-painted editors own their IME state. Publish it after the current edit/layout/paint
// finishes, and only for the focused editor. Geometry is also checked after viewport paints:
// lazy measurement and nested code scrolling can move the caret without a text edit. Comparing
// the queried values keeps caret blinking and decoration repaints from waking the platform IME.
class InputMethodState final : public QObject {
public:
  explicit InputMethodState(QAbstractScrollArea* editor) : QObject(editor), editor_(editor) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] { publish(); });
    editor_->installEventFilter(this);
    editor_->viewport()->installEventFilter(this);
    for (auto* bar : {editor_->horizontalScrollBar(), editor_->verticalScrollBar()})
      connect(bar, &QScrollBar::valueChanged, this, [this] { schedule(geometryQueries()); });
  }

  void schedule(Qt::InputMethodQueries queries = Qt::ImQueryInput | Qt::ImFont | Qt::ImEnabled |
                                                 Qt::ImAbsolutePosition) {
    if (QGuiApplication::focusObject() != editor_)
      return;
    pending_ |= queries;
    if (!timer_.isActive())
      timer_.start(0);
  }

private:
  static Qt::InputMethodQueries geometryQueries() { return Qt::ImCursorRectangle | Qt::ImInputItemClipRectangle; }

  bool eventFilter(QObject* object, QEvent* event) override {
    switch (event->type()) {
    case QEvent::FocusIn:
    case QEvent::Show:
    case QEvent::ParentChange:
      if (window_ != editor_->window()) {
        if (window_ && window_ != editor_)
          window_->removeEventFilter(this);
        window_ = editor_->window();
        if (window_ != editor_)
          window_->installEventFilter(this);
      }
      schedule();
      break;
    case QEvent::InputMethod:
      schedule();
      break;
    case QEvent::Paint:
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::DevicePixelRatioChange:
      schedule(geometryQueries());
      break;
    case QEvent::FocusOut:
    case QEvent::Hide:
      if (object == editor_) {
        timer_.stop();
        pending_ = {};
        values_.clear();
      }
      break;
    default:
      break;
    }
    return false;
  }

  void publish() {
    const auto queries = std::exchange(pending_, {});
    if (!editor_->isVisible() || QGuiApplication::focusObject() != editor_) {
      values_.clear();
      return;
    }
    QInputMethodQueryEvent query(queries);
    QCoreApplication::sendEvent(editor_, &query);
    Qt::InputMethodQueries changed;
    for (uint bits = queries.toInt(); bits; bits &= bits - 1) {
      const uint bit = bits & (~bits + 1);
      const auto attribute = static_cast<Qt::InputMethodQuery>(bit);
      const auto value = query.value(attribute);
      if (!values_.contains(bit) || values_.value(bit) != value) {
        values_.insert(bit, value);
        changed |= attribute;
      }
    }
    // xcb/IBus consumes global native coordinates. A top-level move or a screen/DPR change
    // needs a notification even when the caret's editor-local rectangle is unchanged.
    const auto origin = editor_->mapToGlobal(QPoint());
    const auto dpr = editor_->devicePixelRatioF();
    if (origin != origin_ || dpr != dpr_)
      changed |= Qt::ImCursorRectangle;
    origin_ = origin;
    dpr_ = dpr;
    if (changed)
      QGuiApplication::inputMethod()->update(changed);
  }

  QAbstractScrollArea* editor_;
  QPointer<QWidget> window_;
  QTimer timer_;
  Qt::InputMethodQueries pending_;
  QHash<uint, QVariant> values_;
  QPoint origin_;
  qreal dpr_ = 0;
};

} // namespace muffin
