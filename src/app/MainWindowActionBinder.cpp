#include "app/MainWindowActionBinder.h"

#include "app/MainWindow.h"
#include "app/UpdateChecker.h"
#include "app/SidebarWidget.h"
#include "document/MarkdownTypes.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"
#include "editor/EditorView.h"
#include "io/ImageFileOps.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTextCursor>
#include <QUrl>

void muffin::MainWindowActionBinder::bindCommands(MainWindow& window) {
  window.commands_.bind(QStringLiteral("file.new"), [&window] {
    window.editorController_.clearHistoryAndSelection();
    window.fileController_.newFile(window.session_, &window);
  });
  window.commands_.bind(QStringLiteral("file.new_window"), [&window] { window.openNewWindow(); });
  window.commands_.bind(QStringLiteral("file.open_folder"), [&window] { window.openFolder(); });
  window.commands_.bind(QStringLiteral("file.open"), [&window] {
    if (window.fileController_.open(window.session_, &window)) {
      window.editorController_.clearHistoryAndSelection();
      window.addRecentFile(window.session_.filePath());
    }
  });
  window.commands_.bind(QStringLiteral("file.save"), [&window] {
    if (window.fileController_.save(window.session_, &window)) {
      window.addRecentFile(window.session_.filePath());
    }
  });
  window.commands_.bind(QStringLiteral("file.save_as"), [&window] {
    if (window.fileController_.saveAs(window.session_, &window)) {
      window.addRecentFile(window.session_.filePath());
    }
  });
  window.commands_.bind(QStringLiteral("file.properties"), [&window] { window.showDocumentProperties(); });
  window.commands_.bind(QStringLiteral("file.preferences"), [&window] { window.showPreferences(); });
  window.commands_.bind(QStringLiteral("file.print"), [&window] { window.printDocument(); });
  window.commands_.bind(QStringLiteral("file.reveal"), [&window] { window.revealCurrentFile(); });
  window.commands_.bind(QStringLiteral("file.close"), [&window] { window.close(); });
  window.commands_.bind(QStringLiteral("file.move_to"), [&window] { window.moveToFile(); });
  window.commands_.bind(QStringLiteral("file.save_all"), [&window] { window.saveAllOpenFiles(); });
  window.commands_.bind(QStringLiteral("file.sidebar"), [&window] { window.showInSidebar(); });
  window.commands_.bind(QStringLiteral("file.delete"), [&window] { window.deleteFile(); });

  window.commands_.bind(QStringLiteral("edit.undo"), [&window] { window.undoEdit(); });
  window.commands_.bind(QStringLiteral("edit.redo"), [&window] { window.redoEdit(); });
  window.commands_.bind(QStringLiteral("edit.cut"), [&window] { window.backend_->cut(); });
  window.commands_.bind(QStringLiteral("edit.copy"), [&window] { window.backend_->copy(); });
  window.commands_.bind(QStringLiteral("edit.paste"), [&window] { window.backend_->paste(); });
  window.commands_.bind(QStringLiteral("edit.select_all"), [&window] { window.backend_->selectAll(); });
  window.commands_.bind(QStringLiteral("edit.delete"), [&window] { window.backend_->deleteForward(); });
  window.commands_.bind(QStringLiteral("edit.delete_word"), [&window] {
    window.backend_->deleteRange(muffin::DeleteTarget::Word);
  });
  window.commands_.bind(QStringLiteral("edit.delete_format"), [&window] {
    window.backend_->deleteRange(muffin::DeleteTarget::FormatSpan);
  });
  window.commands_.bind(QStringLiteral("edit.delete_line"), [&window] {
    window.backend_->deleteRange(muffin::DeleteTarget::Line);
  });
  window.commands_.bind(QStringLiteral("edit.delete_block"), [&window] {
    window.backend_->deleteRange(muffin::DeleteTarget::Block);
  });
  window.commands_.bind(QStringLiteral("edit.copy_plain"), [&window] { window.backend_->copyAsPlainText(); });
  window.commands_.bind(QStringLiteral("edit.copy_markdown"), [&window] { window.backend_->copyAsMarkdown(); });
  window.commands_.bind(QStringLiteral("edit.copy_html"), [&window] { window.backend_->copyAsHtml(); });
  window.commands_.bind(QStringLiteral("edit.paste_plain"), [&window] { window.backend_->pasteAsPlainText(); });
  window.commands_.bind(QStringLiteral("edit.select_line"), [&window] { window.backend_->selectLine(); });
  window.commands_.bind(QStringLiteral("edit.select_format"), [&window] { window.backend_->selectFormatSpan(); });
  window.commands_.bind(QStringLiteral("edit.select_block"), [&window] { window.backend_->selectBlock(); });
  window.commands_.bind(QStringLiteral("edit.select_word"), [&window] { window.backend_->selectWord(); });
  window.commands_.bind(QStringLiteral("edit.jump_doc_start"), [&window] { window.backend_->moveDocumentStart(); });
  window.commands_.bind(QStringLiteral("edit.jump_doc_end"), [&window] { window.backend_->moveDocumentEnd(); });
  window.commands_.bind(QStringLiteral("edit.jump_line_start"), [&window] { window.backend_->moveLineStart(); });
  window.commands_.bind(QStringLiteral("edit.jump_line_end"), [&window] { window.backend_->moveLineEnd(); });
  window.commands_.bind(QStringLiteral("edit.jump_selection"), [&window] { window.backend_->selectNextOccurrence(); });
  window.commands_.bind(QStringLiteral("edit.move_line_up"), [&window] { window.backend_->moveLineUp(); });
  window.commands_.bind(QStringLiteral("edit.move_line_down"), [&window] { window.backend_->moveLineDown(); });

  window.commands_.bind(QStringLiteral("edit.find"), [&window] { window.showFindBar(); });
  window.commands_.bind(QStringLiteral("edit.replace"), [&window] { window.showReplaceBar(); });
  window.commands_.bind(QStringLiteral("edit.find_next"), [&window] {
    if (!window.findBar_ || !window.findBar_->isVisible()) {
      window.showFindBar();
    }
    window.performFindNext();
  });
  window.commands_.bind(QStringLiteral("edit.find_previous"), [&window] {
    if (!window.findBar_ || !window.findBar_->isVisible()) {
      window.showFindBar();
    }
    window.performFindPrevious();
  });

  window.commands_.bind(QStringLiteral("format.bold"), [&window] { window.backend_->toggleBold(); });
  window.commands_.bind(QStringLiteral("format.italic"), [&window] { window.backend_->toggleItalic(); });
  window.commands_.bind(QStringLiteral("format.code"), [&window] { window.backend_->toggleCode(); });
  window.commands_.bind(QStringLiteral("format.strike"), [&window] { window.backend_->toggleStrikethrough(); });
  window.commands_.bind(QStringLiteral("format.inline_math"), [&window] { window.backend_->toggleInlineMath(); });
  window.commands_.bind(QStringLiteral("format.underline"), [&window] { window.backend_->toggleUnderline(); });
  window.commands_.bind(QStringLiteral("format.link"), [&window] { window.backend_->insertLink(); });

  window.commands_.bind(QStringLiteral("image.insert"), [&window] { window.insertImageWithDialog(); });
  window.commands_.bind(QStringLiteral("image.insert_local"), [&window] { window.insertLocalImageWithDialog(); });
  window.commands_.bind(QStringLiteral("image.reload_all"), [&window] {
    if (window.renderView_) {
      window.renderView_->setDocument(window.session_.document(), window.session_.filePath());
    }
  });
  window.commands_.bind(QStringLiteral("image.insert_relative"), [&window] {
    QSettings().setValue(QStringLiteral("image/insertAction"), 0);
  });
  window.commands_.bind(QStringLiteral("image.insert_absolute"), [&window] {
    QSettings().setValue(QStringLiteral("image/insertAction"), 1);
  });
  window.commands_.bind(QStringLiteral("image.insert_copy"), [&window] {
    QSettings().setValue(QStringLiteral("image/insertAction"), 2);
  });
  window.commands_.bind(QStringLiteral("image.insert_upload"), [&window] {
    QSettings().setValue(QStringLiteral("image/insertAction"), 3);
  });
  window.commands_.bind(QStringLiteral("image.open_location"), [&window] {
    const QString src = window.renderCommands_.imageSrcAtCursor();
    const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
    const QString resolved = muffin::ImageFileOps::resolveImagePath(src, docDir);
    if (!resolved.isEmpty()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(resolved).absolutePath()));
    }
  });
  window.commands_.bind(QStringLiteral("image.copy_image"), [&window] {
    const QString src = window.renderCommands_.imageSrcAtCursor();
    const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
    const QString resolved = muffin::ImageFileOps::resolveImagePath(src, docDir);
    if (resolved.isEmpty()) {
      return;
    }
    QImage image(resolved);
    if (image.isNull()) {
      return;
    }
    QApplication::clipboard()->setImage(image);
  });
  window.commands_.bind(QStringLiteral("image.delete_image"), [&window] {
    const QString src = window.renderCommands_.imageSrcAtCursor();
    const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
    const QString resolved = muffin::ImageFileOps::resolveImagePath(src, docDir);
    if (resolved.isEmpty()) {
      return;
    }
    if (QMessageBox::question(&window,
            muffin::MainWindow::tr("Delete Image"),
            muffin::MainWindow::tr("Delete image file \"%1\"?\nThis cannot be undone.").arg(QFileInfo(resolved).fileName())) !=
        QMessageBox::Yes) {
      return;
    }
    // Remove the image markdown syntax first
    qsizetype srcStart = 0, srcEnd = 0;
    if (window.renderCommands_.imageSourceRangeAtCursor(srcStart, srcEnd)) {
      window.session_.applyTextDelta(srcStart, srcEnd - srcStart, QString(), true);
    }
    muffin::ImageFileOps::deleteImageFile(resolved);
  });
  window.commands_.bind(QStringLiteral("image.copy_to"), [&window] {
    const QString src = window.renderCommands_.imageSrcAtCursor();
    const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
    const QString resolved = muffin::ImageFileOps::resolveImagePath(src, docDir);
    if (resolved.isEmpty()) {
      return;
    }
    const QString destDir = QFileDialog::getExistingDirectory(&window,
        muffin::MainWindow::tr("Copy Image To"),
        docDir);
    if (destDir.isEmpty()) {
      return;
    }
    QString newPath;
    if (muffin::ImageFileOps::copyImageTo(resolved, QDir(destDir), &newPath)) {
      // Update the markdown src to the new relative path
      const QString relPath = QDir(docDir).relativeFilePath(newPath);
      qsizetype srcStart = 0, srcEnd = 0;
      if (window.renderCommands_.imageSourceRangeAtCursor(srcStart, srcEnd)) {
        const QString& md = window.session_.markdownText();
        const QString oldImage = md.mid(srcStart, srcEnd - srcStart);
        // Replace the src URL in the image syntax
        QString newImage = oldImage;
        const int urlStart = oldImage.indexOf(QStringLiteral("](")) + 2;
        if (urlStart > 1) {
          int urlEnd = urlStart;
          while (urlEnd < oldImage.size() && oldImage[urlEnd] != QChar(')') && oldImage[urlEnd] != QChar(' ')) {
            ++urlEnd;
          }
          newImage = oldImage.left(urlStart) + relPath + oldImage.mid(urlEnd);
          window.session_.applyTextDelta(srcStart, srcEnd - srcStart, newImage, true);
        }
      }
    }
  });
  window.commands_.bind(QStringLiteral("image.move_to"), [&window] {
    const QString src = window.renderCommands_.imageSrcAtCursor();
    const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
    const QString resolved = muffin::ImageFileOps::resolveImagePath(src, docDir);
    if (resolved.isEmpty()) {
      return;
    }
    const QString destDir = QFileDialog::getExistingDirectory(&window,
        muffin::MainWindow::tr("Move Image To"),
        docDir);
    if (destDir.isEmpty()) {
      return;
    }
    QString newPath;
    if (muffin::ImageFileOps::moveImageTo(resolved, QDir(destDir), &newPath)) {
      const QString relPath = QDir(docDir).relativeFilePath(newPath);
      qsizetype srcStart = 0, srcEnd = 0;
      if (window.renderCommands_.imageSourceRangeAtCursor(srcStart, srcEnd)) {
        const QString& md = window.session_.markdownText();
        const QString oldImage = md.mid(srcStart, srcEnd - srcStart);
        QString newImage = oldImage;
        const int urlStart = oldImage.indexOf(QStringLiteral("](")) + 2;
        if (urlStart > 1) {
          int urlEnd = urlStart;
          while (urlEnd < oldImage.size() && oldImage[urlEnd] != QChar(')') && oldImage[urlEnd] != QChar(' ')) {
            ++urlEnd;
          }
          newImage = oldImage.left(urlStart) + relPath + oldImage.mid(urlEnd);
          window.session_.applyTextDelta(srcStart, srcEnd - srcStart, newImage, true);
        }
      }
    }
  });
  window.commands_.bind(QStringLiteral("image.upload"), [&window] {
    // Placeholder — upload infrastructure not yet implemented
    QMessageBox::information(&window, muffin::MainWindow::tr("Upload Image"),
        muffin::MainWindow::tr("Image upload is not yet configured.\nSet up an image uploader in Preferences → Images."));
  });
  window.commands_.bind(QStringLiteral("image.upload_all"), [&window] {
    QMessageBox::information(&window, muffin::MainWindow::tr("Upload All Images"),
        muffin::MainWindow::tr("Image upload is not yet configured.\nSet up an image uploader in Preferences → Images."));
  });

  // Resize image actions
  const QList<QPair<QString, int>> resizeMap = {
      {QStringLiteral("image.resize_25"), 25},
      {QStringLiteral("image.resize_50"), 50},
      {QStringLiteral("image.resize_75"), 75},
      {QStringLiteral("image.resize_100"), 100},
      {QStringLiteral("image.resize_150"), 150},
  };
  for (const auto& [id, pct] : resizeMap) {
    window.commands_.bind(id, [&window, pct] {
      Q_UNUSED(pct);
      // Resize not yet fully supported — placeholder
      // Future: update image size attribute in markdown
    });
  }
  window.commands_.bind(QStringLiteral("image.resize_custom"), [&window] {
    Q_UNUSED(window);
    // Placeholder
  });

  // Convert image syntax
  window.commands_.bind(QStringLiteral("image.to_standard"), [&window] {
    // Placeholder — will convert <img> to ![](url) when HTML images are supported
  });
  window.commands_.bind(QStringLiteral("image.to_html"), [&window] {
    const QString src = window.renderCommands_.imageSrcAtCursor();
    if (src.isEmpty()) {
      return;
    }
    qsizetype imgStart = 0, imgEnd = 0;
    if (!window.renderCommands_.imageSourceRangeAtCursor(imgStart, imgEnd)) {
      return;
    }
    const QString& md = window.session_.markdownText();
    const QString oldSyntax = md.mid(imgStart, imgEnd - imgStart);
    // Extract alt text: ![alt](...)
    QString alt;
    const int altStart = oldSyntax.indexOf(QStringLiteral("![")) + 2;
    if (altStart > 1) {
      const int altEnd = oldSyntax.indexOf(QChar(']'), altStart);
      if (altEnd > altStart) {
        alt = oldSyntax.mid(altStart, altEnd - altStart);
      }
    }
    const QString html = QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(src, alt);
    window.session_.applyTextDelta(imgStart, imgEnd - imgStart, html, true);
  });

  // Batch image operations
  window.commands_.bind(QStringLiteral("image.copy_all_to"), [&window] {
    const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
    if (docDir.isEmpty()) {
      return;
    }
    const QString destDir = QFileDialog::getExistingDirectory(&window,
        muffin::MainWindow::tr("Copy All Images To"), docDir);
    if (destDir.isEmpty()) {
      return;
    }
    const QStringList images = muffin::ImageFileOps::collectLocalImagePaths(window.session_.document(), docDir);
    int copied = 0;
    for (const QString& img : images) {
      if (muffin::ImageFileOps::copyImageTo(img, QDir(destDir), nullptr)) {
        ++copied;
      }
    }
    QMessageBox::information(&window, muffin::MainWindow::tr("Copy All Images"),
        muffin::MainWindow::tr("Copied %1 of %2 image(s).").arg(copied).arg(images.size()));
  });
  window.commands_.bind(QStringLiteral("image.move_all_to"), [&window] {
    const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
    if (docDir.isEmpty()) {
      return;
    }
    const QString destDir = QFileDialog::getExistingDirectory(&window,
        muffin::MainWindow::tr("Move All Images To"), docDir);
    if (destDir.isEmpty()) {
      return;
    }
    const auto refs = muffin::ImageFileOps::collectImageRefs(window.session_.document());
    int moved = 0;
    // Move files and update markdown references
    for (const auto& ref : refs) {
      if (!muffin::ImageFileOps::isLocalImageSrc(ref.href)) {
        continue;
      }
      const QString resolved = muffin::ImageFileOps::resolveImagePath(ref.href, docDir);
      if (resolved.isEmpty()) {
        continue;
      }
      QString newPath;
      if (muffin::ImageFileOps::moveImageTo(resolved, QDir(destDir), &newPath)) {
        ++moved;
      }
    }
    // After moving, update all relative paths in the markdown
    const QString relPrefix = QDir(docDir).relativeFilePath(destDir);
    QString md = window.session_.markdownText();
    // Re-collect refs after text may have shifted — use the originals from before
    // Since we only moved files, the markdown hasn't changed yet. Update hrefs now.
    for (int i = refs.size() - 1; i >= 0; --i) {
      const auto& ref = refs[i];
      if (!muffin::ImageFileOps::isLocalImageSrc(ref.href)) {
        continue;
      }
      const QString resolved = muffin::ImageFileOps::resolveImagePath(ref.href, docDir);
      if (resolved.isEmpty()) {
        continue;
      }
      const QString fileName = QFileInfo(resolved).fileName();
      const QString newRelPath = relPrefix.isEmpty()
          ? fileName
          : relPrefix + QChar('/') + fileName;
      // Replace href in the source range — find the url portion
      const int urlSearchStart = md.indexOf(QStringLiteral("]("), ref.sourceStart);
      if (urlSearchStart < 0 || urlSearchStart >= ref.sourceEnd) {
        continue;
      }
      const int urlStart = urlSearchStart + 2;
      int urlEnd = urlStart;
      while (urlEnd < md.size() && urlEnd <= ref.sourceEnd && md[urlEnd] != QChar(')') && md[urlEnd] != QChar(' ')) {
        ++urlEnd;
      }
      md.replace(urlStart, urlEnd - urlStart, newRelPath);
    }
    window.session_.applyMarkdownText(md, true);
    QMessageBox::information(&window, muffin::MainWindow::tr("Move All Images"),
        muffin::MainWindow::tr("Moved %1 image(s).").arg(moved));
  });
  window.commands_.bind(QStringLiteral("image.global_settings"), [&window] {
    window.showPreferences();
  });

  window.commands_.bind(QStringLiteral("table.insert_row_before"), [&window] { window.renderCommands_.insertRowBefore(); });
  window.commands_.bind(QStringLiteral("table.insert_row_after"), [&window] { window.renderCommands_.insertRowAfter(); });
  window.commands_.bind(QStringLiteral("table.delete_row"), [&window] { window.renderCommands_.deleteCurrentRow(); });
  window.commands_.bind(QStringLiteral("table.move_row_up"), [&window] { window.renderCommands_.moveCurrentRowUp(); });
  window.commands_.bind(QStringLiteral("table.move_row_down"), [&window] { window.renderCommands_.moveCurrentRowDown(); });
  window.commands_.bind(QStringLiteral("table.insert_column_before"), [&window] { window.renderCommands_.insertColumnBefore(); });
  window.commands_.bind(QStringLiteral("table.insert_column_after"), [&window] { window.renderCommands_.insertColumnAfter(); });
  window.commands_.bind(QStringLiteral("table.delete_column"), [&window] { window.renderCommands_.deleteCurrentColumn(); });
  window.commands_.bind(QStringLiteral("table.move_column_left"), [&window] { window.renderCommands_.moveCurrentColumnLeft(); });
  window.commands_.bind(QStringLiteral("table.move_column_right"), [&window] { window.renderCommands_.moveCurrentColumnRight(); });
  window.commands_.bind(QStringLiteral("table.align_left"), [&window] { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Left); });
  window.commands_.bind(QStringLiteral("table.align_center"), [&window] { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Center); });
  window.commands_.bind(QStringLiteral("table.align_right"), [&window] { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Right); });
  window.commands_.bind(QStringLiteral("table.align_none"), [&window] { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::None); });
  window.commands_.bind(QStringLiteral("table.copy_table"), [&window] { window.renderCommands_.copyCurrentTable(); });
  window.commands_.bind(QStringLiteral("table.format_source"), [&window] { window.renderCommands_.formatCurrentTableSource(); });
  window.commands_.bind(QStringLiteral("table.delete_table"), [&window] { window.renderCommands_.deleteCurrentTable(); });
  window.commands_.bind(QStringLiteral("table.insert_table"), [&window] { window.insertTableWithDialog(); });
  window.commands_.bind(QStringLiteral("paragraph.yaml"), [&window] { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Yaml); });
  window.commands_.bind(QStringLiteral("paragraph.toml"), [&window] { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Toml); });
  window.commands_.bind(QStringLiteral("paragraph.json"), [&window] { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Json); });

  for (int level = 1; level <= 6; ++level) {
    window.commands_.bind(QStringLiteral("paragraph.heading_%1").arg(level), [&window, level] { window.renderCommands_.setHeadingLevel(level); });
  }
  window.commands_.bind(QStringLiteral("paragraph.paragraph"), [&window] { window.renderCommands_.setHeadingLevel(0); });
  window.commands_.bind(QStringLiteral("paragraph.promote_heading"), [&window] { window.renderCommands_.promoteHeading(); });
  window.commands_.bind(QStringLiteral("paragraph.demote_heading"), [&window] { window.renderCommands_.demoteHeading(); });

  window.commands_.bind(QStringLiteral("paragraph.math_block"), [&window] { window.renderCommands_.toggleFormulaBlock(); });
  window.commands_.bind(QStringLiteral("paragraph.code_block"), [&window] { window.renderCommands_.toggleCodeBlock(); });
  window.commands_.bind(QStringLiteral("paragraph.insert_before"), [&window] { window.renderCommands_.insertParagraphBefore(); });
  window.commands_.bind(QStringLiteral("paragraph.insert_after"), [&window] { window.renderCommands_.insertParagraphAfter(); });
  window.commands_.bind(QStringLiteral("paragraph.link_ref"), [&window] { window.renderCommands_.insertLinkReference(); });
  window.commands_.bind(QStringLiteral("paragraph.footnote"), [&window] { window.renderCommands_.insertFootnoteDefinition(); });
  window.commands_.bind(QStringLiteral("paragraph.hr"), [&window] { window.renderCommands_.insertHorizontalRule(); });
  window.commands_.bind(QStringLiteral("paragraph.toc"), [&window] { window.renderCommands_.insertTableOfContents(); });

  window.commands_.bind(QStringLiteral("paragraph.quote"), [&window] { window.renderCommands_.toggleQuote(); });
  window.commands_.bind(QStringLiteral("paragraph.ordered_list"), [&window] { window.renderCommands_.convertToOrderedList(); });
  window.commands_.bind(QStringLiteral("paragraph.unordered_list"), [&window] { window.renderCommands_.convertToUnorderedList(); });
  window.commands_.bind(QStringLiteral("paragraph.task_list"), [&window] { window.renderCommands_.convertToTaskList(); });

  window.commands_.bind(QStringLiteral("code.enter_edit"), [&window] { window.renderCommands_.enterCodeEditMode(); });
  window.commands_.bind(QStringLiteral("code.exit_edit"), [&window] { window.renderCommands_.exitCodeEditMode(); });
  window.commands_.bind(QStringLiteral("code.set_language"), [&window] {
    bool ok = false;
    const QString language = QInputDialog::getText(
        &window, muffin::MainWindow::tr("Code Language"), muffin::MainWindow::tr("Language:"), QLineEdit::Normal, QString(), &ok);
    if (ok) {
      window.renderCommands_.setCodeLanguage(language);
    }
  });

  window.commands_.bind(QStringLiteral("html.enter_edit"), [&window] { window.renderCommands_.enterHtmlEditMode(); });
  window.commands_.bind(QStringLiteral("html.exit_edit"), [&window] { window.renderCommands_.exitHtmlEditMode(); });
  window.commands_.bind(QStringLiteral("html.set_source"), [&window] {
    bool ok = false;
    const QString html = QInputDialog::getMultiLineText(&window, muffin::MainWindow::tr("HTML Source"), muffin::MainWindow::tr("HTML:"), QString(), &ok);
    if (ok) {
      window.renderCommands_.setHtmlSource(html);
    }
  });

  window.commands_.bind(QStringLiteral("math.enter_edit"), [&window] { window.renderCommands_.enterMathEditMode(); });
  window.commands_.bind(QStringLiteral("math.exit_edit"), [&window] { window.renderCommands_.exitMathEditMode(); });
  window.commands_.bind(QStringLiteral("math.set_tex"), [&window] {
    bool ok = false;
    const QString tex = QInputDialog::getMultiLineText(&window, muffin::MainWindow::tr("Math TeX"), muffin::MainWindow::tr("TeX:"), QString(), &ok);
    if (ok) {
      window.renderCommands_.setMathTex(tex);
    }
  });

  window.commands_.bind(QStringLiteral("view.word_wrap"), [&window] {
    const bool enabled = window.commands_.action(QStringLiteral("view.word_wrap"))->isChecked();
    window.editor_->setWordWrapEnabled(enabled);
    QSettings().setValue(QStringLiteral("view/wordWrap"), enabled);
  });

  window.commands_.bind(QStringLiteral("edit.linebreak_crlf"), [&window] {
    window.commands_.setChecked(QStringLiteral("edit.linebreak_crlf"), true);
    window.commands_.setChecked(QStringLiteral("edit.linebreak_lf"), false);
    QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 1);
  });
  window.commands_.bind(QStringLiteral("edit.linebreak_lf"), [&window] {
    window.commands_.setChecked(QStringLiteral("edit.linebreak_lf"), true);
    window.commands_.setChecked(QStringLiteral("edit.linebreak_crlf"), false);
    QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 0);
  });
  window.commands_.bind(QStringLiteral("edit.trailing_newline"), [&window] {
    const bool checked = window.commands_.action(QStringLiteral("edit.trailing_newline"))->isChecked();
    QSettings().setValue(QStringLiteral("editor/trailingNewline"), checked);
  });

  window.commands_.bind(QStringLiteral("view.sidebar"), [&window] {
    window.updateSidebarMode();
    QSettings().setValue(QStringLiteral("view/sidebarVisible"),
        window.commands_.action(QStringLiteral("view.sidebar"))->isChecked());
  });
  window.commands_.bind(QStringLiteral("view.outline"), [&window] { window.setSidebarPanel(SidebarWidget::Panel::Outline); });
  window.commands_.bind(QStringLiteral("view.file_tree"), [&window] { window.setSidebarPanel(SidebarWidget::Panel::Files); });
  window.commands_.bind(QStringLiteral("view.source_mode"), [&window] {
    window.updateViewMode();
    QSettings().setValue(QStringLiteral("view/sourceMode"),
        window.commands_.action(QStringLiteral("view.source_mode"))->isChecked());
  });
  window.commands_.bind(QStringLiteral("view.status_bar"), [&window] {
    const bool visible = window.commands_.action(QStringLiteral("view.status_bar"))->isChecked();
    window.setStatusBarVisible(visible);
    window.saveAppearanceStatusBarVisible(visible);
  });
  window.commands_.bind(QStringLiteral("view.fullscreen"), [&window] {
    window.isFullScreen() ? window.showNormal() : window.showFullScreen();
  });
  window.commands_.bind(QStringLiteral("view.focus"), [&window] {
    const bool checked = window.commands_.action(QStringLiteral("view.focus"))->isChecked();
    window.setFocusMode(checked);
    window.saveAppearanceFocusMode(checked);
  });
  window.commands_.bind(QStringLiteral("view.typewriter"), [&window] {
    const bool checked = window.commands_.action(QStringLiteral("view.typewriter"))->isChecked();
    window.setTypewriterMode(checked);
    window.saveAppearanceTypewriterMode(checked);
  });
  window.commands_.bind(QStringLiteral("view.always_on_top"), [&window] {
    const bool checked = window.commands_.action(QStringLiteral("view.always_on_top"))->isChecked();
    window.setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    window.show();
    QSettings().setValue(QStringLiteral("view/alwaysOnTop"), checked);
  });
  window.commands_.bind(QStringLiteral("view.zoom_in"), [&window] {
    window.setZoomPercent(window.zoomPercent_ + 10);
    window.saveAppearanceZoomPercent(window.zoomPercent_);
  });
  window.commands_.bind(QStringLiteral("view.zoom_out"), [&window] {
    window.setZoomPercent(window.zoomPercent_ - 10);
    window.saveAppearanceZoomPercent(window.zoomPercent_);
  });
  window.commands_.bind(QStringLiteral("view.actual_size"), [&window] {
    window.setZoomPercent(100);
    window.saveAppearanceZoomPercent(window.zoomPercent_);
  });

  window.commands_.bind(QStringLiteral("help.website"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin")));
  });
  window.commands_.bind(QStringLiteral("help.changelog"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/blob/main/CHANGELOG.md")));
  });
  window.commands_.bind(QStringLiteral("help.feedback"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/issues")));
  });
  window.commands_.bind(QStringLiteral("help.update"), [&window] {
    muffin::UpdateChecker::instance().checkForUpdates();
  });
  window.commands_.bind(QStringLiteral("help.about"), [&window] {
    QMessageBox::about(
        &window,
        muffin::MainWindow::tr("About Muffin"),
        muffin::MainWindow::tr("Muffin %1\n\nA fast, lightweight native Markdown editor built with C++ and Qt 6 Widgets.")
            .arg(QApplication::applicationVersion()));
  });

  window.commands_.bind(QStringLiteral("theme.github"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("github"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
  window.commands_.bind(QStringLiteral("theme.newsprint"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("newsprint"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
  window.commands_.bind(QStringLiteral("theme.night"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("night"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
  window.commands_.bind(QStringLiteral("theme.pixyll"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("pixyll"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
  window.commands_.bind(QStringLiteral("theme.whitey"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("whitey"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
}

void muffin::MainWindowActionBinder::restorePersistentActionStates(MainWindow& window) {
  QSettings settings;

  const int lineBreak = settings.value(QStringLiteral("editor/defaultLineBreak"), 1).toInt();
  if (QAction* crlf = window.commands_.action(QStringLiteral("edit.linebreak_crlf"))) {
    crlf->setChecked(lineBreak == 1);
  }
  if (QAction* lf = window.commands_.action(QStringLiteral("edit.linebreak_lf"))) {
    lf->setChecked(lineBreak == 0);
  }
  if (QAction* trailingNewline = window.commands_.action(QStringLiteral("edit.trailing_newline"))) {
    trailingNewline->setChecked(settings.value(QStringLiteral("editor/trailingNewline"), true).toBool());
  }

  const bool wordWrap = settings.value(QStringLiteral("view/wordWrap"), true).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.word_wrap"))) {
    action->setChecked(wordWrap);
  }
  const bool sidebarVisible = settings.value(QStringLiteral("view/sidebarVisible"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.sidebar"))) {
    action->setChecked(sidebarVisible);
  }
  const bool sourceMode = settings.value(QStringLiteral("view/sourceMode"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.source_mode"))) {
    action->setChecked(sourceMode);
  }
  const bool focusMode = settings.value(QStringLiteral("appearance/focusMode"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.focus"))) {
    action->setChecked(focusMode);
  }
  const bool typewriterMode = settings.value(QStringLiteral("appearance/typewriterMode"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.typewriter"))) {
    action->setChecked(typewriterMode);
  }
  const bool alwaysOnTop = settings.value(QStringLiteral("view/alwaysOnTop"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.always_on_top"))) {
    action->setChecked(alwaysOnTop);
    window.setWindowFlag(Qt::WindowStaysOnTopHint, alwaysOnTop);
    window.show();
  }

  // Image insert action radio group
  const int imageInsertAction = settings.value(QStringLiteral("image/insertAction"), 0).toInt();
  const QStringList insertActionIds = {
      QStringLiteral("image.insert_relative"),
      QStringLiteral("image.insert_absolute"),
      QStringLiteral("image.insert_copy"),
      QStringLiteral("image.insert_upload"),
  };
  for (int i = 0; i < insertActionIds.size(); ++i) {
    if (QAction* action = window.commands_.action(insertActionIds[i])) {
      action->setChecked(i == imageInsertAction);
    }
  }
}

void muffin::MainWindowActionBinder::updateFileActions(MainWindow& window) {
  const bool hasFile = !window.session_.filePath().isEmpty();
  window.commands_.setEnabled(QStringLiteral("file.properties"), hasFile);
  window.commands_.setEnabled(QStringLiteral("file.reveal"), hasFile);
  if (window.reopenEncodingMenu_) {
    window.reopenEncodingMenu_->setEnabled(hasFile);
  }
  window.commands_.setEnabled(QStringLiteral("file.move_to"), hasFile);
  window.commands_.setEnabled(QStringLiteral("file.save_all"), true);
  window.commands_.setEnabled(QStringLiteral("file.sidebar"), hasFile);
  window.commands_.setEnabled(QStringLiteral("file.delete"), hasFile);
  window.commands_.setEnabled(QStringLiteral("file.print"), hasFile);
}

void muffin::MainWindowActionBinder::updateEditActions(MainWindow& window) {
  const bool src = window.backend_->isSourceMode();
  const bool hasCursor = src ? true : window.editorController_.selection().hasCursor();
  const bool hasSelection = src
      ? window.editor_->editor()->textCursor().hasSelection()
      : hasCursor && !window.editorController_.selection().selection().isCollapsed();

  window.commands_.setEnabled(QStringLiteral("edit.delete"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.delete_block"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.delete_line"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.delete_format"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.delete_word"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.copy_plain"), hasSelection);
  window.commands_.setEnabled(QStringLiteral("edit.copy_markdown"), hasSelection);
  window.commands_.setEnabled(QStringLiteral("edit.copy_html"), hasSelection);
  window.commands_.setEnabled(QStringLiteral("edit.paste_plain"), true);
  window.commands_.setEnabled(QStringLiteral("edit.select_line"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.select_format"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.select_block"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.select_word"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.jump_doc_start"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.jump_doc_end"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.jump_line_start"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.jump_line_end"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.jump_selection"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.move_line_up"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.move_line_down"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.find"), true);
  window.commands_.setEnabled(QStringLiteral("edit.replace"), true);
  window.commands_.setEnabled(QStringLiteral("edit.find_next"), true);
  window.commands_.setEnabled(QStringLiteral("edit.find_previous"), true);
}

void muffin::MainWindowActionBinder::updateTableActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool enabled = !sourceMode && window.renderCommands_.hasCurrentTableCell();
  const QStringList ids = {
      QStringLiteral("table.insert_row_before"),
      QStringLiteral("table.insert_row_after"),
      QStringLiteral("table.delete_row"),
      QStringLiteral("table.move_row_up"),
      QStringLiteral("table.move_row_down"),
      QStringLiteral("table.insert_column_before"),
      QStringLiteral("table.insert_column_after"),
      QStringLiteral("table.delete_column"),
      QStringLiteral("table.move_column_left"),
      QStringLiteral("table.move_column_right"),
      QStringLiteral("table.align_left"),
      QStringLiteral("table.align_center"),
      QStringLiteral("table.align_right"),
      QStringLiteral("table.align_none"),
      QStringLiteral("table.copy_table"),
      QStringLiteral("table.format_source"),
      QStringLiteral("table.delete_table"),
  };
  for (const QString& id : ids) {
    window.commands_.setEnabled(id, enabled);
  }
  window.commands_.setEnabled(QStringLiteral("table.insert_table"), !sourceMode);
}

void muffin::MainWindowActionBinder::updateParagraphActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool editable = !sourceMode && window.renderCommands_.isOnEditableParagraphBlock();
  const int headingLevel = sourceMode ? -1 : window.renderCommands_.currentHeadingLevel();

  for (int level = 1; level <= 6; ++level) {
    const QString id = QStringLiteral("paragraph.heading_%1").arg(level);
    window.commands_.setEnabled(id, editable);
    window.commands_.setChecked(id, headingLevel == level);
  }

  window.commands_.setEnabled(QStringLiteral("paragraph.paragraph"), editable);
  window.commands_.setChecked(QStringLiteral("paragraph.paragraph"), headingLevel == 0 && editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.promote_heading"), editable && headingLevel > 1);
  window.commands_.setEnabled(QStringLiteral("paragraph.demote_heading"), editable && headingLevel > 0 && headingLevel < 6);

  const bool wysiwyg = !sourceMode;
  const bool inCodeBlock = wysiwyg && window.renderCommands_.isInCodeBlock();
  const bool inMathBlock = wysiwyg && window.renderCommands_.isInMathBlock();
  // Insert-paragraph-before/after also works adjacent to code/math blocks, so it is
  // enabled more broadly than the style-oriented `editable` predicate.
  const bool canInsertAround = editable || (wysiwyg && window.renderCommands_.canInsertParagraphAround());
  window.commands_.setEnabled(QStringLiteral("paragraph.math_block"), wysiwyg);
  window.commands_.setEnabled(QStringLiteral("paragraph.code_block"), wysiwyg);
  window.commands_.setChecked(QStringLiteral("paragraph.code_block"), inCodeBlock);
  window.commands_.setChecked(QStringLiteral("paragraph.math_block"), inMathBlock);
  window.commands_.setEnabled(QStringLiteral("paragraph.insert_before"), canInsertAround);
  window.commands_.setEnabled(QStringLiteral("paragraph.insert_after"), canInsertAround);
  window.commands_.setEnabled(QStringLiteral("paragraph.link_ref"), wysiwyg);
  window.commands_.setEnabled(QStringLiteral("paragraph.footnote"), wysiwyg);
  window.commands_.setEnabled(QStringLiteral("paragraph.hr"), wysiwyg);
  window.commands_.setEnabled(QStringLiteral("paragraph.toc"), wysiwyg);

  window.commands_.setEnabled(QStringLiteral("paragraph.quote"), editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.ordered_list"), editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.unordered_list"), editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.task_list"), editable);
}

void muffin::MainWindowActionBinder::updateCodeActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool codeActive = !sourceMode && window.renderCommands_.isInCodeBlock();
  const bool codeEditing = !sourceMode && window.renderCommands_.isEditingCodeBlock();
  window.commands_.setEnabled(QStringLiteral("code.enter_edit"), codeActive);
  window.commands_.setEnabled(QStringLiteral("code.exit_edit"), codeEditing);
  window.commands_.setEnabled(QStringLiteral("code.set_language"), codeActive || codeEditing);
}

void muffin::MainWindowActionBinder::updateHtmlActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool htmlActive = !sourceMode && window.renderCommands_.isInHtmlBlock();
  const bool htmlEditing = !sourceMode && window.renderCommands_.isEditingHtmlBlock();
  window.commands_.setEnabled(QStringLiteral("html.enter_edit"), htmlActive);
  window.commands_.setEnabled(QStringLiteral("html.exit_edit"), htmlEditing);
  window.commands_.setEnabled(QStringLiteral("html.set_source"), htmlActive || htmlEditing);
}

void muffin::MainWindowActionBinder::updateMathActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool mathActive = !sourceMode && window.renderCommands_.isInMathBlock();
  const bool mathEditing = !sourceMode && window.renderCommands_.isEditingMathBlock();
  window.commands_.setEnabled(QStringLiteral("math.enter_edit"), mathActive);
  window.commands_.setEnabled(QStringLiteral("math.exit_edit"), mathEditing);
  window.commands_.setEnabled(QStringLiteral("math.set_tex"), mathActive || mathEditing);
}

void muffin::MainWindowActionBinder::updateContextActions(MainWindow& window) {
  updateEditActions(window);
  updateTableActions(window);
  updateParagraphActions(window);
  updateCodeActions(window);
  updateHtmlActions(window);
  updateMathActions(window);
  updateImageActions(window);
  updateFormatActions(window);
}

void muffin::MainWindowActionBinder::updateFormatActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool editable = !sourceMode && window.renderCommands_.isInlineFormatEnabled();

  window.commands_.setEnabled(QStringLiteral("format.bold"), editable);
  window.commands_.setEnabled(QStringLiteral("format.italic"), editable);
  window.commands_.setEnabled(QStringLiteral("format.underline"), editable);
  window.commands_.setEnabled(QStringLiteral("format.code"), editable);
  window.commands_.setEnabled(QStringLiteral("format.inline_math"), editable);
  window.commands_.setEnabled(QStringLiteral("format.strike"), editable);

  if (editable) {
    const CursorFormatState fmt = window.renderCommands_.currentInlineFormats();
    window.commands_.setChecked(QStringLiteral("format.bold"), fmt.bold);
    window.commands_.setChecked(QStringLiteral("format.italic"), fmt.italic);
    window.commands_.setChecked(QStringLiteral("format.underline"), fmt.underline);
    window.commands_.setChecked(QStringLiteral("format.code"), fmt.code);
    window.commands_.setChecked(QStringLiteral("format.inline_math"), fmt.inlineMath);
    window.commands_.setChecked(QStringLiteral("format.strike"), fmt.strikethrough);
  } else {
    window.commands_.setChecked(QStringLiteral("format.bold"), false);
    window.commands_.setChecked(QStringLiteral("format.italic"), false);
    window.commands_.setChecked(QStringLiteral("format.underline"), false);
    window.commands_.setChecked(QStringLiteral("format.code"), false);
    window.commands_.setChecked(QStringLiteral("format.inline_math"), false);
    window.commands_.setChecked(QStringLiteral("format.strike"), false);
  }
}

void muffin::MainWindowActionBinder::updateImageActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool onImage = !sourceMode && window.renderCommands_.isOnImage();
  const QString imageSrc = onImage ? window.renderCommands_.imageSrcAtCursor() : QString();
  const bool isLocalImage = onImage && !imageSrc.isEmpty() && QFileInfo(imageSrc).isFile();

  window.commands_.setEnabled(QStringLiteral("image.insert"), !sourceMode);
  window.commands_.setEnabled(QStringLiteral("image.insert_local"), !sourceMode);
  window.commands_.setEnabled(QStringLiteral("image.open_location"), isLocalImage);
  window.commands_.setEnabled(QStringLiteral("image.copy_image"), onImage);
  window.commands_.setEnabled(QStringLiteral("image.delete_image"), isLocalImage);
  window.commands_.setEnabled(QStringLiteral("image.copy_to"), isLocalImage);
  window.commands_.setEnabled(QStringLiteral("image.move_to"), isLocalImage);
  window.commands_.setEnabled(QStringLiteral("image.upload"), isLocalImage);
  window.commands_.setEnabled(QStringLiteral("image.upload_all"), !sourceMode);
  window.commands_.setEnabled(QStringLiteral("image.reload_all"), !sourceMode);

  const QStringList resizeIds = {
      QStringLiteral("image.resize_25"),   QStringLiteral("image.resize_50"),
      QStringLiteral("image.resize_75"),   QStringLiteral("image.resize_100"),
      QStringLiteral("image.resize_150"),  QStringLiteral("image.resize_custom"),
  };
  for (const QString& id : resizeIds) {
    window.commands_.setEnabled(id, onImage);
  }

  window.commands_.setEnabled(QStringLiteral("image.to_standard"), onImage);
  window.commands_.setEnabled(QStringLiteral("image.to_html"), onImage);

  window.commands_.setEnabled(QStringLiteral("image.copy_all_to"), !sourceMode);
  window.commands_.setEnabled(QStringLiteral("image.move_all_to"), !sourceMode);
  window.commands_.setEnabled(QStringLiteral("image.global_settings"), false);
}

void muffin::MainWindowActionBinder::updateThemeActions(MainWindow& window) {
  const QString current = window.themeManager_.currentThemeName();
  window.commands_.setChecked(QStringLiteral("theme.github"), current == QStringLiteral("github"));
  window.commands_.setChecked(QStringLiteral("theme.newsprint"), current == QStringLiteral("newsprint"));
  window.commands_.setChecked(QStringLiteral("theme.night"), current == QStringLiteral("night"));
  window.commands_.setChecked(QStringLiteral("theme.pixyll"), current == QStringLiteral("pixyll"));
  window.commands_.setChecked(QStringLiteral("theme.whitey"), current == QStringLiteral("whitey"));
}
