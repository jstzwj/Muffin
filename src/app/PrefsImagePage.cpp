#include "app/PrefsImagePage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QOverload>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

muffin::PrefsImagePage::PrefsImagePage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPageLeftMargin, kPageTopMargin, kPageRightMargin, kPageBottomMargin);
  layout->setSpacing(14);

  auto* cardContainer = new QWidget(this);
  cardContainer->setObjectName(QStringLiteral("settingsGroup"));
  cardContainer->setMaximumWidth(kContentWidth);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(kCardSpacing);
  layout->addWidget(cardContainer);

  // --- Card 1: When Inserting Images ---
  auto* insertCard = new QWidget(this);
  insertCard->setObjectName(QStringLiteral("settingsCard"));
  auto* insertCardLayout = new QVBoxLayout(insertCard);
  insertCardLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  insertCardLayout->setSpacing(kRowSpacing);

  auto* insertHeaderRow = new QHBoxLayout();
  insertLabel_ = makeSectionLabel(insertCard);
  auto* insertLearnMore = new QLabel(insertCard);
  insertLearnMore->setOpenExternalLinks(true);
  insertHeaderRow->addWidget(insertLabel_);
  insertHeaderRow->addWidget(insertLearnMore);
  insertHeaderRow->addStretch(1);

  insertCombo_ = new QComboBox(insertCard);
  insertCombo_->setMinimumWidth(320);

  applyToLocalCheck_ = new QCheckBox(insertCard);
  applyToLocalCheck_->setChecked(true);
  applyToNetworkCheck_ = new QCheckBox(insertCard);
  allowYamlUploadCheck_ = new QCheckBox(insertCard);

  insertCardLayout->addLayout(insertHeaderRow);
  insertCardLayout->addWidget(insertCombo_);
  insertCardLayout->addSpacing(2);
  insertCardLayout->addWidget(applyToLocalCheck_);
  insertCardLayout->addWidget(applyToNetworkCheck_);
  insertCardLayout->addWidget(allowYamlUploadCheck_);
  cardColumn->addWidget(insertCard);

  // --- Card 2: Image Syntax Preferences ---
  auto* syntaxCard = new QWidget(this);
  syntaxCard->setObjectName(QStringLiteral("settingsCard"));
  auto* syntaxLayout = new QVBoxLayout(syntaxCard);
  syntaxLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  syntaxLayout->setSpacing(kRowSpacing);

  auto* syntaxHeaderRow = new QHBoxLayout();
  syntaxLabel_ = makeSectionLabel(syntaxCard);
  auto* syntaxLearnMore = new QLabel(syntaxCard);
  syntaxLearnMore->setOpenExternalLinks(true);
  syntaxHeaderRow->addWidget(syntaxLabel_);
  syntaxHeaderRow->addWidget(syntaxLearnMore);
  syntaxHeaderRow->addStretch(1);

  preferRelativePathCheck_ = new QCheckBox(syntaxCard);

  auto* slashRow = new QHBoxLayout();
  slashRow->setSpacing(8);
  addLeadingSlashCheck_ = new QCheckBox(syntaxCard);
  addLeadingSlashCheck_->setEnabled(false);
  auto* slashInfo = makeInfoIcon(syntaxCard);
  slashRow->addWidget(addLeadingSlashCheck_);
  slashRow->addWidget(slashInfo);
  slashRow->addStretch(1);

  escapeImageUrlCheck_ = new QCheckBox(syntaxCard);

  syntaxLayout->addLayout(syntaxHeaderRow);
  syntaxLayout->addSpacing(2);
  syntaxLayout->addWidget(preferRelativePathCheck_);
  syntaxLayout->addLayout(slashRow);
  syntaxLayout->addWidget(escapeImageUrlCheck_);
  cardColumn->addWidget(syntaxCard);

  // --- Card 3: Upload Service Settings ---
  auto* uploadCard = new QWidget(this);
  uploadCard->setObjectName(QStringLiteral("settingsCard"));
  uploadCard->setProperty("lastSettingsRow", true);
  auto* uploadCardLayout = new QVBoxLayout(uploadCard);
  uploadCardLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  uploadCardLayout->setSpacing(kRowSpacing);

  auto* uploadHeaderRow = new QHBoxLayout();
  uploadLabel_ = makeSectionLabel(uploadCard);
  auto* uploadLearnMore = new QLabel(uploadCard);
  uploadLearnMore->setOpenExternalLinks(true);
  uploadHeaderRow->addWidget(uploadLabel_);
  uploadHeaderRow->addWidget(uploadLearnMore);
  uploadHeaderRow->addStretch(1);

  auto* serviceRow = new QHBoxLayout();
  serviceRow->setSpacing(24);
  uploadServiceLabel_ = new QLabel(uploadCard);
  uploadServiceCombo_ = new QComboBox(uploadCard);
  uploadServiceCombo_->setMinimumWidth(260);
  serviceRow->addWidget(uploadServiceLabel_);
  serviceRow->addStretch(1);
  serviceRow->addWidget(uploadServiceCombo_);

  uploadCardLayout->addLayout(uploadHeaderRow);
  uploadCardLayout->addSpacing(2);
  uploadCardLayout->addLayout(serviceRow);
  cardColumn->addWidget(uploadCard);

  layout->addStretch(1);

  // Conditional enable: leading slash only when relative path is checked
  connect(preferRelativePathCheck_, &QCheckBox::toggled, addLeadingSlashCheck_, &QCheckBox::setEnabled);

  retranslateUi();
  loadSettings();

  // Wire persistence
  connect(insertCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("image/insertAction"), index); });
  connect(applyToLocalCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("image/applyToLocal"), checked); });
  connect(applyToNetworkCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("image/applyToNetwork"), checked); });
  connect(allowYamlUploadCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("image/allowYamlUpload"), checked); });
  connect(preferRelativePathCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("image/preferRelativePath"), checked); });
  connect(addLeadingSlashCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("image/addLeadingSlash"), checked); });
  connect(escapeImageUrlCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("image/escapeImageUrl"), checked); });
  connect(uploadServiceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("image/uploadService"), index); });
}

void muffin::PrefsImagePage::retranslateUi() {
  // Card 1: Insert images
  insertLabel_->setText(tr("When Inserting Images"));
  {
    const auto learnMore = tr("Learn more...");
    const auto prev = insertCombo_->currentIndex();
    insertCombo_->blockSignals(true);
    insertCombo_->clear();
    insertCombo_->addItem(tr("No special operation"));
    insertCombo_->addItem(tr("Copy to custom folder"));
    insertCombo_->addItem(tr("Upload image"));
    polishComboBox(insertCombo_);
    insertCombo_->setCurrentIndex(qBound(0, prev, insertCombo_->count() - 1));
    insertCombo_->blockSignals(false);
    // Update "Learn more" label — stored as the 2nd widget in the header row
    if (auto* lm = insertLabel_->parentWidget()->findChildren<QLabel*>().value(1)) {
      lm->setText(QStringLiteral("<a href=\"#\">%1</a>").arg(learnMore));
    }
  }
  applyToLocalCheck_->setText(tr("Apply the above rules to local images"));
  applyToNetworkCheck_->setText(tr("Apply the above rules to network images"));
  allowYamlUploadCheck_->setText(tr("Allow automatic image upload based on YAML settings"));

  // Card 2: Syntax preferences
  syntaxLabel_->setText(tr("Image Syntax Preferences"));
  preferRelativePathCheck_->setText(tr("Prefer relative paths"));
  addLeadingSlashCheck_->setText(tr("Add / to relative paths"));
  escapeImageUrlCheck_->setText(tr("Auto-escape image URLs on insertion"));

  // Card 3: Upload service
  uploadLabel_->setText(tr("Upload Service Settings"));
  uploadServiceLabel_->setText(tr("Upload Service"));
  {
    const auto prev = uploadServiceCombo_->currentIndex();
    uploadServiceCombo_->blockSignals(true);
    uploadServiceCombo_->clear();
    uploadServiceCombo_->addItem(tr("None"));
    polishComboBox(uploadServiceCombo_);
    uploadServiceCombo_->setCurrentIndex(qBound(0, prev, uploadServiceCombo_->count() - 1));
    uploadServiceCombo_->blockSignals(false);
  }
}

void muffin::PrefsImagePage::loadSettings() {
  QSettings settings;

  const int insert = settings.value(QStringLiteral("image/insertAction"), 0).toInt();
  if (insertCombo_->count() > 0) {
    insertCombo_->blockSignals(true);
    insertCombo_->setCurrentIndex(qBound(0, insert, insertCombo_->count() - 1));
    insertCombo_->blockSignals(false);
  }

  applyToLocalCheck_->blockSignals(true);
  applyToLocalCheck_->setChecked(settings.value(QStringLiteral("image/applyToLocal"), true).toBool());
  applyToLocalCheck_->blockSignals(false);

  applyToNetworkCheck_->blockSignals(true);
  applyToNetworkCheck_->setChecked(settings.value(QStringLiteral("image/applyToNetwork"), false).toBool());
  applyToNetworkCheck_->blockSignals(false);

  allowYamlUploadCheck_->blockSignals(true);
  allowYamlUploadCheck_->setChecked(settings.value(QStringLiteral("image/allowYamlUpload"), false).toBool());
  allowYamlUploadCheck_->blockSignals(false);

  const bool preferRelative = settings.value(QStringLiteral("image/preferRelativePath"), false).toBool();
  preferRelativePathCheck_->blockSignals(true);
  preferRelativePathCheck_->setChecked(preferRelative);
  preferRelativePathCheck_->blockSignals(false);

  addLeadingSlashCheck_->blockSignals(true);
  addLeadingSlashCheck_->setChecked(settings.value(QStringLiteral("image/addLeadingSlash"), false).toBool());
  addLeadingSlashCheck_->setEnabled(preferRelative);
  addLeadingSlashCheck_->blockSignals(false);

  escapeImageUrlCheck_->blockSignals(true);
  escapeImageUrlCheck_->setChecked(settings.value(QStringLiteral("image/escapeImageUrl"), false).toBool());
  escapeImageUrlCheck_->blockSignals(false);

  const int service = settings.value(QStringLiteral("image/uploadService"), 0).toInt();
  if (uploadServiceCombo_->count() > 0) {
    uploadServiceCombo_->blockSignals(true);
    uploadServiceCombo_->setCurrentIndex(qBound(0, service, uploadServiceCombo_->count() - 1));
    uploadServiceCombo_->blockSignals(false);
  }
}
