#pragma once

#include "app/PreferencesPage.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace muffin {

class PrefsSearchPage final : public PreferencesPage {
  Q_OBJECT
public:
  explicit PrefsSearchPage(QWidget* parent = nullptr);
  void retranslateUi() override;

private:
  QLabel *scopeTitle_, *scopeHint_, *extensionsLabel_, *extensionsHint_, *ignoredLabel_, *ignoredHint_;
  QLabel *limitsTitle_, *sizeLabel_, *regexSizeLabel_, *limitsHint_, *rulesHint_;
  QLineEdit* extensions_;
  QPlainTextEdit* ignored_;
  QCheckBox *hidden_, *gitIgnore_, *ignore_;
  QSpinBox *size_, *regexSize_;
  QPushButton* reset_;
};

} // namespace muffin
