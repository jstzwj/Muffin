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
  Highlight,
  Subscript,
  Superscript,
  FootnoteReference,
  Unknown
};

enum class ListKind {
  None,
  Bullet,
  Ordered
};

// GitHub-style alert, recognized when a blockquote's first line is `[!NOTE]`/`[!TIP]`/...
// (cmark parses the blockquote; this kind is annotated in a post-parse pass so the renderer can
// draw a themed card instead of a plain quote bar).
enum class AlertKind {
  None,
  Note,
  Tip,
  Important,
  Warning,
  Caution
};

// Display-math block delimiter kind. cmark-gfm only parses `$$`, so LaTeX-style
// `\[ ... \]` blocks are normalized to `$$` for parsing and re-expanded on serialize.
enum class MathDelimiter {
  Dollar,
  Bracket
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
