#include "app/PreferencesDialog.h"

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
#include <QStackedWidget>
#include <QVBoxLayout>

muffin::PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
  setModal(true);
  setMinimumSize(880, 620);
  resize(1040, 720);
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setStyleSheet(QStringLiteral(
      // Dialog base
      "QDialog { background:#f6f7f9; color:#1f2328; }"
      "QWidget#preferencesSidebar { background:#ffffff; border:1px solid #e7eaf0; border-radius:8px; }"

      // Settings groups and rows
      "QWidget#settingsGroup { background:#ffffff; border:1px solid #e4e7ec; border-radius:8px; }"
      "QWidget#settingsCard { background:transparent; border:0; border-bottom:1px solid #eef1f4; border-radius:0; }"
      "QWidget#settingsCard[lastSettingsRow=\"true\"] { border-bottom:0; }"

      // QPushButton
      "QPushButton { border:1px solid #d0d7de; border-radius:6px; background:#ffffff; min-height:30px; padding:0 13px; color:#24292f; }"
      "QPushButton:hover { background:#f6f8fa; border-color:#afb8c1; }"
      "QPushButton:pressed { background:#eef1f4; border-color:#8c959f; }"
      "QPushButton:focus { border-color:#54aeff; }"
      "QPushButton:disabled { background:#f6f8fa; color:#8c959f; border-color:#d8dee4; }"

      // QCheckBox
      "QCheckBox { spacing:8px; color:#24292f; min-height:24px; }"
      "QCheckBox:disabled { color:#8c959f; }"
      "QCheckBox::indicator { width:16px; height:16px; border:1px solid #8c959f; border-radius:4px; background:#ffffff; }"
      "QCheckBox::indicator:hover { border-color:#0969da; }"
      "QCheckBox::indicator:checked { border:1px solid #0969da; background:#0969da; image:url(:/icons/ui/check.svg); }"
      "QCheckBox::indicator:checked:hover { background:#0550ae; border-color:#0550ae; }"
      "QCheckBox::indicator:disabled { background:#f6f8fa; border-color:#d0d7de; }"
      "QCheckBox::indicator:checked:disabled { background:#8c959f; border-color:#8c959f; }"

      // QRadioButton
      "QRadioButton { spacing:8px; color:#24292f; min-height:24px; }"
      "QRadioButton:disabled { color:#8c959f; }"
      "QRadioButton::indicator { width:16px; height:16px; border:1px solid #8c959f; border-radius:8px; background:#ffffff; }"
      "QRadioButton::indicator:hover { border-color:#0969da; }"
      "QRadioButton::indicator:checked { border:1px solid #0969da; background:#ffffff; image:url(:/icons/ui/radio-dot.svg); }"
      "QRadioButton::indicator:disabled { background:#f6f8fa; border-color:#d0d7de; }"

      // QComboBox
      "QComboBox { border:1px solid #d0d7de; border-radius:6px; background:#ffffff; padding:4px 30px 4px 10px; min-height:24px; color:#24292f; }"
      "QComboBox:hover { border-color:#afb8c1; }"
      "QComboBox:focus, QComboBox:on { border-color:#54aeff; }"
      "QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:center right; width:28px; border:0; }"
      "QComboBox QAbstractItemView { border:1px solid #d0d7de; border-radius:6px; background:#ffffff; color:#24292f; selection-background-color:#edf5ff; selection-color:#0969da; outline:0; padding:4px; }"
      "QComboBox QAbstractItemView::item { min-height:28px; padding:4px 10px; background:#ffffff; color:#24292f; }"
      "QComboBox QAbstractItemView::item:hover { background:#f3f6fa; color:#24292f; }"
      "QComboBox QAbstractItemView::item:selected { background:#edf5ff; color:#0969da; }"

      // QLineEdit
      "QLineEdit { border:1px solid #d0d7de; border-radius:6px; background:#ffffff; padding:4px 10px; min-height:24px; color:#24292f; }"
      "QLineEdit:hover { border-color:#afb8c1; }"
      "QLineEdit:focus { border-color:#54aeff; }"
      "QLineEdit:disabled { background:#f6f8fa; color:#8c959f; }"

      // QComboBox dropdown scrollbar
      "QComboBox QAbstractItemView QScrollBar:vertical { width:8px; background:transparent; margin:2px; border:0; }"
      "QComboBox QAbstractItemView QScrollBar::handle:vertical { background:#c9d1d9; min-height:28px; border-radius:4px; }"
      "QComboBox QAbstractItemView QScrollBar::handle:vertical:hover { background:#8c959f; }"
      "QComboBox QAbstractItemView QScrollBar::add-line:vertical, QComboBox QAbstractItemView QScrollBar::sub-line:vertical { height:0; }"
      "QComboBox QAbstractItemView QScrollBar::add-page:vertical, QComboBox QAbstractItemView QScrollBar::sub-page:vertical { background:transparent; }"

      // Sidebar QListWidget
      "QListWidget { border:0; outline:0; background:transparent; }"
      "QListWidget::item { min-height:34px; padding-left:14px; margin:2px 6px; color:#57606a; border-radius:6px; border-left:3px solid transparent; }"
      "QListWidget::item:hover { background:#f3f6fa; color:#24292f; }"
      "QListWidget::item:selected { background:#edf5ff; color:#0969da; border-left:3px solid #0969da; font-weight:500; }"

      // Page QScrollArea
      "QScrollArea { border:0; background:transparent; }"
      "QScrollArea > QWidget > QWidget { background:transparent; }"));

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
  sidebarTitleLabel_->setStyleSheet(QStringLiteral("font-size:15px; font-weight:600; color:#24292f; padding-left:8px; padding-bottom:4px;"));
  sidebarLayout->addWidget(sidebarTitleLabel_);

  categoryList_ = new QListWidget(sidebar);
  categoryList_->setFocusPolicy(Qt::NoFocus);
  sidebarLayout->addWidget(categoryList_, 1);
  rootLayout->addWidget(sidebar);

  contentStack_ = new QStackedWidget(this);
  contentStack_->setStyleSheet(QStringLiteral("QStackedWidget { background:transparent; }"));
  rootLayout->addWidget(contentStack_, 1);

  // Page 0: Files
  {
    auto* scroll = makeScrollArea();
    filesPage_ = new PrefsFilesPage(scroll);
    scroll->setWidget(filesPage_);
    contentStack_->addWidget(scroll);
  }

  // Page 1: Editor
  {
    auto* scroll = makeScrollArea();
    editorPage_ = new PrefsEditorPage(scroll);
    scroll->setWidget(editorPage_);
    contentStack_->addWidget(scroll);
  }

  // Page 2: Image
  {
    auto* scroll = makeScrollArea();
    imagePage_ = new PrefsImagePage(scroll);
    scroll->setWidget(imagePage_);
    contentStack_->addWidget(scroll);
  }

  // Page 3: Markdown
  {
    auto* scroll = makeScrollArea();
    markdownPage_ = new PrefsMarkdownPage(scroll);
    scroll->setWidget(markdownPage_);
    contentStack_->addWidget(scroll);
  }

  // Page 4: Export
  {
    auto* scroll = makeScrollArea();
    exportPage_ = new PrefsExportPage(scroll);
    scroll->setWidget(exportPage_);
    contentStack_->addWidget(scroll);
  }

  // Page 5: Appearance
  {
    auto* scroll = makeScrollArea();
    appearancePage_ = new PrefsAppearancePage(scroll);
    scroll->setWidget(appearancePage_);
    contentStack_->addWidget(scroll);
  }

  // Page 6: General
  {
    auto* scroll = makeScrollArea();
    auto* generalPage = new PrefsGeneralPage(scroll);
    scroll->setWidget(generalPage);
    contentStack_->addWidget(scroll);
  }

  connect(categoryList_, &QListWidget::currentRowChanged, contentStack_, &QStackedWidget::setCurrentIndex);

  // Forward page signals
  connect(filesPage_, &PrefsFilesPage::clearRecentFilesRequested,
          this, &PreferencesDialog::clearRecentFilesRequested);
  connect(editorPage_, &PrefsEditorPage::disableTypewriterFocusRequested,
          this, &PreferencesDialog::disableTypewriterFocusRequested);
  connect(appearancePage_, &PrefsAppearancePage::themeRequested,
          this, &PreferencesDialog::themeRequested);
  connect(appearancePage_, &PrefsAppearancePage::statusBarVisibleRequested,
          this, &PreferencesDialog::statusBarVisibleRequested);
  connect(appearancePage_, &PrefsAppearancePage::zoomPercentRequested,
          this, &PreferencesDialog::zoomPercentRequested);
  connect(appearancePage_, &PrefsAppearancePage::fontSizePxRequested,
          this, &PreferencesDialog::fontSizePxRequested);

  retranslateUi();
}

void muffin::PreferencesDialog::setAvailableThemes(const QStringList& themes) {
  if (appearancePage_) {
    appearancePage_->setAvailableThemes(themes);
  }
}

void muffin::PreferencesDialog::setCurrentThemeName(const QString& name) {
  if (appearancePage_) {
    appearancePage_->setCurrentThemeName(name);
  }
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
      tr("Files"),
      tr("Editor"),
      tr("Image"),
      QStringLiteral("Markdown"),
      tr("Export"),
      tr("Appearance"),
      tr("General"),
  };

  int currentRow = categoryList_->currentRow();
  if (currentRow < 0 || currentRow >= categories.size()) {
    currentRow = categories.size() - 1;
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
  scroll->setStyleSheet(QStringLiteral(
      "QScrollBar:vertical { width:8px; background:transparent; margin:0; border:0; }"
      "QScrollBar::handle:vertical { background:#c9d1d9; min-height:36px; border-radius:4px; }"
      "QScrollBar::handle:vertical:hover { background:#8c959f; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"));
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
  title->setStyleSheet(QStringLiteral("font-size:26px; font-weight:600; color:#111111;"));
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
  label->setStyleSheet(QStringLiteral("color:#666666;"));
  label->setMinimumHeight(80);
  label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->addWidget(label);
  layout->addStretch(1);
  placeholderLabels_.append(label);
}
