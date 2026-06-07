#pragma once

#include "app/PreferencesPage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace muffin {

class PrefsMarkdownPage final : public PreferencesPage {
  Q_OBJECT

public:
  explicit PrefsMarkdownPage(QWidget* parent = nullptr);

  void retranslateUi() override;

private:
  void loadSettings();

  // Card 1: Syntax Preferences
  QLabel* syntaxLabel_ = nullptr;
  QCheckBox* strictModeCheck_ = nullptr;
  QLabel* headingStyleLabel_ = nullptr;
  QComboBox* headingStyleCombo_ = nullptr;
  QLabel* unorderedListLabel_ = nullptr;
  QComboBox* unorderedListCombo_ = nullptr;
  QLabel* orderedListLabel_ = nullptr;
  QComboBox* orderedListCombo_ = nullptr;

  // Card 2: Extended Syntax
  QLabel* extSyntaxLabel_ = nullptr;
  QCheckBox* autoLinkCheck_ = nullptr;
  QCheckBox* inlineMathCheck_ = nullptr;
  QCheckBox* subscriptCheck_ = nullptr;
  QCheckBox* superscriptCheck_ = nullptr;
  QCheckBox* highlightCheck_ = nullptr;
  QCheckBox* alertBoxCheck_ = nullptr;
  QCheckBox* diagramsCheck_ = nullptr;
  QPushButton* diagramOptionsButton_ = nullptr;

  // Card 3: Smart Punctuation
  QLabel* smartPunctLabel_ = nullptr;
  QLabel* convertOnInputLabel_ = nullptr;
  QComboBox* convertOnInputCombo_ = nullptr;
  QCheckBox* smartQuotesCheck_ = nullptr;
  QComboBox* singleQuoteCombo_ = nullptr;
  QComboBox* doubleQuoteCombo_ = nullptr;
  QCheckBox* smartDashesCheck_ = nullptr;
  QCheckBox* unicodePunctCheck_ = nullptr;

  // Card 4: Code Blocks
  QLabel* codeBlockLabel_ = nullptr;
  QCheckBox* showLineNumbersCheck_ = nullptr;
  QCheckBox* codeBlockWrapCheck_ = nullptr;
  QCheckBox* shiftTabIndentCheck_ = nullptr;
  QLabel* codeIndentLabel_ = nullptr;
  QComboBox* codeIndentCombo_ = nullptr;
  QLabel* defaultLangLabel_ = nullptr;
  QComboBox* defaultLangCombo_ = nullptr;
  QLabel* autoLangLabel_ = nullptr;
  QComboBox* autoLangCombo_ = nullptr;
};

}  // namespace muffin
