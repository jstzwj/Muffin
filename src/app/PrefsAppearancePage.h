#pragma once

#include "app/PreferencesPage.h"

#include <QPair>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace muffin {

class PrefsAppearancePage final : public PreferencesPage {
  Q_OBJECT

public:
  explicit PrefsAppearancePage(QWidget* parent = nullptr);

  void retranslateUi() override;

  // Populate the theme dropdown. Each entry is an (id, display-label) pair so
  // imported custom themes show their authored label rather than the raw id.
  void setAvailableThemes(const QVector<QPair<QString, QString>>& themes);
  void setCurrentThemeName(const QString& name);
  void setStatusBarVisible(bool visible);
  void setZoomPercent(int percent);
  void setFontSizePx(int px);
  void setContentWidthPx(int px);

signals:
  void themeRequested(QString name);
  // "Import Theme..." clicked — MainWindow handles the file picker + reload, then
  // refreshes the dropdown via setAvailableThemes.
  void importThemeRequested();
  void statusBarVisibleRequested(bool visible);
  void zoomPercentRequested(int percent);
  void fontSizePxRequested(int px);
  void contentWidthPxRequested(int px);

private:
  void addNumberItems(QComboBox* combo, const QVector<int>& values, const QString& suffix) const;
  static void setNumberComboValue(QComboBox* combo, int value);

  QLabel* themeLabel_ = nullptr;
  QComboBox* themeCombo_ = nullptr;
  QPushButton* importThemeButton_ = nullptr;
  QVector<QPair<QString, QString>> themes_;  // (id, label) backing the dropdown
  QLabel* zoomLabel_ = nullptr;
  QComboBox* zoomCombo_ = nullptr;
  QPushButton* resetZoomButton_ = nullptr;
  QLabel* fontSizeLabel_ = nullptr;
  QComboBox* fontSizeCombo_ = nullptr;
  QLabel* contentWidthLabel_ = nullptr;
  QComboBox* contentWidthCombo_ = nullptr;
  QLabel* statusBarLabel_ = nullptr;
  QCheckBox* showStatusBarCheck_ = nullptr;
};

}  // namespace muffin
