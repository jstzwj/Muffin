#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QStackedWidget;
class QWidget;

namespace muffin {

class PrefsAppearancePage;
class PrefsEditorPage;
class PrefsFilesPage;

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
  void clearRecentFilesRequested();
  void disableTypewriterFocusRequested();

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();
  QWidget* makePage(QWidget* parent);
  void addPlaceholderPage();

  QListWidget* categoryList_ = nullptr;
  QStackedWidget* contentStack_ = nullptr;
  QVector<QLabel*> pageTitleLabels_;
  QVector<QLabel*> placeholderLabels_;

  PrefsFilesPage* filesPage_ = nullptr;
  PrefsEditorPage* editorPage_ = nullptr;
  PrefsAppearancePage* appearancePage_ = nullptr;
};

}  // namespace muffin
