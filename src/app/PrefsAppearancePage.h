#pragma once

#include "app/PreferencesPage.h"

#include <QPair>
#include <QVector>
#include "theme/ThemeDefinition.h"
#include <array>

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
  void setPreviewTheme(const ThemeDefinition& definition);

signals:
  void themeRequested(QString name);
  // "Import Theme..." clicked — MainWindow handles the file picker + reload, then
  // refreshes the dropdown via setAvailableThemes.
  void importThemeRequested();
  void statusBarVisibleRequested(bool visible);
  void zoomPercentRequested(int percent);
  void fontSizePxRequested(int px);
  void contentWidthPxRequested(int px);
  void typographyChanged();

private:
  void addNumberItems(QComboBox* combo, const QVector<int>& values, const QString& suffix) const;
  static void setNumberComboValue(QComboBox* combo, int value);
  void loadTypography();
  void saveTypography();
  void updateThemeFontSamples();

  QLabel* themeLabel_ = nullptr;
  QComboBox* themeCombo_ = nullptr;
  QPushButton* importThemeButton_ = nullptr;
  QVector<QPair<QString, QString>> themes_; // (id, label) backing the dropdown
  QLabel* zoomLabel_ = nullptr;
  QComboBox* zoomCombo_ = nullptr;
  QPushButton* resetZoomButton_ = nullptr;
  QLabel* fontSizeLabel_ = nullptr;
  QComboBox* fontSizeCombo_ = nullptr;
  QLabel* contentWidthLabel_ = nullptr;
  QComboBox* contentWidthCombo_ = nullptr;
  QLabel* statusBarLabel_ = nullptr;
  QCheckBox* showStatusBarCheck_ = nullptr;
  QLabel* typographyLabel_ = nullptr;
  QLabel* typographyNote_ = nullptr;
  std::array<QLabel*, 3> typographySections_{};
  std::array<QLabel*, 2> familyLabels_{};
  std::array<QComboBox*, 2> familyCombos_{};
  std::array<QLabel*, 9> sizeLabels_{};
  std::array<QComboBox*, 9> sizeCombos_{};
  QPushButton* resetFontsButton_ = nullptr;
  ThemeDefinition previewTheme_;
};

} // namespace muffin
