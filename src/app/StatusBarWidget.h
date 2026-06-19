#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class QMenu;
class QToolButton;

namespace muffin {

// Statistics shown in the click-to-open detail popup.
struct StatusBarStats {
  int words = 0;
  qsizetype characters = 0;
  int lines = 0;
  int readingMinutes = 0;
  // Selection breakdown; negative means "no selection" (row hidden).
  int selectedWords = -1;
  qsizetype selectedCharacters = -1;
};

// A self-painted status bar replacing the native QStatusBar + QLabel/QToolButton
// stack. Paints a flat themed strip with: two real QToolButtons on the left
// (sidebar / source-mode toggles), an optional block-source preview in the
// middle, and a right-aligned cluster (encoding·line-ending, a clickable stats
// trigger, cursor position, and a clickable spell-check language button that
// opens a language menu). Clicking the stats trigger emits statsClicked() so the
// owner can compute counts and call showStatsPopup().
class StatusBarWidget : public QWidget {
  Q_OBJECT

public:
  explicit StatusBarWidget(QWidget* parent = nullptr);

  // Real child buttons (kept as QToolButtons for reliable click/hover/checked);
  // the owner wires them exactly as before.
  QToolButton* sidebarButton() const;
  QToolButton* sourceModeButton() const;

  // Painted-segment setters.
  void setCursorStatus(const QString& text);
  void setWordCount(int words);            // drives the compact "N words" trigger.
  void setBlockSource(const QString& text, const QString& toolTip);
  void setEncodingLineEnding(const QString& text);
  void setSpellLanguage(const QString& localeCode, bool enabled);
  void setSidebarChecked(bool checked);
  void setSourceModeChecked(bool checked);

  // Per-theme colors; also recolors the button icons and re-styles the buttons.
  void applyThemeColors(const QColor& bg, const QColor& text, const QColor& muted, const QColor& border);

  // Pop up the detail stats panel anchored above the stats trigger.
  void showStatsPopup(const StatusBarStats& stats);

  // Localize labels/menu/popup. Call from the owner's retranslateUi().
  void retranslateUi();

signals:
  // Emitted when the user clicks the compact stats segment.
  void statsClicked();

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

private:
  enum class Hover { None, Stats, Spell };

  struct SegmentRects {
    QRect encoding;
    QRect stats;
    QRect cursor;
    QRect spell;
    QRect blockSource;
    int leftStart = 0;
    int rightEdge = 0;
  };

  void relayoutButtons();
  SegmentRects layoutSegments() const;
  void drawSegmentText(QPainter& p, const QString& text, const QRect& rect, const QColor& color,
                       int flags = Qt::AlignVCenter | Qt::AlignRight) const;
  void ensureSpellMenu();
  void rebuildSpellMenu();

  QToolButton* sidebarButton_ = nullptr;
  QToolButton* sourceModeButton_ = nullptr;
  QMenu* spellMenu_ = nullptr;

  QString cursorStatus_;
  QString wordCountText_;
  int wordCount_ = 0;
  QString blockSource_;
  QString blockSourceToolTip_;
  QString encodingLineEnding_;
  QString spellLocaleCode_;
  QString spellDisplayLabel_;
  bool spellEnabled_ = true;

  QColor bg_;
  QColor text_;
  QColor muted_;
  QColor border_;

  Hover hover_ = Hover::None;
};

}  // namespace muffin
