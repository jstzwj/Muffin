#include "app/PrefsEditorPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

namespace muffin {

PrefsEditorPage::PrefsEditorPage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(38, 34, 46, 34);
  layout->setSpacing(22);

  auto* cardContainer = new QWidget(this);
  cardContainer->setMaximumWidth(640);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(14);
  layout->addWidget(cardContainer);

  // --- Card 1: Default Indent ---
  auto* indentCard = new QWidget(this);
  indentCard->setObjectName(QStringLiteral("settingsCard"));
  auto* indentCardLayout = new QVBoxLayout(indentCard);
  indentCardLayout->setContentsMargins(18, 16, 18, 16);
  indentCardLayout->setSpacing(12);
  indentLabel_ = makeSectionLabel(indentCard);
  indentDescLabel_ = makeMutedLabel(indentCard);
  indentDescLabel_->setWordWrap(true);

  auto* indentRow = new QHBoxLayout();
  indentRow->setSpacing(16);
  indentCombo_ = new QComboBox(indentCard);
  indentCombo_->setMinimumWidth(120);
  indentCombo_->setMaximumWidth(160);
  alignIndentCheck_ = new QCheckBox(indentCard);
  indentRow->addWidget(indentCombo_);
  indentRow->addWidget(alignIndentCheck_);
  indentRow->addStretch(1);

  indentCardLayout->addWidget(indentLabel_);
  indentCardLayout->addWidget(indentDescLabel_);
  indentCardLayout->addSpacing(2);
  indentCardLayout->addLayout(indentRow);
  cardColumn->addWidget(indentCard);

  // --- Card 2: Paired Symbols ---
  auto* pairsCard = new QWidget(this);
  pairsCard->setObjectName(QStringLiteral("settingsCard"));
  auto* pairsLayout = new QVBoxLayout(pairsCard);
  pairsLayout->setContentsMargins(18, 16, 18, 16);
  pairsLayout->setSpacing(12);
  pairsLabel_ = makeSectionLabel(pairsCard);
  matchBracketsCheck_ = new QCheckBox(pairsCard);
  matchBracketsCheck_->setChecked(true);
  matchMarkdownCheck_ = new QCheckBox(pairsCard);
  pairsLayout->addWidget(pairsLabel_);
  pairsLayout->addSpacing(2);
  pairsLayout->addWidget(matchBracketsCheck_);
  pairsLayout->addWidget(matchMarkdownCheck_);
  cardColumn->addWidget(pairsCard);

  // --- Card 3: Autocomplete ---
  auto* autoCard = new QWidget(this);
  autoCard->setObjectName(QStringLiteral("settingsCard"));
  auto* autoLayout = new QVBoxLayout(autoCard);
  autoLayout->setContentsMargins(18, 16, 18, 16);
  autoLayout->setSpacing(12);
  autocompleteLabel_ = makeSectionLabel(autoCard);
  emojiAutocompleteCheck_ = new QCheckBox(autoCard);
  emojiAutocompleteCheck_->setChecked(true);
  autoLayout->addWidget(autocompleteLabel_);
  autoLayout->addSpacing(2);
  autoLayout->addWidget(emojiAutocompleteCheck_);
  cardColumn->addWidget(autoCard);

  // --- Card 4: Live Rendering ---
  auto* liveCard = new QWidget(this);
  liveCard->setObjectName(QStringLiteral("settingsCard"));
  auto* liveLayout = new QVBoxLayout(liveCard);
  liveLayout->setContentsMargins(18, 16, 18, 16);
  liveLayout->setSpacing(12);
  liveRenderLabel_ = makeSectionLabel(liveCard);
  showBlockSourceCheck_ = new QCheckBox(liveCard);
  liveLayout->addWidget(liveRenderLabel_);
  liveLayout->addSpacing(2);
  liveLayout->addWidget(showBlockSourceCheck_);
  cardColumn->addWidget(liveCard);

  // --- Card 5: Default Copy Behavior ---
  auto* copyCard = new QWidget(this);
  copyCard->setObjectName(QStringLiteral("settingsCard"));
  auto* copyLayout = new QVBoxLayout(copyCard);
  copyLayout->setContentsMargins(18, 16, 18, 16);
  copyLayout->setSpacing(12);
  copyLabel_ = makeSectionLabel(copyCard);
  copyAsMarkdownCheck_ = new QCheckBox(copyCard);
  copyAsMarkdownCheck_->setChecked(true);
  copyLineWhenNoSelectionCheck_ = new QCheckBox(copyCard);
  copyLayout->addWidget(copyLabel_);
  copyLayout->addSpacing(2);
  copyLayout->addWidget(copyAsMarkdownCheck_);
  copyLayout->addWidget(copyLineWhenNoSelectionCheck_);
  cardColumn->addWidget(copyCard);

  // --- Card 6: Default Line Break ---
  auto* lbCard = new QWidget(this);
  lbCard->setObjectName(QStringLiteral("settingsCard"));
  auto* lbLayout = new QVBoxLayout(lbCard);
  lbLayout->setContentsMargins(18, 16, 18, 16);
  lbLayout->setSpacing(12);
  lineBreakLabel_ = makeSectionLabel(lbCard);
  lineBreakDescLabel_ = makeMutedLabel(lbCard);
  lineBreakLfRadio_ = new QRadioButton(lbCard);
  lineBreakCrlfRadio_ = new QRadioButton(lbCard);
  lineBreakCrlfRadio_->setChecked(true);
  lbLayout->addWidget(lineBreakLabel_);
  lbLayout->addWidget(lineBreakDescLabel_);
  lbLayout->addSpacing(2);
  lbLayout->addWidget(lineBreakLfRadio_);
  lbLayout->addWidget(lineBreakCrlfRadio_);
  cardColumn->addWidget(lbCard);

  // --- Card 7: Spell Check ---
  auto* spellCard = new QWidget(this);
  spellCard->setObjectName(QStringLiteral("settingsCard"));
  auto* spellLayout = new QHBoxLayout(spellCard);
  spellLayout->setContentsMargins(18, 16, 18, 16);
  spellLayout->setSpacing(24);
  spellCheckLabel_ = makeSectionLabel(spellCard);
  spellCheckCombo_ = new QComboBox(spellCard);
  spellCheckCombo_->setMinimumWidth(260);
  spellCheckCombo_->setMaximumWidth(380);
  spellLayout->addWidget(spellCheckLabel_);
  spellLayout->addStretch(1);
  spellLayout->addWidget(spellCheckCombo_);
  cardColumn->addWidget(spellCard);

  // --- Card 8: Typewriter Mode ---
  auto* twCard = new QWidget(this);
  twCard->setObjectName(QStringLiteral("settingsCard"));
  auto* twLayout = new QVBoxLayout(twCard);
  twLayout->setContentsMargins(18, 16, 18, 16);
  twLayout->setSpacing(12);
  typewriterLabel_ = makeSectionLabel(twCard);
  typewriterCursorMiddleCheck_ = new QCheckBox(twCard);
  typewriterCursorMiddleCheck_->setChecked(true);
  disableTypewriterFocusButton_ = makeButton(twCard);
  twLayout->addWidget(typewriterLabel_);
  twLayout->addSpacing(2);
  twLayout->addWidget(typewriterCursorMiddleCheck_);
  twLayout->addWidget(disableTypewriterFocusButton_, 0, Qt::AlignLeft);
  cardColumn->addWidget(twCard);

  layout->addStretch(1);

  retranslateUi();
  loadSettings();

  // Wire persistence
  connect(indentCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("editor/indentSize"), index); });
  connect(alignIndentCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/alignIndent"), checked); });
  connect(matchBracketsCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/matchBrackets"), checked); });
  connect(matchMarkdownCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/matchMarkdown"), checked); });
  connect(emojiAutocompleteCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/emojiAutocomplete"), checked); });
  connect(showBlockSourceCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/showBlockSource"), checked); });
  connect(copyAsMarkdownCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/copyAsMarkdown"), checked); });
  connect(copyLineWhenNoSelectionCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/copyLineNoSelection"), checked); });
  connect(lineBreakLfRadio_, &QRadioButton::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), checked ? 0 : 1); });
  connect(spellCheckCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("editor/spellCheckLanguage"), index); });
  connect(typewriterCursorMiddleCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/typewriterCursorMiddle"), checked); });
  connect(disableTypewriterFocusButton_, &QPushButton::clicked, this,
          &PrefsEditorPage::disableTypewriterFocusRequested);
}

void PrefsEditorPage::retranslateUi() {
  indentLabel_->setText(tr("Default Indent"));
  indentDescLabel_->setText(tr("Only effective for references and lists generated via menu bar or shortcuts"));
  {
    const int cur = indentCombo_->currentIndex();
    indentCombo_->blockSignals(true);
    indentCombo_->clear();
    indentCombo_->addItem(QStringLiteral("2"));
    indentCombo_->addItem(QStringLiteral("4"));
    indentCombo_->addItem(QStringLiteral("8"));
    indentCombo_->setCurrentIndex(qBound(0, cur, indentCombo_->count() - 1));
    indentCombo_->blockSignals(false);
  }
  alignIndentCheck_->setText(tr("Align Indent"));

  pairsLabel_->setText(tr("Use Paired Symbols"));
  matchBracketsCheck_->setText(tr("Match brackets and quotes"));
  matchMarkdownCheck_->setText(tr("Match Markdown characters"));

  autocompleteLabel_->setText(tr("Autocomplete"));
  emojiAutocompleteCheck_->setText(tr("Enable Emoji autocomplete"));

  liveRenderLabel_->setText(tr("Live Rendering"));
  showBlockSourceCheck_->setText(tr("Show Markdown source of the current block element"));

  copyLabel_->setText(tr("Default Copy Behavior"));
  copyAsMarkdownCheck_->setText(tr("Copy Markdown source when copying plain text"));
  copyLineWhenNoSelectionCheck_->setText(tr("Copy or cut the entire line when no text is selected"));

  lineBreakLabel_->setText(tr("Default Line Break"));
  lineBreakDescLabel_->setText(tr("Default line break for new files"));
  lineBreakLfRadio_->setText(tr("LF (Unix Style)"));
  lineBreakCrlfRadio_->setText(tr("CRLF (Windows Style)"));

  spellCheckLabel_->setText(tr("Spell Check"));
  {
    const int cur = spellCheckCombo_->currentIndex();
    spellCheckCombo_->blockSignals(true);
    spellCheckCombo_->clear();
    spellCheckCombo_->addItem(tr("Auto-detect language"));
    spellCheckCombo_->addItem(QStringLiteral("English (US)"));
    spellCheckCombo_->addItem(QStringLiteral("English (UK)"));
    spellCheckCombo_->addItem(tr("Chinese (Simplified)"));
    spellCheckCombo_->setCurrentIndex(qBound(0, cur, spellCheckCombo_->count() - 1));
    spellCheckCombo_->blockSignals(false);
  }

  typewriterLabel_->setText(tr("Typewriter Mode"));
  typewriterCursorMiddleCheck_->setText(tr("Always keep the cursor in the middle of the screen"));
  disableTypewriterFocusButton_->setText(tr("Disable Typewriter / Focus Mode"));
}

void PrefsEditorPage::loadSettings() {
  QSettings settings;

  const int indent = settings.value(QStringLiteral("editor/indentSize"), 0).toInt();
  if (indentCombo_->count() > 0) {
    indentCombo_->blockSignals(true);
    indentCombo_->setCurrentIndex(qBound(0, indent, indentCombo_->count() - 1));
    indentCombo_->blockSignals(false);
  }
  alignIndentCheck_->blockSignals(true);
  alignIndentCheck_->setChecked(settings.value(QStringLiteral("editor/alignIndent"), false).toBool());
  alignIndentCheck_->blockSignals(false);

  matchBracketsCheck_->blockSignals(true);
  matchBracketsCheck_->setChecked(settings.value(QStringLiteral("editor/matchBrackets"), true).toBool());
  matchBracketsCheck_->blockSignals(false);
  matchMarkdownCheck_->blockSignals(true);
  matchMarkdownCheck_->setChecked(settings.value(QStringLiteral("editor/matchMarkdown"), false).toBool());
  matchMarkdownCheck_->blockSignals(false);

  emojiAutocompleteCheck_->blockSignals(true);
  emojiAutocompleteCheck_->setChecked(settings.value(QStringLiteral("editor/emojiAutocomplete"), true).toBool());
  emojiAutocompleteCheck_->blockSignals(false);

  showBlockSourceCheck_->blockSignals(true);
  showBlockSourceCheck_->setChecked(settings.value(QStringLiteral("editor/showBlockSource"), false).toBool());
  showBlockSourceCheck_->blockSignals(false);

  copyAsMarkdownCheck_->blockSignals(true);
  copyAsMarkdownCheck_->setChecked(settings.value(QStringLiteral("editor/copyAsMarkdown"), true).toBool());
  copyAsMarkdownCheck_->blockSignals(false);
  copyLineWhenNoSelectionCheck_->blockSignals(true);
  copyLineWhenNoSelectionCheck_->setChecked(settings.value(QStringLiteral("editor/copyLineNoSelection"), false).toBool());
  copyLineWhenNoSelectionCheck_->blockSignals(false);

  const int lb = settings.value(QStringLiteral("editor/defaultLineBreak"), 1).toInt();
  lineBreakLfRadio_->blockSignals(true);
  lineBreakCrlfRadio_->blockSignals(true);
  if (lb == 0) {
    lineBreakLfRadio_->setChecked(true);
  } else {
    lineBreakCrlfRadio_->setChecked(true);
  }
  lineBreakLfRadio_->blockSignals(false);
  lineBreakCrlfRadio_->blockSignals(false);

  const int spell = settings.value(QStringLiteral("editor/spellCheckLanguage"), 0).toInt();
  if (spellCheckCombo_->count() > 0) {
    spellCheckCombo_->blockSignals(true);
    spellCheckCombo_->setCurrentIndex(qBound(0, spell, spellCheckCombo_->count() - 1));
    spellCheckCombo_->blockSignals(false);
  }

  typewriterCursorMiddleCheck_->blockSignals(true);
  typewriterCursorMiddleCheck_->setChecked(settings.value(QStringLiteral("editor/typewriterCursorMiddle"), true).toBool());
  typewriterCursorMiddleCheck_->blockSignals(false);
}

}  // namespace muffin
