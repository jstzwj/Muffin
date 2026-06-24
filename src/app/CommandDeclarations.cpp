#include "app/CommandDeclarations.h"

#include "app/MainWindow.h"
#include "app/MarkdownSettings.h"
#include "app/HelpViewerDialog.h"
#include "app/UpdateChecker.h"
#include "app/SidebarWidget.h"
#include "document/MarkdownTypes.h"
#include "editor/EditorView.h"
#include "export/ExportFormat.h"
#include "editor/FindBarWidget.h"
#include "editor/ResourceUrl.h"
#include "editor/SourceEditorWidget.h"
#include "image/CustomCommandUploader.h"
#include "io/ImageFileOps.h"
#include "spellcheck/SpellChecker.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QSysInfo>
#include <QUrl>

namespace muffin {

namespace {
// Thin wrappers over MainWindow's composite state queries so predicate lambdas
// read as the old per-domain update*Actions did (e.g. `selectionPresent(w)`).
bool cursorPresent(const MainWindow& w) { return w.commandHasCursor(); }
bool selectionPresent(const MainWindow& w) { return w.commandHasSelection(); }
bool editableParagraph(const MainWindow& w) { return w.commandOnEditableParagraph(); }
int headingLevel(const MainWindow& w) { return w.commandHeadingLevel(); }
bool inlineFormat(const MainWindow& w) { return w.commandInlineFormatEnabled(); }
bool inTableCell(const MainWindow& w) { return w.commandInTableCell(); }
bool onImage(const MainWindow& w) { return w.commandOnImage(); }
bool localImageAtCursor(const MainWindow& w) { return w.commandOnLocalImage(); }
}  // namespace

// ---- Command table ----------------------------------------------------------
//
// One row per command. Handlers/predicates take MainWindow& / const MainWindow&
// (the table is built friend-accessible, see MainWindow.h). Handler bodies are
// kept verbatim from the former bindCommands; the only change is the lambda now
// receives `window` as a parameter instead of capturing it.
namespace {
// Rebuildable caches: the tables are built lazily and kept across cursor moves
// (predicates are hot), but invalidated by refreshDeclarations() so a locale
// change re-evaluates every tr() label.
std::vector<CommandDeclaration>& commandTable() {
  static std::vector<CommandDeclaration> table;
  return table;
}
bool& commandsValid() {
  static bool valid = false;
  return valid;
}
std::vector<MenuSpec>& menuSpecTable() {
  static std::vector<MenuSpec> table;
  return table;
}
bool& menuSpecValid() {
  static bool valid = false;
  return valid;
}
}  // namespace

const std::vector<CommandDeclaration>& commandDeclarations() {
  if (!commandsValid()) {
    commandTable() = std::vector<CommandDeclaration>{
      // ---------------- File ----------------
      {.id = QStringLiteral("file.new"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("New"),
       .shortcut = QKeySequence::New,
       .handler = [](MainWindow& window) {
         window.editorController_.clearHistoryAndSelection();
         window.fileController_.newFile(window.session_, &window);
       }},
      {.id = QStringLiteral("file.new_window"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("New Window"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+N")),
       .handler = [](MainWindow& window) { window.openNewWindow(); }},
      {.id = QStringLiteral("file.open_folder"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Open Folder..."),
       .handler = [](MainWindow& window) { window.openFolder(); }},
      {.id = QStringLiteral("file.open"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Open..."),
       .shortcut = QKeySequence::Open,
       .handler = [](MainWindow& window) {
         if (window.fileController_.open(window.session_, &window)) {
           window.editorController_.clearHistoryAndSelection();
           window.addRecentFile(window.session_.filePath());
         }
       }},
      {.id = QStringLiteral("file.quick_open"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Quick Open..."),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+P")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.quickOpen(); },
       .enabled = [](const MainWindow& w) {
         return !w.recentFiles().isEmpty() || !w.session_.filePath().isEmpty();
       }},
      {.id = QStringLiteral("file.save"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Save"),
       .shortcut = QKeySequence::Save,
       .handler = [](MainWindow& window) {
         if (window.fileController_.save(window.session_, &window)) {
           window.addRecentFile(window.session_.filePath());
         }
       }},
      {.id = QStringLiteral("file.save_as"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Save As..."),
       .shortcut = QKeySequence::SaveAs,
       .handler = [](MainWindow& window) {
         if (window.fileController_.saveAs(window.session_, &window)) {
           window.addRecentFile(window.session_.filePath());
         }
       }},
      {.id = QStringLiteral("file.move_to"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Move To..."),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.moveToFile(); },
       .enabled = [](const MainWindow& w) { return !w.session_.filePath().isEmpty(); }},
      {.id = QStringLiteral("file.save_all"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Save All Open Files..."),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.saveAllOpenFiles(); },
       .enabled = [](const MainWindow&) { return true; }},
      {.id = QStringLiteral("file.properties"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Properties"),
       .handler = [](MainWindow& window) { window.showDocumentProperties(); },
       .enabled = [](const MainWindow& w) { return !w.session_.filePath().isEmpty(); }},
      {.id = QStringLiteral("file.reveal"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Show in File Manager..."),
       .handler = [](MainWindow& window) { window.revealCurrentFile(); },
       .enabled = [](const MainWindow& w) { return !w.session_.filePath().isEmpty(); }},
      {.id = QStringLiteral("file.sidebar"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Show in Sidebar"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.showInSidebar(); },
       .enabled = [](const MainWindow& w) { return !w.session_.filePath().isEmpty(); }},
      {.id = QStringLiteral("file.delete"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Delete..."),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.deleteFile(); },
       .enabled = [](const MainWindow& w) { return !w.session_.filePath().isEmpty(); }},
      {.id = QStringLiteral("file.import"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Import..."),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.importFile(); },
       .enabled = [](const MainWindow&) { return true; }},
      {.id = QStringLiteral("file.print"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Print..."),
       .shortcut = QKeySequence(QStringLiteral("Alt+Shift+P")),
       .handler = [](MainWindow& window) { window.printDocument(); },
       .enabled = [](const MainWindow& w) { return !w.session_.filePath().isEmpty(); }},
      // ---- Export (native PDF/HTML; Pandoc-driven for the rest) ----
      {.id = QStringLiteral("file.export_pdf"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("PDF"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Pdf); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_html"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("HTML"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Html); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_html_plain"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("HTML (without Styles)"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::HtmlPlain); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_docx"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Word (.docx)"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Docx); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_odt"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("OpenOffice"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Odt); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_rtf"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("RTF"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Rtf); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_epub"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Epub"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Epub); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_latex"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("LaTeX"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Latex); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_mediawiki"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Media Wiki"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::MediaWiki); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_rst"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("reStructuredText"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Rst); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_textile"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Textile"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Textile); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.export_opml"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("OPML"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.exportAs(ExportFormat::Opml); },
       .enabled = [](const MainWindow& w) { return !w.session_.markdownText().isEmpty(); }},
      {.id = QStringLiteral("file.preferences"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Preferences..."),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+,")),
       .handler = [](MainWindow& window) { window.showPreferences(); }},
      {.id = QStringLiteral("file.close"),
       .category = CommandCategory::File,
       .text = muffin::MainWindow::tr("Close"),
       .shortcut = QKeySequence::Close,
       .handler = [](MainWindow& window) { window.close(); }},

      // ---------------- Edit ----------------
      {.id = QStringLiteral("edit.undo"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Undo"),
       .shortcut = QKeySequence::Undo,
       .handler = [](MainWindow& window) { window.undoEdit(); }},
      {.id = QStringLiteral("edit.redo"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Redo"),
       .shortcut = QKeySequence::Redo,
       .handler = [](MainWindow& window) { window.redoEdit(); }},
      {.id = QStringLiteral("edit.cut"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Cut"),
       .shortcut = QKeySequence::Cut,
       .handler = [](MainWindow& window) { window.backend_->cut(); }},
      {.id = QStringLiteral("edit.copy"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Copy"),
       .shortcut = QKeySequence::Copy,
       .handler = [](MainWindow& window) { window.backend_->copy(); }},
      {.id = QStringLiteral("link.open"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Open Link"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) {
         const QString href = window.renderView_->cursorHit().linkHref;
         if (href.isEmpty()) {
           return;
         }
         QDesktopServices::openUrl(resolvedUrlForDocumentResource(href, window.session_.filePath()));
       },
       .enabled = [](const MainWindow& w) {
         return !w.backend_->isSourceMode() && !w.renderView_->cursorHit().linkHref.isEmpty();
       }},
      {.id = QStringLiteral("link.copy_address"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Copy Link Address"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) {
         const QString href = window.renderView_->cursorHit().linkHref;
         if (!href.isEmpty()) {
           QApplication::clipboard()->setText(href);
         }
       },
       .enabled = [](const MainWindow& w) {
         return !w.backend_->isSourceMode() && !w.renderView_->cursorHit().linkHref.isEmpty();
       }},
      {.id = QStringLiteral("edit.paste"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Paste"),
       .shortcut = QKeySequence::Paste,
       .handler = [](MainWindow& window) { window.backend_->paste(); }},
      {.id = QStringLiteral("edit.copy_plain"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Copy as Plain Text"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->copyAsPlainText(); },
       .enabled = [](const MainWindow& w) { return selectionPresent(w); }},
      {.id = QStringLiteral("edit.copy_markdown"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Copy as Markdown"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+C")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->copyAsMarkdown(); },
       .enabled = [](const MainWindow& w) { return selectionPresent(w); }},
      {.id = QStringLiteral("edit.copy_html"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Copy as HTML"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->copyAsHtml(); },
       .enabled = [](const MainWindow& w) { return selectionPresent(w); }},
      {.id = QStringLiteral("edit.paste_plain"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Paste as Plain Text"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+V")),
       .handler = [](MainWindow& window) { window.backend_->pasteAsPlainText(); }},
      {.id = QStringLiteral("edit.select_all"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Select All"),
       .shortcut = QKeySequence::SelectAll,
       .handler = [](MainWindow& window) { window.backend_->selectAll(); }},
      {.id = QStringLiteral("edit.select_block"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Select Paragraph or Block"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Alt+P")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->selectBlock(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.select_line"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Select Current Line or Sentence"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+L")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->selectLine(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.select_format"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Select Current Format Text"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+E")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->selectFormatSpan(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.select_word"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Select Current Word"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+D")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->selectWord(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.jump_doc_start"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Jump to Start of Document"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Home")),
       .shortcutWidgetContext = true,
       .handler = [](MainWindow& window) { window.backend_->moveDocumentStart(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.jump_selection"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Jump to Selection"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+J")),
       .handler = [](MainWindow& window) { window.backend_->selectNextOccurrence(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.jump_doc_end"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Jump to End of Document"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+End")),
       .shortcutWidgetContext = true,
       .handler = [](MainWindow& window) { window.backend_->moveDocumentEnd(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.jump_line_start"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Jump to Start of Line"),
       .shortcut = QKeySequence(QStringLiteral("Home")),
       .shortcutWidgetContext = true,
       .handler = [](MainWindow& window) { window.backend_->moveLineStart(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.jump_line_end"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Jump to End of Line"),
       .shortcut = QKeySequence(QStringLiteral("End")),
       .shortcutWidgetContext = true,
       .handler = [](MainWindow& window) { window.backend_->moveLineEnd(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.move_line_up"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Move Line Up"),
       .shortcut = QKeySequence(QStringLiteral("Alt+Up")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->moveLineUp(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.move_line_down"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Move Line Down"),
       .shortcut = QKeySequence(QStringLiteral("Alt+Down")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->moveLineDown(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.delete"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Delete"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->deleteForward(); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.delete_block"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Delete Block"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->deleteRange(DeleteTarget::Block); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.delete_line"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Delete Current Line"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->deleteRange(DeleteTarget::Line); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.delete_format"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Delete Current Format Text"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->deleteRange(DeleteTarget::FormatSpan); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.delete_word"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Delete Current Word"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->deleteRange(DeleteTarget::Word); },
       .enabled = [](const MainWindow& w) { return cursorPresent(w); }},
      {.id = QStringLiteral("edit.spellcheck"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Spell Check"),
       .checkable = true,
       .handler = [](MainWindow& window) {
         if (QAction* action = window.commands_.action(QStringLiteral("edit.spellcheck"))) {
           SpellChecker::instance().setEnabled(action->isChecked());
         }
       },
       .enabled = [](const MainWindow&) { return true; },
       .checked = [](const MainWindow&) { return SpellChecker::instance().isEnabled(); }},
      {.id = QStringLiteral("edit.linebreak_crlf"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Windows (CRLF)"),
       .checkable = true,
       .checkedInitial = true,
       .handler = [](MainWindow& window) {
         window.commands_.setChecked(QStringLiteral("edit.linebreak_crlf"), true);
         window.commands_.setChecked(QStringLiteral("edit.linebreak_lf"), false);
         QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 1);
       }},
      {.id = QStringLiteral("edit.linebreak_lf"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Unix (LF)"),
       .checkable = true,
       .checkedInitial = false,
       .handler = [](MainWindow& window) {
         window.commands_.setChecked(QStringLiteral("edit.linebreak_lf"), true);
         window.commands_.setChecked(QStringLiteral("edit.linebreak_crlf"), false);
         QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 0);
       }},
      {.id = QStringLiteral("edit.trailing_newline"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Ensure Trailing Newline on Save"),
       .checkable = true,
       .checkedInitial = true,
       .handler = [](MainWindow& window) {
         const bool checked = window.commands_.action(QStringLiteral("edit.trailing_newline"))->isChecked();
         QSettings().setValue(QStringLiteral("editor/trailingNewline"), checked);
       }},
      {.id = QStringLiteral("edit.find"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Find..."),
       .shortcut = QKeySequence::Find,
       .handler = [](MainWindow& window) { window.showFindBar(); }},
      {.id = QStringLiteral("edit.replace"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Replace..."),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+H")),
       .handler = [](MainWindow& window) { window.showReplaceBar(); }},
      {.id = QStringLiteral("edit.find_next"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Find Next"),
       .shortcut = QKeySequence(QStringLiteral("F3")),
       .handler = [](MainWindow& window) {
         if (!window.findBar_ || !window.findBar_->isVisible()) {
           window.showFindBar();
         }
         window.performFindNext();
       }},
      {.id = QStringLiteral("edit.find_previous"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Find Previous"),
       .shortcut = QKeySequence(QStringLiteral("Shift+F3")),
       .handler = [](MainWindow& window) {
         if (!window.findBar_ || !window.findBar_->isVisible()) {
           window.showFindBar();
         }
         window.performFindPrevious();
       }},

      // ---------------- Paragraph ----------------
      {.id = QStringLiteral("paragraph.heading_1"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Heading 1"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+1")),
       .checkable = true,
       .handler = [level = 1](MainWindow& window) { window.renderCommands_.setHeadingLevel(level); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); },
       .checked = [](const MainWindow& w) { return headingLevel(w) == 1; }},
      {.id = QStringLiteral("paragraph.heading_2"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Heading 2"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+2")),
       .checkable = true,
       .handler = [level = 2](MainWindow& window) { window.renderCommands_.setHeadingLevel(level); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); },
       .checked = [](const MainWindow& w) { return headingLevel(w) == 2; }},
      {.id = QStringLiteral("paragraph.heading_3"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Heading 3"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+3")),
       .checkable = true,
       .handler = [level = 3](MainWindow& window) { window.renderCommands_.setHeadingLevel(level); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); },
       .checked = [](const MainWindow& w) { return headingLevel(w) == 3; }},
      {.id = QStringLiteral("paragraph.heading_4"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Heading 4"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+4")),
       .checkable = true,
       .handler = [level = 4](MainWindow& window) { window.renderCommands_.setHeadingLevel(level); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); },
       .checked = [](const MainWindow& w) { return headingLevel(w) == 4; }},
      {.id = QStringLiteral("paragraph.heading_5"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Heading 5"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+5")),
       .checkable = true,
       .handler = [level = 5](MainWindow& window) { window.renderCommands_.setHeadingLevel(level); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); },
       .checked = [](const MainWindow& w) { return headingLevel(w) == 5; }},
      {.id = QStringLiteral("paragraph.heading_6"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Heading 6"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+6")),
       .checkable = true,
       .handler = [level = 6](MainWindow& window) { window.renderCommands_.setHeadingLevel(level); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); },
       .checked = [](const MainWindow& w) { return headingLevel(w) == 6; }},
      {.id = QStringLiteral("paragraph.paragraph"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Paragraph"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+0")),
       .checkable = true,
       .checkedInitial = true,
       .handler = [](MainWindow& window) { window.renderCommands_.setHeadingLevel(0); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); },
       .checked = [](const MainWindow& w) { return headingLevel(w) == 0 && editableParagraph(w); }},
      {.id = QStringLiteral("paragraph.promote_heading"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Promote Heading"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+-")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.promoteHeading(); },
       .enabled = [](const MainWindow& w) {
         const int lvl = headingLevel(w);
         return editableParagraph(w) && lvl > 1;
       }},
      {.id = QStringLiteral("paragraph.demote_heading"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Demote Heading"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+=")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.demoteHeading(); },
       .enabled = [](const MainWindow& w) {
         const int lvl = headingLevel(w);
         return editableParagraph(w) && lvl > 0 && lvl < 6;
       }},
      {.id = QStringLiteral("paragraph.math_block"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Formula Block"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+M")),
       .checkable = true,
       .handler = [](MainWindow& window) { window.renderCommands_.toggleFormulaBlock(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); },
       .checked = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isInMathBlock(); }},
      {.id = QStringLiteral("paragraph.code_block"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Code Block"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+K")),
       .checkable = true,
       .handler = [](MainWindow& window) { window.renderCommands_.toggleCodeBlock(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); },
       .checked = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isInCodeBlock(); }},
      {.id = QStringLiteral("paragraph.quote"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Quote"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+Q")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.toggleQuote(); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); }},
      {.id = QStringLiteral("paragraph.ordered_list"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Ordered List"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+[")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.convertToOrderedList(); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); }},
      {.id = QStringLiteral("paragraph.unordered_list"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Unordered List"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+]")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.convertToUnorderedList(); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); }},
      {.id = QStringLiteral("paragraph.task_list"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Task List"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+X")),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.convertToTaskList(); },
       .enabled = [](const MainWindow& w) { return editableParagraph(w); }},
      {.id = QStringLiteral("paragraph.insert_before"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Insert Paragraph Before"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertParagraphBefore(); },
       .enabled = [](const MainWindow& w) {
         return editableParagraph(w) || (!w.backend_->isSourceMode() && w.renderCommands_.canInsertParagraphAround());
       }},
      {.id = QStringLiteral("paragraph.insert_after"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Insert Paragraph After"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertParagraphAfter(); },
       .enabled = [](const MainWindow& w) {
         return editableParagraph(w) || (!w.backend_->isSourceMode() && w.renderCommands_.canInsertParagraphAround());
       }},
      {.id = QStringLiteral("paragraph.link_ref"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Link Reference"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertLinkReference(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("paragraph.footnote"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Footnote"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertFootnoteDefinition(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("paragraph.hr"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Horizontal Rule"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertHorizontalRule(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("paragraph.toc"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Table of Contents"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertTableOfContents(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("paragraph.yaml"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("YAML"),
       .handler = [](MainWindow& window) { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Yaml); }},
      {.id = QStringLiteral("paragraph.toml"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("TOML"),
       .handler = [](MainWindow& window) { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Toml); }},
      {.id = QStringLiteral("paragraph.json"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("JSON"),
       .handler = [](MainWindow& window) { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Json); }},

      // Alert blocks (GFM [!NOTE]/[!TIP]/[!IMPORTANT]/[!WARNING]/[!CAUTION]).
      {.id = QStringLiteral("paragraph.insert_alert_note"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Note"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertAlert(AlertKind::Note); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("paragraph.insert_alert_tip"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Tip"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertAlert(AlertKind::Tip); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("paragraph.insert_alert_important"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Important"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertAlert(AlertKind::Important); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("paragraph.insert_alert_warning"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Warning"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertAlert(AlertKind::Warning); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("paragraph.insert_alert_caution"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Caution"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.insertAlert(AlertKind::Caution); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},

      // Task status toggle (menu entry; the rendered-checkbox click path calls
      // toggleTaskListItem directly with the clicked block id).
      {.id = QStringLiteral("paragraph.task_toggle"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Toggle Task Status"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.toggleCurrentTaskListItem(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isOnTaskItem(); }},

      // List indent/outdent (menu entry; Tab/Shift+Tab already drive these in the editor).
      {.id = QStringLiteral("paragraph.indent_list"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Indent"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.indentListItem(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isOnListItem(); }},
      {.id = QStringLiteral("paragraph.outdent_list"),
       .category = CommandCategory::Paragraph,
       .text = muffin::MainWindow::tr("Outdent"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.outdentListItem(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isOnListItem(); }},

      // ---------------- Table ----------------
      {.id = QStringLiteral("table.insert_table"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Insert Table"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+T")),
       .handler = [](MainWindow& window) { window.insertTableWithDialog(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("table.insert_row_before"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Insert Row Above"),
       .handler = [](MainWindow& window) { window.renderCommands_.insertRowBefore(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.insert_row_after"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Insert Row Below"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Enter")),
       .handler = [](MainWindow& window) { window.renderCommands_.insertRowAfter(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.insert_column_before"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Insert Column Left"),
       .handler = [](MainWindow& window) { window.renderCommands_.insertColumnBefore(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.insert_column_after"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Insert Column Right"),
       .handler = [](MainWindow& window) { window.renderCommands_.insertColumnAfter(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.move_row_up"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Move Row Up"),
       .handler = [](MainWindow& window) { window.renderCommands_.moveCurrentRowUp(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.move_row_down"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Move Row Down"),
       .handler = [](MainWindow& window) { window.renderCommands_.moveCurrentRowDown(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.move_column_left"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Move Column Left"),
       .shortcut = QKeySequence(QStringLiteral("Alt+Left")),
       .handler = [](MainWindow& window) { window.renderCommands_.moveCurrentColumnLeft(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.move_column_right"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Move Column Right"),
       .shortcut = QKeySequence(QStringLiteral("Alt+Right")),
       .handler = [](MainWindow& window) { window.renderCommands_.moveCurrentColumnRight(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.delete_row"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Delete Row"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+Backspace")),
       .handler = [](MainWindow& window) { window.renderCommands_.deleteCurrentRow(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.delete_column"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Delete Column"),
       .handler = [](MainWindow& window) { window.renderCommands_.deleteCurrentColumn(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.copy_table"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Copy Table"),
       .handler = [](MainWindow& window) { window.renderCommands_.copyCurrentTable(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.format_source"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Format Table Source"),
       .handler = [](MainWindow& window) { window.renderCommands_.formatCurrentTableSource(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.delete_table"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Delete Table"),
       .handler = [](MainWindow& window) { window.renderCommands_.deleteCurrentTable(); },
       .enabled = [](const MainWindow& w) { return inTableCell(w); }},
      {.id = QStringLiteral("table.align_left"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Align Left"),
       .handler = [](MainWindow& window) { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Left); }},
      {.id = QStringLiteral("table.align_center"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Align Center"),
       .handler = [](MainWindow& window) { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Center); }},
      {.id = QStringLiteral("table.align_right"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Align Right"),
       .handler = [](MainWindow& window) { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Right); }},
      {.id = QStringLiteral("table.align_none"),
       .category = CommandCategory::Table,
       .text = muffin::MainWindow::tr("Clear Alignment"),
       .handler = [](MainWindow& window) { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::None); }},

      // ---------------- Code ----------------
      {.id = QStringLiteral("code.enter_edit"),
       .category = CommandCategory::Code,
       .text = muffin::MainWindow::tr("Enter Edit"),
       .handler = [](MainWindow& window) { window.renderCommands_.enterCodeEditMode(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isInCodeBlock(); }},
      {.id = QStringLiteral("code.exit_edit"),
       .category = CommandCategory::Code,
       .text = muffin::MainWindow::tr("Exit Edit"),
       .handler = [](MainWindow& window) { window.renderCommands_.exitCodeEditMode(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isEditingCodeBlock(); }},
      {.id = QStringLiteral("code.set_language"),
       .category = CommandCategory::Code,
       .text = muffin::MainWindow::tr("Set Language..."),
       .handler = [](MainWindow& window) {
         bool ok = false;
         const QString language = QInputDialog::getText(
             &window, muffin::MainWindow::tr("Code Language"), muffin::MainWindow::tr("Language:"), QLineEdit::Normal, QString(), &ok);
         if (ok) {
           window.renderCommands_.setCodeLanguage(language);
         }
       },
       .enabled = [](const MainWindow& w) {
         return !w.backend_->isSourceMode() && (w.renderCommands_.isInCodeBlock() || w.renderCommands_.isEditingCodeBlock());
       }},
      {.id = QStringLiteral("code.copy_content"),
       .category = CommandCategory::Code,
       .text = muffin::MainWindow::tr("Copy Code Block Content"),
       .handler = [](MainWindow& window) {
         const QString content = window.renderCommands_.codeContentAtCursor();
         if (!content.isEmpty()) {
           QApplication::clipboard()->setText(content);
         }
       },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isInCodeBlock(); }},
      {.id = QStringLiteral("code.indent_selection"),
       .category = CommandCategory::Code,
       .text = muffin::MainWindow::tr("Indent Selection"),
       .handler = [](MainWindow& window) { window.renderCommands_.indentCodeSelection(); },
       .enabled = [](const MainWindow& w) {
         return !w.backend_->isSourceMode() && w.renderCommands_.isInCodeBlock() && w.commandHasSelection();
       }},
      {.id = QStringLiteral("code.dedent_selection"),
       .category = CommandCategory::Code,
       .text = muffin::MainWindow::tr("Dedent Selection"),
       .handler = [](MainWindow& window) { window.renderCommands_.dedentCodeSelection(); },
       .enabled = [](const MainWindow& w) {
         return !w.backend_->isSourceMode() && w.renderCommands_.isInCodeBlock() && w.commandHasSelection();
       }},
      {.id = QStringLiteral("code.indent_block"),
       .category = CommandCategory::Code,
       .text = muffin::MainWindow::tr("Indent Whole Block"),
       .handler = [](MainWindow& window) { window.renderCommands_.indentCodeBlock(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isInCodeBlock(); }},
      {.id = QStringLiteral("code.dedent_block"),
       .category = CommandCategory::Code,
       .text = muffin::MainWindow::tr("Dedent Whole Block"),
       .handler = [](MainWindow& window) { window.renderCommands_.dedentCodeBlock(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isInCodeBlock(); }},

      // ---------------- HTML ----------------
      {.id = QStringLiteral("html.enter_edit"),
       .category = CommandCategory::Html,
       .text = muffin::MainWindow::tr("Enter Edit"),
       .handler = [](MainWindow& window) { window.renderCommands_.enterHtmlEditMode(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isInHtmlBlock(); }},
      {.id = QStringLiteral("html.exit_edit"),
       .category = CommandCategory::Html,
       .text = muffin::MainWindow::tr("Exit Edit"),
       .handler = [](MainWindow& window) { window.renderCommands_.exitHtmlEditMode(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isEditingHtmlBlock(); }},
      {.id = QStringLiteral("html.set_source"),
       .category = CommandCategory::Html,
       .text = muffin::MainWindow::tr("Set HTML..."),
       .handler = [](MainWindow& window) {
         bool ok = false;
         const QString html =
             QInputDialog::getMultiLineText(&window, muffin::MainWindow::tr("HTML Source"), muffin::MainWindow::tr("HTML:"), QString(), &ok);
         if (ok) {
           window.renderCommands_.setHtmlSource(html);
         }
       },
       .enabled = [](const MainWindow& w) {
         return !w.backend_->isSourceMode() && (w.renderCommands_.isInHtmlBlock() || w.renderCommands_.isEditingHtmlBlock());
       }},

      // ---------------- Math ----------------
      {.id = QStringLiteral("math.enter_edit"),
       .category = CommandCategory::Math,
       .text = muffin::MainWindow::tr("Enter Edit"),
       .handler = [](MainWindow& window) { window.renderCommands_.enterMathEditMode(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isInMathBlock(); }},
      {.id = QStringLiteral("math.exit_edit"),
       .category = CommandCategory::Math,
       .text = muffin::MainWindow::tr("Exit Edit"),
       .handler = [](MainWindow& window) { window.renderCommands_.exitMathEditMode(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode() && w.renderCommands_.isEditingMathBlock(); }},
      {.id = QStringLiteral("math.set_tex"),
       .category = CommandCategory::Math,
       .text = muffin::MainWindow::tr("Set TeX..."),
       .handler = [](MainWindow& window) {
         bool ok = false;
         const QString tex =
             QInputDialog::getMultiLineText(&window, muffin::MainWindow::tr("Math TeX"), muffin::MainWindow::tr("TeX:"), QString(), &ok);
         if (ok) {
           window.renderCommands_.setMathTex(tex);
         }
       },
       .enabled = [](const MainWindow& w) {
         return !w.backend_->isSourceMode() && (w.renderCommands_.isInMathBlock() || w.renderCommands_.isEditingMathBlock());
       }},

      // ---------------- Format ----------------
      {.id = QStringLiteral("format.bold"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Bold"),
       .shortcut = QKeySequence::Bold,
       .checkable = true,
       .handler = [](MainWindow& window) { window.backend_->toggleBold(); },
       .enabled = [](const MainWindow& w) { return inlineFormat(w); },
       .checked = [](const MainWindow& w) { return inlineFormat(w) && w.renderCommands_.currentInlineFormats().bold; }},
      {.id = QStringLiteral("format.italic"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Italic"),
       .shortcut = QKeySequence::Italic,
       .checkable = true,
       .handler = [](MainWindow& window) { window.backend_->toggleItalic(); },
       .enabled = [](const MainWindow& w) { return inlineFormat(w); },
       .checked = [](const MainWindow& w) { return inlineFormat(w) && w.renderCommands_.currentInlineFormats().italic; }},
      {.id = QStringLiteral("format.underline"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Underline"),
       .shortcut = QKeySequence::Underline,
       .checkable = true,
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->toggleUnderline(); },
       .enabled = [](const MainWindow& w) { return inlineFormat(w); },
       .checked = [](const MainWindow& w) { return inlineFormat(w) && w.renderCommands_.currentInlineFormats().underline; }},
      {.id = QStringLiteral("format.code"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Inline Code"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+`")),
       .checkable = true,
       .handler = [](MainWindow& window) { window.backend_->toggleCode(); },
       .enabled = [](const MainWindow& w) { return inlineFormat(w); },
       .checked = [](const MainWindow& w) { return inlineFormat(w) && w.renderCommands_.currentInlineFormats().code; }},
      {.id = QStringLiteral("format.inline_math"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Inline Formula"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+M")),
       .checkable = true,
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->toggleInlineMath(); },
       .enabled = [](const MainWindow& w) { return inlineFormat(w); },
       .checked = [](const MainWindow& w) { return inlineFormat(w) && w.renderCommands_.currentInlineFormats().inlineMath; }},
      {.id = QStringLiteral("format.strike"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Strikethrough"),
       .shortcut = QKeySequence(QStringLiteral("Alt+Shift+5")),
       .checkable = true,
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.backend_->toggleStrikethrough(); },
       .enabled = [](const MainWindow& w) { return inlineFormat(w); },
       .checked = [](const MainWindow& w) { return inlineFormat(w) && w.renderCommands_.currentInlineFormats().strikethrough; }},
      {.id = QStringLiteral("format.comment"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Comment"),
       .enabledInitial = false},
      {.id = QStringLiteral("format.link"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Hyperlink"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+K")),
       .handler = [](MainWindow& window) { window.backend_->insertLink(); }},
      {.id = QStringLiteral("format.clear"),
       .category = CommandCategory::Format,
       .text = muffin::MainWindow::tr("Clear Style"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+\\")),
       .handler = [](MainWindow& window) { window.backend_->clearFormatting(); },
       .enabled = [](const MainWindow& w) { return inlineFormat(w); }},

      // ---------------- Math ----------------
      {.id = QStringLiteral("math.refresh_all"),
       .category = CommandCategory::Math,
       .text = muffin::MainWindow::tr("Refresh All Math"),
       .handler = [](MainWindow& window) {
         window.renderView_->refreshVisibleBlocks(window.session_.document());
       }},

      // ---------------- Smart Punctuation ----------------
      {.id = QStringLiteral("edit.smart_convert_on_input"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Convert on Input"),
       .checkable = true,
       .handler = [](MainWindow& window) {
         const bool on = window.commands_.action(QStringLiteral("edit.smart_convert_on_input"))->isChecked();
         QSettings().setValue(QStringLiteral("markdown/convertOnInput"), on ? 1 : 0);
         if (on) {
           // Mutually exclusive with Convert on Rendering (alternate conversion timings): the two are
           // alternate conversion timings, never both on at once.
           QSettings().setValue(QStringLiteral("markdown/convertOnRendering"), false);
           if (auto* a = window.commands_.action(QStringLiteral("edit.smart_convert_on_rendering"))) {
             a->setChecked(false);
           }
         }
       },
       .checked = [](const MainWindow&) {
         return QSettings().value(QStringLiteral("markdown/convertOnInput"), 0).toInt() > 0;
       }},
      {.id = QStringLiteral("edit.smart_convert_on_rendering"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Convert on Rendering"),
       .checkable = true,
       .handler = [](MainWindow& window) {
         const bool on = window.commands_.action(QStringLiteral("edit.smart_convert_on_rendering"))->isChecked();
         QSettings().setValue(QStringLiteral("markdown/convertOnRendering"), on);
         if (on) {
           // Mutually exclusive with Convert on Input.
           QSettings().setValue(QStringLiteral("markdown/convertOnInput"), 0);
           if (auto* a = window.commands_.action(QStringLiteral("edit.smart_convert_on_input"))) {
             a->setChecked(false);
           }
         }
         window.renderView_->refreshVisibleBlocks(window.session_.document());
       },
       .checked = [](const MainWindow&) {
         return QSettings().value(QStringLiteral("markdown/convertOnRendering"), false).toBool();
       }},
      {.id = QStringLiteral("edit.smart_quotes"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Smart Quotes"),
       .checkable = true,
       .handler = [](MainWindow& window) {
         const bool on = window.commands_.action(QStringLiteral("edit.smart_quotes"))->isChecked();
         QSettings().setValue(QStringLiteral("markdown/smartQuotes"), on);
         window.renderView_->refreshVisibleBlocks(window.session_.document());
       },
       .checked = [](const MainWindow&) {
         return QSettings().value(QStringLiteral("markdown/smartQuotes"), false).toBool();
       }},
      {.id = QStringLiteral("edit.smart_dashes"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Smart Dashes"),
       .checkable = true,
       .handler = [](MainWindow& window) {
         const bool on = window.commands_.action(QStringLiteral("edit.smart_dashes"))->isChecked();
         QSettings().setValue(QStringLiteral("markdown/smartDashes"), on);
         window.renderView_->refreshVisibleBlocks(window.session_.document());
       },
       .checked = [](const MainWindow&) {
         return QSettings().value(QStringLiteral("markdown/smartDashes"), false).toBool();
       }},
      {.id = QStringLiteral("edit.smart_remap_unicode"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("Remap Unicode Punctuation on Parse"),
       .checkable = true,
       .handler = [](MainWindow& window) {
         const bool on = window.commands_.action(QStringLiteral("edit.smart_remap_unicode"))->isChecked();
         QSettings().setValue(QStringLiteral("markdown/remapUnicode"), on);
         window.session_.setParseOptions(markdownParseOptions());
       },
       .checked = [](const MainWindow&) {
         return QSettings().value(QStringLiteral("markdown/remapUnicode"), false).toBool();
       }},
      {.id = QStringLiteral("edit.smart_more_options"),
       .category = CommandCategory::Edit,
       .text = muffin::MainWindow::tr("More Options..."),
       .handler = [](MainWindow& window) { window.showPreferences(); }},

      // ---------------- Image ----------------
      {.id = QStringLiteral("image.insert"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Insert Image..."),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+I")),
       .handler = [](MainWindow& window) { window.insertImageWithDialog(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("image.insert_local"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Insert Local Image..."),
       .handler = [](MainWindow& window) { window.insertLocalImageWithDialog(); },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("image.open_location"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Open Image Location..."),
       .enabledInitial = false,
       .handler = [](MainWindow& window) {
         const QString src = window.renderCommands_.imageSrcAtCursor();
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         const QString resolved = ImageFileOps::resolveImagePath(src, docDir);
         if (!resolved.isEmpty()) {
           QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(resolved).absolutePath()));
         }
       },
       .enabled = [](const MainWindow& w) { return localImageAtCursor(w); }},
      {.id = QStringLiteral("image.copy_image"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Copy Image"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) {
         const QString src = window.renderCommands_.imageSrcAtCursor();
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         const QString resolved = ImageFileOps::resolveImagePath(src, docDir);
         if (resolved.isEmpty()) {
           return;
         }
         QImage image(resolved);
         if (image.isNull()) {
           return;
         }
         QApplication::clipboard()->setImage(image);
       },
       .enabled = [](const MainWindow& w) { return onImage(w); }},
      {.id = QStringLiteral("image.delete_image"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Delete Image File"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) {
         const QString src = window.renderCommands_.imageSrcAtCursor();
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         const QString resolved = ImageFileOps::resolveImagePath(src, docDir);
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
         ImageFileOps::deleteImageFile(resolved);
       },
       .enabled = [](const MainWindow& w) { return localImageAtCursor(w); }},
      {.id = QStringLiteral("image.copy_to"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Copy Image To..."),
       .enabledInitial = false,
       .handler = [](MainWindow& window) {
         const QString src = window.renderCommands_.imageSrcAtCursor();
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         const QString resolved = ImageFileOps::resolveImagePath(src, docDir);
         if (resolved.isEmpty()) {
           return;
         }
         const QString destDir = QFileDialog::getExistingDirectory(&window, muffin::MainWindow::tr("Copy Image To"), docDir);
         if (destDir.isEmpty()) {
           return;
         }
         QString newPath;
         if (ImageFileOps::copyImageTo(resolved, QDir(destDir), &newPath)) {
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
       },
       .enabled = [](const MainWindow& w) { return localImageAtCursor(w); }},
      {.id = QStringLiteral("image.move_to"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Rename / Move Image To..."),
       .enabledInitial = false,
       .handler = [](MainWindow& window) {
         const QString src = window.renderCommands_.imageSrcAtCursor();
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         const QString resolved = ImageFileOps::resolveImagePath(src, docDir);
         if (resolved.isEmpty()) {
           return;
         }
         const QString destDir = QFileDialog::getExistingDirectory(&window, muffin::MainWindow::tr("Move Image To"), docDir);
         if (destDir.isEmpty()) {
           return;
         }
         QString newPath;
         if (ImageFileOps::moveImageTo(resolved, QDir(destDir), &newPath)) {
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
       },
       .enabled = [](const MainWindow& w) { return localImageAtCursor(w); }},
      {.id = QStringLiteral("image.upload"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Upload Image"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) {
         if (!CustomCommandUploader::isAvailable()) {
           QMessageBox::information(&window, muffin::MainWindow::tr("Upload Image"),
               muffin::MainWindow::tr("No upload command is configured.\nSet up a custom upload command in Preferences → Images."));
           return;
         }
         const QString src = window.renderCommands_.imageSrcAtCursor();
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         const QString resolved = ImageFileOps::resolveImagePath(src, docDir);
         if (resolved.isEmpty()) {
           return;
         }
         const CustomCommandResult res = CustomCommandUploader::upload(&window, {resolved});
         if (res.canceled) {
           return;
         }
         if (!res.ran || res.urls.isEmpty()) {
           QMessageBox::warning(&window, muffin::MainWindow::tr("Upload Image"),
               muffin::MainWindow::tr("Upload failed:\n%1").arg(res.error));
           return;
         }
         const QString url = res.urls.first();
         qsizetype srcStart = 0, srcEnd = 0;
         if (window.renderCommands_.imageSourceRangeAtCursor(srcStart, srcEnd)) {
           const QString& md = window.session_.markdownText();
           const QString oldImage = md.mid(srcStart, srcEnd - srcStart);
           const int urlStart = oldImage.indexOf(QStringLiteral("](")) + 2;
           if (urlStart > 1) {
             int urlEnd = urlStart;
             while (urlEnd < oldImage.size() && oldImage[urlEnd] != QChar(')') && oldImage[urlEnd] != QChar(' ')) {
               ++urlEnd;
             }
             const QString newImage = oldImage.left(urlStart) + url + oldImage.mid(urlEnd);
             window.session_.applyTextDelta(srcStart, srcEnd - srcStart, newImage, true);
           }
         }
       },
       .enabled = [](const MainWindow& w) { return localImageAtCursor(w); }},
      {.id = QStringLiteral("image.upload_all"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Upload All Local Images"),
       .handler = [](MainWindow& window) {
         if (!CustomCommandUploader::isAvailable()) {
           QMessageBox::information(&window, muffin::MainWindow::tr("Upload All Images"),
               muffin::MainWindow::tr("No upload command is configured.\nSet up a custom upload command in Preferences → Images."));
           return;
         }
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         const auto refs = ImageFileOps::collectImageRefs(window.session_.document());
         // Collect unique resolved local paths (uploaders take a batch; order preserved).
         QStringList paths;
         QHash<QString, int> pathIndex;
         for (const auto& ref : refs) {
           if (!ImageFileOps::isLocalImageSrc(ref.href)) {
             continue;
           }
           const QString resolved = ImageFileOps::resolveImagePath(ref.href, docDir);
           if (resolved.isEmpty() || pathIndex.contains(resolved)) {
             continue;
           }
           pathIndex.insert(resolved, paths.size());
           paths.append(resolved);
         }
         if (paths.isEmpty()) {
           QMessageBox::information(&window, muffin::MainWindow::tr("Upload All Images"),
               muffin::MainWindow::tr("There are no local images to upload."));
           return;
         }
         const CustomCommandResult res = CustomCommandUploader::upload(&window, paths);
         if (res.canceled) {
           return;
         }
         if (!res.ran || res.urls.size() != paths.size()) {
           QMessageBox::warning(&window, muffin::MainWindow::tr("Upload All Images"),
               muffin::MainWindow::tr("Upload failed:\n%1").arg(res.error.isEmpty() ? QStringLiteral("the uploader did not return one URL per image") : res.error));
           return;
         }
         QHash<QString, QString> pathToUrl;
         for (int i = 0; i < paths.size(); ++i) {
           pathToUrl.insert(paths[i], res.urls[i]);
         }
         // Rewrite each ref's href in reverse so earlier offsets stay valid.
         QString md = window.session_.markdownText();
         int uploaded = 0;
         for (int i = refs.size() - 1; i >= 0; --i) {
           const auto& ref = refs[i];
           if (!ImageFileOps::isLocalImageSrc(ref.href)) {
             continue;
           }
           const QString resolved = ImageFileOps::resolveImagePath(ref.href, docDir);
           const auto it = pathToUrl.constFind(resolved);
           if (it == pathToUrl.constEnd()) {
             continue;
           }
           const int urlSearchStart = md.indexOf(QStringLiteral("]("), ref.sourceStart);
           if (urlSearchStart < 0 || urlSearchStart >= ref.sourceEnd) {
             continue;
           }
           const int urlStart = urlSearchStart + 2;
           int urlEnd = urlStart;
           while (urlEnd < md.size() && urlEnd <= ref.sourceEnd && md[urlEnd] != QChar(')') && md[urlEnd] != QChar(' ')) {
             ++urlEnd;
           }
           md.replace(urlStart, urlEnd - urlStart, it.value());
           ++uploaded;
         }
         window.session_.applyMarkdownText(md, true);
         QMessageBox::information(&window, muffin::MainWindow::tr("Upload All Images"),
             muffin::MainWindow::tr("Uploaded %1 image(s).").arg(uploaded));
       },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("image.reload_all"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Reload All Images"),
       .handler = [](MainWindow& window) {
         if (window.renderView_) {
           window.renderView_->setDocument(window.session_.document(), window.session_.filePath());
         }
       },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("image.resize_25"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("25%"),
       .checkable = true,
       .actionGroup = QStringLiteral("image.resize"),
       .handler = [](MainWindow& window) { window.renderCommands_.setImageZoomAtCursor(25); },
       .enabled = [](const MainWindow& w) { return onImage(w); },
       .checked = [](const MainWindow& w) { return onImage(w) && w.renderCommands_.currentImageZoomPercent() == 25; }},
      {.id = QStringLiteral("image.resize_50"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("50%"),
       .checkable = true,
       .actionGroup = QStringLiteral("image.resize"),
       .handler = [](MainWindow& window) { window.renderCommands_.setImageZoomAtCursor(50); },
       .enabled = [](const MainWindow& w) { return onImage(w); },
       .checked = [](const MainWindow& w) { return onImage(w) && w.renderCommands_.currentImageZoomPercent() == 50; }},
      {.id = QStringLiteral("image.resize_75"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("75%"),
       .checkable = true,
       .actionGroup = QStringLiteral("image.resize"),
       .handler = [](MainWindow& window) { window.renderCommands_.setImageZoomAtCursor(75); },
       .enabled = [](const MainWindow& w) { return onImage(w); },
       .checked = [](const MainWindow& w) { return onImage(w) && w.renderCommands_.currentImageZoomPercent() == 75; }},
      {.id = QStringLiteral("image.resize_100"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("100%"),
       .checkable = true,
       .checkedInitial = true,
       .actionGroup = QStringLiteral("image.resize"),
       .handler = [](MainWindow& window) { window.renderCommands_.setImageZoomAtCursor(100); },
       .enabled = [](const MainWindow& w) { return onImage(w); },
       .checked = [](const MainWindow& w) { return onImage(w) && w.renderCommands_.currentImageZoomPercent() == 100; }},
      {.id = QStringLiteral("image.resize_150"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("150%"),
       .checkable = true,
       .actionGroup = QStringLiteral("image.resize"),
       .handler = [](MainWindow& window) { window.renderCommands_.setImageZoomAtCursor(150); },
       .enabled = [](const MainWindow& w) { return onImage(w); },
       .checked = [](const MainWindow& w) { return onImage(w) && w.renderCommands_.currentImageZoomPercent() == 150; }},
      {.id = QStringLiteral("image.resize_custom"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Custom..."),
       .checkable = true,
       .actionGroup = QStringLiteral("image.resize"),
       .handler = [](MainWindow& window) {
         bool ok = false;
         const int percent = QInputDialog::getInt(
             &window, muffin::MainWindow::tr("Image Size"), muffin::MainWindow::tr("Zoom percent:"),
             window.renderCommands_.currentImageZoomPercent(), 1, 1000, 1, &ok);
         if (ok) {
           window.renderCommands_.setImageZoomAtCursor(percent);
         }
       },
       .enabled = [](const MainWindow& w) { return onImage(w); },
       .checked = [](const MainWindow& w) {
         const int zoom = w.renderCommands_.currentImageZoomPercent();
         return onImage(w) && zoom != 25 && zoom != 50 && zoom != 75 && zoom != 100 && zoom != 150;
       }},
      {.id = QStringLiteral("image.to_standard"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Standard Markdown ![](url)"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.convertImageAtCursorToMarkdown(); },
       .enabled = [](const MainWindow& w) { return onImage(w); }},
      {.id = QStringLiteral("image.to_html"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("HTML <img>"),
       .enabledInitial = false,
       .handler = [](MainWindow& window) { window.renderCommands_.convertImageAtCursorToHtml(); },
       .enabled = [](const MainWindow& w) { return onImage(w); }},
      {.id = QStringLiteral("image.copy_all_to"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Copy All Images To..."),
       .handler = [](MainWindow& window) {
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         if (docDir.isEmpty()) {
           return;
         }
         const QString destDir = QFileDialog::getExistingDirectory(&window, muffin::MainWindow::tr("Copy All Images To"), docDir);
         if (destDir.isEmpty()) {
           return;
         }
         const QStringList images = ImageFileOps::collectLocalImagePaths(window.session_.document(), docDir);
         int copied = 0;
         for (const QString& img : images) {
           if (ImageFileOps::copyImageTo(img, QDir(destDir), nullptr)) {
             ++copied;
           }
         }
         QMessageBox::information(&window, muffin::MainWindow::tr("Copy All Images"),
             muffin::MainWindow::tr("Copied %1 of %2 image(s).").arg(copied).arg(images.size()));
       },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("image.move_all_to"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Move All Images To..."),
       .handler = [](MainWindow& window) {
         const QString docDir = QFileInfo(window.session_.filePath()).absolutePath();
         if (docDir.isEmpty()) {
           return;
         }
         const QString destDir = QFileDialog::getExistingDirectory(&window, muffin::MainWindow::tr("Move All Images To"), docDir);
         if (destDir.isEmpty()) {
           return;
         }
         const auto refs = ImageFileOps::collectImageRefs(window.session_.document());
         int moved = 0;
         // Move files and update markdown references
         for (const auto& ref : refs) {
           if (!ImageFileOps::isLocalImageSrc(ref.href)) {
             continue;
           }
           const QString resolved = ImageFileOps::resolveImagePath(ref.href, docDir);
           if (resolved.isEmpty()) {
             continue;
           }
           QString newPath;
           if (ImageFileOps::moveImageTo(resolved, QDir(destDir), &newPath)) {
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
           if (!ImageFileOps::isLocalImageSrc(ref.href)) {
             continue;
           }
           const QString resolved = ImageFileOps::resolveImagePath(ref.href, docDir);
           if (resolved.isEmpty()) {
             continue;
           }
           const QString fileName = QFileInfo(resolved).fileName();
           const QString newRelPath = relPrefix.isEmpty() ? fileName : relPrefix + QChar('/') + fileName;
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
         QMessageBox::information(&window, muffin::MainWindow::tr("Move All Images"), muffin::MainWindow::tr("Moved %1 image(s).").arg(moved));
       },
       .enabled = [](const MainWindow& w) { return !w.backend_->isSourceMode(); }},
      {.id = QStringLiteral("image.global_settings"),
       .category = CommandCategory::Image,
       .text = muffin::MainWindow::tr("Global Image Settings..."),
       .handler = [](MainWindow& window) { window.showPreferences(); }},

      // ---------------- View ----------------
      {.id = QStringLiteral("view.sidebar"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Show / Hide Sidebar"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+L")),
       .checkable = true,
       .handler = [](MainWindow& window) {
         window.updateSidebarMode();
         QSettings().setValue(QStringLiteral("view/sidebarVisible"),
             window.commands_.action(QStringLiteral("view.sidebar"))->isChecked());
       }},
      {.id = QStringLiteral("view.outline"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Outline"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+1")),
       .handler = [](MainWindow& window) { window.setSidebarPanel(SidebarWidget::Panel::Outline); }},
      {.id = QStringLiteral("view.file_tree"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("File Tree"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+3")),
       .handler = [](MainWindow& window) { window.setSidebarPanel(SidebarWidget::Panel::Files); }},
      {.id = QStringLiteral("view.source_mode"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Source Code Mode"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+/")),
       .checkable = true,
       .handler = [](MainWindow& window) {
         window.updateViewMode();
         QSettings().setValue(QStringLiteral("view/sourceMode"),
             window.commands_.action(QStringLiteral("view.source_mode"))->isChecked());
       }},
      {.id = QStringLiteral("view.word_wrap"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Word Wrap"),
       .checkable = true,
       .checkedInitial = true,
       .handler = [](MainWindow& window) {
         const bool enabled = window.commands_.action(QStringLiteral("view.word_wrap"))->isChecked();
         window.editor_->setWordWrapEnabled(enabled);
         QSettings().setValue(QStringLiteral("view/wordWrap"), enabled);
       }},
      {.id = QStringLiteral("view.focus"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Focus Mode"),
       .shortcut = QKeySequence(QStringLiteral("F8")),
       .checkable = true,
       .handler = [](MainWindow& window) {
         const bool checked = window.commands_.action(QStringLiteral("view.focus"))->isChecked();
         window.setFocusMode(checked);
         window.saveAppearanceFocusMode(checked);
       }},
      {.id = QStringLiteral("view.typewriter"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Typewriter Mode"),
       .shortcut = QKeySequence(QStringLiteral("F9")),
       .checkable = true,
       .handler = [](MainWindow& window) {
         const bool checked = window.commands_.action(QStringLiteral("view.typewriter"))->isChecked();
         window.setTypewriterMode(checked);
         window.saveAppearanceTypewriterMode(checked);
       }},
      {.id = QStringLiteral("view.status_bar"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Show Status Bar"),
       .checkable = true,
       .checkedInitial = true,
       .handler = [](MainWindow& window) {
         const bool visible = window.commands_.action(QStringLiteral("view.status_bar"))->isChecked();
         window.setStatusBarVisible(visible);
         window.saveAppearanceStatusBarVisible(visible);
       }},
      {.id = QStringLiteral("view.word_count"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Word Count Window"),
       .enabledInitial = false},
      {.id = QStringLiteral("view.fullscreen"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Toggle Full Screen"),
       .shortcut = QKeySequence(QStringLiteral("F11")),
       .handler = [](MainWindow& window) { window.isFullScreen() ? window.showNormal() : window.showFullScreen(); }},
      {.id = QStringLiteral("view.always_on_top"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Always on Top"),
       .checkable = true,
       .handler = [](MainWindow& window) {
         const bool checked = window.commands_.action(QStringLiteral("view.always_on_top"))->isChecked();
         window.setWindowFlag(Qt::WindowStaysOnTopHint, checked);
         window.show();
         QSettings().setValue(QStringLiteral("view/alwaysOnTop"), checked);
       }},
      {.id = QStringLiteral("view.actual_size"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Actual Size"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+9")),
       .handler = [](MainWindow& window) {
         window.setZoomPercent(100);
         window.saveAppearanceZoomPercent(window.zoomPercent_);
       }},
      {.id = QStringLiteral("view.zoom_in"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Zoom In"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+=")),
       .handler = [](MainWindow& window) {
         window.setZoomPercent(window.zoomPercent_ + 10);
         window.saveAppearanceZoomPercent(window.zoomPercent_);
       }},
      {.id = QStringLiteral("view.zoom_out"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Zoom Out"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+-")),
       .handler = [](MainWindow& window) {
         window.setZoomPercent(window.zoomPercent_ - 10);
         window.saveAppearanceZoomPercent(window.zoomPercent_);
       }},
      {.id = QStringLiteral("view.window_switch"),
       .category = CommandCategory::View,
       .text = muffin::MainWindow::tr("Switch Windows"),
       .shortcut = QKeySequence(QStringLiteral("Ctrl+Tab")),
       .enabledInitial = false},

      // ---------------- Theme ----------------
      // Theme selection is no longer a fixed command list — the Theme menu is a
      // DynamicMenu::Themes (see mainMenuSpec below), enumerated at runtime from
      // ThemeManager::definitions() so user-imported custom themes appear too.

      // ---------------- Help ----------------
      {.id = QStringLiteral("help.quick_start"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("Quick Start"),
       .handler = [](MainWindow& w) { HelpViewerDialog::open(&w, HelpTopic::QuickStart); }},
      {.id = QStringLiteral("help.markdown_ref"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("Markdown Reference"),
       .handler = [](MainWindow& w) { HelpViewerDialog::open(&w, HelpTopic::MarkdownReference); }},
      {.id = QStringLiteral("help.custom_themes"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("Custom Themes"),
       .enabledInitial = false},
      {.id = QStringLiteral("help.acknowledgements"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("Acknowledgements"),
       .handler = [](MainWindow& w) { HelpViewerDialog::open(&w, HelpTopic::Acknowledgements); }},
      {.id = QStringLiteral("help.changelog"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("Changelog"),
       .handler = [](MainWindow&) {
         QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/blob/main/CHANGELOG.md")));
       }},
      {.id = QStringLiteral("help.website"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("Official Website"),
       .handler = [](MainWindow&) {
         QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin")));
       }},
      {.id = QStringLiteral("help.feedback"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("Feedback"),
       .handler = [](MainWindow&) {
         QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/issues")));
       }},
      {.id = QStringLiteral("help.update"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("Check for Updates..."),
       .handler = [](MainWindow&) { UpdateChecker::instance().checkForUpdates(); }},
      {.id = QStringLiteral("help.about"),
       .category = CommandCategory::Help,
       .text = muffin::MainWindow::tr("About"),
       .handler = [](MainWindow& window) {
         QMessageBox::about(
             &window,
             muffin::MainWindow::tr("About Muffin"),
             muffin::MainWindow::tr(
                 "<h3>Muffin %1</h3>"
                 "<p><b>Platform:</b> %2<br>"
                 "<b>Author:</b> jstzwj<br>"
                 "<b>License:</b> MIT License<br>"
                 "<b>Website:</b> https://github.com/jstzwj/Muffin</p>")
                 .arg(QApplication::applicationVersion(), QSysInfo::prettyProductName()));
       }},
  };
    commandsValid() = true;
  }
  return commandTable();
}

const CommandDeclaration* commandDeclaration(const QString& id) {
  for (const CommandDeclaration& decl : commandDeclarations()) {
    if (decl.id == id) {
      return &decl;
    }
  }
  return nullptr;
}

void refreshDeclarations() {
  commandsValid() = false;
  menuSpecValid() = false;
  commandDeclarations();
  mainMenuSpec();
}

// ---- Menu layout -----------------------------------------------------------

const std::vector<MenuSpec>& mainMenuSpec() {
  if (!menuSpecValid()) {
    menuSpecTable() = std::vector<MenuSpec>{
      // File
      {muffin::MainWindow::tr("File"),
       {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.new")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.new_window")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.open")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.open_folder")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.quick_open")},
           {.kind = MenuItem::Kind::DynamicSubmenu, .title = muffin::MainWindow::tr("Open Recent"), .dynamicId = DynamicMenu::RecentFiles},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::DynamicSubmenu, .title = muffin::MainWindow::tr("Reopen with Encoding"), .dynamicId = DynamicMenu::ReopenEncoding},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.save")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.save_as")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.move_to")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.save_all")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.properties")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.reveal")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.sidebar")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.delete")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.import")},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Export"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_pdf")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_html")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_html_plain")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_docx")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_odt")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_rtf")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_epub")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_latex")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_mediawiki")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_rst")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_textile")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.export_opml")},
            }},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.print")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.preferences")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("file.close")},
       }},
      // Edit
      {muffin::MainWindow::tr("Edit"),
       {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.undo")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.redo")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.cut")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.copy")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.paste")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.copy_plain")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.copy_markdown")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.copy_html")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.paste_plain")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Select"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.select_all")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.select_block")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.select_line")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.select_format")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.select_word")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.jump_doc_start")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.jump_selection")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.jump_doc_end")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.jump_line_start")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.jump_line_end")},
            }},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.move_line_up")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.move_line_down")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.delete")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Delete Range"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.delete_block")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.delete_line")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.delete_format")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.delete_word")},
            }},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Math Tools"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("math.refresh_all")},
            }},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Smart Punctuation"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.smart_convert_on_input")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.smart_convert_on_rendering")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.smart_quotes")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.smart_dashes")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.smart_remap_unicode")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.smart_more_options")},
            }},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Line Breaks"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.linebreak_crlf")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.linebreak_lf")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.trailing_newline")},
            }},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.spellcheck")},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Find and Replace"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.find")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.replace")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.find_next")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("edit.find_previous")},
            }},
       }},
      // Paragraph
      {muffin::MainWindow::tr("Paragraph"),
       {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.heading_1")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.heading_2")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.heading_3")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.heading_4")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.heading_5")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.heading_6")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.paragraph")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.promote_heading")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.demote_heading")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Table"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.insert_table")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.insert_row_before")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.insert_row_after")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.insert_column_before")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.insert_column_after")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.move_row_up")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.move_row_down")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.move_column_left")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.move_column_right")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.delete_row")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.delete_column")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.copy_table")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.format_source")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.delete_table")},
            }},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.math_block")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.code_block")},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Code Tools"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("code.copy_content")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("code.indent_selection")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("code.dedent_selection")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("code.indent_block")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("code.dedent_block")},
            }},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Alert"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.insert_alert_note")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.insert_alert_tip")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.insert_alert_important")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.insert_alert_warning")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.insert_alert_caution")},
            }},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.quote")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.ordered_list")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.unordered_list")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.task_list")},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Task Status"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.task_toggle")},
            }},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("List Indent"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.indent_list")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.outdent_list")},
            }},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.insert_before")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.insert_after")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.link_ref")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.footnote")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.hr")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.toc")},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Front Matter"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.yaml")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.toml")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("paragraph.json")},
            }},
       }},
      // Table (top-level, hidden)
      {.title = muffin::MainWindow::tr("Table"),
       .items = {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.align_left")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.align_center")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.align_right")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("table.align_none")},
       },
       .hidden = true},
      // Code (hidden)
      {.title = muffin::MainWindow::tr("Code"),
       .items = {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("code.enter_edit")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("code.exit_edit")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("code.set_language")},
       },
       .hidden = true},
      // HTML (hidden)
      {.title = QStringLiteral("HTML(&H)"),
       .items = {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("html.enter_edit")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("html.exit_edit")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("html.set_source")},
       },
       .hidden = true},
      // Math (hidden)
      {.title = muffin::MainWindow::tr("Math"),
       .items = {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("math.enter_edit")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("math.exit_edit")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("math.set_tex")},
       },
       .hidden = true},
      // Format
      {muffin::MainWindow::tr("Format"),
       {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.bold")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.italic")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.underline")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.code")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.inline_math")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.strike")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.comment")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.link")},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Link Actions"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("link.open")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("link.copy_address")},
            }},
           {.kind = MenuItem::Kind::Submenu,
            .title = muffin::MainWindow::tr("Image"),
            .children = {
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.insert")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.insert_local")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.open_location")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.copy_image")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.delete_image")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.copy_to")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.move_to")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.upload")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.upload_all")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.reload_all")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Submenu,
                 .title = muffin::MainWindow::tr("Resize Image"),
                 .children = {
                     {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.resize_25")},
                     {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.resize_50")},
                     {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.resize_75")},
                     {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.resize_100")},
                     {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.resize_150")},
                     {.kind = MenuItem::Kind::Separator},
                     {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.resize_custom")},
                 }},
                {.kind = MenuItem::Kind::Submenu,
                 .title = muffin::MainWindow::tr("Convert Image Syntax"),
                 .children = {
                     {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.to_standard")},
                     {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.to_html")},
                 }},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.copy_all_to")},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.move_all_to")},
                {.kind = MenuItem::Kind::Separator},
                {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("image.global_settings")},
            }},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("format.clear")},
       }},
      // View
      {muffin::MainWindow::tr("View"),
       {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.sidebar")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.outline")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.file_tree")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.source_mode")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.word_wrap")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.focus")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.typewriter")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.status_bar")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.word_count")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.fullscreen")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.always_on_top")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.actual_size")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.zoom_in")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.zoom_out")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("view.window_switch")},
       }},
      // Theme — top-level menu populated dynamically from ThemeManager::definitions()
      // (built-ins + any imported custom themes). buildMenus captures it into
      // themesMenu_; rebuildThemesMenu() fills it.
      {muffin::MainWindow::tr("Theme"), {}, false, DynamicMenu::Themes},
      // Help
      {muffin::MainWindow::tr("Help"),
       {
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.quick_start")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.markdown_ref")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.custom_themes")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.acknowledgements")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.changelog")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.website")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.feedback")},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.update")},
           {.kind = MenuItem::Kind::Separator},
           {.kind = MenuItem::Kind::Action, .commandId = QStringLiteral("help.about")},
       }},
  };
    menuSpecValid() = true;
  }
  return menuSpecTable();
}

}  // namespace muffin
