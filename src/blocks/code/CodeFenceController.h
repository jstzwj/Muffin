#pragma once

#include "blocks/literal/LiteralBlockController.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/EditorContext.h"

#include <QObject>

#include <functional>

namespace muffin {

class CodeFenceController final : public QObject {
  Q_OBJECT

public:
  explicit CodeFenceController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);

  NodeId currentCodeFenceId() const;
  bool isEditing() const;
  bool enterEditMode();
  bool exitEditMode();

  bool insertText(QString text);
  // Shift+Tab dedent: removes one codeIndentUnit of leading space from every line the current
  // selection covers (min-rule: never removes more spaces than the line has). Returns false when
  // there is no selection, so the caller can fall back to inserting a tab.
  bool dedentSelection();
  bool deleteBackward();
  bool deleteForward();
  bool deleteSelection();
  bool setLanguage(QString language);
  bool setLanguageFor(NodeId codeId, QString language);
  bool setContent(QString content);
  // Code Tools menu helpers: copy raw content and indent/dedent by line.
  QString currentContent() const;
  bool indentSelection();
  bool indentWholeBlock();
  bool dedentWholeBlock();
  bool hasPendingTrailingNewline() const;
  void clearPendingTrailingNewline();
  QString tabText() const;

signals:
  void codeCommandRejected(QString reason);

private:
  enum class IndentScope {
    Selection,        // the selected lines; requires a non-empty selection
    SelectionOrLine,  // selected lines, or the caret's line when nothing is selected
    WholeBlock,       // every line in the block
  };
  bool setLanguageForCodeFence(NodeId requestedCodeId, QString language);
  // Shared indent/dedent transform. indent=true inserts a unit at each in-scope line start;
  // false strips up to a unit of leading space. Returns false only when Selection scope has no
  // selection, so callers (e.g. Tab) can fall back to inserting an indent unit.
  bool adjustIndent(bool indent, IndentScope scope);

  LiteralBlockController literal_;
  EditorContext ctx_;
};

}  // namespace muffin
