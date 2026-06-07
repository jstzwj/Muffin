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

namespace muffin {

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
  setModal(true);
  setMinimumSize(880, 620);
  resize(1040, 720);
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setStyleSheet(QStringLiteral(
      "QDialog { background:#f3f3f3; color:#181818; }"
      "QWidget#settingsCard { background:#ffffff; border:1px solid #e6e6e6; border-radius:8px; }"
      "QPushButton { border:1px solid #c8c8c8; border-radius:4px; background:#ffffff; min-height:30px; padding:0 14px; }"
      "QPushButton:hover { background:#f5f5f5; border-color:#a8a8a8; }"
      "QCheckBox { spacing:8px; }"
      "QCheckBox::indicator { width:14px; height:14px; border:1px solid #111111; border-radius:2px; background:#ffffff; }"
      "QCheckBox::indicator:hover { border-color:#0067c0; }"
      "QCheckBox::indicator:checked { border:1px solid #111111; background:#0067c0; }"
      "QCheckBox::indicator:checked:hover { background:#005a9e; }"
      "QCheckBox::indicator:disabled { border-color:#9a9a9a; background:#eeeeee; }"
      "QCheckBox::indicator:checked:disabled { border-color:#9a9a9a; background:#b8d8f3; }"
      "QRadioButton { spacing:8px; }"
      "QRadioButton::indicator { width:14px; height:14px; border:1px solid #111111; border-radius:7px; background:#ffffff; }"
      "QRadioButton::indicator:hover { border-color:#0067c0; }"
      "QRadioButton::indicator:checked { border:1px solid #111111; background:#0067c0; }"
      "QRadioButton::indicator:checked:hover { background:#005a9e; }"
      "QComboBox QAbstractItemView QScrollBar:vertical { width:10px; background:#f2f2f2; margin:2px; border:0; border-radius:5px; }"
      "QComboBox QAbstractItemView QScrollBar::handle:vertical { background:#c8c8c8; min-height:28px; border-radius:5px; }"
      "QComboBox QAbstractItemView QScrollBar::handle:vertical:hover { background:#a8a8a8; }"
      "QComboBox QAbstractItemView QScrollBar::add-line:vertical, QComboBox QAbstractItemView QScrollBar::sub-line:vertical { height:0; border:0; background:transparent; }"
      "QComboBox QAbstractItemView QScrollBar::add-page:vertical, QComboBox QAbstractItemView QScrollBar::sub-page:vertical { background:transparent; }"
      "QListWidget { border:0; outline:0; background:#f3f3f3; }"
      "QListWidget::item { min-height:34px; padding-left:12px; color:#222222; }"
      "QListWidget::item:hover { background:#ececec; }"
      "QListWidget::item:selected { background:#dadada; color:#111111; }"
      "QScrollArea { border:0; background:#ffffff; }"));

  auto* rootLayout = new QHBoxLayout(this);
  rootLayout->setContentsMargins(28, 30, 28, 28);
  rootLayout->setSpacing(34);

  auto* sidebar = new QWidget(this);
  sidebar->setFixedWidth(210);
  auto* sidebarLayout = new QVBoxLayout(sidebar);
  sidebarLayout->setContentsMargins(0, 0, 0, 0);
  sidebarLayout->setSpacing(16);

  categoryList_ = new QListWidget(sidebar);
  categoryList_->setFocusPolicy(Qt::NoFocus);
  sidebarLayout->addWidget(categoryList_, 1);
  rootLayout->addWidget(sidebar);

  contentStack_ = new QStackedWidget(this);
  contentStack_->setStyleSheet(QStringLiteral("QStackedWidget { background:#ffffff; }"));
  rootLayout->addWidget(contentStack_, 1);

  // Page 0: Files
  {
    auto* scroll = new QScrollArea(contentStack_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filesPage_ = new PrefsFilesPage(scroll);
    scroll->setWidget(filesPage_);
    contentStack_->addWidget(scroll);
    auto* title = new QLabel(filesPage_);
    title->setStyleSheet(QStringLiteral("font-size:26px; font-weight:600; color:#111111;"));
    title->setVisible(false);  // Title is shown via page title label in stack
    // Actually, pages need title labels visible in the scroll area above the cards.
    // Let's embed the title into the page itself.
  }

  // Page 1: Editor
  {
    auto* scroll = new QScrollArea(contentStack_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editorPage_ = new PrefsEditorPage(scroll);
    scroll->setWidget(editorPage_);
    contentStack_->addWidget(scroll);
  }

  // Page 2: Image
  {
    auto* scroll = new QScrollArea(contentStack_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* imagePage = new PrefsImagePage(scroll);
    scroll->setWidget(imagePage);
    contentStack_->addWidget(scroll);
  }

  // Page 3: Markdown
  {
    auto* scroll = new QScrollArea(contentStack_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* markdownPage = new PrefsMarkdownPage(scroll);
    scroll->setWidget(markdownPage);
    contentStack_->addWidget(scroll);
  }

  // Page 4: Export
  {
    auto* scroll = new QScrollArea(contentStack_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* exportPage = new PrefsExportPage(scroll);
    scroll->setWidget(exportPage);
    contentStack_->addWidget(scroll);
  }

  // Page 5: Appearance
  {
    auto* scroll = new QScrollArea(contentStack_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    appearancePage_ = new PrefsAppearancePage(scroll);
    scroll->setWidget(appearancePage_);
    contentStack_->addWidget(scroll);
  }

  // Page 6: General
  {
    auto* scroll = new QScrollArea(contentStack_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

void PreferencesDialog::setAvailableThemes(const QStringList& themes) {
  if (appearancePage_) {
    appearancePage_->setAvailableThemes(themes);
  }
}

void PreferencesDialog::setCurrentThemeName(const QString& name) {
  if (appearancePage_) {
    appearancePage_->setCurrentThemeName(name);
  }
}

void PreferencesDialog::setStatusBarVisible(bool visible) {
  if (appearancePage_) {
    appearancePage_->setStatusBarVisible(visible);
  }
}

void PreferencesDialog::setZoomPercent(int percent) {
  if (appearancePage_) {
    appearancePage_->setZoomPercent(percent);
  }
}

void PreferencesDialog::setFontSizePx(int px) {
  if (appearancePage_) {
    appearancePage_->setFontSizePx(px);
  }
}

void PreferencesDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QDialog::changeEvent(event);
}

void PreferencesDialog::retranslateUi() {
  setWindowTitle(tr("Preferences"));

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
  if (appearancePage_) {
    appearancePage_->retranslateUi();
  }
  // General and Image pages handle their own retranslation via LanguageManager
}

QWidget* PreferencesDialog::makePage(QWidget* parent) {
  auto* scroll = new QScrollArea(parent);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto* page = new QWidget(scroll);
  page->setStyleSheet(QStringLiteral("background:#ffffff;"));
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(38, 34, 46, 34);
  layout->setSpacing(22);

  auto* title = new QLabel(page);
  title->setStyleSheet(QStringLiteral("font-size:26px; font-weight:600; color:#111111;"));
  layout->addWidget(title);
  pageTitleLabels_.append(title);

  scroll->setWidget(page);
  contentStack_->addWidget(scroll);
  return page;
}

void PreferencesDialog::addPlaceholderPage() {
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

}  // namespace muffin
