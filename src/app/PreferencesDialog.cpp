#include "app/PreferencesDialog.h"

#include "theme/ChromeStyleSheet.h"

#include "app/LanguageManager.h"
#include "app/PrefsAppearancePage.h"
#include "app/PrefsEditorPage.h"
#include "app/PrefsFilesPage.h"
#include "app/PrefsImagePage.h"
#include "app/PrefsGeneralPage.h"
#include "app/PrefsMarkdownPage.h"
#include "app/PrefsExportPage.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
// Stable internal keys for the preferences categories, in the same order as the category list built
// in retranslateUi(). Persisted to QSettings so the dialog reopens on the last-viewed page instead
// of always landing on General. The translated labels can't be used as keys — they change with the
// UI language.
const QStringList kCategoryKeys = {
    QStringLiteral("general"),
    QStringLiteral("appearance"),
    QStringLiteral("editor"),
    QStringLiteral("markdown"),
    QStringLiteral("image"),
    QStringLiteral("files"),
    QStringLiteral("export"),
};
const QString kLastCategorySetting = QStringLiteral("preferences/lastCategory");
}  // namespace

muffin::PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
  setModal(true);
  setMinimumSize(880, 620);
  resize(1040, 720);
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  // Default to the github theme; MainWindow re-themes via setThemeDefinition()
  // before exec() so the dialog matches the active theme.
  themeDefinition_ = ThemeDefinition::builtIn(QStringLiteral("github")).value();
  setStyleSheet(dialogStyleSheet(themeDefinition_));

  auto* rootLayout = new QHBoxLayout(this);
  rootLayout->setContentsMargins(24, 24, 24, 24);
  rootLayout->setSpacing(14);

  auto* sidebar = new QWidget(this);
  sidebar->setObjectName(QStringLiteral("preferencesSidebar"));
  sidebar->setFixedWidth(216);
  auto* sidebarLayout = new QVBoxLayout(sidebar);
  sidebarLayout->setContentsMargins(10, 18, 10, 14);
  sidebarLayout->setSpacing(10);

  sidebarTitleLabel_ = new QLabel(sidebar);
  sidebarTitleLabel_->setObjectName(QStringLiteral("preferencesSidebarTitle"));
  sidebarLayout->addWidget(sidebarTitleLabel_);

  categoryList_ = new QListWidget(sidebar);
  categoryList_->setFocusPolicy(Qt::NoFocus);
  sidebarLayout->addWidget(categoryList_, 1);
  rootLayout->addWidget(sidebar);

  contentStack_ = new QStackedWidget(this);
  contentStack_->setStyleSheet(QStringLiteral("QStackedWidget { background:transparent; }"));
  rootLayout->addWidget(contentStack_, 1);

  // Page 0: General
  {
    auto* scroll = makeScrollArea();
    auto* generalPage = new PrefsGeneralPage(scroll);
    scroll->setWidget(generalPage);
    contentStack_->addWidget(scroll);
  }

  // Page 1: Appearance
  {
    auto* scroll = makeScrollArea();
    appearancePage_ = new PrefsAppearancePage(scroll);
    scroll->setWidget(appearancePage_);
    contentStack_->addWidget(scroll);
  }

  // Page 2: Editor
  {
    auto* scroll = makeScrollArea();
    editorPage_ = new PrefsEditorPage(scroll);
    scroll->setWidget(editorPage_);
    contentStack_->addWidget(scroll);
  }

  // Page 3: Markdown
  {
    auto* scroll = makeScrollArea();
    markdownPage_ = new PrefsMarkdownPage(scroll);
    scroll->setWidget(markdownPage_);
    contentStack_->addWidget(scroll);
  }

  // Page 4: Image
  {
    auto* scroll = makeScrollArea();
    imagePage_ = new PrefsImagePage(scroll);
    scroll->setWidget(imagePage_);
    contentStack_->addWidget(scroll);
  }

  // Page 5: Files
  {
    auto* scroll = makeScrollArea();
    filesPage_ = new PrefsFilesPage(scroll);
    scroll->setWidget(filesPage_);
    contentStack_->addWidget(scroll);
  }

  // Page 6: Export
  {
    auto* scroll = makeScrollArea();
    exportPage_ = new PrefsExportPage(scroll);
    scroll->setWidget(exportPage_);
    contentStack_->addWidget(scroll);
  }

  connect(categoryList_, &QListWidget::currentRowChanged, contentStack_, &QStackedWidget::setCurrentIndex);
  // Remember the last-viewed category so the dialog reopens on it next time. retranslateUi sets the
  // row with signals blocked, so this only fires on genuine user navigation.
  connect(categoryList_, &QListWidget::currentRowChanged, this, [](int row) {
    if (row >= 0 && row < kCategoryKeys.size()) {
      QSettings().setValue(kLastCategorySetting, kCategoryKeys.at(row));
    }
  });

  // Forward page signals
  connect(filesPage_, &PrefsFilesPage::clearRecentFilesRequested,
          this, &PreferencesDialog::clearRecentFilesRequested);
  connect(filesPage_, &PrefsFilesPage::outlineFoldableChanged,
          this, &PreferencesDialog::outlineFoldableChanged);
  connect(filesPage_, &PrefsFilesPage::restoreDraftsRequested,
          this, &PreferencesDialog::restoreDraftsRequested);
  connect(editorPage_, &PrefsEditorPage::disableTypewriterFocusRequested,
          this, &PreferencesDialog::disableTypewriterFocusRequested);
  connect(appearancePage_, &PrefsAppearancePage::themeRequested,
          this, &PreferencesDialog::themeRequested);
  connect(appearancePage_, &PrefsAppearancePage::importThemeRequested,
          this, &PreferencesDialog::importThemeRequested);
  connect(appearancePage_, &PrefsAppearancePage::statusBarVisibleRequested,
          this, &PreferencesDialog::statusBarVisibleRequested);
  connect(appearancePage_, &PrefsAppearancePage::zoomPercentRequested,
          this, &PreferencesDialog::zoomPercentRequested);
  connect(appearancePage_, &PrefsAppearancePage::fontSizePxRequested,
          this, &PreferencesDialog::fontSizePxRequested);

  retranslateUi();
}

void muffin::PreferencesDialog::setAvailableThemes(const QVector<QPair<QString, QString>>& themes) {
  if (appearancePage_) {
    appearancePage_->setAvailableThemes(themes);
  }
}

void muffin::PreferencesDialog::setCurrentThemeName(const QString& name) {
  if (appearancePage_) {
    appearancePage_->setCurrentThemeName(name);
  }
}

void muffin::PreferencesDialog::setThemeDefinition(const ThemeDefinition& definition) {
  themeDefinition_ = definition;
  setStyleSheet(dialogStyleSheet(themeDefinition_));
}

void muffin::PreferencesDialog::setStatusBarVisible(bool visible) {
  if (appearancePage_) {
    appearancePage_->setStatusBarVisible(visible);
  }
}

void muffin::PreferencesDialog::setZoomPercent(int percent) {
  if (appearancePage_) {
    appearancePage_->setZoomPercent(percent);
  }
}

void muffin::PreferencesDialog::setFontSizePx(int px) {
  if (appearancePage_) {
    appearancePage_->setFontSizePx(px);
  }
}

void muffin::PreferencesDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QDialog::changeEvent(event);
}

void muffin::PreferencesDialog::retranslateUi() {
  setWindowTitle(tr("Preferences"));
  if (sidebarTitleLabel_) {
    sidebarTitleLabel_->setText(tr("Preferences"));
  }

  const QStringList categories = {
      tr("General"),
      tr("Appearance"),
      tr("Editor"),
      QStringLiteral("Markdown"),
      tr("Image"),
      tr("Files"),
      tr("Export"),
  };

  int currentRow = categoryList_->currentRow();
  if (currentRow < 0 || currentRow >= categories.size()) {
    // First construction (the list was empty): reopen the last-viewed category instead of always
    // landing on General. On a live retranslate (language change) currentRow is already valid and is
    // preserved by the branch above, so we only restore on the initial build.
    const QString savedKey = QSettings().value(kLastCategorySetting).toString();
    const int restored = kCategoryKeys.indexOf(savedKey);
    currentRow = (restored >= 0 && restored < categories.size()) ? restored : categories.size() - 1;
  }

  categoryList_->blockSignals(true);
  categoryList_->clear();
  categoryList_->addItems(categories);
  categoryList_->setCurrentRow(currentRow);
  categoryList_->blockSignals(false);
  contentStack_->setCurrentIndex(currentRow);

  // Retranslate page title labels
  for (int i = 0; i < pageTitleLabels_.size() && i < categories.size(); ++i) {
    pageTitleLabels_[i]->setText(categories[i]);
  }
  for (QLabel* label : placeholderLabels_) {
    label->setText(tr("No settings available."));
  }

  // Forward retranslateUi to each page
  if (filesPage_) {
    filesPage_->retranslateUi();
  }
  if (editorPage_) {
    editorPage_->retranslateUi();
  }
  if (imagePage_) {
    imagePage_->retranslateUi();
  }
  if (markdownPage_) {
    markdownPage_->retranslateUi();
  }
  if (exportPage_) {
    exportPage_->retranslateUi();
  }
  if (appearancePage_) {
    appearancePage_->retranslateUi();
  }
  // General page handles its own retranslation via LanguageManager
}

QScrollArea* muffin::PreferencesDialog::makeScrollArea() {
  auto* scroll = new QScrollArea(contentStack_);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // Scrollbar styling comes from the dialog-level stylesheet (dialogStyleSheet)
  // so it follows the theme; no per-widget sheet needed here.
  return scroll;
}

QWidget* muffin::PreferencesDialog::makePage(QWidget* parent) {
  auto* scroll = new QScrollArea(parent);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto* page = new QWidget(scroll);
  page->setStyleSheet(QStringLiteral("background:transparent;"));
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(22, 26, 34, 26);
  layout->setSpacing(14);

  auto* title = new QLabel(page);
  title->setObjectName(QStringLiteral("preferencesPageTitle"));
  layout->addWidget(title);
  pageTitleLabels_.append(title);

  scroll->setWidget(page);
  contentStack_->addWidget(scroll);
  return page;
}

void muffin::PreferencesDialog::addPlaceholderPage() {
  auto* page = makePage(contentStack_);
  auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
  auto* label = new QLabel(page);
  label->setObjectName(QStringLiteral("preferencesPlaceholder"));
  label->setMinimumHeight(80);
  label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->addWidget(label);
  layout->addStretch(1);
  placeholderLabels_.append(label);
}
