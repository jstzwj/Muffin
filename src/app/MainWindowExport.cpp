#include "app/MainWindow.h"
#include "editor/EditorView.h"
#include "export/ExportOptions.h"
#include "export/HtmlExporter.h"
#include "export/NativeExporter.h"
#include "export/PandocRunner.h"
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>

namespace {
bool writeBytes(const QString& path, const QByteArray& bytes, QString* error) {
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
    return true;
  *error = file.errorString();
  return false;
}

QString suggestedDirectory(const QString& path) {
  QSettings settings;
  const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (settings.value("export/defaultFolder", 0).toInt() == 2)
    return settings.value("export/customFolder", settings.value("export/lastCustomFolder", documents)).toString();
  return path.isEmpty() ? documents : QFileInfo(path).absolutePath();
}
}

void muffin::MainWindow::rebuildExportPresetsMenu() {
  if (!exportPresetsMenu_)
    return;
  exportPresetsMenu_->clear();
  for (const auto& preset : muffin::exportPresets()) {
    auto* action = exportPresetsMenu_->addAction(preset.name);
    action->setObjectName("export.preset." + preset.id);
    action->setEnabled(!exporting_ && !tab_->session.isOpenInProgress() && !tab_->session.markdownText().isEmpty());
    connect(action, &QAction::triggered, this, [this, id = preset.id] { exportPreset(id); });
  }
  if (!exportPresetsMenu_->isEmpty())
    exportPresetsMenu_->addSeparator();
  exportPresetsMenu_->addAction(tr("Manage Export Presets..."), this, [this] {
    QSettings().setValue("preferences/lastCategory", "export");
    showPreferences();
  });
}

void muffin::MainWindow::exportAs(ExportFormat format) {
  ExportRecord record;
  record.preset = builtInExportPreset(format);
  runExport(std::move(record), true);
}

void muffin::MainWindow::exportPreset(const QString& id) {
  for (const auto& preset : muffin::exportPresets())
    if (preset.id == id) {
      ExportRecord record;
      record.preset = preset;
      runExport(std::move(record), true);
      return;
    }
}

void muffin::MainWindow::repeatExport(bool overwrite) {
  if (tab_->lastExport)
    runExport(*tab_->lastExport, !overwrite);
}

void muffin::MainWindow::runExport(ExportRecord record, bool chooseTarget) {
  if (exporting_ || tab_->session.isOpenInProgress() || !tab_->pendingOpenPath.isEmpty())
    return;
  const QScopedValueRollback<bool> busy(exporting_, true);
  const QPointer<DocumentTab> document(tab_);
  const bool repeated = !record.target.isEmpty();
  const auto& values = record.preset.options;
  const auto format = record.preset.format;
  const QString ext = exportPresetExtension(record.preset);
  if (ext.isEmpty()) {
    QMessageBox::warning(this, tr("Export Failed"), tr("Enter a valid output extension in the export preset."));
    return;
  }
  const auto sourcePath = document->session.filePath();
  const auto baseName = sourcePath.isEmpty() ? tr("Untitled") : QFileInfo(sourcePath).completeBaseName();
  if (chooseTarget) {
    const auto suggested = repeated ? record.target : QDir(suggestedDirectory(sourcePath)).filePath(baseName + ext);
    const auto label = record.preset.name.isEmpty() ? ext.mid(1).toUpper() : record.preset.name;
    record.target =
        QFileDialog::getSaveFileName(this, tr("Export As"), suggested, tr("%1 files (*%2)").arg(label, ext));
    if (record.target.isEmpty())
      return;
  }
  if (!document || document->session.isOpenInProgress())
    return;
  // A custom exporter may produce Markdown too; never overwrite a live source.
  const auto destinationIsOpen = [&] {
    for (auto* widget : QApplication::topLevelWidgets())
      if (auto* window = qobject_cast<MainWindow*>(widget); window && window->isPathOpen(record.target))
        return true;
    return false;
  };
  if (destinationIsOpen()) {
    QMessageBox::warning(this, tr("Export Failed"),
                         tr("The export destination is an open document. Choose another file."));
    return;
  }
  const auto markdown = document->session.markdownText().toString();
  if (markdown.trimmed().isEmpty()) {
    statusBar()->showMessage(tr("Nothing to export"), 4000);
    return;
  }
  if (!repeated) {
    if (format >= ExportFormat::Docx && format != ExportFormat::Command)
      record.preset.options.insert("converterPath", PandocRunner::resolvedExecutable());
    record.preset.options.insert("inputFormat", QSettings().value("markdown/breakOnSingleNewline", true).toBool()
                                                    ? "gfm+hard_line_breaks"
                                                    : "gfm");
    const auto name = values.value("theme", "current").toString();
    const auto fontSize = qBound(10, values.value("fontSize", 16).toInt(), 40);
    record.theme = name == "github" ? RenderTheme::fromDefinition(themeManager_.definition("github"), 100, fontSize)
                                    : themeManager_.currentTheme(100, fontSize);
    record.themeCss = name == "github" ? ThemeManager().currentThemeCss() : themeManager_.currentThemeCss();
    if ((format == ExportFormat::Pdf || format == ExportFormat::Image) &&
        values.value("useEditorTypography", false).toBool()) {
      const auto typography = TypographyOverrides::load();
      record.theme.setTypographyOverrides(typography);
    }
  }
  QTemporaryDir staging(QDir(QFileInfo(record.target).absolutePath()).filePath(".muffin-export-XXXXXX"));
  if (!staging.isValid()) {
    QMessageBox::critical(this, tr("Export Failed"), tr("Could not create a temporary export file."));
    return;
  }
  const auto staged = staging.filePath("document" + ext);
  auto* cache = document->renderView->mermaidRenderCache();
  bool ok = false;
  QString error;
  if (format == ExportFormat::Pdf) {
    ok = exportNativePdf(staged, document->session.document(), record.theme, sourcePath, pdfExportOptions(values),
                         cache);
    if (!ok)
      error = tr("PDF export failed: the printer device could not be opened.");
  } else if (format == ExportFormat::Image) {
    ok = exportNativeImage(staged, document->session.document(), record.theme, sourcePath, imageExportOptions(values),
                           cache);
    if (!ok)
      error = tr(
          "Image export failed. The image must fit within 64 million pixels and 32,767 pixels in height. Try a smaller width or scale, or export as PDF.");
  } else if (format == ExportFormat::Html || format == ExportFormat::HtmlPlain) {
    const bool styled = format == ExportFormat::Html;
    const auto html = renderDocumentHtml(markdown, baseName, styled, styled ? record.themeCss : QString(),
                                         {.outline = values.value("outline", false).toBool(),
                                          .head = values.value("head").toString(),
                                          .body = values.value("body").toString()});
    ok = writeBytes(staged, html.toUtf8(), &error);
  } else {
    QString program;
    QStringList arguments;
    const auto label = tr("Exporting %1…").arg(baseName + ext);
    if (format == ExportFormat::Command) {
      const auto input = staging.filePath("input.md");
      if (!writeBytes(input, markdown.toUtf8(), &error)) {
        QMessageBox::critical(this, tr("Export Failed"), error);
        return;
      }
      if (!exportCommandArguments(
              record.preset,
              {{"inputPath", input},
               {"outputPath", staged},
               {"currentPath", sourcePath},
               {"fileName", baseName},
               {"currentFolder", sourcePath.isEmpty() ? QDir::homePath() : QFileInfo(sourcePath).absolutePath()}},
              &program, &arguments))
        error = tr(
            "Configure an executable and arguments containing ${outputPath}. Supported variables: ${inputPath}, ${outputPath}, ${currentPath}, ${currentFolder}, ${fileName}.");
    } else {
      program = values.value("converterPath").toString();
      if (!PandocRunner::isAvailable(program))
        error = tr("Pandoc was not found. Install Pandoc or set its path in Preferences → Export.");
      const auto options = pandocExportOptions(values);
      for (const auto& path : {options.referenceDocument, options.css})
        if (!path.isEmpty() && !QFileInfo(path).isFile())
          error = tr("Export resource was not found: %1").arg(path);
      const auto inputFormat = values.value("inputFormat").toString();
      if (!pandocExportArguments(format, inputFormat, staged, options, &arguments))
        error =
            tr("Additional arguments cannot override the input format, output format, output path, or defaults file.");
    }
    if (error.isEmpty()) {
      const auto result =
          ProcessRunner::run(this, program, arguments,
                             {.stdinData = markdown.toUtf8(),
                              .workDir = sourcePath.isEmpty() ? QDir::homePath() : QFileInfo(sourcePath).absolutePath(),
                              .progressLabel = label,
                              .outputLimit = 1024 * 1024});
      if (result.canceled)
        return;
      ok = result.ran && result.exitCode == 0;
      if (!ok)
        error = tr("Export command failed:") + "\n\n" + QString::fromUtf8(result.err);
    }
  }
  // Other windows can still be used while a converter is running.
  if (ok && destinationIsOpen()) {
    ok = false;
    error = tr("The export destination is an open document. Choose another file.");
  }
  if (ok) {
    QFile input(staged);
    QSaveFile output(record.target);
    output.setDirectWriteFallback(false);
    ok = input.open(QIODevice::ReadOnly);
    if (!ok)
      error = input.errorString();
    else if (!(ok = output.open(QIODevice::WriteOnly)))
      error = output.errorString();
    while (ok && !input.atEnd()) {
      const auto bytes = input.read(64 * 1024);
      if (bytes.isEmpty()) {
        ok = false;
        error = input.errorString();
      } else if (output.write(bytes) != bytes.size()) {
        ok = false;
        error = output.errorString();
      }
    }
    if (ok && !(ok = output.commit()))
      error = output.errorString();
  }
  if (!ok) {
    QMessageBox::critical(this, tr("Export Failed"), error);
    return;
  }
  if (document)
    document->lastExport = record;
  exporting_ = false;
  updateAllActions();
  statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(record.target)), 6000);
  if (values.value("openDirectory", false).toBool())
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(record.target).absolutePath()));
  if (values.value("openFile", false).toBool())
    QDesktopServices::openUrl(QUrl::fromLocalFile(record.target));
}
