#pragma once

#include "editor/CursorPosition.h"
#include "document/NodeId.h"

#include <QString>
#include <QVector>

namespace muffin {

struct DocumentSnapshot {
  QString markdownText;
  CursorPosition cursor;
};

struct TextDelta {
  qsizetype start = -1;
  QString removedText;
  QString insertedText;

  bool isValid() const;
};

struct TextDeltaCommand {
  TextDelta delta;
  CursorPosition beforeCursor;
  CursorPosition afterCursor;
  QVector<NodeId> affectedNodes;

  bool isValid() const;
};

class EditTransaction {
public:
  enum class Kind {
    ReplaceDocumentText,
    InsertText,
    DeleteText,
    SplitParagraph
  };

  enum class Storage {
    Invalid,
    Snapshot,
    TextDeltaCommand
  };

  EditTransaction() = default;
  EditTransaction(Kind kind, QString label, DocumentSnapshot before, DocumentSnapshot after);
  EditTransaction(
      Kind kind,
      QString label,
      TextDeltaCommand command);

  Kind kind() const;
  QString label() const;
  Storage storage() const;
  bool isSnapshot() const;
  bool isTextDeltaCommand() const;
  const DocumentSnapshot& before() const;
  const DocumentSnapshot& after() const;
  const TextDeltaCommand& textDeltaCommand() const;
  bool isValid() const;

private:
  Storage storage_ = Storage::Invalid;
  Kind kind_ = Kind::ReplaceDocumentText;
  QString label_;
  DocumentSnapshot before_;
  DocumentSnapshot after_;
  TextDeltaCommand textDeltaCommand_;
};

}  // namespace muffin
