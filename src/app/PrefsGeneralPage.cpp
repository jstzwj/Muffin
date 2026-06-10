#include "app/PrefsGeneralPage.h"

#include "app/LanguageManager.h"
#include "app/UpdateChecker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

muffin::PrefsGeneralPage::PrefsGeneralPage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPageLeftMargin, kPageTopMargin, kPageRightMargin, kPageBottomMargin);
  layout->setSpacing(18);

  auto* cardContainer = new QWidget(this);
  cardContainer->setMaximumWidth(kContentWidth);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(kCardSpacing);
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
  updateLayout->setSpacing(10);
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
  advancedLayout->setSpacing(10);
  advancedLabel_ = makeSectionLabel(advancedCard);
  debugModeCheck_ = new QCheckBox(advancedCard);
  advancedLayout->addWidget(advancedLabel_);
  advancedLayout->addSpacing(2);
  advancedLayout->addWidget(debugModeCheck_);
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

  // --- Update check button ---
  connect(checkUpdateButton_, &QPushButton::clicked, this, [this] {
    checkUpdateButton_->setEnabled(false);
    checkUpdateButton_->setText(tr("Checking..."));
    auto& checker = muffin::UpdateChecker::instance();
    connect(&checker, &muffin::UpdateChecker::updateAvailable, this,
        [this](const QString& version, const QString& url) {
          checkUpdateButton_->setEnabled(true);
          retranslateUi();
          QMessageBox::information(window(), tr("Update Available"),
              tr("A new version of Muffin (%1) is available.").arg(version));
        }, Qt::SingleShotConnection);
    connect(&checker, &muffin::UpdateChecker::upToDate, this,
        [this]() {
          checkUpdateButton_->setEnabled(true);
          retranslateUi();
          QMessageBox::information(window(), tr("Up to Date"),
              tr("You are running the latest version of Muffin."));
        }, Qt::SingleShotConnection);
    connect(&checker, &muffin::UpdateChecker::checkFailed, this,
        [this](const QString& errorMessage) {
          checkUpdateButton_->setEnabled(true);
          retranslateUi();
          QMessageBox::warning(window(), tr("Update Check Failed"),
              tr("Could not check for updates:\n%1").arg(errorMessage));
        }, Qt::SingleShotConnection);
    checker.checkForUpdates();
  });

  // --- Auto-update checkbox ---
  wireBoolSetting(autoUpdateCheck_, QStringLiteral("update/autoCheck"));
  loadCheck(autoUpdateCheck_, QStringLiteral("update/autoCheck"), true);
}

void muffin::PrefsGeneralPage::retranslateUi() {
  languageLabel_->setText(tr("Language"));
  updateLabel_->setText(tr("Update"));
  checkUpdateButton_->setText(tr("Check for Updates"));
  autoUpdateCheck_->setText(tr("Automatically check for updates"));
  advancedLabel_->setText(tr("Advanced"));
  debugModeCheck_->setText(tr("Enable debug mode"));
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

  polishComboBox(languageCombo_, qMin(8, qMax(3, languageCombo_->count())));
  languageCombo_->setCurrentIndex(currentIndex);
  languageCombo_->blockSignals(false);
}
