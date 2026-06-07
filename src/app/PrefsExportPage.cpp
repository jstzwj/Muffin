#include "app/PrefsExportPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

muffin::PrefsExportPage::PrefsExportPage(QWidget* parent) : PreferencesPage(parent) {
  auto* rootLayout = new QHBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  // Left: format list (fixed width)
  auto* leftWidget = new QWidget(this);
  leftWidget->setFixedWidth(200);
  leftWidget->setStyleSheet(QStringLiteral(
      "background:#f8f8f8; border-right:1px solid #e0e0e0;"));
  auto* leftLayout = new QVBoxLayout(leftWidget);
  leftLayout->setContentsMargins(12, 18, 12, 12);
  leftLayout->setSpacing(6);

  auto* sectionLabel = new QLabel(leftWidget);
  sectionLabel->setStyleSheet(QStringLiteral("font-weight:600; color:#555555; font-size:12px;"));
  leftLayout->addWidget(sectionLabel);

  formatList_ = new QListWidget(leftWidget);
  formatList_->setFocusPolicy(Qt::NoFocus);
  formatList_->setStyleSheet(QStringLiteral(
      "QListWidget { border:0; outline:0; background:transparent; }"
      "QListWidget::item { min-height:28px; padding-left:8px; color:#222222; border-radius:4px; }"
      "QListWidget::item:hover { background:#eeeeee; }"
      "QListWidget::item:selected { background:#e0e0e0; color:#111111; }"));
  leftLayout->addWidget(formatList_, 1);

  rootLayout->addWidget(leftWidget);

  // Right: settings card
  auto* rightWidget = new QWidget(this);
  rightWidget->setStyleSheet(QStringLiteral("background:#ffffff;"));
  auto* rightLayout = new QVBoxLayout(rightWidget);
  rightLayout->setContentsMargins(38, 34, 46, 34);
  rightLayout->setSpacing(22);

  auto* card = new QWidget(rightWidget);
  card->setObjectName(QStringLiteral("settingsCard"));
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(18, 16, 18, 16);
  cardLayout->setSpacing(16);

  // Default export folder
  defaultFolderLabel_ = makeSectionLabel(card);
  defaultFolderCombo_ = new QComboBox(card);
  defaultFolderCombo_->setMinimumWidth(320);
  cardLayout->addWidget(defaultFolderLabel_);
  cardLayout->addWidget(defaultFolderCombo_);

  // Pandoc path
  auto* pandocHeaderRow = new QHBoxLayout();
  pandocHeaderRow->setSpacing(8);
  pandocLabel_ = makeSectionLabel(card);
  pandocHeaderRow->addWidget(pandocLabel_);
  pandocHeaderRow->addStretch(1);

  auto* pandocRow = new QHBoxLayout();
  pandocRow->setSpacing(8);
  pandocPathEdit_ = new QLineEdit(card);
  pandocPathEdit_->setMinimumWidth(280);
  pandocBrowseButton_ = makeButton(card);
  pandocBrowseButton_->setFixedWidth(36);
  pandocRow->addWidget(pandocPathEdit_);
  pandocRow->addWidget(pandocBrowseButton_);

  cardLayout->addLayout(pandocHeaderRow);
  cardLayout->addLayout(pandocRow);

  // After export
  afterExportLabel_ = makeSectionLabel(card);
  openAfterExportCheck_ = new QCheckBox(card);
  cardLayout->addWidget(afterExportLabel_);
  cardLayout->addWidget(openAfterExportCheck_);

  rightLayout->addWidget(card);
  rightLayout->addStretch(1);

  rootLayout->addWidget(rightWidget, 1);

  retranslateUi();
  loadSettings();

  // Wire persistence
  connect(defaultFolderCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("export/defaultFolder"), index); });
  connect(pandocPathEdit_, &QLineEdit::textChanged, this,
          [](const QString& text) { QSettings().setValue(QStringLiteral("export/pandocPath"), text); });
  connect(openAfterExportCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("export/openAfterExport"), checked); });
  connect(pandocBrowseButton_, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getOpenFileName(this, tr("Select Pandoc Executable"), QString(),
        tr("Executables (*.exe);;All Files (*)"));
    if (!path.isEmpty()) {
      pandocPathEdit_->setText(path);
    }
  });
}

void muffin::PrefsExportPage::retranslateUi() {
  // Left sidebar format list
  {
    const int cur = formatList_->currentRow();
    formatList_->blockSignals(true);
    formatList_->clear();

    // "General" section header — use a disabled item as separator
    auto* generalItem = new QListWidgetItem(tr("General"));
    generalItem->setFlags(Qt::NoItemFlags);
    generalItem->setForeground(QColor(0x55, 0x55, 0x55));
    generalItem->setFont([&] { QFont f = font(); f.setBold(true); return f; }());
    formatList_->addItem(generalItem);

    formatList_->addItem(QStringLiteral("PDF"));
    formatList_->addItem(QStringLiteral("HTML"));
    formatList_->addItem(tr("HTML (without Styles)"));
    formatList_->addItem(tr("Image"));
    formatList_->addItem(QStringLiteral("Word (.docx)"));
    formatList_->addItem(QStringLiteral("OpenOffice"));
    formatList_->addItem(QStringLiteral("RTF"));
    formatList_->addItem(QStringLiteral("Epub"));
    formatList_->addItem(QStringLiteral("LaTeX"));
    formatList_->addItem(QStringLiteral("Media Wiki"));
    formatList_->addItem(QStringLiteral("reStructuredText"));
    formatList_->addItem(QStringLiteral("Textile"));
    formatList_->addItem(QStringLiteral("OPML"));

    if (cur >= 0 && cur < formatList_->count()) {
      formatList_->setCurrentRow(cur);
    } else {
      formatList_->setCurrentRow(1);  // Default to first real item (PDF)
    }
    formatList_->blockSignals(false);
  }

  // Update section label above the format list
  if (auto* sectionLabel = formatList_->parentWidget()->findChild<QLabel*>()) {
    sectionLabel->setText(tr("General"));
  }

  // Right card
  defaultFolderLabel_->setText(tr("Default Export Folder"));
  {
    const int cur = defaultFolderCombo_->currentIndex();
    defaultFolderCombo_->blockSignals(true);
    defaultFolderCombo_->clear();
    defaultFolderCombo_->addItem(tr("Auto"));
    defaultFolderCombo_->addItem(tr("Same folder as current file"));
    defaultFolderCombo_->addItem(tr("Custom..."));
    defaultFolderCombo_->setCurrentIndex(qBound(0, cur, defaultFolderCombo_->count() - 1));
    defaultFolderCombo_->blockSignals(false);
  }

  pandocLabel_->setText(tr("Pandoc Path"));
  if (pandocPathEdit_->text().isEmpty()) {
    pandocPathEdit_->setPlaceholderText(tr("(Auto-detect)"));
  }

  afterExportLabel_->setText(tr("After Export"));
  openAfterExportCheck_->setText(tr("Open the exported file directory"));
}

void muffin::PrefsExportPage::loadSettings() {
  QSettings settings;

  const int folder = settings.value(QStringLiteral("export/defaultFolder"), 0).toInt();
  if (defaultFolderCombo_->count() > 0) {
    defaultFolderCombo_->blockSignals(true);
    defaultFolderCombo_->setCurrentIndex(qBound(0, folder, defaultFolderCombo_->count() - 1));
    defaultFolderCombo_->blockSignals(false);
  }

  pandocPathEdit_->blockSignals(true);
  pandocPathEdit_->setText(settings.value(QStringLiteral("export/pandocPath")).toString());
  pandocPathEdit_->blockSignals(false);

  openAfterExportCheck_->blockSignals(true);
  openAfterExportCheck_->setChecked(settings.value(QStringLiteral("export/openAfterExport"), false).toBool());
  openAfterExportCheck_->blockSignals(false);
}
