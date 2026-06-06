#include "app/PreferencesDialog.h"

#include "app/LanguageManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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
      "QDialog { background:#f7f7f7; color:#181818; }"
      "QComboBox { border:1px solid #b8b8b8; min-height:28px; padding:2px 9px; background:#ffffff; }"
      "QComboBox:focus { border-color:#4f8fd9; }"
      "QPushButton { border:1px solid #b9b9b9; background:#ffffff; min-height:30px; padding:0 14px; }"
      "QPushButton:hover { background:#f0f0f0; }"
      "QCheckBox { min-height:26px; }"
      "QListWidget { border:0; outline:0; background:#f7f7f7; }"
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

  for (int i = 0; i < 6; ++i) {
    addPlaceholderPage();
  }

  auto* generalPage = makePage(contentStack_);
  auto* generalLayout = qobject_cast<QVBoxLayout*>(generalPage->layout());
  generalTitleLabel_ = pageTitleLabels_.back();

  auto* form = new QGridLayout();
  form->setColumnMinimumWidth(0, 170);
  form->setColumnStretch(1, 1);
  form->setHorizontalSpacing(34);
  form->setVerticalSpacing(22);
  generalLayout->addLayout(form);

  int row = 0;
  languageLabel_ = makeSectionLabel(generalPage);
  languageRestartLabel_ = makeMutedLabel(generalPage);
  languageCombo_ = new QComboBox(generalPage);
  languageCombo_->setFixedWidth(360);
  populateLanguages();

  auto* languageTitle = new QWidget(generalPage);
  auto* languageTitleLayout = new QHBoxLayout(languageTitle);
  languageTitleLayout->setContentsMargins(0, 0, 0, 0);
  languageTitleLayout->setSpacing(14);
  languageTitleLayout->addWidget(languageLabel_);
  languageTitleLayout->addWidget(languageRestartLabel_);
  languageTitleLayout->addStretch(1);
  addSectionRow(form, row++, nullptr, languageCombo_);
  form->addWidget(languageTitle, row - 1, 0, Qt::AlignTop);

  updateLabel_ = makeSectionLabel(generalPage);
  auto* updateBox = new QWidget(generalPage);
  auto* updateLayout = new QVBoxLayout(updateBox);
  updateLayout->setContentsMargins(0, 0, 0, 0);
  updateLayout->setSpacing(10);
  checkUpdateButton_ = makeButton(updateBox);
  autoUpdateCheck_ = new QCheckBox(updateBox);
  betaUpdateCheck_ = new QCheckBox(updateBox);
  updateLayout->addWidget(checkUpdateButton_, 0, Qt::AlignLeft);
  updateLayout->addWidget(autoUpdateCheck_);
  updateLayout->addWidget(betaUpdateCheck_);
  addSectionRow(form, row++, updateLabel_, updateBox);

  advancedLabel_ = makeSectionLabel(generalPage);
  auto* advancedBox = new QWidget(generalPage);
  auto* advancedLayout = new QVBoxLayout(advancedBox);
  advancedLayout->setContentsMargins(0, 0, 0, 0);
  advancedLayout->setSpacing(10);
  debugModeCheck_ = new QCheckBox(advancedBox);
  telemetryCheck_ = new QCheckBox(advancedBox);
  telemetryCheck_->setChecked(true);
  auto* advancedButtons = new QHBoxLayout();
  advancedButtons->setSpacing(12);
  openAdvancedButton_ = makeButton(advancedBox);
  resetAdvancedButton_ = makeButton(advancedBox);
  advancedButtons->addWidget(openAdvancedButton_);
  advancedButtons->addWidget(resetAdvancedButton_);
  advancedButtons->addStretch(1);
  advancedLayout->addWidget(debugModeCheck_);
  advancedLayout->addWidget(telemetryCheck_);
  advancedLayout->addLayout(advancedButtons);
  addSectionRow(form, row++, advancedLabel_, advancedBox);

  generalLayout->addStretch(1);

  connect(categoryList_, &QListWidget::currentRowChanged, contentStack_, &QStackedWidget::setCurrentIndex);
  connect(languageCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    const QString code = languageCombo_->itemData(index).toString();
    if (!code.isEmpty()) {
      LanguageManager::instance().setLanguage(code);
    }
  });
  connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this] {
    populateLanguages();
    retranslateUi();
  });

  retranslateUi();
}

void PreferencesDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    populateLanguages();
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

  for (int i = 0; i < pageTitleLabels_.size() && i < categories.size(); ++i) {
    pageTitleLabels_[i]->setText(categories[i]);
  }
  for (QLabel* label : placeholderLabels_) {
    label->setText(tr("No settings available."));
  }

  generalTitleLabel_->setText(tr("General"));
  languageLabel_->setText(tr("Language"));
  languageRestartLabel_->setText(tr("(restart Muffin to fully apply)"));
  updateLabel_->setText(tr("Update"));
  checkUpdateButton_->setText(tr("Check for Updates"));
  autoUpdateCheck_->setText(tr("Automatically check for updates"));
  betaUpdateCheck_->setText(tr("Include beta updates"));
  advancedLabel_->setText(tr("Advanced"));
  debugModeCheck_->setText(tr("Enable debug mode"));
  telemetryCheck_->setText(tr("Send anonymous usage data"));
  openAdvancedButton_->setText(tr("Open Advanced Settings"));
  resetAdvancedButton_->setText(tr("Reset Advanced Settings"));
}

void PreferencesDialog::populateLanguages() {
  if (!languageCombo_) {
    return;
  }
  languageCombo_->blockSignals(true);
  languageCombo_->clear();

  const QString currentCode = LanguageManager::instance().currentLanguageCode();
  int currentIndex = 0;
  const QVector<LanguageInfo> languages = LanguageManager::instance().availableLanguages();
  for (const LanguageInfo& language : languages) {
    const QString label = language.englishName == language.nativeName
        ? language.nativeName
        : QStringLiteral("%1 - %2").arg(language.nativeName, language.englishName);
    languageCombo_->addItem(label, language.code);
    if (language.code == currentCode) {
      currentIndex = languageCombo_->count() - 1;
    }
  }

  languageCombo_->setCurrentIndex(currentIndex);
  languageCombo_->blockSignals(false);
}

QWidget* PreferencesDialog::makePage(QWidget* parent) {
  auto* scroll = new QScrollArea(parent);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto* page = new QWidget(scroll);
  page->setStyleSheet(QStringLiteral("background:#ffffff;"));
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(34, 30, 46, 30);
  layout->setSpacing(24);

  auto* title = new QLabel(page);
  title->setStyleSheet(QStringLiteral("font-size:26px; font-weight:600; color:#111111;"));
  layout->addWidget(title);
  pageTitleLabels_.append(title);

  scroll->setWidget(page);
  contentStack_->addWidget(scroll);
  return page;
}

QLabel* PreferencesDialog::makeSectionLabel(QWidget* parent) const {
  auto* label = new QLabel(parent);
  label->setStyleSheet(QStringLiteral("font-weight:600; color:#111111;"));
  return label;
}

QLabel* PreferencesDialog::makeMutedLabel(QWidget* parent) const {
  auto* label = new QLabel(parent);
  label->setStyleSheet(QStringLiteral("color:#666666;"));
  return label;
}

QPushButton* PreferencesDialog::makeButton(QWidget* parent) const {
  return new QPushButton(parent);
}

void PreferencesDialog::addSectionRow(QGridLayout* grid, int row, QLabel* label, QWidget* field) {
  if (label) {
    grid->addWidget(label, row, 0, Qt::AlignTop);
  }
  grid->addWidget(field, row, 1, Qt::AlignTop);
}

void PreferencesDialog::addPlaceholderPage() {
  auto* page = makePage(contentStack_);
  auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
  auto* label = makeMutedLabel(page);
  label->setMinimumHeight(80);
  label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->addWidget(label);
  layout->addStretch(1);
  placeholderLabels_.append(label);
}

}  // namespace muffin
