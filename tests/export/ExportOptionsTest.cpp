#include "export/ExportOptions.h"
#include "export/ExportPreset.h"
#include "export/NativeExporter.h"
#include "document/DocumentSession.h"
#include "../TestUtils.h"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QDir>

using namespace muffin;

namespace {
QByteArray read(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "read exported file");
  return file.readAll();
}

void pdfPageSettingsAndHeadingBreaks() {
  DocumentSession document;
  document.setMarkdownText("# First\n\nAlpha\n\n# Second\n\nBeta\n", false);
  QSettings().setValue("export/pdf/paper", "A5");
  QSettings().setValue("export/pdf/landscape", true);
  QSettings().setValue("export/pdf/margin", 10.0);
  auto options = pdfExportOptions();
  require(options.pageLayout.orientation() == QPageLayout::Landscape && options.pageLayout.margins().left() == 10,
          "PDF settings become a real page layout");
  QTemporaryDir folder;
  const auto plain = folder.filePath("plain.pdf");
  auto theme = RenderTheme::defaultTheme();
  require(exportNativePdf(plain, document.document(), theme, {}, options), "export ordinary PDF");
  const auto data = QString::fromLatin1(read(plain));
  const QRegularExpression pages("/Type\\s*/Page\\b");
  const auto countPages = [&](const QString& text) {
    auto it = pages.globalMatch(text);
    int count = 0;
    while (it.hasNext()) {
      it.next();
      ++count;
    }
    return count;
  };
  require(countPages(data) == 1, "short note fits one PDF page");
  const auto media = QRegularExpression("/MediaBox\\s*\\[\\s*0\\s+0\\s+([0-9.]+)\\s+([0-9.]+)").match(data);
  require(media.hasMatch() && qAbs(media.captured(1).toDouble() - 595) < 2 &&
              qAbs(media.captured(2).toDouble() - 420) < 2,
          "PDF has A5 landscape dimensions in points");
  options.breakOnHeading = true;
  const auto split = folder.filePath("split.pdf");
  require(exportNativePdf(split, document.document(), theme, {}, options), "export heading-paginated PDF");
  require(countPages(QString::fromLatin1(read(split))) == 2, "level-one headings really introduce page breaks");
}

void imageDimensionsAndContent() {
  DocumentSession document;
  document.setMarkdownText("# Image\n\nRendered **content**\n", false);
  QTemporaryDir folder;
  const auto path = folder.filePath("note.png");
  ImageExportOptions options;
  options.width = 640;
  options.scale = 2;
  auto theme = RenderTheme::defaultTheme();
  require(exportNativeImage(path, document.document(), theme, {}, options), "export native PNG");
  QImage image(path);
  require(!image.isNull() && image.width() == 1280 && image.height() > 50, "image applies width and output scale");
  bool hasInk = false;
  const auto background = image.pixel(0, 0);
  for (int y = 0; y < image.height() && !hasInk; ++y)
    for (int x = 0; x < image.width(); ++x)
      if (image.pixel(x, y) != background) {
        hasInk = true;
        break;
      }
  require(hasInk, "PNG contains painted document content");
}

void pdfHeadersFootersAndPageNumbers() {
  DocumentSession document;
  QString markdown = QStringLiteral("# 中文长文\n\n");
  for (int i = 0; i < 80; ++i)
    markdown += QStringLiteral("正文记录 ROW%1 中文内容与 English text，验证分页保留正文。\n\n")
                    .arg(i, 3, 10, QLatin1Char('0'));
  markdown += QStringLiteral("# 第二章\n\n```cpp\n");
  for (int i = 0; i < 80; ++i)
    markdown += QStringLiteral("int CODE%1 = %2; // 跨页代码\n").arg(i, 3, 10, QLatin1Char('0')).arg(i);
  markdown += QStringLiteral("```\n\n最后一行 END_MARKER\n");
  document.setMarkdownText(markdown, false);
  QSettings().setValue("export/pdf/header", "${title} · ${author}");
  QSettings().setValue("export/pdf/footer", "${fileName} | ${pageNo} of ${pageCount}");
  QSettings().setValue("export/pdf/author", QStringLiteral("作者"));
  QSettings().setValue("export/pdf/pageNumbers", true);
  auto options = pdfExportOptions();
  require(options.header.contains("${title}") && options.footer.contains("${pageCount}") && options.pageNumbers &&
              options.author == QStringLiteral("作者"),
          "PDF decoration settings are loaded");
  options.breakOnHeading = true;
  QTemporaryDir temporary;
  const auto output = qEnvironmentVariable("MUFFIN_PDF_QA_DIRECTORY", temporary.path());
  require(QDir().mkpath(output), "PDF QA directory");
  for (int variant = 0; variant < 3; ++variant) {
    options.pageLayout =
        QPageLayout(QPageSize(QPageSize::A5), variant == 2 ? QPageLayout::Landscape : QPageLayout::Portrait,
                    variant == 1 ? QMarginsF() : QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
    const auto path = QDir(output).filePath(QStringLiteral("decorated-%1.pdf").arg(variant));
    require(exportNativePdf(path, document.document(), RenderTheme::defaultTheme(), QStringLiteral("notes/中文.md"),
                            options),
            "export decorated multipage PDF");
    const auto data = QString::fromLatin1(read(path));
    auto pages = QRegularExpression("/Type\\s*/Page\\b").globalMatch(data);
    int count = 0;
    while (pages.hasNext()) {
      pages.next();
      ++count;
    }
    require(count > 2, "decorations are tested across paragraphs, heading breaks and a long code block");
  }
  for (const auto& key : {"header", "footer", "author", "pageNumbers"})
    QSettings().remove(QStringLiteral("export/pdf/") + key);
}

void formatOptionsAndQuotedArguments() {
  QSettings().setValue("export/docx/referenceDocument", "/notes/style template.docx");
  QSettings().setValue("export/docx/toc", true);
  QSettings().setValue("export/docx/tocDepth", 2);
  QSettings().setValue("export/docx/arguments", "--metadata subtitle=\"hello world\"");
  QSettings().setValue("export/epub/css", "/notes/book.css");
  QStringList args;
  require(pandocExportArguments(ExportFormat::Docx, "gfm", "out.docx", pandocExportOptions(ExportFormat::Docx), &args),
          "build Word arguments");
  require(args.contains("/notes/style template.docx") && args.contains("--toc-depth=2") &&
              args.contains("subtitle=hello world") && !args.contains("--css"),
          "Word keeps quoted paths, TOC and its own options");
  require(pandocExportArguments(ExportFormat::Epub, "gfm", "out.epub", pandocExportOptions(ExportFormat::Epub), &args),
          "build EPUB arguments");
  require(args.contains("--css") && args.contains("/notes/book.css") && !args.contains("--reference-doc") &&
              !args.contains("--toc"),
          "EPUB settings do not inherit Word settings");
  for (const auto& argument : QStringList{"--output=other.docx", "--out=other.docx", "--ou=other.docx", "-oother.docx",
                                          "-sNoother.docx", "--to=html", "--from=gfm", "--defaults=settings.yaml"}) {
    PandocExportOptions options;
    options.extraArguments = argument;
    require(!pandocExportArguments(ExportFormat::Docx, "gfm", "out.docx", options, &args),
            "reject arguments which bypass managed export destinations");
  }
  PandocExportOptions options;
  options.extraArguments = "--toc --toc-depth=2 -sVfoo=bar -sfalse";
  require(pandocExportArguments(ExportFormat::Docx, "gfm", "out.docx", options, &args),
          "custom TOC arguments are allowed");
}

void namedPresetsAreIndependentAndPersistent() {
  const auto builtin = builtInExportPreset(ExportFormat::Docx);
  ExportPreset first{"first", QStringLiteral("工作报告"), ExportFormat::Docx, builtin.options};
  first.options.insert("author", "Alice");
  first.options.insert("tocDepth", 5);
  auto second = first;
  second.id = "second";
  second.name = "Short report";
  second.options.insert("author", "Bob");
  saveExportPresets({second, first});
  QSettings().sync();
  const auto loaded = exportPresets();
  require(loaded.size() == 2 && loaded[0].id == "second" && loaded[1].name == first.name,
          "named presets retain names, identity and user ordering");
  const auto options = pandocExportOptions(loaded[1].options);
  require(options.author == "Alice" && options.tocDepth == 5 && loaded[0].options.value("author") == "Bob" &&
              builtInExportPreset(ExportFormat::Docx).options == builtin.options,
          "format options read a preset snapshot without changing defaults or another preset");
  auto pdf = builtInExportPreset(ExportFormat::Pdf).options;
  pdf.insert("paper", "Custom");
  pdf.insert("width", 170.5);
  pdf.insert("height", 240.0);
  pdf.insert("header", "${title}");
  const auto layout = pdfExportOptions(pdf);
  require(qAbs(layout.pageLayout.pageSize().size(QPageSize::Millimeter).width() - 170.5) < 0.5 &&
              layout.header == "${title}",
          "native options also read independent preset snapshots");
  QSettings().remove("export/presets");
}

void commandVariablesStayLiteralArguments() {
  ExportPreset command{"command",
                       "Converter",
                       ExportFormat::Command,
                       {{"program", "/tools/converter"},
                        {"extension", "custom"},
                        {"arguments", "--input \"${inputPath}\" --out=${outputPath} --name \"${fileName}\""}}};
  const QVariantMap variables{{"inputPath", QStringLiteral("/笔记/a b\"c.md")},
                              {"outputPath", "/output/with spaces.custom"},
                              {"fileName", "${outputPath}; $(do not execute)"}};
  QString program;
  QStringList args;
  require(exportCommandArguments(command, variables, &program, &args) && program == "/tools/converter" &&
              args == QStringList{"--input", variables.value("inputPath").toString(),
                                  "--out=/output/with spaces.custom", "--name", variables.value("fileName").toString()},
          "variables are substituted after tokenization, without shell evaluation or recursive expansion");
  command.options.insert("arguments", "${unknown} ${outputPath}");
  require(!exportCommandArguments(command, variables, &program, &args), "unknown variables are rejected");
  command.options.insert("arguments", "${inputPath}");
  require(!exportCommandArguments(command, variables, &program, &args), "command must declare an output file");
  command.options.insert("extension", "../md");
  require(exportPresetExtension(command).isEmpty(), "extension cannot escape the staging directory");
}
}

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  QCoreApplication::setOrganizationName("MuffinTests");
  QCoreApplication::setApplicationName("ExportOptions");
  runTest("pdfPageSettingsAndHeadingBreaks", pdfPageSettingsAndHeadingBreaks);
  runTest("imageDimensionsAndContent", imageDimensionsAndContent);
  runTest("pdfHeadersFootersAndPageNumbers", pdfHeadersFootersAndPageNumbers);
  runTest("formatOptionsAndQuotedArguments", formatOptionsAndQuotedArguments);
  runTest("namedPresetsAreIndependentAndPersistent", namedPresetsAreIndependentAndPersistent);
  runTest("commandVariablesStayLiteralArguments", commandVariablesStayLiteralArguments);
}
