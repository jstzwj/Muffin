#pragma once

#include "editor/EditTransaction.h"

namespace Muffin {

class EditTransactionValidator {
public:
    static EditTransactionValidationResult validate(const EditTransaction& transaction);
};

} // namespace Muffin
