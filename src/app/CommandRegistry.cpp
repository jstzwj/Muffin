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
  const QString actionId = std::move(id);
  actions_.insert(actionId, action);
  attachHandler(actionId, action);
  return action;
}

void CommandRegistry::clearActions() {
  actions_.clear();
}

void CommandRegistry::bind(const QString& id, std::function<void()> handler) {
  handlers_.insert(id, std::move(handler));
  QAction* registered = action(id);
  if (!registered) {
    return;
  }
  attachHandler(id, registered);
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

void CommandRegistry::attachHandler(const QString& id, QAction* action) {
  if (!action || !handlers_.contains(id)) {
    return;
  }
  connect(action, &QAction::triggered, this, [this, id] {
    const auto handler = handlers_.value(id);
    if (handler) {
      handler();
    }
  });
}

}  // namespace muffin
