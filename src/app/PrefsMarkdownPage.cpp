#include "app/PrefsMarkdownPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

muffin::PrefsMarkdownPage::PrefsMarkdownPage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(38, 34, 46, 34);
  layout->setSpacing(22);

  // Restart note
  auto* noteLabel = makeMutedLabel(this);
  noteLabel->setWordWrap(true);
  layout->addWidget(noteLabel);

  auto* cardContainer = new QWidget(this);
  cardContainer->setMaximumWidth(640);
  auto* cardColumn = new QVBoxLayout(cardContainer);
  cardColumn->setContentsMargins(0, 0, 0, 0);
  cardColumn->setSpacing(14);
  layout->addWidget(cardContainer);

  // --- Card 1: Markdown Syntax Preferences ---
  auto* syntaxCard = new QWidget(this);
  syntaxCard->setObjectName(QStringLiteral("settingsCard"));
  auto* syntaxLayout = new QVBoxLayout(syntaxCard);
  syntaxLayout->setContentsMargins(18, 16, 18, 16);
  syntaxLayout->setSpacing(12);

  auto* syntaxHeaderRow = new QHBoxLayout();
  syntaxLabel_ = makeSectionLabel(syntaxCard);
  syntaxHeaderRow->addWidget(syntaxLabel_);
  syntaxHeaderRow->addStretch(1);

  strictModeCheck_ = new QCheckBox(syntaxCard);
  strictModeCheck_->setChecked(true);

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
  syntaxLayout->addSpacing(4);
  syntaxLayout->addLayout(headingRow);
  syntaxLayout->addLayout(ulRow);
  syntaxLayout->addLayout(olRow);
  cardColumn->addWidget(syntaxCard);

  // --- Card 2: Extended Syntax ---
  auto* extCard = new QWidget(this);
  extCard->setObjectName(QStringLiteral("settingsCard"));
  auto* extLayout = new QVBoxLayout(extCard);
  extLayout->setContentsMargins(18, 16, 18, 16);
  extLayout->setSpacing(12);

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
  auto* punctCard = new QWidget(this);
  punctCard->setObjectName(QStringLiteral("settingsCard"));
  auto* punctLayout = new QVBoxLayout(punctCard);
  punctLayout->setContentsMargins(18, 16, 18, 16);
  punctLayout->setSpacing(12);

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
  unicodePunctCheck_ = new QCheckBox(punctCard);

  punctLayout->addLayout(punctHeaderRow);
  punctLayout->addLayout(convertRow);
  punctLayout->addWidget(smartQuotesCheck_);
  punctLayout->addLayout(quotesRow);
  punctLayout->addWidget(smartDashesCheck_);
  punctLayout->addWidget(unicodePunctCheck_);
  cardColumn->addWidget(punctCard);

  // --- Card 4: Code Blocks ---
  auto* codeCard = new QWidget(this);
  codeCard->setObjectName(QStringLiteral("settingsCard"));
  auto* codeLayout = new QVBoxLayout(codeCard);
  codeLayout->setContentsMargins(18, 16, 18, 16);
  codeLayout->setSpacing(12);

  codeBlockLabel_ = makeSectionLabel(codeCard);
  showLineNumbersCheck_ = new QCheckBox(codeCard);
  codeBlockWrapCheck_ = new QCheckBox(codeCard);
  codeBlockWrapCheck_->setChecked(true);
  shiftTabIndentCheck_ = new QCheckBox(codeCard);

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
  codeLayout->addWidget(shiftTabIndentCheck_);
  codeLayout->addLayout(indentRow);
  codeLayout->addLayout(langRow);
  codeLayout->addLayout(autoLangRow);
  cardColumn->addWidget(codeCard);

  layout->addStretch(1);

  retranslateUi();
  loadSettings();

  // Wire persistence
  connect(strictModeCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/strictMode"), checked); });
  connect(headingStyleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/headingStyle"), index); });
  connect(unorderedListCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/unorderedList"), index); });
  connect(orderedListCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/orderedList"), index); });
  connect(autoLinkCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/autoLink"), checked); });
  connect(inlineMathCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/inlineMath"), checked); });
  connect(subscriptCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/subscript"), checked); });
  connect(superscriptCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/superscript"), checked); });
  connect(highlightCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/highlight"), checked); });
  connect(alertBoxCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/alertBox"), checked); });
  connect(diagramsCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/diagrams"), checked); });
  connect(convertOnInputCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/convertOnInput"), index); });
  connect(smartQuotesCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/smartQuotes"), checked); });
  connect(singleQuoteCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/singleQuoteStyle"), index); });
  connect(doubleQuoteCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/doubleQuoteStyle"), index); });
  connect(smartDashesCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/smartDashes"), checked); });
  connect(unicodePunctCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/unicodePunctuation"), checked); });
  connect(showLineNumbersCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/showLineNumbers"), checked); });
  connect(codeBlockWrapCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/codeBlockWrap"), checked); });
  connect(shiftTabIndentCheck_, &QCheckBox::toggled, this,
          [](bool checked) { QSettings().setValue(QStringLiteral("markdown/shiftTabIndent"), checked); });
  connect(codeIndentCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/codeIndent"), index); });
  connect(defaultLangCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/defaultCodeLang"), index); });
  connect(autoLangCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [](int index) { QSettings().setValue(QStringLiteral("markdown/autoCodeLang"), index); });
}

void muffin::PrefsMarkdownPage::retranslateUi() {
  // Card 1: Syntax Preferences
  syntaxLabel_->setText(tr("Markdown Syntax Preferences"));
  strictModeCheck_->setText(tr("Strict Mode"));
  headingStyleLabel_->setText(tr("Heading Style"));
  {
    const int cur = headingStyleCombo_->currentIndex();
    headingStyleCombo_->blockSignals(true);
    headingStyleCombo_->clear();
    headingStyleCombo_->addItem(QStringLiteral("atx (#)"));
    headingStyleCombo_->addItem(QStringLiteral("setext (===)"));
    headingStyleCombo_->setCurrentIndex(qBound(0, cur, headingStyleCombo_->count() - 1));
    headingStyleCombo_->blockSignals(false);
  }
  unorderedListLabel_->setText(tr("Unordered List"));
  {
    const int cur = unorderedListCombo_->currentIndex();
    unorderedListCombo_->blockSignals(true);
    unorderedListCombo_->clear();
    unorderedListCombo_->addItem(QStringLiteral("-"));
    unorderedListCombo_->addItem(QStringLiteral("*"));
    unorderedListCombo_->addItem(QStringLiteral("+"));
    unorderedListCombo_->setCurrentIndex(qBound(0, cur, unorderedListCombo_->count() - 1));
    unorderedListCombo_->blockSignals(false);
  }
  orderedListLabel_->setText(tr("Ordered List"));
  {
    const int cur = orderedListCombo_->currentIndex();
    orderedListCombo_->blockSignals(true);
    orderedListCombo_->clear();
    orderedListCombo_->addItem(tr("1. ... 2. ... 3. ..."));
    orderedListCombo_->addItem(tr("1. ... 1. ... 1. ..."));
    orderedListCombo_->setCurrentIndex(qBound(0, cur, orderedListCombo_->count() - 1));
    orderedListCombo_->blockSignals(false);
  }

  // Card 2: Extended Syntax
  extSyntaxLabel_->setText(tr("Markdown Extended Syntax"));
  autoLinkCheck_->setText(tr("Auto Recognize Links") + QStringLiteral(" (https://typora.io)"));
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
  {
    const int cur = convertOnInputCombo_->currentIndex();
    convertOnInputCombo_->blockSignals(true);
    convertOnInputCombo_->clear();
    convertOnInputCombo_->addItem(tr("No conversion"));
    convertOnInputCombo_->addItem(tr("When typing"));
    convertOnInputCombo_->addItem(tr("Always"));
    convertOnInputCombo_->setCurrentIndex(qBound(0, cur, convertOnInputCombo_->count() - 1));
    convertOnInputCombo_->blockSignals(false);
  }
  smartQuotesCheck_->setText(tr("Smart Quotes"));
  {
    const int cur = singleQuoteCombo_->currentIndex();
    singleQuoteCombo_->blockSignals(true);
    singleQuoteCombo_->clear();
    singleQuoteCombo_->addItem(QStringLiteral("\xe2\x80\x98" "abc" "\xe2\x80\x99"));  // 'abc'
    singleQuoteCombo_->addItem(QStringLiteral("'abc'"));
    singleQuoteCombo_->setCurrentIndex(qBound(0, cur, singleQuoteCombo_->count() - 1));
    singleQuoteCombo_->blockSignals(false);
  }
  {
    const int cur = doubleQuoteCombo_->currentIndex();
    doubleQuoteCombo_->blockSignals(true);
    doubleQuoteCombo_->clear();
    doubleQuoteCombo_->addItem(QStringLiteral("\xe2\x80\x9c" "abc" "\xe2\x80\x9d"));  // "abc"
    doubleQuoteCombo_->addItem(QStringLiteral("\"abc\""));
    doubleQuoteCombo_->setCurrentIndex(qBound(0, cur, doubleQuoteCombo_->count() - 1));
    doubleQuoteCombo_->blockSignals(false);
  }
  smartDashesCheck_->setText(tr("Smart Dashes"));
  unicodePunctCheck_->setText(tr("Allow and convert Unicode punctuation when parsing Markdown"));

  // Card 4: Code Blocks
  codeBlockLabel_->setText(tr("Code Blocks"));
  showLineNumbersCheck_->setText(tr("Show Line Numbers"));
  codeBlockWrapCheck_->setText(tr("Code Blocks Auto Wrap"));
  shiftTabIndentCheck_->setText(tr("Use Shift+Tab to auto adjust indent of selected code"));
  codeIndentLabel_->setText(tr("Code Indent"));
  {
    const int cur = codeIndentCombo_->currentIndex();
    codeIndentCombo_->blockSignals(true);
    codeIndentCombo_->clear();
    codeIndentCombo_->addItem(QStringLiteral("2"));
    codeIndentCombo_->addItem(QStringLiteral("4"));
    codeIndentCombo_->addItem(QStringLiteral("8"));
    codeIndentCombo_->setCurrentIndex(qBound(0, cur, codeIndentCombo_->count() - 1));
    codeIndentCombo_->blockSignals(false);
  }
  defaultLangLabel_->setText(tr("Default Code Block Language"));
  {
    const int cur = defaultLangCombo_->currentIndex();
    defaultLangCombo_->blockSignals(true);
    defaultLangCombo_->clear();
    defaultLangCombo_->addItem(tr("(empty)"));
    defaultLangCombo_->addItem(QStringLiteral("cpp"));
    defaultLangCombo_->addItem(QStringLiteral("python"));
    defaultLangCombo_->addItem(QStringLiteral("javascript"));
    defaultLangCombo_->addItem(QStringLiteral("java"));
    defaultLangCombo_->setCurrentIndex(qBound(0, cur, defaultLangCombo_->count() - 1));
    defaultLangCombo_->blockSignals(false);
  }
  autoLangLabel_->setText(tr("Automatically add code block language"));
  {
    const int cur = autoLangCombo_->currentIndex();
    autoLangCombo_->blockSignals(true);
    autoLangCombo_->clear();
    autoLangCombo_->addItem(tr("When inserting code blocks via Markdown code"));
    autoLangCombo_->addItem(tr("Always"));
    autoLangCombo_->addItem(tr("Never"));
    autoLangCombo_->setCurrentIndex(qBound(0, cur, autoLangCombo_->count() - 1));
    autoLangCombo_->blockSignals(false);
  }
}

void muffin::PrefsMarkdownPage::loadSettings() {
  QSettings s;

  strictModeCheck_->blockSignals(true);
  strictModeCheck_->setChecked(s.value(QStringLiteral("markdown/strictMode"), true).toBool());
  strictModeCheck_->blockSignals(false);

  auto setCombo = [&](QComboBox* combo, const QString& key, int def) {
    if (combo->count() > 0) {
      const int v = s.value(key, def).toInt();
      combo->blockSignals(true);
      combo->setCurrentIndex(qBound(0, v, combo->count() - 1));
      combo->blockSignals(false);
    }
  };
  setCombo(headingStyleCombo_, QStringLiteral("markdown/headingStyle"), 0);
  setCombo(unorderedListCombo_, QStringLiteral("markdown/unorderedList"), 0);
  setCombo(orderedListCombo_, QStringLiteral("markdown/orderedList"), 0);

  auto setCheck = [&](QCheckBox* cb, const QString& key, bool def) {
    cb->blockSignals(true);
    cb->setChecked(s.value(key, def).toBool());
    cb->blockSignals(false);
  };
  setCheck(autoLinkCheck_, QStringLiteral("markdown/autoLink"), true);
  setCheck(inlineMathCheck_, QStringLiteral("markdown/inlineMath"), true);
  setCheck(subscriptCheck_, QStringLiteral("markdown/subscript"), false);
  setCheck(superscriptCheck_, QStringLiteral("markdown/superscript"), false);
  setCheck(highlightCheck_, QStringLiteral("markdown/highlight"), false);
  setCheck(alertBoxCheck_, QStringLiteral("markdown/alertBox"), true);
  setCheck(diagramsCheck_, QStringLiteral("markdown/diagrams"), true);

  setCombo(convertOnInputCombo_, QStringLiteral("markdown/convertOnInput"), 0);
  setCheck(smartQuotesCheck_, QStringLiteral("markdown/smartQuotes"), false);
  setCombo(singleQuoteCombo_, QStringLiteral("markdown/singleQuoteStyle"), 0);
  setCombo(doubleQuoteCombo_, QStringLiteral("markdown/doubleQuoteStyle"), 0);
  setCheck(smartDashesCheck_, QStringLiteral("markdown/smartDashes"), false);
  setCheck(unicodePunctCheck_, QStringLiteral("markdown/unicodePunctuation"), false);

  setCheck(showLineNumbersCheck_, QStringLiteral("markdown/showLineNumbers"), false);
  setCheck(codeBlockWrapCheck_, QStringLiteral("markdown/codeBlockWrap"), true);
  setCheck(shiftTabIndentCheck_, QStringLiteral("markdown/shiftTabIndent"), false);
  setCombo(codeIndentCombo_, QStringLiteral("markdown/codeIndent"), 1);  // default 4
  setCombo(defaultLangCombo_, QStringLiteral("markdown/defaultCodeLang"), 0);
  setCombo(autoLangCombo_, QStringLiteral("markdown/autoCodeLang"), 0);
}
