#pragma once

#include "editor/CursorPosition.h"

#include <QString>

namespace muffin {

struct DocumentSnapshot {
  QString markdownText;
  CursorPosition cursor;
};

class EditTransaction {
public:
  enum class Kind {
    ReplaceDocumentText,
    InsertText,
    DeleteText,
    SplitParagraph
  };

  EditTransaction() = default;
  EditTransaction(Kind kind, QString label, DocumentSnapshot before, DocumentSnapshot after);

  Kind kind() const;
  QString label() const;
  const DocumentSnapshot& before() const;
  const DocumentSnapshot& after() const;
  bool isValid() const;

private:
  Kind kind_ = Kind::ReplaceDocumentText;
  QString label_;
  DocumentSnapshot before_;
  DocumentSnapshot after_;
};

}  // namespace muffin
