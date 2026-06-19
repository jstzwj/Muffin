#include "app/PrefsImagePage.h"

#include "image/CustomCommandUploader.h"
#include "image/ImageInsertAction.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QOverload>
#include <QPushButton>
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

  insertLabel_ = makeSectionLabel(insertCard);
  insertCombo_ = new QComboBox(insertCard);
  insertCombo_->setMinimumWidth(360);

  // "Copy to specified path" sub-row — only meaningful for that insert action.
  customFolderRow_ = new QWidget(insertCard);
  auto* customFolderLayout = new QVBoxLayout(customFolderRow_);
  customFolderLayout->setContentsMargins(0, 0, 0, 0);
  customFolderLayout->setSpacing(4);
  auto* customFolderRowLine = new QHBoxLayout();
  customFolderRowLine->setSpacing(8);
  customFolderEdit_ = new QLineEdit(customFolderRow_);
  customFolderEdit_->setMinimumWidth(300);
  customFolderBrowse_ = makeButton(customFolderRow_);
  customFolderBrowse_->setFixedWidth(36);
  customFolderRowLine->addWidget(customFolderEdit_, 1);
  customFolderRowLine->addWidget(customFolderBrowse_);
  customFolderLayout->addLayout(customFolderRowLine);

  applyToLocalCheck_ = new QCheckBox(insertCard);
  applyToLocalCheck_->setChecked(true);
  applyToNetworkCheck_ = new QCheckBox(insertCard);
  allowYamlUploadCheck_ = new QCheckBox(insertCard);

  insertCardLayout->addWidget(insertLabel_);
  insertCardLayout->addWidget(insertCombo_);
  insertCardLayout->addWidget(customFolderRow_);
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

  syntaxLabel_ = makeSectionLabel(syntaxCard);

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

  syntaxLayout->addWidget(syntaxLabel_);
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

  uploadLabel_ = makeSectionLabel(uploadCard);

  auto* serviceRow = new QHBoxLayout();
  serviceRow->setSpacing(24);
  uploadServiceLabel_ = new QLabel(uploadCard);
  uploadServiceCombo_ = new QComboBox(uploadCard);
  uploadServiceCombo_->setMinimumWidth(260);
  serviceRow->addWidget(uploadServiceLabel_);
  serviceRow->addStretch(1);
  serviceRow->addWidget(uploadServiceCombo_);

  // Custom-command sub-row — only meaningful when "Custom Command" is selected.
  commandRow_ = new QWidget(uploadCard);
  auto* commandLayout = new QVBoxLayout(commandRow_);
  commandLayout->setContentsMargins(0, 0, 0, 0);
  commandLayout->setSpacing(4);
  auto* commandRowLine = new QHBoxLayout();
  commandRowLine->setSpacing(8);
  commandEdit_ = new QLineEdit(commandRow_);
  commandEdit_->setMinimumWidth(300);
  testButton_ = makeButton(commandRow_);
  commandRowLine->addWidget(commandEdit_, 1);
  commandRowLine->addWidget(testButton_);
  commandLayout->addLayout(commandRowLine);

  uploadCardLayout->addWidget(uploadLabel_);
  uploadCardLayout->addSpacing(2);
  uploadCardLayout->addLayout(serviceRow);
  uploadCardLayout->addWidget(commandRow_);
  cardColumn->addWidget(uploadCard);

  layout->addStretch(1);

  // Conditional enable: leading slash only when relative path is checked
  connect(preferRelativePathCheck_, &QCheckBox::toggled, addLeadingSlashCheck_, &QCheckBox::setEnabled);

  retranslateUi();
  loadSettings();
  updateConditionalRows();

  // Wire persistence
  connect(insertCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
    QSettings().setValue(QStringLiteral("image/insertAction"), index);
    updateConditionalRows();
  });
  connect(customFolderEdit_, &QLineEdit::textChanged, this,
          [](const QString& text) { QSettings().setValue(QStringLiteral("image/customFolder"), text); });
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
  connect(uploadServiceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
    QSettings().setValue(QStringLiteral("image/uploadService"), index);
    updateConditionalRows();
  });
  connect(commandEdit_, &QLineEdit::textChanged, this,
          [](const QString& text) { QSettings().setValue(QStringLiteral("image/uploadCommand"), text); });
  connect(customFolderBrowse_, &QPushButton::clicked, this, [this] { browseCustomFolder(); });
  connect(testButton_, &QPushButton::clicked, this, [this] { testUploader(); });
}

void muffin::PrefsImagePage::retranslateUi() {
  // Card 1: Insert images
  insertLabel_->setText(tr("When Inserting Images"));
  {
    const auto prev = insertCombo_->currentIndex();
    insertCombo_->blockSignals(true);
    insertCombo_->clear();
    insertCombo_->addItem(tr("No special operation"));
    insertCombo_->addItem(tr("Copy image to current folder (./)"));
    insertCombo_->addItem(tr("Copy image to ./assets folder"));
    insertCombo_->addItem(tr("Copy image to ./%1 folder").arg(QStringLiteral("$(filename).assets")));
    insertCombo_->addItem(tr("Upload image"));
    insertCombo_->addItem(tr("Copy to specified path"));
    polishComboBox(insertCombo_);
    insertCombo_->setCurrentIndex(qBound(0, prev, insertCombo_->count() - 1));
    insertCombo_->blockSignals(false);
  }
  customFolderEdit_->setPlaceholderText(tr("Destination folder for copied images"));
  customFolderBrowse_->setText(tr("Browse..."));
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
    uploadServiceCombo_->addItem(tr("Custom Command"));
    polishComboBox(uploadServiceCombo_);
    uploadServiceCombo_->setCurrentIndex(qBound(0, prev, uploadServiceCombo_->count() - 1));
    uploadServiceCombo_->blockSignals(false);
  }
  commandEdit_->setPlaceholderText(tr("e.g. picgo upload or uPic -m upload"));
  testButton_->setText(tr("Test"));
}

void muffin::PrefsImagePage::loadSettings() {
  QSettings settings;

  const int insert = settings.value(QStringLiteral("image/insertAction"), 0).toInt();
  if (insertCombo_->count() > 0) {
    insertCombo_->blockSignals(true);
    insertCombo_->setCurrentIndex(qBound(0, insert, insertCombo_->count() - 1));
    insertCombo_->blockSignals(false);
  }

  customFolderEdit_->blockSignals(true);
  customFolderEdit_->setText(settings.value(QStringLiteral("image/customFolder")).toString());
  customFolderEdit_->blockSignals(false);

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

  commandEdit_->blockSignals(true);
  commandEdit_->setText(settings.value(QStringLiteral("image/uploadCommand")).toString());
  commandEdit_->blockSignals(false);
}

void muffin::PrefsImagePage::updateConditionalRows() {
  if (customFolderRow_) {
    const bool copyToCustom = insertCombo_->currentIndex() == static_cast<int>(ImageInsertAction::CopyToCustomFolder);
    customFolderRow_->setVisible(copyToCustom);
  }
  if (commandRow_) {
    // index 1 == "Custom Command" (see retranslateUi)
    commandRow_->setVisible(uploadServiceCombo_->currentIndex() == 1);
  }
}

void muffin::PrefsImagePage::browseCustomFolder() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Image Folder"), customFolderEdit_->text());
  if (!dir.isEmpty()) {
    customFolderEdit_->setText(dir);
  }
}

void muffin::PrefsImagePage::testUploader() {
  const QString command = commandEdit_->text().trimmed();
  if (command.isEmpty()) {
    QMessageBox::warning(this, tr("Test Upload"), tr("Enter an upload command first."));
    return;
  }
  // Generate a tiny throwaway image so the uploader has a real file to post.
  QImage test(16, 16, QImage::Format_RGB32);
  test.fill(Qt::white);
  const QString tempPath = QDir::tempPath() + QStringLiteral("/muffin_upload_test.png");
  if (!test.save(tempPath, "PNG")) {
    QMessageBox::warning(this, tr("Test Upload"), tr("Could not create a test image."));
    return;
  }
  const CustomCommandResult res = CustomCommandUploader::upload(this, {tempPath}, command);
  QFile::remove(tempPath);
  if (res.canceled) {
    QMessageBox::information(this, tr("Test Upload"), tr("Upload canceled."));
  } else if (res.ran && !res.urls.isEmpty()) {
    QMessageBox::information(this, tr("Test Upload"), tr("Upload succeeded. Returned URL:\n%1").arg(res.urls.first()));
  } else {
    QMessageBox::warning(this, tr("Test Upload"), tr("Upload failed:\n%1").arg(res.error));
  }
}
