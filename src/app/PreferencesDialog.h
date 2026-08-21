#pragma once

#include "theme/ThemeDefinition.h"

#include <QDialog>
#include <QPair>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QScrollArea;
class QStackedWidget;
class QWidget;

namespace muffin {

class PrefsAppearancePage;
class PrefsEditorPage;
class PrefsExportPage;
class PrefsFilesPage;
class PrefsImagePage;
class PrefsMarkdownPage;

class PreferencesDialog final : public QDialog {
  Q_OBJECT

public:
  explicit PreferencesDialog(QWidget* parent = nullptr);

  void setAvailableThemes(const QVector<QPair<QString, QString>>& themes);
  void setCurrentThemeName(const QString& name);
  // Theme the dialog chrome (canvas, cards, every control) from the unified
  // definition. Called by MainWindow before exec() so Preferences follows the
  // active theme instead of always rendering light.
  void setThemeDefinition(const ThemeDefinition& definition);
  void setStatusBarVisible(bool visible);
  void setZoomPercent(int percent);
  void setFontSizePx(int px);
  void setContentWidthPx(int px);

signals:
  void themeRequested(QString name);
  void importThemeRequested();
  void statusBarVisibleRequested(bool visible);
  void zoomPercentRequested(int percent);
  void fontSizePxRequested(int px);
  void contentWidthPxRequested(int px);
  void clearRecentFilesRequested();
  void outlineFoldableChanged(bool foldable);
  void restoreDraftsRequested();
  void disableTypewriterFocusRequested();

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();
  QWidget* makePage(QWidget* parent);
  void addPlaceholderPage();
  QScrollArea* makeScrollArea();

  QListWidget* categoryList_ = nullptr;
  QStackedWidget* contentStack_ = nullptr;
  QLabel* sidebarTitleLabel_ = nullptr;
  QVector<QLabel*> pageTitleLabels_;
  QVector<QLabel*> placeholderLabels_;
  ThemeDefinition themeDefinition_;

  PrefsFilesPage* filesPage_ = nullptr;
  PrefsEditorPage* editorPage_ = nullptr;
  PrefsImagePage* imagePage_ = nullptr;
  PrefsMarkdownPage* markdownPage_ = nullptr;
  PrefsExportPage* exportPage_ = nullptr;
  PrefsAppearancePage* appearancePage_ = nullptr;
};

}  // namespace muffin
