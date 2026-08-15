#include "app/MainWindow.h"

#include "document/MarkdownDocument.h"
#include "editor/CursorPosition.h"
#include "editor/EditorView.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "spellcheck/SpellChecker.h"
#include "unicode/WordBoundary.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QSaveFile>
#include <QStatusBar>
#include <QString>
#include <QStringList>

#include <functional>
#include <initializer_list>
#include <utility>

// Rendered-mode right-click menu. Defined out-of-namespace (no `namespace muffin`
// wrapper) because this translation unit contains tr() calls — see the lupdate
// namespace-context rule in CLAUDE.md. The menu reuses the already-registered
// command actions from `commands_` (labels / shortcuts / handlers / enabled-
// state for free), keyed off the click's HitTestResult zone. The pattern mirrors
// MainWindowSignalBinder's tableMoreActionsRequested handler.

void muffin::MainWindow::buildEditorContextMenu(const HitTestResult& hit, QPoint globalPos) {
  // Refresh every command's enabled/checked predicate so the menu reflects the
  // current caret/selection/document state at exec time.
  updateAllActions();

  QMenu menu(this);

  const auto addCommand = [&menu, this](const QString& id) {
    if (QAction* action = commands_.action(id)) {
      menu.addAction(action);
    }
  };
  // Pull every listed command id into a submenu, skipping any that are not
  // registered (defensive: a typo should not abort the whole menu).
  const auto fill = [this](QMenu* sub, std::initializer_list<QString> ids) {
    for (const QString& id : ids) {
      if (QAction* action = commands_.action(id)) {
        sub->addAction(action);
      }
    }
  };

  // ---- [A] Spelling suggestions (word under the click is misspelled) ----------
  // Strings stay in the muffin::EditorView context so the existing translations
  // apply — the spell menu used to live in EditorView::contextMenuEvent.
  auto& checker = SpellChecker::instance();
  if (checker.isEnabled() && hit.zone == HitTestResult::Zone::Text && hit.sourceOffset >= 0) {
    const QString markdown = session_.document().markdownText().toString();
    const WordSegment seg = findWordSegment(markdown, hit.sourceOffset);
    if (seg.isWord && seg.end > seg.start && seg.start < markdown.size()) {
      const QString word = markdown.mid(seg.start, seg.end - seg.start);
      if (!word.isEmpty() && !checker.isCorrect(word)) {
        const QStringList suggestions = checker.suggestions(word);
        if (suggestions.isEmpty()) {
          QAction* none = menu.addAction(QCoreApplication::translate("muffin::EditorView", "(no spelling suggestions)"));
          none->setEnabled(false);
        } else {
          const qsizetype start = seg.start;
          const qsizetype length = seg.end - seg.start;
          const int cap = 8;
          for (int i = 0; i < qMin(suggestions.size(), cap); ++i) {
            const QString suggestion = suggestions.at(i);
            QAction* replace = menu.addAction(suggestion);
            connect(replace, &QAction::triggered, this, [this, start, length, suggestion]() {
              session_.applyTextDelta(start, length, suggestion, true);
            });
          }
        }
        QAction* ignore = menu.addAction(QCoreApplication::translate("muffin::EditorView", "Ignore \"%1\"").arg(word));
        connect(ignore, &QAction::triggered, this, [this, word]() {
          SpellChecker::instance().ignoreWord(word);
          renderView_->refreshVisibleBlocks(session_.document());  // clear the squiggle on screen
        });
        menu.addSeparator();
      }
    }
  }

  // ---- [B] Zone-specific head (link / image / table) -------------------------
  if (hit.mermaidRendered) {
    QAction* exportSvg = menu.addAction(tr("Export Mermaid as SVG..."));
    connect(exportSvg, &QAction::triggered, this,
            [this, blockId = hit.blockId] { exportMermaidDiagram(blockId); });
    menu.addSeparator();
  }
  if (!hit.linkHref.isEmpty()) {
    addCommand(QStringLiteral("link.open"));
    addCommand(QStringLiteral("link.copy_address"));
    addCommand(QStringLiteral("format.link"));
    menu.addSeparator();
  } else if (!hit.imageSrc.isEmpty()) {
    QMenu* imageMenu = menu.addMenu(tr("Image"));
    fill(imageMenu,
        {QStringLiteral("image.open_location"), QStringLiteral("image.copy_image")});
    imageMenu->addSeparator();
    QMenu* resizeMenu = imageMenu->addMenu(tr("Resize"));
    fill(resizeMenu,
        {QStringLiteral("image.resize_25"), QStringLiteral("image.resize_50"), QStringLiteral("image.resize_75"),
         QStringLiteral("image.resize_100"), QStringLiteral("image.resize_150"), QStringLiteral("image.resize_custom")});
    QMenu* convertMenu = imageMenu->addMenu(tr("Convert"));
    fill(convertMenu, {QStringLiteral("image.to_standard"), QStringLiteral("image.to_html")});
    imageMenu->addSeparator();
    fill(imageMenu,
        {QStringLiteral("image.upload"), QStringLiteral("image.move_to"), QStringLiteral("image.delete_image")});
    menu.addSeparator();
  } else if (hit.zone == HitTestResult::Zone::TableCell) {
    // Same id set + separator grouping as the table toolbar's "more" menu
    // (MainWindowSignalBinder::connectEditorSignals, tableMoreActionsRequested).
    QMenu* tableMenu = menu.addMenu(tr("Table"));
    fill(tableMenu, {QStringLiteral("table.insert_table")});
    tableMenu->addSeparator();
    fill(tableMenu, {QStringLiteral("table.insert_row_before"), QStringLiteral("table.insert_row_after")});
    tableMenu->addSeparator();
    fill(tableMenu, {QStringLiteral("table.insert_column_before"), QStringLiteral("table.insert_column_after")});
    tableMenu->addSeparator();
    fill(tableMenu,
        {QStringLiteral("table.move_row_up"), QStringLiteral("table.move_row_down"),
         QStringLiteral("table.move_column_left"), QStringLiteral("table.move_column_right")});
    tableMenu->addSeparator();
    fill(tableMenu, {QStringLiteral("table.delete_row"), QStringLiteral("table.delete_column")});
    tableMenu->addSeparator();
    fill(tableMenu, {QStringLiteral("table.copy_table"), QStringLiteral("table.format_source")});
    tableMenu->addSeparator();
    fill(tableMenu, {QStringLiteral("table.align_none")});
    tableMenu->addSeparator();
    fill(tableMenu, {QStringLiteral("table.delete_table")});
    menu.addSeparator();
  }

  // ---- [C] Text base (common tail for every zone) ----------------------------
  addCommand(QStringLiteral("edit.cut"));
  addCommand(QStringLiteral("edit.copy"));
  addCommand(QStringLiteral("edit.paste"));
  {
    QMenu* copyAs = menu.addMenu(tr("Copy as"));
    fill(copyAs,
        {QStringLiteral("edit.copy_markdown"), QStringLiteral("edit.copy_html"), QStringLiteral("edit.copy_plain")});
  }
  menu.addSeparator();

  addCommand(QStringLiteral("format.bold"));
  addCommand(QStringLiteral("format.italic"));
  addCommand(QStringLiteral("format.code"));
  addCommand(QStringLiteral("format.strike"));
  addCommand(QStringLiteral("format.underline"));
  addCommand(QStringLiteral("format.inline_math"));
  addCommand(QStringLiteral("format.link"));
  addCommand(QStringLiteral("image.insert"));
  addCommand(QStringLiteral("format.clear"));
  menu.addSeparator();

  {
    QMenu* paragraph = menu.addMenu(tr("Paragraph"));
    fill(paragraph,
        {QStringLiteral("paragraph.heading_1"), QStringLiteral("paragraph.heading_2"), QStringLiteral("paragraph.heading_3"),
         QStringLiteral("paragraph.heading_4"), QStringLiteral("paragraph.heading_5"), QStringLiteral("paragraph.heading_6")});
    paragraph->addSeparator();
    fill(paragraph, {QStringLiteral("paragraph.paragraph"), QStringLiteral("paragraph.quote")});
    paragraph->addSeparator();
    fill(paragraph,
        {QStringLiteral("paragraph.ordered_list"), QStringLiteral("paragraph.unordered_list"),
         QStringLiteral("paragraph.task_list")});
    paragraph->addSeparator();
    fill(paragraph,
        {QStringLiteral("paragraph.hr"), QStringLiteral("paragraph.code_block"), QStringLiteral("paragraph.math_block"),
         QStringLiteral("table.insert_table")});
    paragraph->addSeparator();
    fill(paragraph, {QStringLiteral("paragraph.footnote"), QStringLiteral("paragraph.toc")});
  }
  menu.addSeparator();

  {
    QMenu* select = menu.addMenu(tr("Select"));
    fill(select,
        {QStringLiteral("edit.select_word"), QStringLiteral("edit.select_line"), QStringLiteral("edit.select_block"),
         QStringLiteral("edit.select_format")});
  }
  addCommand(QStringLiteral("edit.move_line_up"));
  addCommand(QStringLiteral("edit.move_line_down"));
  menu.addSeparator();

  addCommand(QStringLiteral("edit.find"));
  addCommand(QStringLiteral("edit.replace"));

  menu.exec(globalPos);
}

void muffin::MainWindow::exportMermaidDiagram(NodeId blockId) {
  const MarkdownNode* node = session_.document().node(blockId);
  if (!node || node->type() != BlockType::CodeFence ||
      node->codeLanguage() != QLatin1String("mermaid")) {
    return;
  }

  const QFileInfo documentInfo(session_.filePath());
  const QString baseName = documentInfo.completeBaseName().isEmpty()
      ? QStringLiteral("diagram")
      : documentInfo.completeBaseName() + QStringLiteral("-diagram");
  const QString directory = session_.filePath().isEmpty()
      ? defaultSaveDirectory()
      : documentInfo.absolutePath();
  const QString initialPath = directory.isEmpty()
      ? baseName + QStringLiteral(".svg")
      : QDir(directory).filePath(baseName + QStringLiteral(".svg"));
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export As"), initialPath, QStringLiteral("SVG (*.svg)"));
  if (path.isEmpty()) return;
  if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".svg");

  const auto rendered =
      mermaid::editor::MermaidRenderCache::renderMermaidSourceToSvg(
          node->literal(), 0, QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
  if (rendered.svg.isEmpty()) return;

  QSaveFile file(path);
  const bool opened = file.open(QIODevice::WriteOnly);
  const bool written = opened && file.write(rendered.svg) == rendered.svg.size();
  const bool committed = written && file.commit();
  if (!committed) {
    QMessageBox::warning(this, tr("Export Failed"), file.errorString());
    return;
  }
  statusBar()->showMessage(
      tr("Exported to %1").arg(QDir::toNativeSeparators(path)),
      5000);
}

// Sidebar file-tree right-click menu. Built from ad-hoc actions (no command
// registry) since every entry targets a specific path rather than the open
// document. The variant is decided from `onItem`/`isDir`: a file gets the full
// set, a directory or empty space gets the reduced set (per spec). New File /
// New Folder target the clicked directory, or the file's parent for a file.
void muffin::MainWindow::buildSidebarContextMenu(QString path, bool isDir, bool onItem, QPoint globalPos) {
  QMenu menu(this);
  const QString targetDir = isDir ? path : QFileInfo(path).absolutePath();

  const auto add = [&menu, this](const QString& label, std::function<void()> slot) {
    QAction* action = menu.addAction(label);
    connect(action, &QAction::triggered, this, [this, slot = std::move(slot)] { slot(); });
    return action;
  };

  if (onItem && !isDir) {
    add(tr("Open"), [this, path] { openFile(path); });
    add(tr("Open in New Window"), [this, path] { openFileInNewWindow(path); });
    menu.addSeparator();
    add(tr("New File"), [this, targetDir] { newFileInDirectory(targetDir); });
    add(tr("New Folder"), [this, targetDir] { newFolderInDirectory(targetDir); });
    menu.addSeparator();
    add(tr("Rename"), [this, path] { renamePath(path); });
    add(tr("Duplicate"), [this, path] { duplicateFile(path); });
    add(tr("Delete"), [this, path] { deletePath(path); });
    menu.addSeparator();
    add(tr("Properties"), [this, path] { showPathProperties(path); });
    menu.addSeparator();
    add(tr("Copy Path"), [this, path] { copyPathToClipboard(path); });
    add(tr("Reveal in File Manager"), [this, path] { revealPathInManager(path); });
  } else {
    // Directory, or empty space (path is then the folder root).
    add(tr("Open in New Window"), [this, path] { openFolderInNewWindow(path); });
    menu.addSeparator();
    add(tr("New File"), [this, targetDir] { newFileInDirectory(targetDir); });
    add(tr("New Folder"), [this, targetDir] { newFolderInDirectory(targetDir); });
    menu.addSeparator();
    add(tr("Copy Path"), [this, path] { copyPathToClipboard(path); });
    add(tr("Reveal in File Manager"), [this, path] { revealPathInManager(path); });
  }

  menu.exec(globalPos);
}
