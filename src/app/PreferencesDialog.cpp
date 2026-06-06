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
      "QDialog { background:#f3f3f3; color:#181818; }"
      "QWidget#settingsCard { background:#ffffff; border:1px solid #e6e6e6; border-radius:8px; }"
      "QPushButton { border:1px solid #c8c8c8; border-radius:4px; background:#ffffff; min-height:30px; padding:0 14px; }"
      "QPushButton:hover { background:#f5f5f5; border-color:#a8a8a8; }"
      "QCheckBox { spacing:8px; }"
      "QCheckBox::indicator { width:15px; height:15px; border:1px solid #a8a8a8; border-radius:3px; background:#ffffff; }"
      "QCheckBox::indicator:hover { border-color:#6b9bd2; }"
      "QCheckBox::indicator:checked { border-color:#2f7dd1; background:#2f7dd1; }"
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

  for (int i = 0; i < 6; ++i) {
    addPlaceholderPage();
  }

  auto* generalPage = makePage(contentStack_);
  auto* generalLayout = qobject_cast<QVBoxLayout*>(generalPage->layout());
  generalTitleLabel_ = pageTitleLabels_.back();

  auto* cardContainer = new QWidget(generalPage);
  cardContainer->setMaximumWidth(640);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(14);
  generalLayout->addWidget(cardContainer);

  auto* languageCard = new QWidget(generalPage);
  languageCard->setObjectName(QStringLiteral("settingsCard"));
  auto* languageLayout = new QHBoxLayout(languageCard);
  languageLayout->setContentsMargins(18, 16, 18, 16);
  languageLayout->setSpacing(24);
  languageLabel_ = makeSectionLabel(languageCard);
  languageCombo_ = new QComboBox(languageCard);
  languageCombo_->setMinimumWidth(320);
  languageCombo_->setMaximumWidth(380);
  populateLanguages();
  languageLayout->addWidget(languageLabel_);
  languageLayout->addStretch(1);
  languageLayout->addWidget(languageCombo_);
  cardColumn->addWidget(languageCard);

  auto* updateCard = new QWidget(generalPage);
  updateCard->setObjectName(QStringLiteral("settingsCard"));
  auto* updateLayout = new QVBoxLayout(updateCard);
  updateLayout->setContentsMargins(18, 16, 18, 16);
  updateLayout->setSpacing(12);
  updateLabel_ = makeSectionLabel(updateCard);
  checkUpdateButton_ = makeButton(updateCard);
  autoUpdateCheck_ = new QCheckBox(updateCard);
  betaUpdateCheck_ = new QCheckBox(updateCard);
  updateLayout->addWidget(updateLabel_);
  updateLayout->addSpacing(2);
  updateLayout->addWidget(checkUpdateButton_, 0, Qt::AlignLeft);
  updateLayout->addWidget(autoUpdateCheck_);
  updateLayout->addWidget(betaUpdateCheck_);
  cardColumn->addWidget(updateCard);

  auto* advancedCard = new QWidget(generalPage);
  advancedCard->setObjectName(QStringLiteral("settingsCard"));
  auto* advancedLayout = new QVBoxLayout(advancedCard);
  advancedLayout->setContentsMargins(18, 16, 18, 16);
  advancedLayout->setSpacing(12);
  advancedLabel_ = makeSectionLabel(advancedCard);
  debugModeCheck_ = new QCheckBox(advancedCard);
  telemetryCheck_ = new QCheckBox(advancedCard);
  telemetryCheck_->setChecked(true);
  auto* advancedButtons = new QHBoxLayout();
  advancedButtons->setSpacing(10);
  openAdvancedButton_ = makeButton(advancedCard);
  resetAdvancedButton_ = makeButton(advancedCard);
  advancedButtons->addWidget(openAdvancedButton_);
  advancedButtons->addWidget(resetAdvancedButton_);
  advancedButtons->addStretch(1);
  advancedLayout->addWidget(advancedLabel_);
  advancedLayout->addSpacing(2);
  advancedLayout->addWidget(debugModeCheck_);
  advancedLayout->addWidget(telemetryCheck_);
  advancedLayout->addLayout(advancedButtons);
  cardColumn->addWidget(advancedCard);

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
