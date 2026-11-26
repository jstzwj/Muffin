// Invariants of the command table in app/CommandDeclarations.cpp.
//
// Nothing drove the table directly before this test, which is how two live commands came to
// share a default shortcut (Alt+Left / Alt+Right: navigate back/forward and move table column)
// and how a row without a handler kept a binding that could never fire (view.window_switch,
// Ctrl+Tab, which then shadowed view.next_tab in the shortcut preferences page).

#include "app/CommandDeclarations.h"

#include "../TestUtils.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QSet>
#include <QStringList>
#include <QVector>

using muffin::CommandDeclaration;
using muffin::MenuItem;

namespace {

void collectActionIds(const std::vector<MenuItem>& items, QStringList& ids) {
  for (const MenuItem& item : items) {
    if (item.kind == MenuItem::Kind::Action)
      ids.append(item.commandId);
    collectActionIds(item.children, ids);
  }
}

void commandIdsAreUniqueAndMenusReferenceThem() {
  const std::vector<CommandDeclaration>& commands = muffin::commandDeclarations();
  QSet<QString> ids;
  for (const CommandDeclaration& command : commands) {
    require(!command.id.isEmpty(), QStringLiteral("every command declares an id"));
    require(!ids.contains(command.id), QStringLiteral("duplicate command id %1").arg(command.id));
    ids.insert(command.id);
  }

  QStringList menuIds;
  for (const muffin::MenuSpec& menu : muffin::mainMenuSpec())
    collectActionIds(menu.items, menuIds);
  require(!menuIds.isEmpty(), QStringLiteral("the menu spec references commands"));
  for (const QString& id : menuIds)
    require(ids.contains(id), QStringLiteral("menu item %1 is not a declared command").arg(id));
}

struct Binding {
  QKeySequence key;
  QString command;
};

// Qt fires neither of two enabled actions that share a shortcut, so a default key must belong
// to one command. Alternates of the same command are excluded by construction.
void defaultShortcutsBelongToOneCommand() {
  QVector<Binding> bindings;
  for (const CommandDeclaration& command : muffin::commandDeclarations()) {
    if (!command.handler)
      continue;
    for (const QKeySequence& key : command.defaultShortcuts())
      bindings.append({key, command.id});
  }
  for (int i = 0; i < bindings.size(); ++i) {
    for (int j = 0; j < i; ++j) {
      const QKeySequence::SequenceMatch match = bindings[i].key.matches(bindings[j].key);
      require(match == QKeySequence::NoMatch,
              QStringLiteral("commands %1 and %2 share the default shortcut %3")
                  .arg(bindings[j].command, bindings[i].command, bindings[i].key.toString(QKeySequence::NativeText)));
    }
  }
}

// A shortcut on a row without a handler never fires: the action is never created. It still shows
// up as a conflict for whichever command really owns the key.
void shortcutRowsHaveAHandler() {
  for (const CommandDeclaration& command : muffin::commandDeclarations())
    require(
        command.handler || command.defaultShortcuts().isEmpty(),
        QStringLiteral("command %1 declares a shortcut but has no handler, so the key can never fire").arg(command.id));
}

} // namespace

void runCommandTableTests() {
  runTest("commandIdsAreUniqueAndMenusReferenceThem", commandIdsAreUniqueAndMenusReferenceThem);
  runTest("defaultShortcutsBelongToOneCommand", defaultShortcutsBelongToOneCommand);
  runTest("shortcutRowsHaveAHandler", shortcutRowsHaveAHandler);
}
