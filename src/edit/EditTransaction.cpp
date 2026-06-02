#include "edit/EditTransaction.h"

#include <utility>

namespace muffin {

bool TextDelta::isValid() const {
  return start >= 0 && (!removedText.isEmpty() || !insertedText.isEmpty()) && removedText != insertedText;
}

bool TextDeltaCommand::isValid() const {
  return delta.isValid() && beforeCursor.isValid() && afterCursor.isValid();
}

EditTransaction::EditTransaction(Kind kind, QString label, DocumentSnapshot before, DocumentSnapshot after)
    : storage_(Storage::Snapshot), kind_(kind), label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}

EditTransaction::EditTransaction(
    Kind kind,
    QString label,
    TextDeltaCommand command)
    : storage_(Storage::TextDeltaCommand),
      kind_(kind),
      label_(std::move(label)),
      textDeltaCommand_(std::move(command)) {}

EditTransaction::Kind EditTransaction::kind() const {
  return kind_;
}

QString EditTransaction::label() const {
  return label_;
}

EditTransaction::Storage EditTransaction::storage() const {
  return storage_;
}

bool EditTransaction::isSnapshot() const {
  return storage_ == Storage::Snapshot;
}

bool EditTransaction::isTextDeltaCommand() const {
  return storage_ == Storage::TextDeltaCommand;
}

const DocumentSnapshot& EditTransaction::before() const {
  return before_;
}

const DocumentSnapshot& EditTransaction::after() const {
  return after_;
}

const TextDeltaCommand& EditTransaction::textDeltaCommand() const {
  return textDeltaCommand_;
}

bool EditTransaction::isValid() const {
  if (isSnapshot()) {
    return before_.markdownText != after_.markdownText;
  }
  if (isTextDeltaCommand()) {
    return textDeltaCommand_.isValid();
  }
  return false;
}

}  // namespace muffin
