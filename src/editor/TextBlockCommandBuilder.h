#pragma once

#include "app/DocumentSession.h"
#include "edit/EditTransaction.h"
#include "editor/BlockEditContext.h"
#include "editor/CursorPosition.h"

#include <QVector>

namespace muffin {

class TextBlockCommandBuilder {
public:
  enum class Operation {
    InsertText,
    Backspace,
    Delete,
    Enter
  };

  struct Command {
    bool valid = false;
    bool handled = false;
    EditTransaction::Kind kind = EditTransaction::Kind::InsertText;
    QString label;
    qsizetype sourceStart = -1;
    qsizetype removedLength = 0;
    QString insertedText;
    CursorPosition preferredCursor;
    qsizetype fallbackSourceOffset = -1;
    QVector<LocalEditNodeHint> nodeHints;
    bool preferLaterEmptyAtOffset = false;

    bool hasLocalEdit() const;
  };

  TextBlockCommandBuilder(DocumentSession* session, const BlockEditContextResolver* resolver);

  Command buildTextEdit(const BlockEditContext& context, Operation operation, QString text = {}) const;
  Command buildInsertBlockBefore(const BlockEditContext& context) const;
  Command buildInsertBlockAfter(const BlockEditContext& context) const;
  Command buildSplitTextBlock(const BlockEditContext& context, qsizetype contentOffset) const;
  Command buildMergeWithPreviousParagraph(const BlockEditContext& context) const;
  Command buildMergeWithNextParagraph(const BlockEditContext& context) const;
  Command buildSplitListItem(const BlockEditContext& context) const;
  Command buildExitListItem(const BlockEditContext& context) const;
  Command buildOutdentListItem(const BlockEditContext& context) const;
  Command buildIndentListItem(const BlockEditContext& context) const;

private:
  CursorPosition cursorFor(NodeId blockId, qsizetype offset) const;

  DocumentSession* session_ = nullptr;
  const BlockEditContextResolver* resolver_ = nullptr;
};

}  // namespace muffin
