#include "app/PrefsExportPage.h"
#include "app/PreferencesSpinBox.h"
#include "export/ExportOptions.h"
#include "export/PandocRunner.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPageSize>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QUuid>
#include <algorithm>

namespace {
const QVector<muffin::ExportFormat> formats{
    muffin::ExportFormat::Pdf,       muffin::ExportFormat::Html, muffin::ExportFormat::HtmlPlain,
    muffin::ExportFormat::Image,     muffin::ExportFormat::Docx, muffin::ExportFormat::Odt,
    muffin::ExportFormat::Rtf,       muffin::ExportFormat::Epub, muffin::ExportFormat::Latex,
    muffin::ExportFormat::MediaWiki, muffin::ExportFormat::Rst,  muffin::ExportFormat::Textile,
    muffin::ExportFormat::Opml};
}

muffin::PrefsExportPage::PrefsExportPage(QWidget* parent) : PreferencesPage(parent) {
  presets_ = exportPresets();
  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  auto* left = new QWidget(this);
  left->setObjectName("exportPanel");
  left->setFixedWidth(172);
  auto* navigation = new QVBoxLayout(left);
  navigation->setContentsMargins(12, 18, 12, 12);
  sectionLabel_ = new QLabel(left);
  sectionLabel_->setObjectName("exportPanelHeader");
  navigation->addWidget(sectionLabel_);
  formatList_ = new QListWidget(left);
  formatList_->setObjectName("ExportFormats");
  formatList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  navigation->addWidget(formatList_, 1);
  auto* buttons = new QGridLayout;
  const auto button = [&](int row, int column, const char* name, auto callback) {
    auto* item = new QPushButton(left);
    item->setObjectName(name);
    buttons->addWidget(item, row, column);
    connect(item, &QPushButton::clicked, this, callback);
    return item;
  };
  add_ = button(0, 0, "ExportPresetAdd", [this] { addPreset(false); });
  duplicate_ = button(0, 1, "ExportPresetDuplicate", [this] { addPreset(true); });
  rename_ = button(1, 0, "ExportPresetRename", [this] { renamePreset(); });
  remove_ = button(1, 1, "ExportPresetRemove", [this] { removePreset(); });
  up_ = button(2, 0, "ExportPresetUp", [this] { movePreset(-1); });
  down_ = button(2, 1, "ExportPresetDown", [this] { movePreset(1); });
  navigation->addLayout(buttons);
  root->addWidget(left);
  content_ = new QScrollArea(this);
  content_->setObjectName("ExportOptions");
  content_->setFrameShape(QFrame::NoFrame);
  content_->setWidgetResizable(true);
  content_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  root->addWidget(content_, 1);
  retranslateUi();
  connect(formatList_, &QListWidget::currentRowChanged, this, [this](int row) {
    QSettings().setValue("export/lastPage", row);
    rebuildPage();
    updatePresetButtons();
  });
}

void muffin::PrefsExportPage::retranslateUi() {
  const QSignalBlocker blocked(formatList_);
  const int row =
      formatList_->currentRow() < 0 ? QSettings().value("export/lastPage", 0).toInt() : formatList_->currentRow();
  sectionLabel_->setText(tr("Formats"));
  add_->setText(tr("New..."));
  duplicate_->setText(tr("Duplicate"));
  rename_->setText(tr("Rename..."));
  remove_->setText(tr("Remove"));
  up_->setText(tr("Up"));
  down_->setText(tr("Down"));
  int buttonWidth = 0;
  for (auto* button : {add_, duplicate_, rename_, remove_, up_, down_}) {
    button->ensurePolished();
    buttonWidth = qMax(buttonWidth, button->sizeHint().width());
  }
  formatList_->parentWidget()->setFixedWidth(qMax(172, 2 * buttonWidth + 32));
  formatList_->setAccessibleName(tr("Formats"));
  formatList_->clear();
  formatList_->addItems({tr("General"), "PDF", "HTML", tr("HTML (without Styles)"), tr("Image"), "Word (.docx)",
                         "OpenOffice", "RTF", "Epub", "LaTeX", "Media Wiki", "reStructuredText", "Textile", "OPML"});
  for (const auto& preset : presets_)
    formatList_->addItem(preset.name);
  for (int i = 0; i < formatList_->count(); ++i)
    formatList_->item(i)->setToolTip(formatList_->item(i)->text());
  formatList_->setCurrentRow(qBound(0, row, formatList_->count() - 1));
  rebuildPage();
  updatePresetButtons();
}

void muffin::PrefsExportPage::rebuildPage() {
  pandocPathEdit_ = nullptr;
  pandocStatus_ = nullptr;
  delete content_->takeWidget();
  auto* page = new QWidget(content_);
  auto* outer = new QVBoxLayout(page);
  outer->setContentsMargins(22, 26, 22, 26);
  auto* title = makeSectionLabel(page);
  title->setText(formatList_->currentItem()->text());
  outer->addWidget(title);
  auto* card = new QWidget(page);
  card->setObjectName("settingsGroup");
  card->setMaximumWidth(kContentWidth);
  auto* form = new QFormLayout(card);
  form->setContentsMargins(16, 16, 16, 16);
  form->setVerticalSpacing(12);
  form->setHorizontalSpacing(16);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  outer->addWidget(card);
  outer->addStretch();
  content_->setWidget(page);
  const auto note = [&](const QString& text) {
    auto* label = makeMutedLabel(card);
    label->setText(text);
    label->setWordWrap(true);
    form->addRow(label);
  };
  const auto check = [&](const QString& label, const QString& key, bool fallback = false, const QString& name = {}) {
    auto* box = new QCheckBox(label, card);
    box->setObjectName(name);
    box->setChecked(setting(key, fallback).toBool());
    connect(box, &QCheckBox::toggled, this, [this, key](bool value) { saveSetting(key, value); });
    form->addRow(box);
    return box;
  };
  const auto edit = [&](const QString& label, const QString& key, const QString& name = {}) {
    auto* field = new QLineEdit(card);
    field->setObjectName(name);
    field->setText(setting(key).toString());
    connect(field, &QLineEdit::textChanged, this, [this, key](const QString& value) { saveSetting(key, value); });
    form->addRow(label, field);
    return field;
  };
  const auto file = [&](const QString& label, const QString& key, const QString& filter, bool directory,
                        const QString& name) {
    auto* row = new QWidget(card);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    // Propagate the styled controls' minimum size to the form's field widget.
    // Otherwise a newly rebuilt row can keep its pre-polish height and clip
    // the taller line edit and browse button at the bottom.
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    auto* field = new QLineEdit(row);
    field->setObjectName(name);
    field->setText(setting(key).toString());
    connect(field, &QLineEdit::textChanged, this, [this, key](const QString& value) { saveSetting(key, value); });
    layout->addWidget(field, 1);
    auto* browse = new QPushButton(tr("Browse..."), row);
    layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, field, filter, directory, label] {
      const auto path = directory ? QFileDialog::getExistingDirectory(this, label, field->text())
                                  : QFileDialog::getOpenFileName(this, label, field->text(), filter);
      if (!path.isEmpty())
        field->setText(QDir::toNativeSeparators(path));
    });
    form->addRow(label, row);
    return row;
  };
  const auto combo = [&](const QString& label, const QString& key, const QStringList& labels, const QStringList& values,
                         const QString& fallback, const QString& name = {}) {
    auto* field = new QComboBox(card);
    field->setObjectName(name);
    for (int i = 0; i < labels.size(); ++i)
      field->addItem(labels[i], values[i]);
    const int index = field->findData(setting(key, fallback).toString());
    field->setCurrentIndex(qMax(0, index));
    polishComboBox(field);
    connect(field, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, field, key] { saveSetting(key, field->currentData()); });
    form->addRow(label, field);
    return field;
  };
  const auto number = [&](const QString& label, const QString& key, int fallback, int minimum, int maximum,
                          const QString& name = {}) {
    auto* field = new PreferencesSpinBox<QSpinBox>(card);
    field->setObjectName(name);
    field->setRange(minimum, maximum);
    field->setValue(setting(key, fallback).toInt());
    connect(field, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, key](int value) { saveSetting(key, value); });
    form->addRow(label, field);
    return field;
  };
  const auto decimal = [&](const QString& label, const QString& key, double fallback, double minimum, double maximum,
                           const QString& name) {
    auto* field = new PreferencesSpinBox<QDoubleSpinBox>(card);
    field->setObjectName(name);
    field->setDecimals(1);
    field->setRange(minimum, maximum);
    field->setValue(setting(key, fallback).toDouble());
    field->setSuffix(" mm");
    connect(field, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, key](double value) { saveSetting(key, value); });
    form->addRow(label, field);
    return field;
  };
  const auto multiline = [&](const QString& label, const QString& key, const QString& name) {
    auto* field = new QPlainTextEdit(card);
    field->setObjectName(name);
    field->setMinimumHeight(70);
    field->setMaximumHeight(100);
    field->setPlainText(setting(key).toString());
    connect(field, &QPlainTextEdit::textChanged, this, [this, field, key] { saveSetting(key, field->toPlainText()); });
    form->addRow(label, field);
  };
  const int selected = formatList_->currentRow();
  if (selected == 0) {
    note(tr("Shared defaults for all export formats. Select a format to configure its own options."));
    auto* folder = new QComboBox(card);
    folder->setObjectName("ExportDefaultFolder");
    folder->addItem(tr("Same folder as current file"), 0);
    folder->addItem(tr("Custom..."), 2);
    folder->setCurrentIndex(setting("export/defaultFolder", 0).toInt() == 2 ? 1 : 0);
    polishComboBox(folder);
    form->addRow(tr("Default Export Folder"), folder);
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto custom = setting("export/customFolder", setting("export/lastCustomFolder", documents)).toString();
    auto* customRow = file(tr("Custom Folder"), "export/customFolder", {}, true, "ExportCustomFolder");
    customRow->findChild<QLineEdit*>()->setText(custom);
    customRow->setEnabled(folder->currentData().toInt() == 2);
    connect(folder, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, folder, customRow] {
      saveSetting("export/defaultFolder", folder->currentData());
      customRow->setEnabled(folder->currentData().toInt() == 2);
    });
    note(tr("Untitled documents use the Documents folder."));
    auto* pandocRow = file(tr("Pandoc Path"), "export/pandocPath", tr("All Files (*)"), false, "ExportPandocPath");
    pandocPathEdit_ = pandocRow->findChild<QLineEdit*>();
    pandocPathEdit_->setPlaceholderText(tr("(Auto-detect)"));
    auto* detect = new QPushButton(tr("Auto-detect"), card);
    form->addRow(QString(), detect);
    connect(detect, &QPushButton::clicked, this, [this] {
      pandocPathEdit_->clear();
      refreshPandocStatus();
    });
    pandocStatus_ = makeMutedLabel(card);
    pandocStatus_->setWordWrap(true);
    form->addRow(pandocStatus_);
    note(tr("PDF, HTML and images use Muffin's renderer. Other formats require Pandoc."));
    connect(pandocPathEdit_, &QLineEdit::editingFinished, this, &PrefsExportPage::refreshPandocStatus);
    refreshPandocStatus();
    return;
  }
  const bool preset = selected > formats.size();
  const auto format = preset ? presets_[selected - formats.size() - 1].format : formats[selected - 1];
  const auto p = preset ? QString() : exportSettingsPrefix(format);
  note(tr("Used when exporting as %1. Changes are saved immediately.").arg(formatList_->currentItem()->text()));
  const auto theme = [&] {
    combo(tr("Theme"), p + "theme", {tr("Current theme"), tr("Default light theme")}, {"current", "github"}, "current",
          "ExportTheme");
  };
  if (format == ExportFormat::Command) {
    file(tr("Executable"), "program", tr("All Files (*)"), false, "ExportCommandProgram");
    edit(tr("Arguments"), "arguments", "ExportCommandArguments");
    edit(tr("Output Extension"), "extension", "ExportCommandExtension");
    note(tr("Arguments must contain ${outputPath}. ${inputPath} is a temporary Markdown copy including unsaved edits. "
            "Also available: ${currentPath}, ${currentFolder}, ${fileName}."));
    note(tr(
        "Quote arguments containing spaces. Variables are expanded after splitting arguments; no command shell is used."));
    note(tr(
        "The command must create one file at ${outputPath}. Failed or canceled exports keep the existing destination."));
  } else if (format == ExportFormat::Pdf) {
    auto* paper = combo(tr("Page Size"), p + "paper", {"A4", "A5", "Letter", "Legal", tr("Custom...")},
                        {"A4", "A5", "Letter", "Legal", "Custom"}, "A4", "ExportPdfPaper");
    auto* width = decimal(tr("Width"), p + "width", 210, 50, 1000, "ExportPdfWidth");
    auto* height = decimal(tr("Height"), p + "height", 297, 50, 1000, "ExportPdfHeight");
    const auto custom = [this, paper, width, height, p] {
      const auto id = paper->currentData().toString();
      const bool editable = id == "Custom";
      width->setEnabled(editable);
      height->setEnabled(editable);
      const QSignalBlocker widthBlock(width), heightBlock(height);
      if (editable) {
        width->setValue(setting(p + "width", 210).toDouble());
        height->setValue(setting(p + "height", 297).toDouble());
      } else {
        const auto size = QPageSize(id == "A5"       ? QPageSize::A5
                                    : id == "Letter" ? QPageSize::Letter
                                    : id == "Legal"  ? QPageSize::Legal
                                                     : QPageSize::A4)
                              .size(QPageSize::Millimeter);
        width->setValue(size.width());
        height->setValue(size.height());
      }
    };
    connect(paper, QOverload<int>::of(&QComboBox::currentIndexChanged), this, custom);
    custom();
    check(tr("Landscape"), p + "landscape", false, "ExportPdfLandscape");
    decimal(tr("Page Margins"), p + "margin", 15, 0, 20, "ExportPdfMargin");
    theme();
    number(tr("Font Size (px)"), p + "fontSize", 16, 10, 40, "ExportPdfFontSize");
    check(tr("Start each level-one heading on a new page"), p + "breakOnHeading", false, "ExportPdfHeadingBreak");
    edit(tr("Header"), p + "header", "ExportPdfHeader");
    edit(tr("Footer"), p + "footer", "ExportPdfFooter");
    edit(tr("Author"), p + "author", "ExportPdfAuthor");
    check(tr("Show page numbers"), p + "pageNumbers", false, "ExportPdfPageNumbers");
    note(tr("Header and footer variables: ${title}, ${author}, ${fileName}, ${date}, ${pageNo}, ${pageCount}. "
            "Space is reserved automatically so they do not overlap the document."));
  } else if (format == ExportFormat::Html || format == ExportFormat::HtmlPlain) {
    check(tr("Include a table of contents"), p + "outline", false, "ExportHtmlOutline");
    if (format == ExportFormat::Html)
      theme();
    multiline(tr("Add to <head>"), p + "head", "ExportHtmlHead");
    multiline(tr("Add before document content"), p + "body", "ExportHtmlBody");
    if (format == ExportFormat::HtmlPlain)
      note(tr("Muffin adds no theme stylesheet to this format."));
  } else if (format == ExportFormat::Image) {
    number(tr("Width (px)"), p + "width", 960, 320, 2400, "ExportImageWidth");
    combo(tr("Output Scale"), p + "scale", {"1×", "2×", "3×"}, {"1", "2", "3"}, "1", "ExportImageScale");
    number(tr("Font Size (px)"), p + "fontSize", 16, 10, 40, "ExportImageFontSize");
    theme();
    note(tr("Exports one PNG. Maximum: 64 million pixels and 32,767 pixels in height. Use PDF for longer documents."));
  } else {
    note(tr("Requires Pandoc. Configure its path under General."));
    if (format == ExportFormat::Docx || format == ExportFormat::Odt)
      file(tr("Reference Document"), p + "referenceDocument",
           format == ExportFormat::Docx ? "Word (*.docx)" : "OpenDocument (*.odt)", false, "ExportReferenceDocument");
    if (format == ExportFormat::Epub)
      file(tr("Custom CSS"), p + "css", "CSS (*.css)", false, "ExportEpubCss");
    if (format == ExportFormat::Docx || format == ExportFormat::Odt || format == ExportFormat::Epub ||
        format == ExportFormat::Latex || format == ExportFormat::Rtf || format == ExportFormat::Opml) {
      edit(tr("Title"), p + "title", "ExportTitle");
      if (format != ExportFormat::Opml)
        edit(tr("Author"), p + "author", "ExportAuthor");
    }
    if (format == ExportFormat::Docx || format == ExportFormat::Odt || format == ExportFormat::Epub ||
        format == ExportFormat::Latex || format == ExportFormat::Rtf) {
      auto* toc = check(tr("Include a table of contents"), p + "toc", false, "ExportPandocToc");
      auto* depth = number(tr("TOC Depth"), p + "tocDepth", 3, 1, 6, "ExportTocDepth");
      depth->setEnabled(toc->isChecked());
      connect(toc, &QCheckBox::toggled, depth, &QWidget::setEnabled);
    }
    if (format == ExportFormat::Docx || format == ExportFormat::Odt || format == ExportFormat::Latex)
      check(tr("Number sections"), p + "numberSections", false, "ExportNumberSections");
    if (format == ExportFormat::Latex || format == ExportFormat::MediaWiki || format == ExportFormat::Rst ||
        format == ExportFormat::Textile || format == ExportFormat::Opml)
      combo(tr("Line Wrapping"), p + "wrap", {tr("Do not wrap"), tr("Automatic"), tr("Preserve source wrapping")},
            {"none", "auto", "preserve"}, "none", "ExportWrap");
    edit(tr("Additional Pandoc Arguments"), p + "arguments", "ExportPandocArguments");
    note(tr("Quote paths containing spaces. Muffin manages the input format, output format and output path."));
  }
  auto* after = makeSectionLabel(card);
  after->setText(tr("After Export"));
  form->addRow(after);
  check(tr("Open the exported file directory"), p + "openDirectory", setting("export/openAfterExport", false).toBool(),
        "ExportOpenDirectory");
  check(tr("Open the exported file"), p + "openFile", false, "ExportOpenFile");
}

QVariant muffin::PrefsExportPage::setting(const QString& key, const QVariant& fallback) const {
  const int index = formatList_->currentRow() - formats.size() - 1;
  return index >= 0 && index < presets_.size() ? presets_[index].options.value(key, fallback)
                                               : QSettings().value(key, fallback);
}

void muffin::PrefsExportPage::saveSetting(const QString& key, const QVariant& value) {
  const int index = formatList_->currentRow() - formats.size() - 1;
  if (index >= 0 && index < presets_.size()) {
    presets_[index].options.insert(key, value);
    saveExportPresets(presets_);
  } else
    QSettings().setValue(key, value);
}

void muffin::PrefsExportPage::updatePresetButtons() {
  const int index = formatList_->currentRow() - formats.size() - 1;
  add_->setEnabled(presets_.size() < 64);
  duplicate_->setEnabled(formatList_->currentRow() > 0 && presets_.size() < 64);
  rename_->setEnabled(index >= 0);
  remove_->setEnabled(index >= 0);
  up_->setEnabled(index > 0);
  down_->setEnabled(index >= 0 && index + 1 < presets_.size());
}

void muffin::PrefsExportPage::addPreset(bool duplicate) {
  if (presets_.size() >= 64)
    return;
  const int selected = formatList_->currentRow();
  ExportPreset preset = selected > formats.size() ? presets_[selected - formats.size() - 1]
                        : selected > 0            ? builtInExportPreset(formats[selected - 1])
                                                  : builtInExportPreset(ExportFormat::Pdf);
  QDialog dialog(this);
  dialog.setObjectName("ExportPresetDialog");
  dialog.setWindowTitle(tr("New Export Preset"));
  auto* layout = new QFormLayout(&dialog);
  auto* name = new QLineEdit(&dialog);
  name->setObjectName("ExportPresetName");
  if (duplicate)
    name->setText(tr("%1 (copy)").arg(formatList_->currentItem()->text()));
  layout->addRow(tr("Name"), name);
  auto* format = new QComboBox(&dialog);
  format->setObjectName("ExportPresetFormat");
  for (int i = 0; i < formats.size(); ++i)
    format->addItem(formatList_->item(i + 1)->text(), int(formats[i]));
  format->addItem(tr("Custom Command"), int(ExportFormat::Command));
  format->setCurrentIndex(format->findData(int(preset.format)));
  polishComboBox(format);
  layout->addRow(tr("Format"), format);
  auto* message = makeMutedLabel(&dialog);
  message->setWordWrap(true);
  layout->addRow(message);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    const auto text = name->text().trimmed();
    if (text.isEmpty() || std::any_of(presets_.cbegin(), presets_.cend(),
                                      [&](const auto& p) { return p.name.compare(text, Qt::CaseInsensitive) == 0; })) {
      message->setText(tr("Enter a non-empty, unique preset name."));
      return;
    }
    preset.name = text;
    const auto targetFormat = ExportFormat(format->currentData().toInt());
    if (targetFormat != preset.format)
      preset = builtInExportPreset(targetFormat);
    preset.name = text;
    preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    dialog.accept();
  });
  if (dialog.exec() != QDialog::Accepted)
    return;
  presets_.append(std::move(preset));
  saveExportPresets(presets_);
  retranslateUi();
  formatList_->setCurrentRow(formats.size() + presets_.size());
}

void muffin::PrefsExportPage::renamePreset() {
  const int index = formatList_->currentRow() - formats.size() - 1;
  if (index < 0 || index >= presets_.size())
    return;
  bool accepted = false;
  const auto name = QInputDialog::getText(this, tr("Rename Export Preset"), tr("Name"), QLineEdit::Normal,
                                          presets_[index].name, &accepted)
                        .trimmed();
  if (!accepted || name == presets_[index].name)
    return;
  if (name.isEmpty() || std::any_of(presets_.cbegin(), presets_.cend(), [&](const auto& p) {
        return p.id != presets_[index].id && p.name.compare(name, Qt::CaseInsensitive) == 0;
      })) {
    QMessageBox::warning(this, tr("Rename Export Preset"), tr("Enter a non-empty, unique preset name."));
    return;
  }
  presets_[index].name = name;
  saveExportPresets(presets_);
  retranslateUi();
}

void muffin::PrefsExportPage::removePreset() {
  const int index = formatList_->currentRow() - formats.size() - 1;
  if (index < 0 || index >= presets_.size())
    return;
  presets_.removeAt(index);
  saveExportPresets(presets_);
  retranslateUi();
}

void muffin::PrefsExportPage::movePreset(int direction) {
  const int index = formatList_->currentRow() - formats.size() - 1;
  const int target = index + direction;
  if (index < 0 || index >= presets_.size() || target < 0 || target >= presets_.size())
    return;
  presets_.swapItemsAt(index, target);
  saveExportPresets(presets_);
  retranslateUi();
  formatList_->setCurrentRow(formats.size() + target + 1);
}

void muffin::PrefsExportPage::refreshPandocStatus() {
  if (!pandocPathEdit_ || !pandocStatus_)
    return;
  const auto configured = pandocPathEdit_->text().trimmed();
  if (!configured.isEmpty()) {
    pandocStatus_->setText(QFileInfo(configured).isFile() && QFileInfo(configured).isExecutable()
                               ? tr("Using: %1").arg(QDir::toNativeSeparators(configured))
                               : tr("Not a valid executable; will auto-detect."));
    return;
  }
  QString resolved = PandocRunner::resolvedExecutable();
  if (resolved == "pandoc")
    resolved = PandocRunner::resolveOnPath();
  pandocStatus_->setText(resolved.isEmpty() ? tr("Pandoc was not found. Install it, or click Browse to locate.")
                                            : tr("Detected: %1").arg(QDir::toNativeSeparators(resolved)));
}
