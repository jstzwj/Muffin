#include "app/PrefsFilesPage.h"
#include "app/FileOpenDispatcher.h"
#include "app/PreferencesSpinBox.h"

#include "app/WindowsIntegration.h"
#include "app/FileOpenProtection.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QLocale>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

class FileSizeSpinBox final : public muffin::PreferencesSpinBox<QDoubleSpinBox> {
public:
  explicit FileSizeSpinBox(QWidget* parent) : PreferencesSpinBox(parent) {
    // GB needs fractional values to display an existing KB/MB threshold
    // without rounding it to a different number of bytes.
    setDecimals(12);
  }

protected:
  QString textFromValue(double value) const override {
    QString text = QDoubleSpinBox::textFromValue(value);
    const auto zero = locale().zeroDigit();
    const auto point = locale().decimalPoint();
    if (text.contains(point)) {
      while (text.endsWith(zero))
        text.chop(zero.size());
      if (text.endsWith(point))
        text.chop(point.size());
    }
    return text;
  }
};

// Let extension chips use as many rows as the available settings width needs.
class ExtensionFlowLayout final : public QLayout {
public:
  explicit ExtensionFlowLayout(QWidget* parent) : QLayout(parent) {
    setContentsMargins(0, 0, 0, 0);
    setSpacing(6);
  }
  ~ExtensionFlowLayout() override {
    while (auto* item = takeAt(0))
      delete item;
  }
  void addItem(QLayoutItem* item) override { items_.append(item); }
  int count() const override { return items_.size(); }
  QLayoutItem* itemAt(int index) const override { return items_.value(index); }
  QLayoutItem* takeAt(int index) override {
    return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
  }
  Qt::Orientations expandingDirections() const override { return {}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
  QSize minimumSize() const override {
    QSize size;
    for (auto* item : items_)
      size = size.expandedTo(item->minimumSize());
    return size;
  }
  QSize sizeHint() const override { return minimumSize(); }
  void setGeometry(const QRect& rect) override {
    QLayout::setGeometry(rect);
    arrange(rect, true);
  }

private:
  int arrange(const QRect& rect, bool apply) const {
    int x = rect.x(), y = rect.y(), rowHeight = 0;
    for (auto* item : items_) {
      const QSize size = item->sizeHint();
      if (x > rect.x() && x + size.width() > rect.x() + rect.width()) {
        x = rect.x();
        y += rowHeight + spacing();
        rowHeight = 0;
      }
      if (apply)
        item->setGeometry(QRect(QPoint(x, y), size));
      x += size.width() + spacing();
      rowHeight = qMax(rowHeight, size.height());
    }
    return y - rect.y() + rowHeight;
  }
  QList<QLayoutItem*> items_;
};

} // namespace

muffin::PrefsFilesPage::PrefsFilesPage(QWidget* parent) : PreferencesPage(parent) {
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

  // --- Card 1: Startup ---
  auto* startupCard = new QWidget(this);
  startupCard->setObjectName(QStringLiteral("settingsCard"));
  auto* startupLayout = new QHBoxLayout(startupCard);
  startupLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  startupLayout->setSpacing(18);
  startupLabel_ = makeSectionLabel(startupCard);
  startupCombo_ = new QComboBox(startupCard);
  startupCombo_->setMinimumWidth(320);
  startupCombo_->setMaximumWidth(380);
  startupLayout->addWidget(startupLabel_);
  startupLayout->addStretch(1);
  startupLayout->addWidget(startupCombo_);
  cardColumn->addWidget(startupCard);

  auto* openingCard = makeCard(this);
  auto* openingLayout = makeCardLayout(openingCard);
  openingLabel_ = makeSectionLabel(openingCard);
  openInTabsCheck_ = new QCheckBox(openingCard);
  openInTabsCheck_->setObjectName(QStringLiteral("openFilesInTabs"));
  openInTabsHint_ = makeMutedLabel(openingCard);
  openInTabsHint_->setWordWrap(true);
  openingLayout->addWidget(openingLabel_);
  openingLayout->addWidget(openInTabsCheck_);
  openingLayout->addWidget(openInTabsHint_);
  cardColumn->addWidget(openingCard);

  // --- Card 2: Outline ---
  auto* outlineCard = new QWidget(this);
  outlineCard->setObjectName(QStringLiteral("settingsCard"));
  auto* outlineLayout = new QVBoxLayout(outlineCard);
  outlineLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  outlineLayout->setSpacing(kRowSpacing);
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
  extLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  extLayout->setSpacing(18);
  defaultExtLabel_ = makeSectionLabel(extCard);
  defaultExtCombo_ = new QComboBox(extCard);
  defaultExtCombo_->setMinimumWidth(320);
  defaultExtCombo_->setMaximumWidth(380);
  extLayout->addWidget(defaultExtLabel_);
  extLayout->addStretch(1);
  extLayout->addWidget(defaultExtCombo_);
  cardColumn->addWidget(extCard);

  auto* protectionCard = makeCard(this);
  protectionCard->setObjectName(QStringLiteral("fileOpenProtection"));
  auto* protectionLayout = makeCardLayout(protectionCard);
  protectionLayout->setSpacing(14);
  openProtectionLabel_ = makeSectionLabel(protectionCard);
  protectionLayout->addWidget(openProtectionLabel_);

  auto* extensionPanel = new QWidget(protectionCard);
  extensionPanel->setObjectName(QStringLiteral("fileProtectionOption"));
  auto* extensionLayout = new QVBoxLayout(extensionPanel);
  extensionLayout->setContentsMargins(14, 12, 14, 14);
  extensionLayout->setSpacing(8);
  extensionProtectionCheck_ = new QCheckBox(extensionPanel);
  extensionProtectionCheck_->setObjectName(QStringLiteral("extensionProtection"));
  textExtensionsHint_ = makeMutedLabel(extensionPanel);
  textExtensionsHint_->setWordWrap(true);
  extensionLayout->addWidget(extensionProtectionCheck_);
  extensionLayout->addWidget(textExtensionsHint_);

  extensionOptions_ = new QWidget(extensionPanel);
  auto* optionsLayout = new QVBoxLayout(extensionOptions_);
  optionsLayout->setContentsMargins(0, 4, 0, 0);
  optionsLayout->setSpacing(10);
  auto* extensionsHeader = new QHBoxLayout();
  textExtensionsLabel_ = new QLabel(extensionOptions_);
  resetExtensionsButton_ = makeButton(extensionOptions_);
  resetExtensionsButton_->setObjectName(QStringLiteral("resetTextExtensions"));
  resetExtensionsButton_->setAutoDefault(false);
  extensionsHeader->addWidget(textExtensionsLabel_, 1);
  extensionsHeader->addWidget(resetExtensionsButton_);
  optionsLayout->addLayout(extensionsHeader);
  textExtensionChips_ = new QWidget(extensionOptions_);
  textExtensionChips_->setObjectName(QStringLiteral("textExtensionChips"));
  new ExtensionFlowLayout(textExtensionChips_);
  optionsLayout->addWidget(textExtensionChips_);

  auto* addRow = new QHBoxLayout();
  addRow->setSpacing(8);
  textExtensionsEdit_ = new QLineEdit(extensionOptions_);
  textExtensionsEdit_->setObjectName(QStringLiteral("textExtensions"));
  textExtensionsLabel_->setBuddy(textExtensionsEdit_);
  addExtensionButton_ = makeButton(extensionOptions_);
  addExtensionButton_->setObjectName(QStringLiteral("addTextExtension"));
  addExtensionButton_->setAutoDefault(false);
  addExtensionButton_->setEnabled(false);
  addRow->addWidget(textExtensionsEdit_, 1);
  addRow->addWidget(addExtensionButton_);
  optionsLayout->addLayout(addRow);
  extensionLayout->addWidget(extensionOptions_);
  protectionLayout->addWidget(extensionPanel);

  auto* sizePanel = new QWidget(protectionCard);
  sizePanel->setObjectName(QStringLiteral("fileProtectionOption"));
  auto* sizeLayout = new QVBoxLayout(sizePanel);
  sizeLayout->setContentsMargins(14, 12, 14, 14);
  sizeLayout->setSpacing(8);
  sizeProtectionCheck_ = new QCheckBox(sizePanel);
  sizeProtectionCheck_->setObjectName(QStringLiteral("sizeProtection"));
  sizeProtectionHint_ = makeMutedLabel(sizePanel);
  sizeProtectionHint_->setWordWrap(true);
  sizeLayout->addWidget(sizeProtectionCheck_);
  sizeLayout->addWidget(sizeProtectionHint_);
  auto* sizeRow = new QHBoxLayout();
  sizeRow->setContentsMargins(0, 4, 0, 0);
  sizeRow->setSpacing(12);
  sizeLimitLabel_ = new QLabel(sizePanel);
  sizeLimitLabel_->setWordWrap(true);
  sizeLimitSpin_ = new FileSizeSpinBox(sizePanel);
  sizeLimitSpin_->setObjectName(QStringLiteral("sizeLimitValue"));
  sizeLimitSpin_->setFixedWidth(144);
  sizeLimitSpin_->setAlignment(Qt::AlignRight);
  sizeLimitSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  sizeLimitSpin_->setKeyboardTracking(false);
  sizeLimitUnit_ = new QComboBox(sizePanel);
  sizeLimitUnit_->setObjectName(QStringLiteral("sizeLimitUnit"));
  sizeLimitUnit_->addItem(QStringLiteral("KB"), qint64(1024));
  sizeLimitUnit_->addItem(QStringLiteral("MB"), qint64(1024) * 1024);
  sizeLimitUnit_->addItem(QStringLiteral("GB"), qint64(1024) * 1024 * 1024);
  sizeLimitUnit_->setFixedWidth(104);
  polishComboBox(sizeLimitUnit_);
  auto* valueRow = new QHBoxLayout();
  valueRow->setSpacing(8);
  valueRow->addWidget(sizeLimitSpin_);
  valueRow->addWidget(sizeLimitUnit_);
  sizeLimitLabel_->setBuddy(sizeLimitSpin_);
  sizeRow->addWidget(sizeLimitLabel_, 1);
  sizeRow->addLayout(valueRow);
  sizeLayout->addLayout(sizeRow);
  protectionLayout->addWidget(sizePanel);
  cardColumn->addWidget(protectionCard);

  // --- Card 4: Save & Restore ---
  auto* saveCard = new QWidget(this);
  saveCard->setObjectName(QStringLiteral("settingsCard"));
  auto* saveLayout = new QVBoxLayout(saveCard);
  saveLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  saveLayout->setSpacing(kRowSpacing);
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
  recentLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  recentLayout->setSpacing(kRowSpacing);
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
  dropLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  dropLayout->setSpacing(kRowSpacing);
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

#ifdef Q_OS_WIN
  // --- Card 7: Windows integration (Explorer verbs + file associations) ---
  auto* winCard = new QWidget(this);
  winCard->setObjectName(QStringLiteral("settingsCard"));
  auto* winLayout = new QVBoxLayout(winCard);
  winLayout->setContentsMargins(kRowHorizontalMargin, kRowVerticalMargin, kRowHorizontalMargin, kRowVerticalMargin);
  winLayout->setSpacing(kRowSpacing);
  winIntegrationLabel_ = makeSectionLabel(winCard);
  winContextMenuCheck_ = new QCheckBox(winCard);
  winAssociateCheck_ = new QCheckBox(winCard);
  winSetDefaultButton_ = makeButton(winCard);
  winLayout->addWidget(winIntegrationLabel_);
  winLayout->addSpacing(2);
  winLayout->addWidget(winContextMenuCheck_);
  winLayout->addWidget(winAssociateCheck_);
  winLayout->addWidget(winSetDefaultButton_, 0, Qt::AlignLeft);
  cardColumn->addWidget(winCard);
  winCard->setProperty("lastSettingsRow", true);
#else
  dropCard->setProperty("lastSettingsRow", true);
#endif

  layout->addStretch(1);

  retranslateUi();
  loadSettings();

  // Wire persistence
  wireBoolSetting(extensionProtectionCheck_, QLatin1String(file_open_protection::kExtensionEnabled));
  connect(addExtensionButton_, &QPushButton::clicked, this, &PrefsFilesPage::addTextExtensions);
  connect(textExtensionsEdit_, &QLineEdit::returnPressed, this, &PrefsFilesPage::addTextExtensions);
  connect(textExtensionsEdit_, &QLineEdit::editingFinished, this, &PrefsFilesPage::addTextExtensions);
  connect(textExtensionsEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
    addExtensionButton_->setEnabled(!file_open_protection::normalizedTextExtensions(text).isEmpty());
  });
  connect(resetExtensionsButton_, &QPushButton::clicked, this, [this] {
    textExtensions_ = file_open_protection::normalizedTextExtensions(file_open_protection::defaultTextExtensions());
    textExtensionsEdit_->clear();
    saveTextExtensions();
  });
  wireBoolSetting(sizeProtectionCheck_, QLatin1String(file_open_protection::kSizeEnabled));
  connect(sizeLimitSpin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    sizeLimitBytes_ =
        qBound(file_open_protection::kMinimumSizeLimitBytes, qint64(std::llround(value * sizeLimitUnitBytes_)),
               file_open_protection::kMaximumSizeLimitBytes);
    QSettings().setValue(QLatin1String(file_open_protection::kSizeLimitBytes), sizeLimitBytes_);
  });
  connect(sizeLimitUnit_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    // Commit pending input in its old unit before reformatting the value.
    sizeLimitSpin_->interpretText();
    updateSizeLimitControls();
    QSettings().setValue(QLatin1String(file_open_protection::kSizeLimitUnit), sizeLimitUnit_->currentText());
  });
  connect(extensionProtectionCheck_, &QCheckBox::toggled, extensionOptions_, &QWidget::setEnabled);
  connect(sizeProtectionCheck_, &QCheckBox::toggled, sizeLimitSpin_, &QWidget::setEnabled);
  connect(sizeProtectionCheck_, &QCheckBox::toggled, sizeLimitUnit_, &QWidget::setEnabled);
  connect(sizeProtectionCheck_, &QCheckBox::toggled, sizeLimitLabel_, &QWidget::setEnabled);
  connect(startupCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("files/startupBehavior"), index); });
  connect(openInTabsCheck_, &QCheckBox::toggled, this, [](bool checked) {
    QSettings settings;
    settings.setValue(QLatin1String(file_open_preferences::kOpenInTabs), checked);
    settings.sync(); // The next external launcher must see the change immediately.
  });
  connect(outlineFoldableCheck_, &QCheckBox::toggled, this, [this](bool checked) {
    QSettings().setValue(QStringLiteral("files/outlineFoldable"), checked);
    emit outlineFoldableChanged(checked);
  });
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
  connect(clearHistoryButton_, &QPushButton::clicked, this, &muffin::PrefsFilesPage::clearRecentFilesRequested);
  connect(restoreDraftButton_, &QPushButton::clicked, this, &muffin::PrefsFilesPage::restoreDraftsRequested);

#ifdef Q_OS_WIN
  // The registry is the single source of truth — each toggle re-applies the
  // full desired state (on = write the Muffin keys, off = remove only them).
  connect(winContextMenuCheck_, &QCheckBox::toggled, this,
          [this](bool on) { WindowsIntegration::applyIntegration(on, winAssociateCheck_->isChecked()); });
  connect(winAssociateCheck_, &QCheckBox::toggled, this,
          [this](bool on) { WindowsIntegration::applyIntegration(winContextMenuCheck_->isChecked(), on); });
  connect(winSetDefaultButton_, &QPushButton::clicked, this, [] { WindowsIntegration::openDefaultAppsSettings(); });
#endif
}

void muffin::PrefsFilesPage::retranslateUi() {
  openProtectionLabel_->setText(tr("File Open Protection"));
  extensionProtectionCheck_->setText(tr("Extension protection"));
  textExtensionsLabel_->setText(tr("Allowed file extensions"));
  textExtensionsHint_->setText(tr("Ask before opening files with extensions outside this list."));
  textExtensionsEdit_->setPlaceholderText(tr("e.g. .txt, .log"));
  addExtensionButton_->setText(tr("Add"));
  resetExtensionsButton_->setText(tr("Restore Defaults"));
  sizeProtectionCheck_->setText(tr("File size protection"));
  sizeProtectionHint_->setText(tr("Large files may take longer to open and use more memory."));
  sizeLimitLabel_->setText(tr("Warn before opening files larger than"));
  textExtensionsEdit_->setAccessibleName(textExtensionsLabel_->text());
  sizeLimitSpin_->setAccessibleName(sizeLimitLabel_->text());
  sizeLimitUnit_->setAccessibleName(tr("File size unit"));
  refreshTextExtensionChips();
  startupLabel_->setText(tr("Startup"));
  openingLabel_->setText(tr("Opening Files"));
  openInTabsCheck_->setText(tr("Open files in tabs by default"));
  openInTabsHint_->setText(tr("Files opened from the file manager or command line use the existing Muffin window. "
                              "Turn this off to open them in a new window."));
  {
    const int cur = startupCombo_->currentIndex();
    startupCombo_->blockSignals(true);
    startupCombo_->clear();
    startupCombo_->addItem(tr("Always start fresh"));
    startupCombo_->addItem(tr("Restore previous session"));
    polishComboBox(startupCombo_);
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
    polishComboBox(defaultExtCombo_);
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
    polishComboBox(dropFolderCombo_);
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
    polishComboBox(dropMdCombo_);
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
    polishComboBox(dropImportCombo_);
    dropImportCombo_->setCurrentIndex(qBound(0, cur, dropImportCombo_->count() - 1));
    dropImportCombo_->blockSignals(false);
  }

#ifdef Q_OS_WIN
  winIntegrationLabel_->setText(tr("Windows Integration"));
  winContextMenuCheck_->setText(tr("Add \"Open with Muffin\" to File Explorer context menu for files and folders"));
  winAssociateCheck_->setText(
      tr("Register Muffin as an editor for Markdown files (.md, .markdown, .mdx, .mkd, .mdown)"));
  winSetDefaultButton_->setText(tr("Set Muffin as Default…"));
#endif
}

void muffin::PrefsFilesPage::addTextExtensions() {
  const auto additions = file_open_protection::normalizedTextExtensions(textExtensionsEdit_->text());
  if (additions.isEmpty())
    return;
  for (const auto& extension : additions) {
    if (!textExtensions_.contains(extension))
      textExtensions_.append(extension);
  }
  textExtensionsEdit_->clear();
  saveTextExtensions();
}

void muffin::PrefsFilesPage::saveTextExtensions() {
  QSettings().setValue(QLatin1String(file_open_protection::kTextExtensions),
                       textExtensions_.join(QStringLiteral(", ")));
  refreshTextExtensionChips();
}

void muffin::PrefsFilesPage::refreshTextExtensionChips() {
  auto* layout = textExtensionChips_->layout();
  while (auto* item = layout->takeAt(0)) {
    item->widget()->hide();
    item->widget()->deleteLater();
    delete item;
  }
  for (const auto& extension : textExtensions_) {
    auto* chip = new QWidget(textExtensionChips_);
    chip->setObjectName(QStringLiteral("textExtensionChip"));
    auto* row = new QHBoxLayout(chip);
    row->setContentsMargins(9, 3, 4, 3);
    row->setSpacing(4);
    auto* label = new QLabel(QLatin1Char('.') + extension, chip);
    label->setTextFormat(Qt::PlainText);
    row->addWidget(label);
    auto* remove = new QToolButton(chip);
    remove->setObjectName(QStringLiteral("removeTextExtension"));
    remove->setProperty("extension", extension);
    remove->setText(QString(QChar(0x00d7)));
    remove->setFixedSize(20, 20);
    remove->setCursor(Qt::PointingHandCursor);
    remove->setToolTip(tr("Remove .%1").arg(extension));
    remove->setAccessibleName(remove->toolTip());
    row->addWidget(remove);
    connect(remove, &QToolButton::clicked, this, [this, extension] {
      textExtensions_.removeAll(extension);
      saveTextExtensions();
      textExtensionsEdit_->setFocus();
    });
    layout->addWidget(chip);
  }
  layout->invalidate();
  textExtensionChips_->updateGeometry();
}

void muffin::PrefsFilesPage::updateSizeLimitControls() {
  const QSignalBlocker blocker(sizeLimitSpin_);
  sizeLimitUnitBytes_ = sizeLimitUnit_->currentData().toLongLong();
  sizeLimitSpin_->setRange(double(file_open_protection::kMinimumSizeLimitBytes) / sizeLimitUnitBytes_,
                           double(file_open_protection::kMaximumSizeLimitBytes) / sizeLimitUnitBytes_);
  sizeLimitSpin_->setSingleStep(sizeLimitUnit_->currentText() == QStringLiteral("GB") ? 0.1 : 1.0);
  sizeLimitSpin_->setValue(double(sizeLimitBytes_) / sizeLimitUnitBytes_);
}

void muffin::PrefsFilesPage::loadSettings() {
  QSettings settings;
  loadCheck(extensionProtectionCheck_, QLatin1String(file_open_protection::kExtensionEnabled), true);
  textExtensions_ = file_open_protection::normalizedTextExtensions(
      settings
          .value(QLatin1String(file_open_protection::kTextExtensions), file_open_protection::defaultTextExtensions())
          .toString());
  refreshTextExtensionChips();
  loadCheck(sizeProtectionCheck_, QLatin1String(file_open_protection::kSizeEnabled), true);
  sizeLimitBytes_ = file_open_protection::sizeLimitBytes();
  const int unit = sizeLimitUnit_->findText(
      settings.value(QLatin1String(file_open_protection::kSizeLimitUnit), QStringLiteral("MB")).toString());
  sizeLimitUnit_->setCurrentIndex(unit >= 0 ? unit : 1);
  updateSizeLimitControls();
  extensionOptions_->setEnabled(extensionProtectionCheck_->isChecked());
  sizeLimitSpin_->setEnabled(sizeProtectionCheck_->isChecked());
  sizeLimitUnit_->setEnabled(sizeProtectionCheck_->isChecked());
  sizeLimitLabel_->setEnabled(sizeProtectionCheck_->isChecked());

  const int startup = settings.value(QStringLiteral("files/startupBehavior"), 1).toInt();
  loadCheck(openInTabsCheck_, QLatin1String(file_open_preferences::kOpenInTabs), true);
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

#ifdef Q_OS_WIN
  // Reflect the live registry state rather than a mirrored preference, so the
  // checkboxes always agree with what Explorer actually sees.
  winContextMenuCheck_->blockSignals(true);
  winContextMenuCheck_->setChecked(WindowsIntegration::isContextMenuEnabled());
  winContextMenuCheck_->blockSignals(false);
  winAssociateCheck_->blockSignals(true);
  winAssociateCheck_->setChecked(WindowsIntegration::isAssociationEnabled());
  winAssociateCheck_->blockSignals(false);
#endif
}
