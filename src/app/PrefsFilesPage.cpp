#include "app/PrefsFilesPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace muffin {

PrefsFilesPage::PrefsFilesPage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(38, 34, 46, 34);
  layout->setSpacing(22);

  auto* cardContainer = new QWidget(this);
  cardContainer->setMaximumWidth(640);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(14);
  layout->addWidget(cardContainer);

  // --- Card 1: Startup ---
  auto* startupCard = new QWidget(this);
  startupCard->setObjectName(QStringLiteral("settingsCard"));
  auto* startupLayout = new QHBoxLayout(startupCard);
  startupLayout->setContentsMargins(18, 16, 18, 16);
  startupLayout->setSpacing(24);
  startupLabel_ = makeSectionLabel(startupCard);
  startupCombo_ = new QComboBox(startupCard);
  startupCombo_->setMinimumWidth(320);
  startupCombo_->setMaximumWidth(380);
  startupLayout->addWidget(startupLabel_);
  startupLayout->addStretch(1);
  startupLayout->addWidget(startupCombo_);
  cardColumn->addWidget(startupCard);

  // --- Card 2: Outline ---
  auto* outlineCard = new QWidget(this);
  outlineCard->setObjectName(QStringLiteral("settingsCard"));
  auto* outlineLayout = new QVBoxLayout(outlineCard);
  outlineLayout->setContentsMargins(18, 16, 18, 16);
  outlineLayout->setSpacing(12);
  outlineLabel_ = makeSectionLabel(outlineCard);
  auto* outlineRow = new QHBoxLayout();
  outlineRow->setSpacing(8);
  outlineFoldableCheck_ = new QCheckBox(outlineCard);
  auto* outlineInfo = makeInfoIcon(outlineCard);
  outlineRow->addWidget(outlineFoldableCheck_);
  outlineRow->addWidget(outlineInfo);
  outlineRow->addStretch(1);
  outlineLayout->addWidget(outlineLabel_);
  outlineLayout->addSpacing(2);
  outlineLayout->addLayout(outlineRow);
  cardColumn->addWidget(outlineCard);

  // --- Card 3: Default file extension ---
  auto* extCard = new QWidget(this);
  extCard->setObjectName(QStringLiteral("settingsCard"));
  auto* extLayout = new QHBoxLayout(extCard);
  extLayout->setContentsMargins(18, 16, 18, 16);
  extLayout->setSpacing(24);
  defaultExtLabel_ = makeSectionLabel(extCard);
  defaultExtCombo_ = new QComboBox(extCard);
  defaultExtCombo_->setMinimumWidth(320);
  defaultExtCombo_->setMaximumWidth(380);
  extLayout->addWidget(defaultExtLabel_);
  extLayout->addStretch(1);
  extLayout->addWidget(defaultExtCombo_);
  cardColumn->addWidget(extCard);

  // --- Card 4: Save & Restore ---
  auto* saveCard = new QWidget(this);
  saveCard->setObjectName(QStringLiteral("settingsCard"));
  auto* saveLayout = new QVBoxLayout(saveCard);
  saveLayout->setContentsMargins(18, 16, 18, 16);
  saveLayout->setSpacing(12);
  saveLabel_ = makeSectionLabel(saveCard);
  autoSaveCheck_ = new QCheckBox(saveCard);
  autoSaveSwitchCheck_ = new QCheckBox(saveCard);
  restoreDraftButton_ = makeButton(saveCard);
  saveLayout->addWidget(saveLabel_);
  saveLayout->addSpacing(2);
  saveLayout->addWidget(autoSaveCheck_);
  saveLayout->addWidget(autoSaveSwitchCheck_);
  saveLayout->addWidget(restoreDraftButton_, 0, Qt::AlignLeft);
  cardColumn->addWidget(saveCard);

  // --- Card 5: Recent files ---
  auto* recentCard = new QWidget(this);
  recentCard->setObjectName(QStringLiteral("settingsCard"));
  auto* recentLayout = new QVBoxLayout(recentCard);
  recentLayout->setContentsMargins(18, 16, 18, 16);
  recentLayout->setSpacing(12);
  recentLabel_ = makeSectionLabel(recentCard);
  auto* recentCheckRow = new QHBoxLayout();
  recentCheckRow->setSpacing(8);
  recordHistoryCheck_ = new QCheckBox(recentCard);
  recordHistoryCheck_->setChecked(true);
  auto* recentInfo = makeInfoIcon(recentCard);
  recentCheckRow->addWidget(recordHistoryCheck_);
  recentCheckRow->addWidget(recentInfo);
  recentCheckRow->addStretch(1);
  clearHistoryButton_ = makeButton(recentCard);
  recentLayout->addWidget(recentLabel_);
  recentLayout->addSpacing(2);
  recentLayout->addLayout(recentCheckRow);
  recentLayout->addWidget(clearHistoryButton_, 0, Qt::AlignLeft);
  cardColumn->addWidget(recentCard);

  // --- Card 6: Drag & Drop ---
  auto* dropCard = new QWidget(this);
  dropCard->setObjectName(QStringLiteral("settingsCard"));
  auto* dropLayout = new QVBoxLayout(dropCard);
  dropLayout->setContentsMargins(18, 16, 18, 16);
  dropLayout->setSpacing(12);
  dropLabel_ = makeSectionLabel(dropCard);

  auto* dropFolderRow = new QHBoxLayout();
  dropFolderRow->setSpacing(16);
  dropFolderLabel_ = new QLabel(dropCard);
  dropFolderCombo_ = new QComboBox(dropCard);
  dropFolderCombo_->setMinimumWidth(260);
  dropFolderRow->addWidget(dropFolderLabel_);
  dropFolderRow->addStretch(1);
  dropFolderRow->addWidget(dropFolderCombo_);

  auto* dropMdRow = new QHBoxLayout();
  dropMdRow->setSpacing(16);
  dropMdLabel_ = new QLabel(dropCard);
  dropMdCombo_ = new QComboBox(dropCard);
  dropMdCombo_->setMinimumWidth(260);
  dropMdRow->addWidget(dropMdLabel_);
  dropMdRow->addStretch(1);
  dropMdRow->addWidget(dropMdCombo_);

  auto* dropImportRow = new QHBoxLayout();
  dropImportRow->setSpacing(16);
  dropImportLabel_ = new QLabel(dropCard);
  dropImportCombo_ = new QComboBox(dropCard);
  dropImportCombo_->setMinimumWidth(260);
  dropImportRow->addWidget(dropImportLabel_);
  dropImportRow->addStretch(1);
  dropImportRow->addWidget(dropImportCombo_);

  dropLayout->addWidget(dropLabel_);
  dropLayout->addSpacing(2);
  dropLayout->addLayout(dropFolderRow);
  dropLayout->addLayout(dropMdRow);
  dropLayout->addLayout(dropImportRow);
  cardColumn->addWidget(dropCard);

  layout->addStretch(1);

  retranslateUi();
  loadSettings();

  // Wire persistence
  connect(startupCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("files/startupBehavior"), index); });
  connect(outlineFoldableCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("files/outlineFoldable"), checked); });
  connect(defaultExtCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("files/defaultExtension"), index); });
  connect(autoSaveCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("files/autoSave"), checked); });
  connect(autoSaveSwitchCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("files/autoSaveOnSwitch"), checked); });
  connect(recordHistoryCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("files/recordHistory"), checked); });
  connect(dropFolderCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("files/dropFolder"), index); });
  connect(dropMdCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("files/dropMarkdown"), index); });
  connect(dropImportCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("files/dropImportable"), index); });
  connect(clearHistoryButton_, &QPushButton::clicked, this, &PrefsFilesPage::clearRecentFilesRequested);
}

void PrefsFilesPage::retranslateUi() {
  startupLabel_->setText(tr("Startup"));
  {
    const int cur = startupCombo_->currentIndex();
    startupCombo_->blockSignals(true);
    startupCombo_->clear();
    startupCombo_->addItem(tr("Open new file"));
    startupCombo_->addItem(tr("Reopen last file"));
    startupCombo_->setCurrentIndex(qBound(0, cur, startupCombo_->count() - 1));
    startupCombo_->blockSignals(false);
  }

  outlineLabel_->setText(tr("Outline"));
  outlineFoldableCheck_->setText(tr("Allow collapsing and expanding the outline view in the sidebar"));

  defaultExtLabel_->setText(tr("Default File Type / Extension"));
  {
    const int cur = defaultExtCombo_->currentIndex();
    defaultExtCombo_->blockSignals(true);
    defaultExtCombo_->clear();
    defaultExtCombo_->addItem(QStringLiteral("Markdown (.md)"));
    defaultExtCombo_->addItem(QStringLiteral("Markdown (.markdown)"));
    defaultExtCombo_->addItem(tr("Plain Text (.txt)"));
    defaultExtCombo_->setCurrentIndex(qBound(0, cur, defaultExtCombo_->count() - 1));
    defaultExtCombo_->blockSignals(false);
  }

  saveLabel_->setText(tr("Save & Restore"));
  autoSaveCheck_->setText(tr("Auto Save"));
  autoSaveSwitchCheck_->setText(tr("Auto-save changes to the previous file when switching files"));
  restoreDraftButton_->setText(tr("Restore Unsaved Drafts"));

  recentLabel_->setText(tr("Recently Used Files"));
  recordHistoryCheck_->setText(tr("Record history files and folders"));
  clearHistoryButton_->setText(tr("Clear History"));

  dropLabel_->setText(tr("When dragging files/folders into the window"));
  dropFolderLabel_->setText(tr("When dragging a folder"));
  {
    const int cur = dropFolderCombo_->currentIndex();
    dropFolderCombo_->blockSignals(true);
    dropFolderCombo_->clear();
    dropFolderCombo_->addItem(tr("Open in Muffin"));
    dropFolderCombo_->addItem(tr("Open in File Manager"));
    dropFolderCombo_->setCurrentIndex(qBound(0, cur, dropFolderCombo_->count() - 1));
    dropFolderCombo_->blockSignals(false);
  }
  dropMdLabel_->setText(tr("When dragging a Markdown file"));
  {
    const int cur = dropMdCombo_->currentIndex();
    dropMdCombo_->blockSignals(true);
    dropMdCombo_->clear();
    dropMdCombo_->addItem(tr("Open in Muffin"));
    dropMdCombo_->addItem(tr("Import File"));
    dropMdCombo_->setCurrentIndex(qBound(0, cur, dropMdCombo_->count() - 1));
    dropMdCombo_->blockSignals(false);
  }
  dropImportLabel_->setText(tr("When dragging an importable file"));
  {
    const int cur = dropImportCombo_->currentIndex();
    dropImportCombo_->blockSignals(true);
    dropImportCombo_->clear();
    dropImportCombo_->addItem(tr("Import File"));
    dropImportCombo_->addItem(tr("Open in Muffin"));
    dropImportCombo_->setCurrentIndex(qBound(0, cur, dropImportCombo_->count() - 1));
    dropImportCombo_->blockSignals(false);
  }
}

void PrefsFilesPage::loadSettings() {
  QSettings settings;

  const int startup = settings.value(QStringLiteral("files/startupBehavior"), 0).toInt();
  if (startupCombo_->count() > 0) {
    startupCombo_->blockSignals(true);
    startupCombo_->setCurrentIndex(qBound(0, startup, startupCombo_->count() - 1));
    startupCombo_->blockSignals(false);
  }

  outlineFoldableCheck_->blockSignals(true);
  outlineFoldableCheck_->setChecked(settings.value(QStringLiteral("files/outlineFoldable"), false).toBool());
  outlineFoldableCheck_->blockSignals(false);

  const int ext = settings.value(QStringLiteral("files/defaultExtension"), 0).toInt();
  if (defaultExtCombo_->count() > 0) {
    defaultExtCombo_->blockSignals(true);
    defaultExtCombo_->setCurrentIndex(qBound(0, ext, defaultExtCombo_->count() - 1));
    defaultExtCombo_->blockSignals(false);
  }

  autoSaveCheck_->blockSignals(true);
  autoSaveCheck_->setChecked(settings.value(QStringLiteral("files/autoSave"), false).toBool());
  autoSaveCheck_->blockSignals(false);

  autoSaveSwitchCheck_->blockSignals(true);
  autoSaveSwitchCheck_->setChecked(settings.value(QStringLiteral("files/autoSaveOnSwitch"), false).toBool());
  autoSaveSwitchCheck_->blockSignals(false);

  recordHistoryCheck_->blockSignals(true);
  recordHistoryCheck_->setChecked(settings.value(QStringLiteral("files/recordHistory"), true).toBool());
  recordHistoryCheck_->blockSignals(false);

  const int dropFolder = settings.value(QStringLiteral("files/dropFolder"), 0).toInt();
  if (dropFolderCombo_->count() > 0) {
    dropFolderCombo_->blockSignals(true);
    dropFolderCombo_->setCurrentIndex(qBound(0, dropFolder, dropFolderCombo_->count() - 1));
    dropFolderCombo_->blockSignals(false);
  }

  const int dropMd = settings.value(QStringLiteral("files/dropMarkdown"), 0).toInt();
  if (dropMdCombo_->count() > 0) {
    dropMdCombo_->blockSignals(true);
    dropMdCombo_->setCurrentIndex(qBound(0, dropMd, dropMdCombo_->count() - 1));
    dropMdCombo_->blockSignals(false);
  }

  const int dropImport = settings.value(QStringLiteral("files/dropImportable"), 0).toInt();
  if (dropImportCombo_->count() > 0) {
    dropImportCombo_->blockSignals(true);
    dropImportCombo_->setCurrentIndex(qBound(0, dropImport, dropImportCombo_->count() - 1));
    dropImportCombo_->blockSignals(false);
  }
}

}  // namespace muffin
