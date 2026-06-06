#pragma once

namespace muffin {

enum class BlockType {
  Document,
  Paragraph,
  Heading,
  BlockQuote,
  List,
  ListItem,
  ThematicBreak,
  FrontMatter,
  CodeFence,
  HtmlBlock,
  MathBlock,
  Table,
  TableRow,
  TableCell,
  FootnoteDefinition,
  LinkDefinition,
  Unknown
};

enum class FrontMatterFormat {
  None,
  Yaml,
  Toml,
  Json
};

enum class InlineType {
  Text,
  Emphasis,
  Strong,
  Code,
  Link,
  Image,
  HtmlInline,
  SoftBreak,
  LineBreak,
  Strikethrough,
  TaskMarker,
  InlineMath,
  Unknown
};

enum class ListKind {
  None,
  Bullet,
  Ordered
};

enum class TableAlignment {
  None,
  Left,
  Center,
  Right
};

enum class CloneMode {
  PreserveIds,
  RegenerateIds
};

}  // namespace muffin
