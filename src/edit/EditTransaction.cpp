#include "edit/EditTransaction.h"

#include <utility>

namespace muffin {

EditTransaction::EditTransaction(Kind kind, QString label, DocumentSnapshot before, DocumentSnapshot after)
    : kind_(kind), label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}

EditTransaction::Kind EditTransaction::kind() const {
  return kind_;
}

QString EditTransaction::label() const {
  return label_;
}

const DocumentSnapshot& EditTransaction::before() const {
  return before_;
}

const DocumentSnapshot& EditTransaction::after() const {
  return after_;
}

bool EditTransaction::isValid() const {
  return before_.markdownText != after_.markdownText;
}

}  // namespace muffin
