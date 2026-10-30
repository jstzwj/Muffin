#include "app/PrefsSearchPage.h"
#include "app/PreferencesSpinBox.h"
#include "app/SearchSettings.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

muffin::PrefsSearchPage::PrefsSearchPage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPageLeftMargin, kPageTopMargin, kPageRightMargin, kPageBottomMargin);
  layout->setSpacing(16);
  auto* card = makeCard(this);
  auto* column = makeCardLayout(card);
  scopeTitle_ = makeSectionLabel(card);
  scopeHint_ = makeMutedLabel(card);
  scopeHint_->setWordWrap(true);
  column->addWidget(scopeTitle_);
  column->addWidget(scopeHint_);
  extensionsLabel_ = new QLabel(card);
  extensions_ = new QLineEdit(card);
  extensions_->setObjectName("SearchExtensions");
  extensionsLabel_->setBuddy(extensions_);
  extensionsHint_ = makeMutedLabel(card);
  extensionsHint_->setWordWrap(true);
  column->addWidget(extensionsLabel_);
  column->addWidget(extensions_);
  column->addWidget(extensionsHint_);
  ignoredLabel_ = new QLabel(card);
  ignored_ = new QPlainTextEdit(card);
  ignored_->setObjectName("SearchIgnorePatterns");
  ignored_->setMinimumHeight(140);
  ignored_->setMaximumHeight(180);
  ignoredLabel_->setBuddy(ignored_);
  ignoredHint_ = makeMutedLabel(card);
  ignoredHint_->setWordWrap(true);
  column->addWidget(ignoredLabel_);
  column->addWidget(ignored_);
  column->addWidget(ignoredHint_);
  hidden_ = new QCheckBox(card);
  hidden_->setObjectName("SearchIncludeHidden");
  gitIgnore_ = new QCheckBox(card);
  gitIgnore_->setObjectName("SearchReadGitIgnore");
  ignore_ = new QCheckBox(card);
  ignore_->setObjectName("SearchReadIgnore");
  column->addWidget(hidden_);
  column->addWidget(gitIgnore_);
  column->addWidget(ignore_);
  rulesHint_ = makeMutedLabel(card);
  rulesHint_->setWordWrap(true);
  column->addWidget(rulesHint_);
  layout->addWidget(card);

  card = makeCard(this);
  column = makeCardLayout(card);
  limitsTitle_ = makeSectionLabel(card);
  column->addWidget(limitsTitle_);
  auto* form = new QFormLayout;
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  sizeLabel_ = new QLabel(card);
  regexSizeLabel_ = new QLabel(card);
  size_ = new PreferencesSpinBox<QSpinBox>(card);
  regexSize_ = new PreferencesSpinBox<QSpinBox>(card);
  size_->setObjectName("SearchMaxFileMiB");
  regexSize_->setObjectName("SearchMaxRegexMiB");
  for (auto* input : {size_, regexSize_}) {
    input->setRange(1, int(SearchSettings::maximumFileBytes / (1024 * 1024)));
    input->setSuffix(QStringLiteral(" MiB"));
  }
  sizeLabel_->setBuddy(size_);
  regexSizeLabel_->setBuddy(regexSize_);
  form->addRow(sizeLabel_, size_);
  form->addRow(regexSizeLabel_, regexSize_);
  column->addLayout(form);
  limitsHint_ = makeMutedLabel(card);
  limitsHint_->setWordWrap(true);
  column->addWidget(limitsHint_);
  layout->addWidget(card);
  reset_ = makeButton(this);
  reset_->setObjectName("SearchResetDefaults");
  layout->addWidget(reset_, 0, Qt::AlignLeft);
  layout->addStretch();

  const auto settings = SearchSettings::load();
  extensions_->setText(settings.extensions.join(QStringLiteral(", ")));
  ignored_->setPlainText(settings.scan.ignorePatterns);
  hidden_->setChecked(settings.scan.includeHidden);
  gitIgnore_->setChecked(settings.scan.readGitIgnore);
  ignore_->setChecked(settings.scan.readIgnore);
  size_->setValue(int(settings.maxFileBytes / (1024 * 1024)));
  regexSize_->setValue(int(settings.maxRegexBytes / (1024 * 1024)));
  wireLineEditSetting(extensions_, QStringLiteral("search/extensions"));
  connect(ignored_, &QPlainTextEdit::textChanged, this,
          [this] { QSettings().setValue("search/ignorePatterns", ignored_->toPlainText()); });
  wireBoolSetting(hidden_, QStringLiteral("search/includeHidden"));
  wireBoolSetting(gitIgnore_, QStringLiteral("search/readGitIgnore"));
  wireBoolSetting(ignore_, QStringLiteral("search/readIgnore"));
  connect(size_, &QSpinBox::valueChanged, this,
          [](int size) { QSettings().setValue("search/maxFileBytes", qint64(size) * 1024 * 1024); });
  connect(regexSize_, &QSpinBox::valueChanged, this,
          [](int size) { QSettings().setValue("search/maxRegexBytes", qint64(size) * 1024 * 1024); });
  connect(reset_, &QPushButton::clicked, this, [this] {
    extensions_->setText(SearchSettings::defaultExtensions());
    ignored_->setPlainText(WorkspaceScanOptions::defaultIgnorePatterns());
    hidden_->setChecked(false);
    gitIgnore_->setChecked(true);
    ignore_->setChecked(true);
    size_->setValue(100);
    regexSize_->setValue(32);
  });
  retranslateUi();
}

void muffin::PrefsSearchPage::retranslateUi() {
  scopeTitle_->setText(tr("Search scope"));
  scopeHint_->setText(tr("Applies to folder search and Quick Open. Changes take effect on the next search."));
  extensionsLabel_->setText(tr("File extensions to search"));
  extensionsHint_->setText(tr(
      "Separate extensions with commas, for example: md, txt. Use * for all file types; leave empty to search no files."));
  ignoredLabel_->setText(tr("Ignored files and folders"));
  ignoredHint_->setText(tr(
      "One rule per line: build/ ignores folders, *.bak ignores files, /drafts/ matches the search root, and ! includes a match again. Uses gitignore-style wildcards."));
  hidden_->setText(tr("Include hidden files and folders"));
  gitIgnore_->setText(tr("Read .gitignore files automatically"));
  ignore_->setText(tr("Read .ignore files automatically"));
  rulesHint_->setText(tr(
      "Reads rules in the search root and its subfolders. .ignore takes priority over .gitignore; the rules above take priority over both. Ignored folders are not traversed. Symbolic links are always skipped."));
  limitsTitle_->setText(tr("Content search limits"));
  sizeLabel_->setText(tr("Maximum file size"));
  regexSizeLabel_->setText(tr("Maximum file size for regex"));
  limitsHint_->setText(tr(
      "Regex searches obey both limits. File size does not restrict Quick Open. Folder replacement keeps its separate 8 MiB safety limit. File opening protection is configured on the Files page."));
  reset_->setText(tr("Restore search defaults"));
  extensions_->setAccessibleName(extensionsLabel_->text());
  ignored_->setAccessibleName(ignoredLabel_->text());
  size_->setAccessibleName(sizeLabel_->text());
  regexSize_->setAccessibleName(regexSizeLabel_->text());
}
