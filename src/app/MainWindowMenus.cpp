#include "app/MainWindow.h"
#include "app/CommandDeclarations.h"
#include "editor/EditorView.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>

namespace muffin {

// Create and register one QAction for a declared command. The label, shortcut,
// checkable/checked initial state, shortcut context and action-group all come
// from the declaration table — this function adds no per-command knowledge.
QAction* MainWindow::registerAction(QMenu* menu, const QString& id) {
  const CommandDeclaration* decl = commandDeclaration(id);
  if (!decl) {
    return nullptr;
  }

  QAction* action = menu->addAction(decl->text);
  if (!decl->shortcut.isEmpty()) {
    action->setShortcut(decl->shortcut);
  }
  action->setEnabled(decl->enabledInitial);
  if (decl->checkable) {
    action->setCheckable(true);
    action->setChecked(decl->checkedInitial);
  }
  if (decl->shortcutWidgetContext) {
    action->setShortcutContext(Qt::WidgetShortcut);
  }
  commands_.registerAction(id, action);

  // Exclusive radio group (e.g. image resize, image insert action). One
  // QActionGroup per actionGroup id, created lazily and cleared by setupMenuBar.
  if (!decl->actionGroup.isEmpty()) {
    QActionGroup*& group = actionGroups_[decl->actionGroup];
    if (!group) {
      group = new QActionGroup(this);
      group->setExclusive(true);
    }
    group->addAction(action);
  }
  return action;
}

void MainWindow::buildMenuItems(QMenu* parent, const std::vector<MenuItem>& items) {
  for (const MenuItem& item : items) {
    switch (item.kind) {
      case MenuItem::Kind::Action:
        registerAction(parent, item.commandId);
        break;
      case MenuItem::Kind::Separator:
        parent->addSeparator();
        break;
      case MenuItem::Kind::Submenu: {
        QMenu* sub = parent->addMenu(item.title);
        if (item.hidden) {
          sub->menuAction()->setVisible(false);
        }
        buildMenuItems(sub, item.children);
        break;
      }
      case MenuItem::Kind::PlaceholderSubmenu: {
        // Future-feature menu: present but disabled, title only.
        QMenu* sub = parent->addMenu(item.title);
        sub->setEnabled(false);
        break;
      }
      case MenuItem::Kind::DynamicSubmenu: {
        // Submenu populated elsewhere (recent files, encodings); capture the
        // pointer so MainWindowFileOps can clear/fill it.
        QMenu* sub = parent->addMenu(item.title);
        if (item.dynamicId == DynamicMenu::RecentFiles) {
          recentFilesMenu_ = sub;
        } else if (item.dynamicId == DynamicMenu::ReopenEncoding) {
          reopenEncodingMenu_ = sub;
          sub->setEnabled(false);
        }
        break;
      }
    }
  }
}

void MainWindow::buildMenus() {
  for (const MenuSpec& spec : mainMenuSpec()) {
    QMenu* menu = menuBar()->addMenu(spec.title);
    if (spec.hidden) {
      menu->menuAction()->setVisible(false);
    }
    buildMenuItems(menu, spec.items);
  }
}

namespace {
// Recursively update submenu titles in place by re-walking the live menu against
// its spec. Creates/deletes nothing — safe during a language change.
void retranslateMenuItemTitles(QMenu* menu, const std::vector<MenuItem>& items) {
  const QList<QAction*> actions = menu->actions();
  for (qsizetype i = 0; i < static_cast<qsizetype>(items.size()) && i < actions.size(); ++i) {
    const MenuItem& item = items[static_cast<size_t>(i)];
    QAction* action = actions[i];
    switch (item.kind) {
      case MenuItem::Kind::Submenu:
        action->setText(item.title);
        if (QMenu* sub = action->menu()) {
          retranslateMenuItemTitles(sub, item.children);
        }
        break;
      case MenuItem::Kind::PlaceholderSubmenu:
      case MenuItem::Kind::DynamicSubmenu:
        action->setText(item.title);
        break;
      case MenuItem::Kind::Action:
      case MenuItem::Kind::Separator:
        break;  // command action labels are updated via the registry; separators have no text
    }
  }
}
}  // namespace

void MainWindow::retranslateMenuTexts() {
  // Rebuild the declaration table so its tr() labels reflect the new locale, then
  // update existing actions/menus in place. No QActions/QMenus are deleted or
  // created here — that deletion (setupMenuBar's clear+rebuild) was what corrupted
  // the heap on language switch, because Qt still had LanguageChange events pending
  // for the freed actions.
  refreshDeclarations();
  for (const CommandDeclaration& decl : commandDeclarations()) {
    if (QAction* action = commands_.action(decl.id)) {
      action->setText(decl.text);
    }
  }
  const std::vector<MenuSpec>& specs = mainMenuSpec();
  const QList<QAction*> topActions = menuBar()->actions();
  for (qsizetype i = 0; i < static_cast<qsizetype>(specs.size()) && i < topActions.size(); ++i) {
    topActions[i]->setText(specs[static_cast<size_t>(i)].title);
    if (QMenu* menu = topActions[i]->menu()) {
      retranslateMenuItemTitles(menu, specs[static_cast<size_t>(i)].items);
    }
  }
}

void MainWindow::retranslateUi() {
  const bool sidebarChecked =
      commands_.action(QStringLiteral("view.sidebar")) && commands_.action(QStringLiteral("view.sidebar"))->isChecked();
  const bool sourceChecked =
      commands_.action(QStringLiteral("view.source_mode")) && commands_.action(QStringLiteral("view.source_mode"))->isChecked();
  const bool wordWrapChecked =
      !commands_.action(QStringLiteral("view.word_wrap")) || commands_.action(QStringLiteral("view.word_wrap"))->isChecked();
  const bool statusBarChecked =
      !commands_.action(QStringLiteral("view.status_bar")) || commands_.action(QStringLiteral("view.status_bar"))->isChecked();
  const bool focusChecked =
      commands_.action(QStringLiteral("view.focus")) && commands_.action(QStringLiteral("view.focus"))->isChecked();
  const bool typewriterChecked =
      commands_.action(QStringLiteral("view.typewriter")) && commands_.action(QStringLiteral("view.typewriter"))->isChecked();

  // Update menu labels/titles in place for the new locale. Do NOT call
  // setupMenuBar() (clear+rebuild) here: deleting and recreating menu actions
  // while Qt still has LanguageChange events pending for them corrupts the heap.
  retranslateMenuTexts();

  if (QAction* action = commands_.action(QStringLiteral("view.sidebar"))) {
    action->setChecked(sidebarChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.source_mode"))) {
    action->setChecked(sourceChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.word_wrap"))) {
    action->setChecked(wordWrapChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.status_bar"))) {
    action->setChecked(statusBarChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.focus"))) {
    action->setChecked(focusChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.typewriter"))) {
    action->setChecked(typewriterChecked);
  }

  // Re-apply focus/typewriter mode state after menu rebuild
  setFocusMode(focusChecked);
  focusMode_ = focusChecked;
  setTypewriterMode(typewriterChecked);

  if (sidebarButton_) {
    sidebarButton_->setToolTip(tr("Show / Hide Sidebar"));
  }
  if (sourceModeButton_) {
    sourceModeButton_->setToolTip(tr("Toggle source / rendered mode"));
  }

  updateFileActions();
  updateContextActions();
  updateThemeActions();
  // Note: rebuildRecentFilesMenu()/buildReopenEncodingMenu() are intentionally
  // NOT called here — they delete and recreate submenu actions (same heap-corruption
  // hazard as setupMenuBar), and their content is language-independent. Their
  // submenu titles are updated in place by retranslateMenuTexts(). Likewise the
  // document is not re-rendered (its content does not change with locale).
  updateStatus();
  wordCountDirty_ = true;
  updateWordCountNow();
}

}  // namespace muffin
