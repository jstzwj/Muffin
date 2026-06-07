#include "app/PrefsGeneralPage.h"

#include "app/LanguageManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

muffin::PrefsGeneralPage::PrefsGeneralPage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(38, 34, 46, 34);
  layout->setSpacing(22);

  auto* cardContainer = new QWidget(this);
  cardContainer->setMaximumWidth(640);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(14);
  layout->addWidget(cardContainer);

  // --- Card 1: Language ---
  auto* languageCard = new QWidget(this);
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

  // --- Card 2: Update ---
  auto* updateCard = new QWidget(this);
  updateCard->setObjectName(QStringLiteral("settingsCard"));
  auto* updateLayout = new QVBoxLayout(updateCard);
  updateLayout->setContentsMargins(18, 16, 18, 16);
  updateLayout->setSpacing(12);
  updateLabel_ = makeSectionLabel(updateCard);
  checkUpdateButton_ = makeButton(updateCard);
  autoUpdateCheck_ = new QCheckBox(updateCard);
  updateLayout->addWidget(updateLabel_);
  updateLayout->addSpacing(2);
  updateLayout->addWidget(checkUpdateButton_, 0, Qt::AlignLeft);
  updateLayout->addWidget(autoUpdateCheck_);
  cardColumn->addWidget(updateCard);

  // --- Card 3: Advanced ---
  auto* advancedCard = new QWidget(this);
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

  layout->addStretch(1);

  retranslateUi();

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
}

void muffin::PrefsGeneralPage::retranslateUi() {
  languageLabel_->setText(tr("Language"));
  updateLabel_->setText(tr("Update"));
  checkUpdateButton_->setText(tr("Check for Updates"));
  autoUpdateCheck_->setText(tr("Automatically check for updates"));
  advancedLabel_->setText(tr("Advanced"));
  debugModeCheck_->setText(tr("Enable debug mode"));
  telemetryCheck_->setText(tr("Send anonymous usage data"));
  openAdvancedButton_->setText(tr("Open Advanced Settings"));
  resetAdvancedButton_->setText(tr("Reset Advanced Settings"));
}

void muffin::PrefsGeneralPage::populateLanguages() {
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
