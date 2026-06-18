#include "app/MainWindow.h"

#include "document/MarkdownDocument.h"
#include "editor/CursorPosition.h"
#include "editor/EditorView.h"
#include "spellcheck/SpellChecker.h"
#include "unicode/WordBoundary.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QString>
#include <QStringList>

#include <initializer_list>

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
    const QString& markdown = session_.document().markdownText();
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
  if (!hit.linkHref.isEmpty()) {
    addCommand(QStringLiteral("link.open"));
    addCommand(QStringLiteral("link.copy_address"));
    addCommand(QStringLiteral("format.link"));
    menu.addSeparator();
  } else if (!hit.imageSrc.isEmpty()) {
    QMenu* imageMenu = menu.addMenu(tr("Image"));
    fill(imageMenu,
        {QStringLiteral("image.open_location"), QStringLiteral("edit.copy_image")});
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
