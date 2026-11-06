#include "document/BlockStructure.h"
#include "document/DocumentSession.h"
#include "document/HtmlContainerStructure.h"
#include "document/ListItemSource.h"
#include "document/InlineBlockConversion.h"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>

using namespace muffin;
static void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
static QString inlineText(const QVector<InlineNode>& inlines) {
  QString text;
  for (const auto& node : inlines) {
    if (node.type() == InlineType::SoftBreak)
      text += u' ';
    else if (node.type() == InlineType::LineBreak)
      text += u'\n';
    else
      text += node.children().isEmpty() ? node.text() : inlineText(node.children());
  }
  return text;
}
static QString plainText(const MarkdownNode& node) {
  QString text = inlineText(node.inlines());
  for (const auto& child : node.children())
    text += plainText(*child);
  return text;
}
static void completeContainersAndSource() {
  DocumentSession session;
  session.setMarkdownText("# Before\n\n<div align='center'>\n\n# Inner\n\n<section>\n\nText\n\n</section>\n\n</div>\n\n"
                          "- list\n  - child\n\n    extra\n\n```cpp\nint x;\n```\n\nLast",
                          false);
  const auto& doc = session.document();
  const auto& blocks = doc.blockStructure();
  require(blocks.units().size() == 5, "paired HTML, complete list and code must be atomic units");
  const auto container = blocks.targetAt(1);
  const auto* unit = blocks.resolve(container);
  const auto source = session.markdownText().mid(unit->start, unit->contentEnd - unit->start);
  require(source.startsWith("<div") && source.endsWith("</div>"),
          "HTML source must include both boundaries and all descendants");
  const auto& nodes = doc.root().children();
  require(blocks.indexFor(doc, nodes[3]->id()) == 1,
          "nested HTML boundaries must resolve to their outer operation unit");
  const auto moved = blocks.move(session.markdownText(), container, blocks.targetAt(4), true);
  require(moved.valid && moved.text.toString().endsWith(source + "\n\n"),
          "moving an HTML container must keep its complete original source");
  const auto removed = blocks.remove(session.markdownText(), container);
  require(removed.valid && !removed.text.toString().contains("<div") && !removed.text.toString().contains("</section>"),
          "deleting a container must remove all of its boundaries");
  const auto duplicate = blocks.duplicate(session.markdownText(), container);
  require(duplicate.valid && duplicate.text.toString().count(source) == 2,
          "duplicating a container must duplicate complete source exactly");
}
static void movementAndMapping() {
  DocumentSession session;
  session.setMarkdownText("Alpha\n\nBravo\n\nCharlie", false);
  const auto& blocks = session.document().blockStructure();
  const auto& text = session.markdownText();
  const auto down = blocks.move(text, blocks.targetAt(0), blocks.targetAt(2), true, 2);
  require(down.valid && down.text.toString() == "Bravo\n\nCharlie\n\nAlpha\n\n",
          "moving to EOF must add necessary separators without merging paragraphs");
  require(down.change.mapOffset(2) == down.cursor && down.change.reversed().mapOffset(down.cursor) == 2,
          "reading positions inside a moved block must map in both directions");
  require(down.change.mapOffset(8) == 1, "unmoved source must retain its relative reading offset");
  const auto up = blocks.move(text, blocks.targetAt(2), blocks.targetAt(0), false);
  require(up.valid && up.text.toString() == "Charlie\n\nAlpha\n\nBravo\n\n",
          "an EOF block without a final newline must move intact");
  require(!blocks.move(text, blocks.targetAt(0), blocks.targetAt(1), false).valid &&
              !blocks.move(text, blocks.targetAt(1), blocks.targetAt(0), true).valid,
          "drops at either existing boundary must be no-ops");
  const auto stale = blocks.targetAt(0);
  session.setMarkdownText("Other\n\nText", false);
  require(!session.document().blockStructure().resolve(stale), "old-generation targets must be rejected");
}
static void sparseMoveAndIndentedSource() {
  DocumentSession session;
  const QString middle = QStringLiteral("```\n") + QString(512 * 1024, u'x') + "\n```\n\n";
  session.setMarkdownText("    indented code\n\n" + middle + "End", false);
  const auto& blocks = session.document().blockStructure();
  const auto moved = blocks.move(session.markdownText(), blocks.targetAt(0), blocks.targetAt(2), true);
  require(moved.valid && moved.text.toString().endsWith("    indented code\n\n"),
          "moving indented code must preserve its four-space gutter");
  require(moved.text.backingStoreBytes() < session.markdownText().backingStoreBytes() + 1024,
          "a distant move must not copy intervening text into the piece-table change buffer");
  require(moved.change.edits.size() == 2 && moved.change.edits[0].removed < 64,
          "a distant move must describe only its removal and insertion");
}
static void frontMatterAndMalformedBoundaries() {
  DocumentSession session;
  session.setMarkdownText("---\ntitle: Test\n---\n\nBody\n\nEnd", false);
  const auto& blocks = session.document().blockStructure();
  require(!blocks.units().front().movable, "front matter must remain at the beginning");
  require(!blocks.move(session.markdownText(), blocks.targetAt(1), blocks.targetAt(0), false).valid,
          "a drop before front matter must be rejected");
  session.setMarkdownText("<div>\n\nText\n\n</section>\n\nAfter", false);
  require(htmlContainerPairs(session.document().root()).isEmpty(), "mismatched tags must retain literal fallback");
}
static const MarkdownNode* firstNode(const MarkdownNode& owner, BlockType type) {
  if (owner.type() == type)
    return &owner;
  for (const auto& child : owner.children())
    if (const auto* found = firstNode(*child, type))
      return found;
  return nullptr;
}
static void listScopesAndSparseReordering() {
  DocumentSession session;
  session.setMarkdownText("- Alpha\n- Bravo\n- Charlie", false);
  const auto& document = session.document();
  const auto& structure = document.blockStructure();
  const auto& list = *document.root().children().front();
  const auto first = structure.targetFor(list.children().front()->id());
  const auto last = structure.targetFor(list.children().back()->id());
  require(first.parent == list.id() && structure.sibling(first, 1).id == list.children()[1]->id(),
          "list items must resolve in their direct sibling scope");
  const auto moved = structure.move(session.markdownText(), first, last, true, 4);
  require(moved.valid && moved.text.toString() == "- Bravo\n- Charlie\n- Alpha\n",
          "tight-list reorder must preserve item boundaries without adding blank lines");
  require(moved.change.mapOffset(4) == moved.cursor && moved.change.reversed().mapOffset(moved.cursor) == 4,
          "list item source anchors must follow a move and its inverse");
  require(!structure.canMove(first, structure.targetAt(0), true),
          "an item cannot be dropped into a different structural scope");
  session.setMarkdownText(moved.text.toString(), false);
  require(session.document().root().children().front()->children().size() == 3,
          "reordered tight items must still parse as three siblings");
  require(!session.document().blockStructure().resolve(first), "list handles must reject obsolete revisions");
}
static void numberedSubtreesAndWidthChanges() {
  DocumentSession session;
  const QString original = "9. First\n   continuation\n\n   ```cpp\n   value\n   ```\n"
                           "10. Second\n    - Nested\n\n    more\n11. Third\n";
  session.setMarkdownText(original, false);
  const auto& structure = session.document().blockStructure();
  const auto* list = firstNode(session.document().root(), BlockType::List);
  require(list && list->children().size() == 3, "numbered subtree fixture must have three direct items");
  const auto second = structure.targetFor(list->children()[1]->id());
  const auto first = structure.targetFor(list->children()[0]->id());
  const auto oldCode = original.indexOf("value") + 2;
  const auto moved = structure.move(session.markdownText(), first, second, true, oldCode);
  const QString expected = "9. Second\n   - Nested\n\n   more\n10. First\n    continuation\n\n"
                           "    ```cpp\n    value\n    ```\n11. Third\n";
  require(moved.valid && moved.text.toString() == expected,
          "numbering across 9/10 must rebase entire subtrees and preserve the original list start");
  require(moved.change.mapOffset(oldCode) == expected.indexOf("value") + 2 &&
              moved.change.reversed().mapOffset(expected.indexOf("value") + 2) == oldCode,
          "source anchors inside rebased moved code must map exactly in both directions");
  session.setMarkdownText(expected, false);
  const auto* code = firstNode(session.document().root(), BlockType::CodeFence);
  require(code && code->literal() == "value", "renumbering must preserve literal code and its ownership");
}
static void listMenusAndQuotedChildren() {
  DocumentSession session;
  session.setMarkdownText("1. One\n2. Two\n3. Three", false);
  const auto& structure = session.document().blockStructure();
  const auto& children = session.document().root().children().front()->children();
  const auto first = structure.targetFor(children[0]->id()), second = structure.targetFor(children[1]->id());
  require(structure.duplicate(session.markdownText(), second).text.toString() == "1. One\n2. Two\n3. Two\n4. Three",
          "duplicating one item must renumber only direct siblings");
  require(structure.remove(session.markdownText(), first).text.toString() == "1. Two\n2. Three",
          "deleting the first item must preserve the original numbering start");
  require(structure.insertEmptyListItem(session.markdownText(), second, true).text.toString() ==
              "1. One\n2. Two\n3. \n4. Three",
          "inserting below a list item must create a sibling rather than split the list");
  require(structure.move(session.markdownText(), second, first, false, -1, false).text.toString() ==
              "1. Two\n1. One\n1. Three",
          "list reorder must respect the repeated-one numbering preference");
  session.setMarkdownText("> - Parent\n>   - Child A\n>   - Child B\n> - Other", false);
  const auto* nested =
      firstNode(*session.document().root().children().front()->children().front()->children().front(), BlockType::List);
  require(nested && nested->children().size() == 2, "quoted sublist fixture");
  const auto& quoted = session.document().blockStructure();
  const auto a = quoted.targetFor(nested->children()[0]->id()), b = quoted.targetFor(nested->children()[1]->id());
  require(quoted.move(session.markdownText(), b, a, false).text.toString() ==
              "> - Parent\n>   - Child B\n>   - Child A\n> - Other",
          "moving quoted child items must retain every quote and indentation prefix");
  session.setMarkdownText("3. 4. Inline child\n   5. Next child", false);
  const auto* inner = firstNode(*session.document().root().children().front()->children().front(), BlockType::List);
  require(inner, "same-line nested list fixture");
  const auto target = session.document().blockStructure().targetFor(inner->children().front()->id());
  require(target.parent != inner->id(), "same-line nested markers must fall back to their complete outer item");
}
static void numberedLiteralPadding() {
  for (const auto& original :
       {QStringLiteral("9.\tFirst\n\n        code\n10. Second\n"), QStringLiteral("9.     code\n10. Second\n"),
        QStringLiteral("9.\t\tcode\n10. Second\n")}) {
    DocumentSession session;
    session.setMarkdownText(original, false);
    const auto* code = firstNode(session.document().root(), BlockType::CodeFence);
    require(code && code->isIndentedCode(), "number-width fixture must contain literal indented code");
    const auto literal = code->literal();
    const auto* list = firstNode(session.document().root(), BlockType::List);
    require(list && list->children().size() == 2, "number-width fixture must contain two siblings");
    const auto& structure = session.document().blockStructure();
    const auto moved = structure.move(session.markdownText(), structure.targetFor(list->children()[0]->id()),
                                      structure.targetFor(list->children()[1]->id()), true);
    require(moved.valid, "indented-code list item reorder must produce an edit");
    const auto oldOffset = original.indexOf("code") + 2;
    const auto newOffset = moved.text.toString().indexOf("code") + 2;
    require(moved.change.mapOffset(oldOffset) == newOffset && moved.change.reversed().mapOffset(newOffset) == oldOffset,
            "literal code anchors must survive padding normalization in both directions");
    session.setMarkdownText(moved.text.toString(), false);
    const auto* result = firstNode(session.document().root(), BlockType::CodeFence);
    if (!result || !result->isIndentedCode() || result->literal() != literal)
      std::cerr << "before=[" << original.toStdString() << "] after=[" << moved.text.toString().toStdString()
                << "] old literal=[" << literal.toStdString() << "] new literal=["
                << (result ? result->literal().toStdString() : "missing") << "]\n";
    require(result && result->isIndentedCode() && result->literal() == literal,
            "a changed number width must preserve literal code under tab padding and first-line code gutters");
  }
}
static void unterminatedLiteralBoundaries() {
  for (const auto& literal :
       {QStringLiteral("```cpp\nint x;"), QStringLiteral("~~~~\nvalue\n~~~"), QStringLiteral("```\nvalue\n    ```"),
        QStringLiteral("```\nvalue\n\u00a0```"), QStringLiteral("```\nvalue\n```\u00a0"),
        QStringLiteral("<!-- comment"), QStringLiteral("<![CDATA[value"), QStringLiteral("<?processing"),
        QStringLiteral("<script>\nvalue"), QStringLiteral("<pre>value"), QStringLiteral("<script>\n</script >"),
        QStringLiteral("<!DOCTYPE html")}) {
    DocumentSession session;
    session.setMarkdownText("Alpha\n\nBeta\n\n" + literal, false);
    const auto& structure = session.document().blockStructure();
    const auto target = structure.targetAt(2);
    require(!structure.closingSuffix(session.markdownText(), target).isEmpty(),
            "EOF-terminated literals must expose the closure needed for relocation");
    const auto up = structure.move(session.markdownText(), target, structure.targetAt(0), false);
    require(up.valid, "an unterminated literal must be movable as one unit");
    session.setMarkdownText(up.text.toString(), false);
    const auto& nodes = session.document().root().children();
    require(nodes.size() >= 3 && nodes[1]->type() == BlockType::Paragraph && plainText(*nodes[1]) == "Alpha" &&
                plainText(*nodes[2]) == "Beta",
            "relocating an unterminated literal must not swallow following paragraphs");

    session.setMarkdownText("Alpha\n\nBeta\n\n" + literal, false);
    const auto& downBlocks = session.document().blockStructure();
    const auto down = downBlocks.move(session.markdownText(), downBlocks.targetAt(0), downBlocks.targetAt(2), true, 2);
    require(down.valid && down.change.mapOffset(2) == down.cursor && down.change.reversed().mapOffset(down.cursor) == 2,
            "moving after an unterminated literal must retain bidirectional anchors");
    session.setMarkdownText(down.text.toString(), false);
    require(session.document().root().findDescendant([](const auto& node) {
      return node.type() == BlockType::Paragraph && plainText(node) == "Alpha";
    }) != nullptr,
            "a destination ending at EOF must close before the arriving block");

    session.setMarkdownText("Alpha\n\n" + literal, false);
    const auto& copies = session.document().blockStructure();
    const auto duplicate = copies.duplicate(session.markdownText(), copies.targetAt(1));
    require(duplicate.valid, "duplicating an unterminated literal must produce an edit");
    session.setMarkdownText(duplicate.text.toString(), false);
    const auto& result = session.document().root().children();
    require(result.size() >= 3 && result[1]->type() == result[2]->type() &&
                result[1]->literal() == result[2]->literal(),
            "a duplicate must remain a separate literal with identical content");
  }
  DocumentSession session;
  session.setMarkdownText("```cpp\nvalue\n```      \n\nTail", false);
  const auto& closed = session.document().blockStructure();
  require(closed.closingSuffix(session.markdownText(), closed.targetAt(0)).isEmpty(),
          "trailing whitespace on a legal closing fence must not produce an extra delimiter");
  session.setMarkdownText("<script>\n</PRE>\n\nTail", false);
  require(session.document()
              .blockStructure()
              .closingSuffix(session.markdownText(), session.document().blockStructure().targetAt(0))
              .isEmpty(),
          "HTML literal termination must match CommonMark even for a different closing raw-tag name");
  session.setMarkdownText("- First\n- Last\n\n  ```\n  value", false);
  const auto* list = firstNode(session.document().root(), BlockType::List);
  require(list && list->children().size() == 2, "unclosed nested-fence fixture");
  const auto& nested = session.document().blockStructure();
  const auto moved = nested.move(session.markdownText(), nested.targetFor(list->children()[1]->id()),
                                 nested.targetFor(list->children()[0]->id()), false);
  session.setMarkdownText(moved.text.toString(), false);
  const auto* newList = firstNode(session.document().root(), BlockType::List);
  require(moved.valid && newList && newList->children().size() == 2 && plainText(*newList->children()[1]) == "First",
          "container dedentation must still bound an unclosed nested fence after an item move");
}
static void sourcePreservingConversions() {
  for (const auto& original : {QStringLiteral("# > quoted"), QStringLiteral("# 1. numbered"), QStringLiteral("# ---"),
                               QStringLiteral("## Title ##"), QStringLiteral("# [label]: note.md"),
                               QStringLiteral("# **bold** [ref][id]"), QStringLiteral("Title\ncontinued\n---")}) {
    DocumentSession session;
    session.setMarkdownText(original, false);
    const auto& node = *session.document().root().children().front();
    const auto expected = plainText(node);
    InlineBlockFormat format;
    const auto converted = convertInlineBlock(session.markdownText(), node, format, session.parseOptions());
    require(converted.valid, "heading-to-paragraph conversion must be available");
    session.setMarkdownText(converted.text, false);
    const auto& nodes = session.document().root().children();
    require(nodes.size() == 1 && nodes.front()->type() == BlockType::Paragraph && plainText(*nodes.front()) == expected,
            "removing a heading marker must preserve inline semantics without creating a different block type");
    if (original.contains("**bold**"))
      require(converted.text == "**bold** [ref][id]", "authored inline Markdown must retain its source spelling");
  }
  DocumentSession session;
  const QString original = "## Title **bold** ##";
  session.setMarkdownText(original, false);
  for (const auto kind : {InlineBlockFormat::Paragraph, InlineBlockFormat::Heading, InlineBlockFormat::BulletList,
                          InlineBlockFormat::OrderedList, InlineBlockFormat::TaskList, InlineBlockFormat::Quote,
                          InlineBlockFormat::Code}) {
    InlineBlockFormat format;
    format.kind = kind;
    format.level = 3;
    const auto converted = convertInlineBlock(session.markdownText(), *session.document().root().children().front(),
                                              format, session.parseOptions());
    const auto old = original.indexOf("bold") + 2;
    const auto mapped = converted.change.mapOffset(old);
    require(converted.valid && converted.text.mid(mapped - 2, 4) == "bold" &&
                converted.change.reversed().mapOffset(mapped) == old && !converted.text.endsWith(" ##"),
            "each target format must remove ATX delimiters and retain reversible inline anchors");
  }
  session.setMarkdownText("Visible ##", false);
  InlineBlockFormat heading;
  heading.kind = InlineBlockFormat::Heading;
  const auto result = convertInlineBlock(session.markdownText(), *session.document().root().children().front(), heading,
                                         session.parseOptions());
  session.setMarkdownText(result.text, false);
  require(result.valid && plainText(*session.document().root().children().front()) == "Visible ##",
          "visible trailing hashes must not become a closing heading delimiter");
  for (const auto kind : {InlineBlockFormat::BulletList, InlineBlockFormat::OrderedList, InlineBlockFormat::Quote}) {
    session.setMarkdownText(kind == InlineBlockFormat::Quote ? "# [!NOTE]" : "# [x]\tTask", false);
    InlineBlockFormat format;
    format.kind = kind;
    const auto converted = convertInlineBlock(session.markdownText(), *session.document().root().children().front(),
                                              format, session.parseOptions());
    session.setMarkdownText(converted.text, false);
    const auto& owner = *session.document().root().children().front();
    require(converted.valid && owner.alertKind() == AlertKind::None &&
                (kind == InlineBlockFormat::Quote || !owner.children().front()->isTaskItem()),
            "container conversion must not reinterpret visible task or alert markers as metadata");
  }
  for (const auto& source :
       {QStringLiteral("# +++\n\nx=1\n\n+++\n\nTail"), QStringLiteral("# {\n\n\"title\": \"value\"\n\n}\n\nTail")}) {
    session.setMarkdownText(source, false);
    const auto converted = convertInlineBlock(session.markdownText(), *session.document().root().children().front(), {},
                                              session.parseOptions());
    auto full = session.markdownText();
    full.replace(converted.range.start, converted.range.end, converted.text);
    session.setMarkdownText(full.toString(), false);
    require(converted.valid && session.document().root().children().front()->type() == BlockType::Paragraph &&
                full.toString().endsWith("Tail"),
            "demoting the first heading must not turn untouched later blocks into front matter");
  }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  completeContainersAndSource();
  movementAndMapping();
  sparseMoveAndIndentedSource();
  frontMatterAndMalformedBoundaries();
  listScopesAndSparseReordering();
  numberedSubtreesAndWidthChanges();
  listMenusAndQuotedChildren();
  numberedLiteralPadding();
  unterminatedLiteralBoundaries();
  sourcePreservingConversions();
}
