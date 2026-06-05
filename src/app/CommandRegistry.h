#pragma once

#include <QAction>
#include <QObject>
#include <QHash>

#include <functional>

namespace muffin {

class CommandRegistry final : public QObject {
  Q_OBJECT

public:
  explicit CommandRegistry(QObject* parent = nullptr);

  QAction* action(const QString& id) const;
  QAction* registerAction(QString id, QAction* action);
  void clearActions();
  void bind(const QString& id, std::function<void()> handler);
  void setEnabled(const QString& id, bool enabled);
  void setChecked(const QString& id, bool checked);

private:
  void attachHandler(const QString& id, QAction* action);

  QHash<QString, QAction*> actions_;
  QHash<QString, std::function<void()>> handlers_;
};

}  // namespace muffin
