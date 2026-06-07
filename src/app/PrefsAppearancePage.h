#pragma once

#include "app/PreferencesPage.h"

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

  void setAvailableThemes(const QStringList& themes);
  void setCurrentThemeName(const QString& name);
  void setStatusBarVisible(bool visible);
  void setZoomPercent(int percent);
  void setFontSizePx(int px);

signals:
  void themeRequested(QString name);
  void statusBarVisibleRequested(bool visible);
  void zoomPercentRequested(int percent);
  void fontSizePxRequested(int px);

private:
  static QString themeDisplayName(const QString& name);
  static void addNumberItems(QComboBox* combo, const QVector<int>& values, const QString& suffix);
  static void setNumberComboValue(QComboBox* combo, int value);

  QLabel* themeLabel_ = nullptr;
  QComboBox* themeCombo_ = nullptr;
  QLabel* zoomLabel_ = nullptr;
  QComboBox* zoomCombo_ = nullptr;
  QPushButton* resetZoomButton_ = nullptr;
  QLabel* fontSizeLabel_ = nullptr;
  QComboBox* fontSizeCombo_ = nullptr;
  QLabel* statusBarLabel_ = nullptr;
  QCheckBox* showStatusBarCheck_ = nullptr;
};

}  // namespace muffin
