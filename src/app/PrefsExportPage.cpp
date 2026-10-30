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

namespace {
const QVector<muffin::ExportFormat> formats{
    muffin::ExportFormat::Pdf,       muffin::ExportFormat::Html, muffin::ExportFormat::HtmlPlain,
    muffin::ExportFormat::Image,     muffin::ExportFormat::Docx, muffin::ExportFormat::Odt,
    muffin::ExportFormat::Rtf,       muffin::ExportFormat::Epub, muffin::ExportFormat::Latex,
    muffin::ExportFormat::MediaWiki, muffin::ExportFormat::Rst,  muffin::ExportFormat::Textile,
    muffin::ExportFormat::Opml};
}

muffin::PrefsExportPage::PrefsExportPage(QWidget* parent) : PreferencesPage(parent) {
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
  });
}

void muffin::PrefsExportPage::retranslateUi() {
  const QSignalBlocker blocked(formatList_);
  const int row =
      formatList_->currentRow() < 0 ? QSettings().value("export/lastPage", 0).toInt() : formatList_->currentRow();
  sectionLabel_->setText(tr("Formats"));
  formatList_->setAccessibleName(tr("Formats"));
  formatList_->clear();
  formatList_->addItems({tr("General"), "PDF", "HTML", tr("HTML (without Styles)"), tr("Image"), "Word (.docx)",
                         "OpenOffice", "RTF", "Epub", "LaTeX", "Media Wiki", "reStructuredText", "Textile", "OPML"});
  for (int i = 0; i < formatList_->count(); ++i)
    formatList_->item(i)->setToolTip(formatList_->item(i)->text());
  formatList_->setCurrentRow(qBound(0, row, formatList_->count() - 1));
  rebuildPage();
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
    box->setChecked(QSettings().value(key, fallback).toBool());
    wireBoolSetting(box, key);
    form->addRow(box);
    return box;
  };
  const auto edit = [&](const QString& label, const QString& key, const QString& name = {}) {
    auto* field = new QLineEdit(card);
    field->setObjectName(name);
    field->setText(QSettings().value(key).toString());
    wireLineEditSetting(field, key);
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
    field->setText(QSettings().value(key).toString());
    wireLineEditSetting(field, key);
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
    const int index = field->findData(QSettings().value(key, fallback).toString());
    field->setCurrentIndex(qMax(0, index));
    polishComboBox(field);
    connect(field, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [field, key] { QSettings().setValue(key, field->currentData()); });
    form->addRow(label, field);
    return field;
  };
  const auto number = [&](const QString& label, const QString& key, int fallback, int minimum, int maximum,
                          const QString& name = {}) {
    auto* field = new PreferencesSpinBox<QSpinBox>(card);
    field->setObjectName(name);
    field->setRange(minimum, maximum);
    field->setValue(QSettings().value(key, fallback).toInt());
    connect(field, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [key](int value) { QSettings().setValue(key, value); });
    form->addRow(label, field);
    return field;
  };
  const auto decimal = [&](const QString& label, const QString& key, double fallback, double minimum, double maximum,
                           const QString& name) {
    auto* field = new PreferencesSpinBox<QDoubleSpinBox>(card);
    field->setObjectName(name);
    field->setDecimals(1);
    field->setRange(minimum, maximum);
    field->setValue(QSettings().value(key, fallback).toDouble());
    field->setSuffix(" mm");
    connect(field, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [key](double value) { QSettings().setValue(key, value); });
    form->addRow(label, field);
    return field;
  };
  const auto multiline = [&](const QString& label, const QString& key, const QString& name) {
    auto* field = new QPlainTextEdit(card);
    field->setObjectName(name);
    field->setMinimumHeight(70);
    field->setMaximumHeight(100);
    field->setPlainText(QSettings().value(key).toString());
    connect(field, &QPlainTextEdit::textChanged, this,
            [field, key] { QSettings().setValue(key, field->toPlainText()); });
    form->addRow(label, field);
  };
  const int selected = formatList_->currentRow();
  if (selected == 0) {
    note(tr("Shared defaults for all export formats. Select a format to configure its own options."));
    auto* folder = new QComboBox(card);
    folder->setObjectName("ExportDefaultFolder");
    folder->addItem(tr("Same folder as current file"), 0);
    folder->addItem(tr("Custom..."), 2);
    folder->setCurrentIndex(QSettings().value("export/defaultFolder", 0).toInt() == 2 ? 1 : 0);
    polishComboBox(folder);
    form->addRow(tr("Default Export Folder"), folder);
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto custom =
        QSettings().value("export/customFolder", QSettings().value("export/lastCustomFolder", documents)).toString();
    auto* customRow = file(tr("Custom Folder"), "export/customFolder", {}, true, "ExportCustomFolder");
    customRow->findChild<QLineEdit*>()->setText(custom);
    customRow->setEnabled(folder->currentData().toInt() == 2);
    connect(folder, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [folder, customRow] {
      QSettings().setValue("export/defaultFolder", folder->currentData());
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
  const auto format = formats[selected - 1];
  const auto p = exportSettingsPrefix(format);
  note(tr("Used when exporting as %1. Changes are saved immediately.").arg(formatList_->currentItem()->text()));
  const auto theme = [&] {
    combo(tr("Theme"), p + "theme", {tr("Current theme"), tr("Default light theme")}, {"current", "github"}, "current",
          "ExportTheme");
  };
  if (format == ExportFormat::Pdf) {
    auto* paper = combo(tr("Page Size"), p + "paper", {"A4", "A5", "Letter", "Legal", tr("Custom...")},
                        {"A4", "A5", "Letter", "Legal", "Custom"}, "A4", "ExportPdfPaper");
    auto* width = decimal(tr("Width"), p + "width", 210, 50, 1000, "ExportPdfWidth");
    auto* height = decimal(tr("Height"), p + "height", 297, 50, 1000, "ExportPdfHeight");
    const auto custom = [paper, width, height, p] {
      const auto id = paper->currentData().toString();
      const bool editable = id == "Custom";
      width->setEnabled(editable);
      height->setEnabled(editable);
      const QSignalBlocker widthBlock(width), heightBlock(height);
      if (editable) {
        width->setValue(QSettings().value(p + "width", 210).toDouble());
        height->setValue(QSettings().value(p + "height", 297).toDouble());
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
  check(tr("Open the exported file directory"), p + "openDirectory",
        QSettings().value("export/openAfterExport", false).toBool(), "ExportOpenDirectory");
  check(tr("Open the exported file"), p + "openFile", false, "ExportOpenFile");
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
