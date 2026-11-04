#include "editor/ShortcutSettings.h"
#include "editor/EditorKeyRouting.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QSettings>

namespace muffin {

ShortcutSettings::ShortcutSettings(QObject* parent) : QObject(parent) {
  QSettings settings;
  settings.beginGroup(QStringLiteral("shortcuts"));
  for (const auto& id : settings.childKeys())
    overrides_.insert(id, QKeySequence::fromString(settings.value(id).toString(), QKeySequence::PortableText));
}

ShortcutSettings& ShortcutSettings::instance() {
  static auto* settings = new ShortcutSettings(QCoreApplication::instance());
  return *settings;
}

QKeySequence ShortcutSettings::sequence(const QString& id, const QKeySequence& fallback) const {
  return overrides_.value(id, fallback);
}

QList<QKeySequence> ShortcutSettings::sequences(const QString& id, const QList<QKeySequence>& fallback) const {
  const auto found = overrides_.constFind(id);
  if (found == overrides_.cend())
    return fallback;
  return found->isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{*found};
}

bool ShortcutSettings::matches(const QKeyEvent& event, const QString& id, QKeySequence::StandardKey fallback) const {
  const auto found = overrides_.constFind(id);
  return found == overrides_.cend() ? event.matches(fallback)
                                    : !found->isEmpty() && *found == QKeySequence(event.keyCombination());
}

bool ShortcutSettings::hasOverride(const QString& id) const {
  return overrides_.contains(id);
}

bool ShortcutSettings::matches(const QKeyEvent& event, const QString& id, const QKeySequence& fallback) const {
  const auto key = sequence(id, fallback);
  return !key.isEmpty() && key == QKeySequence(event.keyCombination());
}

void ShortcutSettings::setOverride(const QString& id, const QKeySequence& sequence) {
  if (overrides_.contains(id) && overrides_.value(id) == sequence)
    return;
  overrides_.insert(id, sequence);
  QSettings().setValue(QStringLiteral("shortcuts/") + id, sequence.toString(QKeySequence::PortableText));
  emit changed();
}

void ShortcutSettings::clearOverride(const QString& id) {
  if (!overrides_.remove(id))
    return;
  QSettings().remove(QStringLiteral("shortcuts/") + id);
  emit changed();
}

void ShortcutSettings::reset() {
  overrides_.clear();
  QSettings().remove(QStringLiteral("shortcuts"));
  emit changed();
}

bool ShortcutSettings::isReserved(const QKeySequence& sequence) {
  if (sequence.isEmpty())
    return false;
  if (sequence.count() != 1)
    return true;
  const auto combination = sequence[0];
  const auto key = combination.key();
  const auto modifiers = combination.keyboardModifiers();
  if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
    return false;
  if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
    return true;
  if ((modifiers & (Qt::ControlModifier | Qt::MetaModifier)) && !(modifiers & Qt::AltModifier) &&
      editor_keys::isEditorOwnedCtrlKey(key, modifiers.testFlag(Qt::ShiftModifier)))
    return true;
  return key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
         key == Qt::Key_Meta || key == Qt::Key_AltGr;
}

} // namespace muffin
