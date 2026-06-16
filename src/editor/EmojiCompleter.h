#pragma once

#include "editor/EmojiProvider.h"

#include <QObject>
#include <QPoint>
#include <QString>
#include <QVector>

class QListView;
class QStringListModel;
class QWidget;

namespace muffin {

// GitHub-style ":shortcode:" emoji completion popup. A floating list positioned at the caret that
// filters the provider's dataset by the shortcode prefix typed so far. Keyboard navigation
// (Up/Down/Enter/Tab/Esc) is driven by InputController::handleKeyPress while the popup is visible.
//
// The controller (InputController) owns the trigger state — the source offset of the leading ':' —
// so this widget is purely presentational: it shows filtered candidates and emits `accepted` with
// the chosen glyph when the user picks one.
class EmojiCompleter : public QObject {
  Q_OBJECT

public:
  EmojiCompleter(QWidget* viewport, const EmojiProvider* provider, QObject* parent = nullptr);

  // Refresh the candidate list for `prefix` and show the popup at `caretViewportPos` (viewport
  // coordinates, typically the caret's bottom-left). Hides itself when there is nothing to show.
  void present(const QString& prefix, const QPoint& caretViewportPos);
  bool isVisible() const;
  void hide();
  void moveSelection(int delta);
  void acceptCurrent();

signals:
  void accepted(QString glyph);

private:
  void ensurePopup();

  QWidget* viewport_;
  const EmojiProvider* provider_;
  QListView* popup_ = nullptr;
  QStringListModel* model_ = nullptr;
  QVector<EmojiEntry> entries_;  // parallel to model rows
  bool active_ = false;  // whether a candidate list is currently presented (owned, not Qt-visibility)
};

}  // namespace muffin
