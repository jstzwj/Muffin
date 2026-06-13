#pragma once

#include "document/DocumentSession.h"
#include "document/MarkdownTypes.h"
#include "document/SourceRangeUtil.h"
#include "edit/EditTransaction.h"
#include "editor/EditorContextHolder.h"

#include <QObject>

namespace muffin {

class MarkdownNode;

class ParagraphController final : public QObject, private EditorContextHolder {
  Q_OBJECT

public:
  explicit ParagraphController(QObject* parent = nullptr);

  using EditorContextHolder::setContext;

  // Query methods for MainWindow enable/disable logic
  int currentHeadingLevel() const;   // 0 = paragraph, 1-6 = heading
  bool isOnEditableBlock() const;    // Paragraph or Heading (not ListItem)
  // Eligible for insert-paragraph-before/after: Paragraph, Heading, CodeFence, or MathBlock.
  // Broader than isOnEditableBlock() because a paragraph may be inserted adjacent to a
  // literal block without editing its content.
  bool canInsertAdjacentParagraph() const;

  // Heading commands
  bool setHeadingLevel(int level);   // 0 = paragraph, 1-6 = heading
  bool promoteHeading();             // H2→H1
  bool demoteHeading();              // H1→H2

  // Block insert commands
  bool insertFormulaBlock();
  bool insertCodeBlock();
  bool insertLinkReference();
  bool insertFootnoteDefinition();
  bool insertHorizontalRule();

  // Toggle commands
  bool toggleCodeBlock();
  bool toggleFormulaBlock();

  // Block conversion commands
  bool toggleQuote();
  bool convertToOrderedList();
  bool convertToUnorderedList();
  bool convertToTaskList();

  // Paragraph insert commands
  bool insertParagraphBefore();
  bool insertParagraphAfter();

private:
  struct BlockContext {
    MarkdownNode* node = nullptr;
    MarkdownNode* editableNode = nullptr;
    NodeId blockId;
    BlockType blockType = BlockType::Unknown;
    qsizetype blockStart = -1;
    qsizetype blockEnd = -1;
    qsizetype contentStart = -1;
    qsizetype contentEnd = -1;
    QString contentText;
    int headingLevel = 0;
    qsizetype cursorSourceOffset = -1;
  };

  bool resolveBlockContext(BlockContext& context) const;
  // Resolves context for insert-paragraph-before/after: succeeds for Paragraph/Heading
  // (delegating to resolveBlockContext) and for CodeFence/MathBlock (using the block's line
  // span via SourceRangeUtil::blockLineSpan). Fills only blockStart/blockEnd for literal blocks.
  bool resolveInsertionContext(BlockContext& context) const;
  // Shared precondition: a session + caret + collapsed selection (no multi-block selection).
  bool hasCollapsedBlockCursor() const;
  qsizetype nodeSourceStart(const MarkdownNode& node) const;
  qsizetype nodeSourceEnd(const MarkdownNode& node) const;
  bool convertLiteralBlockToParagraph(MarkdownNode& node);
  bool convertLiteralBlockToType(MarkdownNode& node, BlockType targetType);
  bool insertCodeBlockWithSplit();
  bool insertFormulaBlockWithSplit();
  bool insertBlockAfterNode(MarkdownNode& node, const QString& blockSource,
                            qsizetype cursorInBlock, const QString& label);
  bool applyBlockDelta(
      EditTransaction::Kind kind,
      const QString& label,
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      qsizetype nextCursorSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {},
      bool structureEdit = false);
  CursorPosition cursorForSourceOffset(qsizetype sourceOffset) const;

};

}  // namespace muffin
