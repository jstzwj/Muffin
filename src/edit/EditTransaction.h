#pragma once

#include "editor/CursorPosition.h"
#include "document/NodeId.h"
#include "document/MarkdownNode.h"

#include <QString>
#include <QVector>

#include <memory>
#include <variant>

namespace muffin {

struct DocumentSnapshot {
  QString markdownText;
  CursorPosition cursor;
  // When replayed via applyMarkdownText, demote every pending marker at these source offsets back
  // to a paragraph, so redo of a lazy-marker edit stays consistent with the forward (demoted)
  // state. Carries all in-progress markers (not just one) so multi-marker documents demote
  // consistently; loaded structural blocks are excluded because they are never paragraphs.
  QVector<qsizetype> demoteAtOffsets;
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

enum class NodeAttribute {
  Unknown,
  HeadingLevel,
  ListKind,
  ListStart,
  ListTight,
  TaskChecked,
  CodeLanguage,
  TableAlignments,
  TableRowIsHeader
};

using NodeAttributeValue = std::variant<std::monostate, int, bool, QString, ListKind, QVector<TableAlignment>>;

bool nodeAttributeAcceptsValue(NodeAttribute attribute, const NodeAttributeValue& value);

struct TableCommand {
  NodeId tableId;
  int tableIndex = -1;
  int beforeRow = -1;
  int beforeColumn = -1;
  int afterRow = -1;
  int afterColumn = -1;
  std::unique_ptr<MarkdownNode> beforeTable;
  std::unique_ptr<MarkdownNode> afterTable;
  CursorPosition beforeCursor;
  CursorPosition afterCursor;

  TableCommand() = default;
  TableCommand(
      NodeId tableId,
      int tableIndex,
      int beforeRow,
      int beforeColumn,
      int afterRow,
      int afterColumn,
      std::unique_ptr<MarkdownNode> beforeTable,
      std::unique_ptr<MarkdownNode> afterTable,
      CursorPosition beforeCursor,
      CursorPosition afterCursor);
  TableCommand(const TableCommand& other);
  TableCommand& operator=(const TableCommand& other);
  TableCommand(TableCommand&&) noexcept = default;
  TableCommand& operator=(TableCommand&&) noexcept = default;

  bool isValid() const;
};

struct InsertNodeCommand {
  NodeId nodeId;
  BlockType nodeType = BlockType::Unknown;
  int nodeIndex = -1;
  TextDelta delta;
  qsizetype nodeSourceStart = -1;
  std::unique_ptr<MarkdownNode> insertedNode;
  CursorPosition beforeCursor;
  CursorPosition afterCursor;
  QVector<NodeId> affectedNodes;

  InsertNodeCommand() = default;
  InsertNodeCommand(
      NodeId nodeId,
      BlockType nodeType,
      int nodeIndex,
      TextDelta delta,
      qsizetype nodeSourceStart,
      std::unique_ptr<MarkdownNode> insertedNode,
      CursorPosition beforeCursor,
      CursorPosition afterCursor,
      QVector<NodeId> affectedNodes);
  InsertNodeCommand(const InsertNodeCommand& other);
  InsertNodeCommand& operator=(const InsertNodeCommand& other);
  InsertNodeCommand(InsertNodeCommand&&) noexcept = default;
  InsertNodeCommand& operator=(InsertNodeCommand&&) noexcept = default;

  bool isValid() const;
};

struct ReplaceNodeCommand {
  NodeId nodeId;
  BlockType nodeType = BlockType::Unknown;
  int nodeIndex = -1;
  std::unique_ptr<MarkdownNode> beforeNode;
  std::unique_ptr<MarkdownNode> afterNode;
  CursorPosition beforeCursor;
  CursorPosition afterCursor;
  QVector<NodeId> affectedNodes;

  ReplaceNodeCommand() = default;
  ReplaceNodeCommand(
      NodeId nodeId,
      BlockType nodeType,
      int nodeIndex,
      std::unique_ptr<MarkdownNode> beforeNode,
      std::unique_ptr<MarkdownNode> afterNode,
      CursorPosition beforeCursor,
      CursorPosition afterCursor,
      QVector<NodeId> affectedNodes);
  ReplaceNodeCommand(const ReplaceNodeCommand& other);
  ReplaceNodeCommand& operator=(const ReplaceNodeCommand& other);
  ReplaceNodeCommand(ReplaceNodeCommand&&) noexcept = default;
  ReplaceNodeCommand& operator=(ReplaceNodeCommand&&) noexcept = default;

  bool isValid() const;
};

struct RemoveNodeCommand {
  NodeId nodeId;
  BlockType nodeType = BlockType::Unknown;
  int nodeIndex = -1;
  TextDelta delta;
  qsizetype nodeSourceStart = -1;
  std::unique_ptr<MarkdownNode> removedNode;
  CursorPosition beforeCursor;
  CursorPosition afterCursor;
  QVector<NodeId> affectedNodes;

  RemoveNodeCommand() = default;
  RemoveNodeCommand(
      NodeId nodeId,
      BlockType nodeType,
      int nodeIndex,
      TextDelta delta,
      qsizetype nodeSourceStart,
      std::unique_ptr<MarkdownNode> removedNode,
      CursorPosition beforeCursor,
      CursorPosition afterCursor,
      QVector<NodeId> affectedNodes);
  RemoveNodeCommand(const RemoveNodeCommand& other);
  RemoveNodeCommand& operator=(const RemoveNodeCommand& other);
  RemoveNodeCommand(RemoveNodeCommand&&) noexcept = default;
  RemoveNodeCommand& operator=(RemoveNodeCommand&&) noexcept = default;

  bool isValid() const;
};

struct SetNodeAttrCommand {
  NodeId nodeId;
  BlockType nodeType = BlockType::Unknown;
  int nodeIndex = -1;
  NodeAttribute attribute = NodeAttribute::Unknown;
  NodeAttributeValue beforeValue;
  NodeAttributeValue afterValue;
  CursorPosition beforeCursor;
  CursorPosition afterCursor;
  QVector<NodeId> affectedNodes;

  SetNodeAttrCommand() = default;
  SetNodeAttrCommand(
      NodeId nodeId,
      BlockType nodeType,
      int nodeIndex,
      NodeAttribute attribute,
      NodeAttributeValue beforeValue,
      NodeAttributeValue afterValue,
      CursorPosition beforeCursor,
      CursorPosition afterCursor,
      QVector<NodeId> affectedNodes);

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
    TextDeltaCommand,
    TableCommand,
    InsertNodeCommand,
    ReplaceNodeCommand,
    RemoveNodeCommand,
    SetNodeAttrCommand
  };

  EditTransaction() = default;
  EditTransaction(const EditTransaction& other);
  EditTransaction& operator=(const EditTransaction& other);
  EditTransaction(EditTransaction&&) noexcept = default;
  EditTransaction& operator=(EditTransaction&&) noexcept = default;
  EditTransaction(Kind kind, QString label, DocumentSnapshot before, DocumentSnapshot after);
  EditTransaction(
      Kind kind,
      QString label,
      TextDeltaCommand command);
  EditTransaction(
      Kind kind,
      QString label,
      TableCommand command);
  EditTransaction(
      Kind kind,
      QString label,
      InsertNodeCommand command);
  EditTransaction(
      Kind kind,
      QString label,
      ReplaceNodeCommand command);
  EditTransaction(
      Kind kind,
      QString label,
      RemoveNodeCommand command);
  EditTransaction(
      Kind kind,
      QString label,
      SetNodeAttrCommand command);

  Kind kind() const;
  QString label() const;
  Storage storage() const;
  bool isSnapshot() const;
  bool isTextDeltaCommand() const;
  bool isTableCommand() const;
  bool isInsertNodeCommand() const;
  bool isReplaceNodeCommand() const;
  bool isRemoveNodeCommand() const;
  bool isSetNodeAttrCommand() const;
  const DocumentSnapshot& before() const;
  const DocumentSnapshot& after() const;
  const TextDeltaCommand& textDeltaCommand() const;
  const TableCommand& tableCommand() const;
  const InsertNodeCommand& insertNodeCommand() const;
  const ReplaceNodeCommand& replaceNodeCommand() const;
  const RemoveNodeCommand& removeNodeCommand() const;
  const SetNodeAttrCommand& setNodeAttrCommand() const;
  bool isValid() const;

  void mergeTextDelta(const TextDeltaCommand& next);
  void updateAfterCursor(const CursorPosition& cursor);

  TextDeltaCommand& textDeltaCommandMut();

private:
  Storage storage_ = Storage::Invalid;
  Kind kind_ = Kind::ReplaceDocumentText;
  QString label_;
  DocumentSnapshot before_;
  DocumentSnapshot after_;
  TextDeltaCommand textDeltaCommand_;
  TableCommand tableCommand_;
  InsertNodeCommand insertNodeCommand_;
  ReplaceNodeCommand replaceNodeCommand_;
  RemoveNodeCommand removeNodeCommand_;
  SetNodeAttrCommand setNodeAttrCommand_;
};

}  // namespace muffin
