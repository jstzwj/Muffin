#include "app/PrefsShortcutsPage.h"
#include "app/CommandDeclarations.h"
#include "editor/ShortcutSettings.h"

#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

muffin::PrefsShortcutsPage::PrefsShortcutsPage(QWidget* parent) : PreferencesPage(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 26, 22, 26);
  layout->setSpacing(12);
  auto* heading = new QVBoxLayout;
  heading->setSpacing(6);
  title_ = new QLabel(this);
  title_->setObjectName(QStringLiteral("preferencesPageTitle"));
  heading->addWidget(title_);
  explanation_ = makeMutedLabel(this);
  explanation_->setWordWrap(true);
  heading->addWidget(explanation_);
  layout->addLayout(heading);
  filter_ = new QLineEdit(this);
  filter_->setObjectName(QStringLiteral("ShortcutFilter"));
  filter_->setClearButtonEnabled(true);
  layout->addWidget(filter_);
  commands_ = new QTreeWidget(this);
  commands_->setObjectName(QStringLiteral("ShortcutCommands"));
  commands_->setRootIsDecorated(false);
  commands_->setUniformRowHeights(true);
  commands_->setSelectionMode(QAbstractItemView::SingleSelection);
  commands_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  commands_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  commands_->setAllColumnsShowFocus(true);
  commands_->setColumnCount(3);
  commands_->setMinimumHeight(180);
  commands_->header()->setStretchLastSection(false);
  commands_->header()->setSectionsClickable(false);
  commands_->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  commands_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  commands_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  commands_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  layout->addWidget(commands_, 1);

  auto* editor = new QWidget(this);
  editor->setObjectName(QStringLiteral("ShortcutEditor"));
  auto* editorLayout = new QVBoxLayout(editor);
  editorLayout->setContentsMargins(14, 12, 14, 12);
  editorLayout->setSpacing(10);
  selectedCommand_ = makeSectionLabel(editor);
  selectedCommand_->setWordWrap(true);
  editorLayout->addWidget(selectedCommand_);
  sequence_ = new QKeySequenceEdit(editor);
  sequence_->setObjectName(QStringLiteral("ShortcutSequence"));
  sequence_->setMaximumSequenceLength(1);
  sequence_->setClearButtonEnabled(true);
  editorLayout->addWidget(sequence_);
  message_ = makeMutedLabel(editor);
  message_->setWordWrap(true);
  editorLayout->addWidget(message_);
  auto* row = new QHBoxLayout;
  row->setSpacing(8);
  apply_ = makeButton(editor);
  apply_->setObjectName(QStringLiteral("ShortcutApply"));
  clear_ = makeButton(editor);
  restore_ = makeButton(editor);
  row->addWidget(clear_);
  row->addWidget(restore_);
  row->addStretch(1);
  row->addWidget(apply_);
  editorLayout->addLayout(row);
  layout->addWidget(editor);
  reset_ = makeButton(this);
  reset_->setObjectName(QStringLiteral("ShortcutReset"));
  layout->addWidget(reset_, 0, Qt::AlignLeft);
  connect(filter_, &QLineEdit::textChanged, this, &PrefsShortcutsPage::rebuild);
  connect(commands_, &QTreeWidget::currentItemChanged, this, &PrefsShortcutsPage::selectionChanged);
  connect(sequence_, &QKeySequenceEdit::keySequenceChanged, this, &PrefsShortcutsPage::validate);
  connect(apply_, &QPushButton::clicked, this, &PrefsShortcutsPage::apply);
  connect(clear_, &QPushButton::clicked, sequence_, &QKeySequenceEdit::clear);
  connect(restore_, &QPushButton::clicked, this, [this] {
    if (auto* item = commands_->currentItem())
      if (const auto* command = commandDeclaration(item->data(0, Qt::UserRole).toString()))
        sequence_->setKeySequence(command->shortcut);
  });
  connect(reset_, &QPushButton::clicked, this, [this] {
    // Parent to the themed dialog rather than the page inside its scroll area.
    // Use an explicit, cancellable action instead of ambiguous Yes/No buttons.
    QMessageBox confirmation(QMessageBox::Question, tr("Keyboard Shortcuts"), tr("Restore all default shortcuts?"),
                             QMessageBox::Cancel, window());
    confirmation.setObjectName(QStringLiteral("ShortcutResetConfirmation"));
    confirmation.setOption(QMessageBox::Option::DontUseNativeDialog);
    confirmation.setPalette(window()->palette());
    confirmation.setInformativeText(tr("All custom shortcuts will be removed. This cannot be undone."));
    auto* restoreDefaults = confirmation.addButton(tr("Restore Defaults"), QMessageBox::AcceptRole);
    restoreDefaults->setObjectName(QStringLiteral("ShortcutResetAccept"));
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.setEscapeButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() == restoreDefaults)
      ShortcutSettings::instance().reset();
  });
  connect(&ShortcutSettings::instance(), &ShortcutSettings::changed, this, &PrefsShortcutsPage::rebuild);
  retranslateUi();
}

void muffin::PrefsShortcutsPage::retranslateUi() {
  title_->setText(tr("Keyboard Shortcuts"));
  explanation_->setText(tr("Select a command and press a key combination. Changes apply to all windows immediately."));
  filter_->setPlaceholderText(tr("Search commands or shortcuts…"));
  filter_->setAccessibleName(tr("Search commands or shortcuts…"));
  commands_->setHeaderLabels({tr("Command"), tr("Shortcut"), tr("Default")});
  sequence_->setAccessibleName(tr("Shortcut"));
  clear_->setText(tr("Clear"));
  restore_->setText(tr("Use Default"));
  reset_->setText(tr("Restore All Defaults…"));
  rebuild();
}

void muffin::PrefsShortcutsPage::rebuild() {
  const auto* current = commands_->currentItem();
  const auto selected = current ? current->data(0, Qt::UserRole).toString() : QString();
  const int scroll = commands_->verticalScrollBar()->value();
  const auto query = filter_->text().trimmed();
  const QSignalBlocker blocked(commands_);
  commands_->clear();
  for (const auto& command : commandDeclarations()) {
    if (!command.handler)
      continue;
    QString label = command.text;
    label.remove('&');
    const auto shortcut = ShortcutSettings::instance().sequence(command.id, command.shortcut);
    const auto shown = shortcut.toString(QKeySequence::NativeText);
    const auto defaultShortcut = command.shortcut.toString(QKeySequence::NativeText);
    if (!query.isEmpty() && !label.contains(query, Qt::CaseInsensitive) &&
        !command.id.contains(query, Qt::CaseInsensitive) && !shown.contains(query, Qt::CaseInsensitive) &&
        !defaultShortcut.contains(query, Qt::CaseInsensitive))
      continue;
    auto* item = new QTreeWidgetItem(commands_, {label, shown, defaultShortcut});
    item->setData(0, Qt::UserRole, command.id);
    item->setToolTip(0, command.id);
    item->setToolTip(1, shown);
    item->setToolTip(2, defaultShortcut);
    if (command.id == selected)
      commands_->setCurrentItem(item);
  }
  if (!commands_->currentItem() && commands_->topLevelItemCount())
    commands_->setCurrentItem(commands_->topLevelItem(0));
  commands_->verticalScrollBar()->setValue(scroll);
  selectionChanged();
}

void muffin::PrefsShortcutsPage::selectionChanged() {
  const auto* item = commands_->currentItem();
  const auto* command = item ? commandDeclaration(item->data(0, Qt::UserRole).toString()) : nullptr;
  selectedCommand_->setText(command ? QString(command->text).remove('&') : tr("No matching commands."));
  sequence_->setEnabled(command);
  restore_->setEnabled(command);
  clear_->setEnabled(command);
  const QSignalBlocker blocked(sequence_);
  sequence_->setKeySequence(command ? ShortcutSettings::instance().sequence(command->id, command->shortcut)
                                    : QKeySequence());
  validate();
}

QStringList muffin::PrefsShortcutsPage::conflicts() const {
  QStringList result;
  const auto* selected = commands_->currentItem();
  const auto sequence = sequence_->keySequence();
  if (!selected || sequence.isEmpty())
    return result;
  const auto id = selected->data(0, Qt::UserRole).toString();
  for (const auto& command : commandDeclarations()) {
    const auto other = ShortcutSettings::instance().sequence(command.id, command.shortcut);
    if (command.handler && command.id != id && !other.isEmpty() &&
        (other.matches(sequence) != QKeySequence::NoMatch || sequence.matches(other) != QKeySequence::NoMatch))
      result.append(command.id);
  }
  return result;
}

void muffin::PrefsShortcutsPage::validate() {
  const auto* selected = commands_->currentItem();
  const auto* command = selected ? commandDeclaration(selected->data(0, Qt::UserRole).toString()) : nullptr;
  const auto sequence = sequence_->keySequence();
  const bool reserved = command && ShortcutSettings::isReserved(sequence) && sequence != command->shortcut;
  const auto collisions = conflicts();
  apply_->setText(collisions.isEmpty() ? tr("Apply") : tr("Reassign"));
  apply_->setEnabled(command && !reserved &&
                     sequence != ShortcutSettings::instance().sequence(command->id, command->shortcut));
  if (reserved)
    message_->setText(
        tr("This key is reserved for text entry or cursor navigation. Use a modifier key or a function key."));
  else if (!collisions.isEmpty()) {
    QStringList labels;
    for (const auto& id : collisions)
      labels.append(QString(commandDeclaration(id)->text).remove('&'));
    message_->setText(
        tr("Used by: %1. Reassigning will clear those shortcuts.").arg(labels.join(QStringLiteral(", "))));
  } else
    message_->setText(tr("Clear and Apply removes the shortcut. Use Default and Apply restores it."));
}

void muffin::PrefsShortcutsPage::apply() {
  const auto* selected = commands_->currentItem();
  if (!selected || !apply_->isEnabled())
    return;
  const auto id = selected->data(0, Qt::UserRole).toString();
  const auto sequence = sequence_->keySequence();
  const auto collisions = conflicts();
  for (const auto& other : collisions)
    ShortcutSettings::instance().setOverride(other, {});
  if (sequence == commandDeclaration(id)->shortcut)
    ShortcutSettings::instance().clearOverride(id);
  else
    ShortcutSettings::instance().setOverride(id, sequence);
}
