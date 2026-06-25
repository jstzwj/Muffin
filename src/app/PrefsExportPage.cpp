#include "app/PrefsExportPage.h"

#include "export/PandocRunner.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
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

  // Left: format list (fixed width). Colours come from the dialog stylesheet
  // (QWidget#exportPanel, QLabel#exportPanelHeader and the generic QListWidget
  // rules in dialogStyleSheet) so this panel follows the theme instead of
  // forcing a white panel with dark text in dark mode.
  auto* leftWidget = new QWidget(this);
  leftWidget->setObjectName(QStringLiteral("exportPanel"));
  leftWidget->setFixedWidth(200);
  auto* leftLayout = new QVBoxLayout(leftWidget);
  leftLayout->setContentsMargins(12, 18, 12, 12);
  leftLayout->setSpacing(6);

  auto* sectionLabel = new QLabel(leftWidget);
  sectionLabel->setObjectName(QStringLiteral("exportPanelHeader"));
  sectionLabel_ = sectionLabel;
  leftLayout->addWidget(sectionLabel);

  formatList_ = new QListWidget(leftWidget);
  formatList_->setFocusPolicy(Qt::NoFocus);
  leftLayout->addWidget(formatList_, 1);

  rootLayout->addWidget(leftWidget);

  // Right: settings card
  auto* rightWidget = new QWidget(this);
  rightWidget->setStyleSheet(QStringLiteral("background:transparent;"));
  auto* rightLayout = new QVBoxLayout(rightWidget);
  rightLayout->setContentsMargins(kPageLeftMargin, kPageTopMargin, kPageRightMargin, kPageBottomMargin);
  rightLayout->setSpacing(14);

  auto* card = new QWidget(rightWidget);
  card->setObjectName(QStringLiteral("settingsGroup"));
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  cardLayout->setSpacing(kRowSpacing);
  card->setMaximumWidth(kContentWidth);

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
  autoDetectButton_ = makeButton(card);
  pandocHeaderRow->addWidget(autoDetectButton_);

  auto* pandocRow = new QHBoxLayout();
  pandocRow->setSpacing(8);
  pandocPathEdit_ = new QLineEdit(card);
  pandocPathEdit_->setMinimumWidth(280);
  pandocBrowseButton_ = makeButton(card);
  pandocRow->addWidget(pandocPathEdit_);
  pandocRow->addWidget(pandocBrowseButton_);

  cardLayout->addLayout(pandocHeaderRow);
  cardLayout->addLayout(pandocRow);

  // Live status line showing which Pandoc will actually be used.
  pandocStatus_ = makeMutedLabel(card);
  pandocStatus_->setWordWrap(true);
  cardLayout->addWidget(pandocStatus_);

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
  connect(pandocPathEdit_, &QLineEdit::textChanged, this, [this] { refreshPandocStatus(); });
  // Auto-detect clears any pinned path so resolution falls back to the system
  // search; refreshPandocStatus() then shows what was found.
  connect(autoDetectButton_, &QPushButton::clicked, this, [this] { pandocPathEdit_->clear(); });
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
      formatList_->setCurrentRow(0);  // Default to first item (PDF)
    }
    formatList_->blockSignals(false);
  }

  // Header above the format list.
  sectionLabel_->setText(tr("Formats"));

  // Right card
  defaultFolderLabel_->setText(tr("Default Export Folder"));
  {
    const int cur = defaultFolderCombo_->currentIndex();
    defaultFolderCombo_->blockSignals(true);
    defaultFolderCombo_->clear();
    defaultFolderCombo_->addItem(tr("Auto"));
    defaultFolderCombo_->addItem(tr("Same folder as current file"));
    defaultFolderCombo_->addItem(tr("Custom..."));
    polishComboBox(defaultFolderCombo_);
    defaultFolderCombo_->setCurrentIndex(qBound(0, cur, defaultFolderCombo_->count() - 1));
    defaultFolderCombo_->blockSignals(false);
  }

  pandocLabel_->setText(tr("Pandoc Path"));
  if (pandocPathEdit_->text().isEmpty()) {
    pandocPathEdit_->setPlaceholderText(tr("(Auto-detect)"));
  }
  pandocBrowseButton_->setText(tr("Browse..."));
  autoDetectButton_->setText(tr("Auto-detect"));

  afterExportLabel_->setText(tr("After Export"));
  openAfterExportCheck_->setText(tr("Open the exported file directory"));

  refreshPandocStatus();
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

  // pandocPathEdit_ was loaded with signals blocked, so refresh manually.
  refreshPandocStatus();
}

void muffin::PrefsExportPage::refreshPandocStatus() {
  const QString configured = pandocPathEdit_->text().trimmed();
  if (!configured.isEmpty()) {
    if (QFileInfo(configured).isExecutable()) {
      pandocStatus_->setText(tr("Using: %1").arg(QDir::toNativeSeparators(configured)));
    } else {
      pandocStatus_->setText(tr("Not a valid executable; will auto-detect."));
    }
    return;
  }
  // Auto mode: report whatever the runner will actually invoke. searchSystem()
  // covers well-known install locations; if it still falls through to a bare
  // "pandoc", resolveOnPath() says whether PATH can find it (for display only).
  const QString resolved = muffin::PandocRunner::resolvedExecutable();
  if (resolved != QStringLiteral("pandoc")) {
    pandocStatus_->setText(tr("Detected: %1").arg(QDir::toNativeSeparators(resolved)));
    return;
  }
  const QString onPath = muffin::PandocRunner::resolveOnPath();
  if (!onPath.isEmpty()) {
    pandocStatus_->setText(tr("Detected: %1").arg(QDir::toNativeSeparators(onPath)));
  } else {
    pandocStatus_->setText(tr("Pandoc was not found. Install it, or click Browse to locate."));
  }
}
