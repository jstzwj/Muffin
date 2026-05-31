#pragma once

#include "document/MarkdownDocument.h"

namespace muffin {

class MarkdownSerializer {
public:
  QString serializeDocument(const MarkdownDocument& document) const;
  QString serializeBlock(const MarkdownNode& node) const;
  QString serializeInlineList(const QVector<InlineNode>& nodes) const;
  QString serializeInline(const InlineNode& node) const;

private:
  QString serializeChildren(const MarkdownNode& node, QString separator) const;
  QString serializeHeading(const MarkdownNode& node) const;
  QString serializeList(const MarkdownNode& node) const;
  QString serializeListItem(const MarkdownNode& node, int index, ListKind kind) const;
  QString serializeTable(const MarkdownNode& node) const;
  QString serializeCodeFence(const MarkdownNode& node) const;
  QString escapeTableCell(QString text) const;
  QString tableDelimiter(TableAlignment alignment) const;
};

}  // namespace muffin
