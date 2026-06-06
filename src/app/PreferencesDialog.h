#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace muffin {

class PreferencesDialog final : public QDialog {
  Q_OBJECT

public:
  explicit PreferencesDialog(QWidget* parent = nullptr);

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

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();
  void populateLanguages();
  QWidget* makePage(QWidget* parent);
  QLabel* makeSectionLabel(QWidget* parent) const;
  QLabel* makeMutedLabel(QWidget* parent) const;
  QPushButton* makeButton(QWidget* parent) const;
  QString themeDisplayName(const QString& name) const;
  void addNumberItems(QComboBox* combo, const QVector<int>& values, const QString& suffix);
  void setNumberComboValue(QComboBox* combo, int value);
  void addPlaceholderPage();
  void buildAppearancePage();

  QListWidget* categoryList_ = nullptr;
  QStackedWidget* contentStack_ = nullptr;
  QVector<QLabel*> pageTitleLabels_;
  QVector<QLabel*> placeholderLabels_;

  QLabel* appearanceTitleLabel_ = nullptr;
  QLabel* themeLabel_ = nullptr;
  QComboBox* themeCombo_ = nullptr;
  QLabel* zoomLabel_ = nullptr;
  QComboBox* zoomCombo_ = nullptr;
  QPushButton* resetZoomButton_ = nullptr;
  QLabel* fontSizeLabel_ = nullptr;
  QComboBox* fontSizeCombo_ = nullptr;
  QLabel* statusBarLabel_ = nullptr;
  QCheckBox* showStatusBarCheck_ = nullptr;

  QLabel* generalTitleLabel_ = nullptr;
  QLabel* languageLabel_ = nullptr;
  QComboBox* languageCombo_ = nullptr;
  QLabel* updateLabel_ = nullptr;
  QPushButton* checkUpdateButton_ = nullptr;
  QCheckBox* autoUpdateCheck_ = nullptr;
  QCheckBox* betaUpdateCheck_ = nullptr;
  QLabel* advancedLabel_ = nullptr;
  QCheckBox* debugModeCheck_ = nullptr;
  QCheckBox* telemetryCheck_ = nullptr;
  QPushButton* openAdvancedButton_ = nullptr;
  QPushButton* resetAdvancedButton_ = nullptr;
};

}  // namespace muffin
