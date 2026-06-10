#include "app/PrefsEditorPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

muffin::PrefsEditorPage::PrefsEditorPage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPageLeftMargin, kPageTopMargin, kPageRightMargin, kPageBottomMargin);
  layout->setSpacing(18);

  auto* cardContainer = new QWidget(this);
  cardContainer->setMaximumWidth(kContentWidth);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(kCardSpacing);
  layout->addWidget(cardContainer);

  // --- Card 1: Default Indent ---
  auto* indentCard = makeCard(this);
  auto* indentCardLayout = makeCardLayout(indentCard);
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
  auto* pairsCard = makeCard(this);
  auto* pairsLayout = makeCardLayout(pairsCard);
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
  auto* autoCard = makeCard(this);
  auto* autoLayout = makeCardLayout(autoCard);
  autocompleteLabel_ = makeSectionLabel(autoCard);
  emojiAutocompleteCheck_ = new QCheckBox(autoCard);
  emojiAutocompleteCheck_->setChecked(true);
  autoLayout->addWidget(autocompleteLabel_);
  autoLayout->addSpacing(2);
  autoLayout->addWidget(emojiAutocompleteCheck_);
  cardColumn->addWidget(autoCard);

  // --- Card 4: Live Rendering ---
  auto* liveCard = makeCard(this);
  auto* liveLayout = makeCardLayout(liveCard);
  liveRenderLabel_ = makeSectionLabel(liveCard);
  showBlockSourceCheck_ = new QCheckBox(liveCard);
  liveLayout->addWidget(liveRenderLabel_);
  liveLayout->addSpacing(2);
  liveLayout->addWidget(showBlockSourceCheck_);
  cardColumn->addWidget(liveCard);

  // --- Card 5: Default Copy Behavior ---
  auto* copyCard = makeCard(this);
  auto* copyLayout = makeCardLayout(copyCard);
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
  auto* lbCard = makeCard(this);
  auto* lbLayout = makeCardLayout(lbCard);
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
  auto* spellCard = makeCard(this);
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
  auto* twCard = makeCard(this);
  auto* twLayout = makeCardLayout(twCard);
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
  wireComboIndexSetting(indentCombo_, QStringLiteral("editor/indentSize"));
  wireBoolSetting(alignIndentCheck_, QStringLiteral("editor/alignIndent"));
  wireBoolSetting(matchBracketsCheck_, QStringLiteral("editor/matchBrackets"));
  wireBoolSetting(matchMarkdownCheck_, QStringLiteral("editor/matchMarkdown"));
  wireBoolSetting(emojiAutocompleteCheck_, QStringLiteral("editor/emojiAutocomplete"));
  wireBoolSetting(showBlockSourceCheck_, QStringLiteral("editor/showBlockSource"));
  wireBoolSetting(copyAsMarkdownCheck_, QStringLiteral("editor/copyAsMarkdown"));
  wireBoolSetting(copyLineWhenNoSelectionCheck_, QStringLiteral("editor/copyLineNoSelection"));
  connect(lineBreakLfRadio_, &QRadioButton::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), checked ? 0 : 1); });
  wireComboIndexSetting(spellCheckCombo_, QStringLiteral("editor/spellCheckLanguage"));
  wireBoolSetting(typewriterCursorMiddleCheck_, QStringLiteral("editor/typewriterCursorMiddle"));
  connect(disableTypewriterFocusButton_, &QPushButton::clicked, this,
          &muffin::PrefsEditorPage::disableTypewriterFocusRequested);
}

void muffin::PrefsEditorPage::retranslateUi() {
  indentLabel_->setText(tr("Default Indent"));
  indentDescLabel_->setText(tr("Only effective for references and lists generated via menu bar or shortcuts"));
  rebuildCombo(indentCombo_, {QStringLiteral("2"), QStringLiteral("4"), QStringLiteral("8")});
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
  rebuildCombo(
      spellCheckCombo_,
      {tr("Auto-detect language"), QStringLiteral("English (US)"), QStringLiteral("English (UK)"), tr("Chinese (Simplified)")});

  typewriterLabel_->setText(tr("Typewriter Mode"));
  typewriterCursorMiddleCheck_->setText(tr("Always keep the cursor in the middle of the screen"));
  disableTypewriterFocusButton_->setText(tr("Disable Typewriter / Focus Mode"));
}

void muffin::PrefsEditorPage::loadSettings() {
  loadComboIndex(indentCombo_, QStringLiteral("editor/indentSize"), 0);
  loadCheck(alignIndentCheck_, QStringLiteral("editor/alignIndent"), false);
  loadCheck(matchBracketsCheck_, QStringLiteral("editor/matchBrackets"), true);
  loadCheck(matchMarkdownCheck_, QStringLiteral("editor/matchMarkdown"), false);
  loadCheck(emojiAutocompleteCheck_, QStringLiteral("editor/emojiAutocomplete"), true);
  loadCheck(showBlockSourceCheck_, QStringLiteral("editor/showBlockSource"), false);
  loadCheck(copyAsMarkdownCheck_, QStringLiteral("editor/copyAsMarkdown"), true);
  loadCheck(copyLineWhenNoSelectionCheck_, QStringLiteral("editor/copyLineNoSelection"), false);

  const int lb = QSettings().value(QStringLiteral("editor/defaultLineBreak"), 1).toInt();
  const QSignalBlocker lfBlocker(lineBreakLfRadio_);
  const QSignalBlocker crlfBlocker(lineBreakCrlfRadio_);
  if (lb == 0) {
    lineBreakLfRadio_->setChecked(true);
  } else {
    lineBreakCrlfRadio_->setChecked(true);
  }

  loadComboIndex(spellCheckCombo_, QStringLiteral("editor/spellCheckLanguage"), 0);
  loadCheck(typewriterCursorMiddleCheck_, QStringLiteral("editor/typewriterCursorMiddle"), true);
}
