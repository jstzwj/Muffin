#pragma once

#include "app/PreferencesPage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;

namespace muffin {

class PrefsEditorPage final : public PreferencesPage {
  Q_OBJECT

public:
  explicit PrefsEditorPage(QWidget* parent = nullptr);

  void retranslateUi() override;

signals:
  void disableTypewriterFocusRequested();

private:
  void loadSettings();

  QLabel* indentLabel_ = nullptr;
  QLabel* indentDescLabel_ = nullptr;
  QComboBox* indentCombo_ = nullptr;
  QCheckBox* alignIndentCheck_ = nullptr;
  QLabel* pairsLabel_ = nullptr;
  QCheckBox* matchBracketsCheck_ = nullptr;
  QCheckBox* matchMarkdownCheck_ = nullptr;
  QLabel* autocompleteLabel_ = nullptr;
  QCheckBox* emojiAutocompleteCheck_ = nullptr;
  QLabel* liveRenderLabel_ = nullptr;
  QCheckBox* showBlockSourceCheck_ = nullptr;
  QLabel* copyLabel_ = nullptr;
  QCheckBox* copyAsMarkdownCheck_ = nullptr;
  QCheckBox* copyLineWhenNoSelectionCheck_ = nullptr;
  QLabel* lineBreakLabel_ = nullptr;
  QLabel* lineBreakDescLabel_ = nullptr;
  QRadioButton* lineBreakLfRadio_ = nullptr;
  QRadioButton* lineBreakCrlfRadio_ = nullptr;
  QLabel* spellCheckLabel_ = nullptr;
  QComboBox* spellCheckCombo_ = nullptr;
  QLabel* typewriterLabel_ = nullptr;
  QCheckBox* typewriterCursorMiddleCheck_ = nullptr;
  QPushButton* disableTypewriterFocusButton_ = nullptr;
};

}  // namespace muffin
