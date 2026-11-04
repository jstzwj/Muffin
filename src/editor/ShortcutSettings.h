#pragma once

#include "Export.h"
#include <QHash>
#include <QKeySequence>
#include <QObject>

class QKeyEvent;

namespace muffin {

// Shared by window actions and the source editor's standalone key handling.
// An explicitly empty override disables a shortcut; an absent key uses defaults.
class MUFFIN_UI_EXPORT ShortcutSettings final : public QObject {
  Q_OBJECT
public:
  static ShortcutSettings& instance();
  QKeySequence sequence(const QString& id, const QKeySequence& fallback) const;
  QList<QKeySequence> sequences(const QString& id, const QList<QKeySequence>& fallback) const;
  bool matches(const QKeyEvent& event, const QString& id, QKeySequence::StandardKey fallback) const;
  bool matches(const QKeyEvent& event, const QString& id, const QKeySequence& fallback) const;
  bool hasOverride(const QString& id) const;
  void setOverride(const QString& id, const QKeySequence& sequence);
  void clearOverride(const QString& id);
  void reset();
  static bool isReserved(const QKeySequence& sequence);
signals:
  void changed();

private:
  explicit ShortcutSettings(QObject* parent);
  QHash<QString, QKeySequence> overrides_;
};

} // namespace muffin
