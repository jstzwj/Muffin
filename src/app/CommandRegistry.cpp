#include "app/CommandRegistry.h"

#include <utility>

namespace muffin {

CommandRegistry::CommandRegistry(QObject* parent) : QObject(parent) {}

QAction* CommandRegistry::action(const QString& id) const {
  return actions_.value(id, nullptr);
}

QAction* CommandRegistry::registerAction(QString id, QAction* action) {
  if (!action) {
    return nullptr;
  }
  actions_.insert(std::move(id), action);
  return action;
}

void CommandRegistry::bind(const QString& id, std::function<void()> handler) {
  QAction* registered = action(id);
  if (!registered) {
    return;
  }
  connect(registered, &QAction::triggered, this, [handler = std::move(handler)] {
    if (handler) {
      handler();
    }
  });
}

void CommandRegistry::setEnabled(const QString& id, bool enabled) {
  if (QAction* registered = action(id)) {
    registered->setEnabled(enabled);
  }
}

void CommandRegistry::setChecked(const QString& id, bool checked) {
  if (QAction* registered = action(id)) {
    registered->setChecked(checked);
  }
}

}  // namespace muffin
