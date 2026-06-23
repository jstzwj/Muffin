#include "app/PrefsMarkdownPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

muffin::PrefsMarkdownPage::PrefsMarkdownPage(QWidget* parent) : PreferencesPage(parent) {
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

  // --- Card 1: Markdown Syntax Preferences ---
  auto* syntaxCard = makeCard(this);
  auto* syntaxLayout = makeCardLayout(syntaxCard);

  auto* syntaxHeaderRow = new QHBoxLayout();
  syntaxLabel_ = makeSectionLabel(syntaxCard);
  syntaxHeaderRow->addWidget(syntaxLabel_);
  syntaxHeaderRow->addStretch(1);

  strictModeCheck_ = new QCheckBox(syntaxCard);
  breakOnSingleNewlineCheck_ = new QCheckBox(syntaxCard);
  breakOnSingleNewlineCheck_->setChecked(true);

  auto* headingRow = new QHBoxLayout();
  headingRow->setSpacing(16);
  headingStyleLabel_ = new QLabel(syntaxCard);
  headingStyleCombo_ = new QComboBox(syntaxCard);
  headingStyleCombo_->setMinimumWidth(180);
  headingRow->addWidget(headingStyleLabel_);
  headingRow->addStretch(1);
  headingRow->addWidget(headingStyleCombo_);

  auto* ulRow = new QHBoxLayout();
  ulRow->setSpacing(16);
  unorderedListLabel_ = new QLabel(syntaxCard);
  unorderedListCombo_ = new QComboBox(syntaxCard);
  unorderedListCombo_->setMinimumWidth(180);
  ulRow->addWidget(unorderedListLabel_);
  ulRow->addStretch(1);
  ulRow->addWidget(unorderedListCombo_);

  auto* olRow = new QHBoxLayout();
  olRow->setSpacing(16);
  orderedListLabel_ = new QLabel(syntaxCard);
  orderedListCombo_ = new QComboBox(syntaxCard);
  orderedListCombo_->setMinimumWidth(180);
  olRow->addWidget(orderedListLabel_);
  olRow->addStretch(1);
  olRow->addWidget(orderedListCombo_);

  syntaxLayout->addLayout(syntaxHeaderRow);
  syntaxLayout->addWidget(strictModeCheck_);
  syntaxLayout->addWidget(breakOnSingleNewlineCheck_);
  syntaxLayout->addSpacing(4);
  syntaxLayout->addLayout(headingRow);
  syntaxLayout->addLayout(ulRow);
  syntaxLayout->addLayout(olRow);
  cardColumn->addWidget(syntaxCard);

  // --- Card 2: Extended Syntax ---
  auto* extCard = makeCard(this);
  auto* extLayout = makeCardLayout(extCard);

  extSyntaxLabel_ = makeSectionLabel(extCard);

  autoLinkCheck_ = new QCheckBox(extCard);
  autoLinkCheck_->setChecked(true);
  inlineMathCheck_ = new QCheckBox(extCard);
  inlineMathCheck_->setChecked(true);
  subscriptCheck_ = new QCheckBox(extCard);
  superscriptCheck_ = new QCheckBox(extCard);
  highlightCheck_ = new QCheckBox(extCard);
  alertBoxCheck_ = new QCheckBox(extCard);
  alertBoxCheck_->setChecked(true);
  diagramsCheck_ = new QCheckBox(extCard);
  diagramsCheck_->setChecked(true);

  auto* diagramRow = new QHBoxLayout();
  diagramRow->setSpacing(10);
  diagramRow->addWidget(diagramsCheck_);
  diagramOptionsButton_ = makeButton(extCard);
  diagramRow->addWidget(diagramOptionsButton_);
  diagramRow->addStretch(1);

  extLayout->addWidget(extSyntaxLabel_);
  extLayout->addSpacing(2);
  extLayout->addWidget(autoLinkCheck_);
  extLayout->addWidget(inlineMathCheck_);
  extLayout->addWidget(subscriptCheck_);
  extLayout->addWidget(superscriptCheck_);
  extLayout->addWidget(highlightCheck_);
  extLayout->addWidget(alertBoxCheck_);
  extLayout->addLayout(diagramRow);
  cardColumn->addWidget(extCard);

  // --- Card 3: Smart Punctuation ---
  auto* punctCard = makeCard(this);
  auto* punctLayout = makeCardLayout(punctCard);

  auto* punctHeaderRow = new QHBoxLayout();
  smartPunctLabel_ = makeSectionLabel(punctCard);
  punctHeaderRow->addWidget(smartPunctLabel_);
  punctHeaderRow->addStretch(1);

  auto* convertRow = new QHBoxLayout();
  convertRow->setSpacing(16);
  convertOnInputLabel_ = new QLabel(punctCard);
  convertOnInputCombo_ = new QComboBox(punctCard);
  convertOnInputCombo_->setMinimumWidth(260);
  convertRow->addWidget(convertOnInputLabel_);
  convertRow->addStretch(1);
  convertRow->addWidget(convertOnInputCombo_);

  smartQuotesCheck_ = new QCheckBox(punctCard);
  auto* quotesRow = new QHBoxLayout();
  quotesRow->setSpacing(10);
  quotesRow->addSpacing(28);  // indent under checkbox
  singleQuoteCombo_ = new QComboBox(punctCard);
  singleQuoteCombo_->setMinimumWidth(100);
  doubleQuoteCombo_ = new QComboBox(punctCard);
  doubleQuoteCombo_->setMinimumWidth(100);
  quotesRow->addWidget(singleQuoteCombo_);
  quotesRow->addWidget(doubleQuoteCombo_);
  quotesRow->addStretch(1);

  smartDashesCheck_ = new QCheckBox(punctCard);
  convertOnRenderingCheck_ = new QCheckBox(punctCard);
  remapUnicodeCheck_ = new QCheckBox(punctCard);

  punctLayout->addLayout(punctHeaderRow);
  punctLayout->addLayout(convertRow);
  punctLayout->addWidget(smartQuotesCheck_);
  punctLayout->addLayout(quotesRow);
  punctLayout->addWidget(smartDashesCheck_);
  punctLayout->addWidget(convertOnRenderingCheck_);
  punctLayout->addWidget(remapUnicodeCheck_);
  cardColumn->addWidget(punctCard);

  // --- Card 4: Code Blocks ---
  auto* codeCard = makeCard(this);
  codeCard->setProperty("lastSettingsRow", true);
  auto* codeLayout = makeCardLayout(codeCard);

  codeBlockLabel_ = makeSectionLabel(codeCard);
  showLineNumbersCheck_ = new QCheckBox(codeCard);
  codeBlockWrapCheck_ = new QCheckBox(codeCard);
  codeBlockWrapCheck_->setChecked(true);

  auto* indentRow = new QHBoxLayout();
  indentRow->setSpacing(16);
  codeIndentLabel_ = new QLabel(codeCard);
  codeIndentCombo_ = new QComboBox(codeCard);
  codeIndentCombo_->setMinimumWidth(120);
  indentRow->addWidget(codeIndentLabel_);
  indentRow->addStretch(1);
  indentRow->addWidget(codeIndentCombo_);

  auto* langRow = new QHBoxLayout();
  langRow->setSpacing(16);
  defaultLangLabel_ = new QLabel(codeCard);
  defaultLangCombo_ = new QComboBox(codeCard);
  defaultLangCombo_->setMinimumWidth(180);
  langRow->addWidget(defaultLangLabel_);
  langRow->addStretch(1);
  langRow->addWidget(defaultLangCombo_);

  auto* autoLangRow = new QHBoxLayout();
  autoLangRow->setSpacing(16);
  autoLangLabel_ = new QLabel(codeCard);
  autoLangCombo_ = new QComboBox(codeCard);
  autoLangCombo_->setMinimumWidth(260);
  autoLangRow->addWidget(autoLangLabel_);
  autoLangRow->addStretch(1);
  autoLangRow->addWidget(autoLangCombo_);

  codeLayout->addWidget(codeBlockLabel_);
  codeLayout->addSpacing(2);
  codeLayout->addWidget(showLineNumbersCheck_);
  codeLayout->addWidget(codeBlockWrapCheck_);
  codeLayout->addLayout(indentRow);
  codeLayout->addLayout(langRow);
  codeLayout->addLayout(autoLangRow);
  cardColumn->addWidget(codeCard);

  layout->addStretch(1);

  retranslateUi();
  loadSettings();

  // Wire persistence
  wireBoolSetting(strictModeCheck_, QStringLiteral("markdown/strictMode"));
  wireBoolSetting(breakOnSingleNewlineCheck_, QStringLiteral("markdown/breakOnSingleNewline"));
  wireComboIndexSetting(headingStyleCombo_, QStringLiteral("markdown/headingStyle"));
  wireComboIndexSetting(unorderedListCombo_, QStringLiteral("markdown/unorderedList"));
  wireComboIndexSetting(orderedListCombo_, QStringLiteral("markdown/orderedList"));
  wireBoolSetting(autoLinkCheck_, QStringLiteral("markdown/autoLink"));
  wireBoolSetting(inlineMathCheck_, QStringLiteral("markdown/inlineMath"));
  wireBoolSetting(subscriptCheck_, QStringLiteral("markdown/subscript"));
  wireBoolSetting(superscriptCheck_, QStringLiteral("markdown/superscript"));
  wireBoolSetting(highlightCheck_, QStringLiteral("markdown/highlight"));
  wireBoolSetting(alertBoxCheck_, QStringLiteral("markdown/alertBox"));
  wireBoolSetting(diagramsCheck_, QStringLiteral("markdown/diagrams"));
  wireComboIndexSetting(convertOnInputCombo_, QStringLiteral("markdown/convertOnInput"));
  wireBoolSetting(smartQuotesCheck_, QStringLiteral("markdown/smartQuotes"));
  wireComboIndexSetting(singleQuoteCombo_, QStringLiteral("markdown/singleQuoteStyle"));
  wireComboIndexSetting(doubleQuoteCombo_, QStringLiteral("markdown/doubleQuoteStyle"));
  wireBoolSetting(smartDashesCheck_, QStringLiteral("markdown/smartDashes"));
  wireBoolSetting(convertOnRenderingCheck_, QStringLiteral("markdown/convertOnRendering"));
  // Convert on Input / Convert on Rendering are mutually exclusive conversion timings.
  connect(convertOnRenderingCheck_, &QCheckBox::toggled, this, [this](bool on) {
    convertOnInputCombo_->setEnabled(!on);
    if (on && convertOnInputCombo_->currentIndex() != 0) {
      convertOnInputCombo_->setCurrentIndex(0);  // zeroes markdown/convertOnInput via its wire
    }
  });
  wireBoolSetting(remapUnicodeCheck_, QStringLiteral("markdown/remapUnicode"));
  wireBoolSetting(showLineNumbersCheck_, QStringLiteral("markdown/showLineNumbers"));
  wireBoolSetting(codeBlockWrapCheck_, QStringLiteral("markdown/codeBlockWrap"));
  wireComboIndexSetting(codeIndentCombo_, QStringLiteral("markdown/codeIndent"));
  wireComboIndexSetting(defaultLangCombo_, QStringLiteral("markdown/defaultCodeLang"));
  wireComboIndexSetting(autoLangCombo_, QStringLiteral("markdown/autoCodeLang"));

  // Diagrams need a rendering engine not yet present in Muffin, so the toggle stays disabled and
  // labelled "coming soon" so users aren't misled into thinking it works.
  for (QCheckBox* unbuilt : {diagramsCheck_}) {
    if (unbuilt) {
      unbuilt->setEnabled(false);
    }
  }
  if (diagramOptionsButton_) {
    diagramOptionsButton_->setEnabled(false);
  }
}

void muffin::PrefsMarkdownPage::retranslateUi() {
  // Card 1: Syntax Preferences
  syntaxLabel_->setText(tr("Markdown Syntax Preferences"));
  strictModeCheck_->setText(tr("Strict Mode"));
  strictModeCheck_->setToolTip(tr(
      "Strict Mode turns off tables, strikethrough and task lists for plain CommonMark "
      "structure. Inline extensions (formulas, auto links, sub/superscript, etc.) always "
      "follow their own switches and are not affected by Strict Mode."));
  breakOnSingleNewlineCheck_->setText(tr("Break on Single Newline"));
  breakOnSingleNewlineCheck_->setToolTip(tr(
      "Render a single line break as a new line, like Typora and Obsidian. "
      "Turn off for strict CommonMark, which joins soft-wrapped lines into one paragraph."));
  headingStyleLabel_->setText(tr("Heading Style"));
  rebuildCombo(headingStyleCombo_, {QStringLiteral("atx (#)"), QStringLiteral("setext (===)")});
  unorderedListLabel_->setText(tr("Unordered List"));
  rebuildCombo(unorderedListCombo_, {QStringLiteral("-"), QStringLiteral("*"), QStringLiteral("+")});
  orderedListLabel_->setText(tr("Ordered List"));
  rebuildCombo(orderedListCombo_, {tr("1. ... 2. ... 3. ..."), tr("1. ... 1. ... 1. ...")});

  // Card 2: Extended Syntax
  extSyntaxLabel_->setText(tr("Markdown Extended Syntax"));
  autoLinkCheck_->setText(tr("Auto Recognize Links"));
  inlineMathCheck_->setText(tr("Inline Formula") + QStringLiteral(" ($\\LaTeX$)"));
  subscriptCheck_->setText(tr("Subscript") + QStringLiteral(" (H~2~O)"));
  superscriptCheck_->setText(tr("Superscript") + QStringLiteral(" (X^2^)"));
  highlightCheck_->setText(tr("Highlight") + QStringLiteral(" (==key==)"));
  alertBoxCheck_->setText(tr("Github Style Alert Box"));
  diagramsCheck_->setText(tr("Diagrams (Sequence, Flowchart, Mermaid)"));
  diagramOptionsButton_->setText(tr("Diagram Options"));

  // Card 3: Smart Punctuation
  smartPunctLabel_->setText(tr("Smart Punctuation"));
  convertOnInputLabel_->setText(tr("Convert on Input"));
  rebuildCombo(convertOnInputCombo_, {tr("No conversion"), tr("When typing"), tr("Always")});
  smartQuotesCheck_->setText(tr("Smart Quotes"));
  rebuildCombo(singleQuoteCombo_, {QString::fromUtf8("\xe2\x80\x98" "abc" "\xe2\x80\x99"), QStringLiteral("'abc'")});
  rebuildCombo(doubleQuoteCombo_, {QString::fromUtf8("\xe2\x80\x9c" "abc" "\xe2\x80\x9d"), QStringLiteral("\"abc\"")});
  smartDashesCheck_->setText(tr("Smart Dashes"));
  convertOnRenderingCheck_->setText(tr("Convert on Rendering"));
  remapUnicodeCheck_->setText(tr("Remap Unicode Punctuation on Parse"));

  // Card 4: Code Blocks
  codeBlockLabel_->setText(tr("Code Blocks"));
  showLineNumbersCheck_->setText(tr("Show Line Numbers"));
  codeBlockWrapCheck_->setText(tr("Code Blocks Auto Wrap"));
  codeIndentLabel_->setText(tr("Code Indent"));
  rebuildCombo(codeIndentCombo_, {QStringLiteral("2"), QStringLiteral("4"), QStringLiteral("8")});
  defaultLangLabel_->setText(tr("Default Code Block Language"));
  rebuildCombo(defaultLangCombo_, {tr("(empty)"), QStringLiteral("cpp"), QStringLiteral("python"), QStringLiteral("javascript"), QStringLiteral("java")});
  autoLangLabel_->setText(tr("Automatically add code block language"));
  rebuildCombo(autoLangCombo_, {tr("When inserting code blocks via Markdown code"), tr("Always"), tr("Never")});
}

void muffin::PrefsMarkdownPage::loadSettings() {
  loadCheck(strictModeCheck_, QStringLiteral("markdown/strictMode"), false);
  loadCheck(breakOnSingleNewlineCheck_, QStringLiteral("markdown/breakOnSingleNewline"), true);
  loadComboIndex(headingStyleCombo_, QStringLiteral("markdown/headingStyle"), 0);
  loadComboIndex(unorderedListCombo_, QStringLiteral("markdown/unorderedList"), 0);
  loadComboIndex(orderedListCombo_, QStringLiteral("markdown/orderedList"), 0);
  loadCheck(autoLinkCheck_, QStringLiteral("markdown/autoLink"), true);
  loadCheck(inlineMathCheck_, QStringLiteral("markdown/inlineMath"), true);
  loadCheck(subscriptCheck_, QStringLiteral("markdown/subscript"), false);
  loadCheck(superscriptCheck_, QStringLiteral("markdown/superscript"), false);
  loadCheck(highlightCheck_, QStringLiteral("markdown/highlight"), false);
  loadCheck(alertBoxCheck_, QStringLiteral("markdown/alertBox"), true);
  loadCheck(diagramsCheck_, QStringLiteral("markdown/diagrams"), true);
  loadComboIndex(convertOnInputCombo_, QStringLiteral("markdown/convertOnInput"), 0);
  loadCheck(smartQuotesCheck_, QStringLiteral("markdown/smartQuotes"), false);
  loadComboIndex(singleQuoteCombo_, QStringLiteral("markdown/singleQuoteStyle"), 0);
  loadComboIndex(doubleQuoteCombo_, QStringLiteral("markdown/doubleQuoteStyle"), 0);
  loadCheck(smartDashesCheck_, QStringLiteral("markdown/smartDashes"), false);
  loadCheck(convertOnRenderingCheck_, QStringLiteral("markdown/convertOnRendering"), false);
  convertOnInputCombo_->setEnabled(!convertOnRenderingCheck_->isChecked());
  loadCheck(remapUnicodeCheck_, QStringLiteral("markdown/remapUnicode"), false);
  loadCheck(showLineNumbersCheck_, QStringLiteral("markdown/showLineNumbers"), false);
  loadCheck(codeBlockWrapCheck_, QStringLiteral("markdown/codeBlockWrap"), true);
  loadComboIndex(codeIndentCombo_, QStringLiteral("markdown/codeIndent"), 1);
  loadComboIndex(defaultLangCombo_, QStringLiteral("markdown/defaultCodeLang"), 0);
  loadComboIndex(autoLangCombo_, QStringLiteral("markdown/autoCodeLang"), 0);
}
