#include "parser/MarkdownSerializer.h"

#include <QStringList>

namespace muffin {
namespace {

QString repeated(QString value, int count) {
  QString out;
  for (int i = 0; i < count; ++i) out += value;
  return out;
}

}  // namespace

QString MarkdownSerializer::serializeDocument(const MarkdownDocument& document) const {
  return serializeBlock(document.root());
}

QString MarkdownSerializer::serializeBlock(const MarkdownNode& node) const {
  switch (node.type()) {
    case BlockType::Document:
      return serializeChildren(node, QStringLiteral("\n\n"));
    case BlockType::Paragraph:
    case BlockType::TableCell:
      return serializeInlineList(node.inlines());
    case BlockType::Heading:
      return serializeHeading(node);
    case BlockType::BlockQuote: {
      const QString body = serializeChildren(node, QStringLiteral("\n\n"));
      QStringList lines = body.split('\n');
      for (QString& line : lines) line.prepend(QStringLiteral("> "));
      return lines.join('\n');
    }
    case BlockType::List:
      return serializeList(node);
    case BlockType::ListItem:
      return serializeChildren(node, QStringLiteral("\n"));
    case BlockType::ThematicBreak:
      return QStringLiteral("---");
    case BlockType::CodeFence:
      return serializeCodeFence(node);
    case BlockType::HtmlBlock:
      return node.literal();
    case BlockType::MathBlock:
      return QStringLiteral("$$\n%1\n$$").arg(node.literal());
    case BlockType::Table:
      return serializeTable(node);
    default:
      return serializeChildren(node, QStringLiteral("\n\n"));
  }
}

QString MarkdownSerializer::serializeInlineList(const QVector<InlineNode>& nodes) const {
  QString out;
  for (const auto& node : nodes) {
    out += serializeInline(node);
  }
  return out;
}

QString MarkdownSerializer::serializeInline(const InlineNode& node) const {
  switch (node.type()) {
    case InlineType::Text:
      return node.text();
    case InlineType::SoftBreak:
      return QStringLiteral("\n");
    case InlineType::LineBreak:
      return QStringLiteral("  \n");
    case InlineType::Code:
      return QStringLiteral("`%1`").arg(node.text());
    case InlineType::HtmlInline:
      return node.text();
    case InlineType::Emphasis:
      return QStringLiteral("%1%2%1").arg(
          node.marker().isEmpty() ? QStringLiteral("*") : node.marker(),
          serializeInlineList(node.children()));
    case InlineType::Strong:
      return QStringLiteral("%1%2%1").arg(
          node.marker().isEmpty() ? QStringLiteral("**") : node.marker(),
          serializeInlineList(node.children()));
    case InlineType::Strikethrough:
      return QStringLiteral("~~%1~~").arg(serializeInlineList(node.children()));
    case InlineType::Link:
      return QStringLiteral("[%1](%2%3)").arg(
          serializeInlineList(node.children()),
          node.href(),
          node.title().isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(node.title()));
    case InlineType::Image:
      return QStringLiteral("![%1](%2%3)").arg(
          node.alt(),
          node.href(),
          node.title().isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(node.title()));
    case InlineType::InlineMath:
      return QStringLiteral("$%1$").arg(node.text());
    default:
      return node.text();
  }
}

QString MarkdownSerializer::serializeChildren(const MarkdownNode& node, QString separator) const {
  QStringList blocks;
  for (const auto& child : node.children()) {
    const QString block = serializeBlock(*child);
    if (!block.isEmpty()) {
      blocks.push_back(block);
    }
  }
  return blocks.join(separator);
}

QString MarkdownSerializer::serializeHeading(const MarkdownNode& node) const {
  return QStringLiteral("%1 %2").arg(
      repeated(QStringLiteral("#"), qMax(1, node.headingLevel())),
      serializeInlineList(node.inlines()));
}

QString MarkdownSerializer::serializeList(const MarkdownNode& node) const {
  QStringList items;
  int index = node.listStart();
  for (const auto& child : node.children()) {
    items.push_back(serializeListItem(*child, index, node.listKind()));
    ++index;
  }
  return items.join('\n');
}

QString MarkdownSerializer::serializeListItem(const MarkdownNode& node, int index, ListKind kind) const {
  const QString marker = kind == ListKind::Ordered
                             ? QStringLiteral("%1. ").arg(index)
                             : QStringLiteral("- ");
  QString content = serializeChildren(node, QStringLiteral("\n"));
  if (content.isEmpty()) {
    content = serializeInlineList(node.inlines());
  }
  QStringList lines = content.split('\n');
  if (lines.isEmpty()) {
    return marker.trimmed();
  }
  lines[0].prepend(marker);
  for (int i = 1; i < lines.size(); ++i) {
    lines[i].prepend(QStringLiteral("  "));
  }
  return lines.join('\n');
}

QString MarkdownSerializer::serializeTable(const MarkdownNode& node) const {
  if (node.children().empty()) {
    return QString();
  }

  QStringList rows;
  for (const auto& row : node.children()) {
    QStringList cells;
    for (const auto& cell : row->children()) {
      cells.push_back(escapeTableCell(serializeBlock(*cell)));
    }
    rows.push_back(QStringLiteral("| %1 |").arg(cells.join(QStringLiteral(" | "))));
  }

  const int columns = static_cast<int>(node.children().front()->children().size());
  QStringList delimiters;
  const auto alignments = node.tableAlignments();
  for (int i = 0; i < columns; ++i) {
    delimiters.push_back(tableDelimiter(i < alignments.size() ? alignments[i] : TableAlignment::None));
  }
  rows.insert(1, QStringLiteral("| %1 |").arg(delimiters.join(QStringLiteral(" | "))));
  return rows.join('\n');
}

QString MarkdownSerializer::serializeCodeFence(const MarkdownNode& node) const {
  const QString literal = node.literal();
  const QString closingSeparator = literal.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n");
  return QStringLiteral("```%1\n%2%3```").arg(node.codeLanguage(), literal, closingSeparator);
}

QString MarkdownSerializer::escapeTableCell(QString text) const {
  return text.replace('|', QStringLiteral("\\|")).replace('\n', QStringLiteral("<br>"));
}

QString MarkdownSerializer::tableDelimiter(TableAlignment alignment) const {
  switch (alignment) {
    case TableAlignment::Left:
      return QStringLiteral(":---");
    case TableAlignment::Center:
      return QStringLiteral(":---:");
    case TableAlignment::Right:
      return QStringLiteral("---:");
    case TableAlignment::None:
    default:
      return QStringLiteral("---");
  }
}

}  // namespace muffin
