#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/LineStartOffsetCache.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"
#include "parser/CmarkNodeAdapter.h"
#include "parser/MarkdownSerializer.h"
extern "C" {
#include "node.h"
}

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

qsizetype countShared(const QVector<InlineNode>& inlines) {
  qsizetype count = 0;
  for (const InlineNode& node : inlines) {
    count += node.usesSharedText() ? 1 : 0;
    count += countShared(node.children());
  }
  return count;
}

qsizetype countShared(const MarkdownNode& node) {
  qsizetype count = countShared(node.inlines());
  for (const auto& child : node.children()) {
    count += countShared(*child);
  }
  return count;
}

MarkdownNode* firstNodeOfType(MarkdownNode& node, BlockType type) {
  if (node.type() == type) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = firstNodeOfType(*child, type)) {
      return found;
    }
  }
  return nullptr;
}

void testLiveTreeSharesSourceText() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# Shared heading\n\nPlain **shared** text."), false);
  require(countShared(session.document().root()) >= 3, "parsed inline text was not source-shared");
}

void compareInlineTrees(const QVector<InlineNode>& a, const QVector<InlineNode>& b) {
  require(a.size() == b.size(), "shared parse inline count differs");
  const auto compareRange = [](InlineRange x, InlineRange y) {
    require(x.start == y.start && x.end == y.end, "shared parse source coordinates differ");
  };
  for (qsizetype i = 0; i < a.size(); ++i) {
    require(a[i].type() == b[i].type() && a[i].textView() == b[i].textView() && a[i].marker() == b[i].marker() &&
                a[i].href() == b[i].href() && a[i].alt() == b[i].alt() && a[i].title() == b[i].title() &&
                a[i].isAutolink() == b[i].isAutolink(),
            "shared parse changed inline values");
    require(a[i].usesSharedText() == b[i].usesSharedText(), "shared parse final binding eligibility differs");
    compareRange(a[i].sourceRange(), b[i].sourceRange());
    compareRange(a[i].contentRange(), b[i].contentRange());
    compareRange(a[i].openMarkerRange(), b[i].openMarkerRange());
    compareRange(a[i].closeMarkerRange(), b[i].closeMarkerRange());
    compareInlineTrees(a[i].children(), b[i].children());
  }
}

void compareBlockTrees(const MarkdownNode& a, const MarkdownNode& b) {
  require(a.type() == b.type() && a.literal() == b.literal() && a.children().size() == b.children().size(),
          "shared parse block values differ");
  require(
      a.sourceRange().byteStart == b.sourceRange().byteStart && a.sourceRange().byteEnd == b.sourceRange().byteEnd &&
          a.sourceRange().lineStart == b.sourceRange().lineStart && a.sourceRange().lineEnd == b.sourceRange().lineEnd,
      "shared parse block coordinates differ");
  compareInlineTrees(a.inlines(), b.inlines());
  for (size_t i = 0; i < a.children().size(); ++i)
    compareBlockTrees(*a.children()[i], *b.children()[i]);
}

void testDirectSharingMatchesDecodedParserAndBinding() {
  QStringList corpus{
      QStringLiteral("---\ntitle: test\n---\n\n# Heading\n\nPlain **bold** ~~strike~~ ==mark== H~2~O X^2^"),
      QStringLiteral("Entities &amp; &#126; &#x1F600; &invalid; and \\*escaped\\*, unmatched ` and `` `x` ``."),
      QStringLiteral("`  padded  ` `a\nb` <kbd>Ctrl</kbd> [l](url \"title\") <https://example.com> ~~*x*~~"),
      QStringLiteral("| A | B |\n|---|---|\n| a\\|b | ~~strike~~ |\n\n> quote\n> H~2~O\n\n- [x] task"),
      QStringLiteral("\\[\nx+1\n\\]\n\n$$\nx^2\n$$\n\n---\n\n[^x]: note\n\nuse[^x]"),
      QString::fromUtf8("中文 **粗体** \xf0\x9f\x98\x80 ~~删除~~\r\n第二行\r最后一行\n"),
      QString::fromUtf8("— … **word**\n\n正文"),
      QStringLiteral("# Fragment\n\nPlain text and ~~strike~~\n\n"),
      QStringLiteral(""),
      QStringLiteral("just `"),
      QStringLiteral("[![nested](img)](url)")};
  QString invalid = QStringLiteral("left");
  invalid += QChar(0xd800);
  invalid += QChar(0);
  invalid += QStringLiteral(" right ~~x~~ ");
  invalid += QChar(0xdc00);
  corpus.append(invalid);
  CmarkGfmParser parser;
  for (const auto& text : corpus) {
    for (bool remap : {false, true}) {
      ParseOptions options;
      options.enableHighlight = options.enableSubscript = options.enableSuperscript = true;
      options.enableUnicodeRemap = remap;
      auto source = std::make_shared<const QString>(text);
      auto owned = parser.parseDocument(text, options);
      auto shared = parser.parseDocumentShared(source, options);
      require(owned.root && shared.root, "shared/owned parsers must produce roots");
      owned.root->bindSharedInlineText(source);
      shared.root->bindSharedInlineText(source);
      compareBlockTrees(*owned.root, *shared.root);
      for (size_t i = 0; i < owned.root->children().size(); ++i)
        require(MarkdownSerializer().serializeBlock(*owned.root->children()[i]) ==
                    MarkdownSerializer().serializeBlock(*shared.root->children()[i]),
                "shared serialization differs");
    }
  }
}

void testDirectSourceSlicesRetainLifetimeAndRejectDecodedDifferences() {
  auto source = std::make_shared<const QString>(QString::fromUtf8("prefix 中文 suffix"));
  std::weak_ptr<const QString> lifetime = source;
  InlineNode value;
  require(value.setSharedTextFromUtf8(source, {7, 9}, QUtf8StringView("中文")),
          "UTF-8 equality must bind Unicode slice");
  auto segment = value.textSegment(1, 1);
  require(segment.usesSharedText() && segment.text() == QString::fromUtf8("文"),
          "text splitting must retain source sharing");
  InlineNode rejected = InlineNode::text("unchanged");
  require(!rejected.setSharedTextFromUtf8(source, {-1, 2}, QUtf8StringView("x")) &&
              !rejected.setSharedTextFromUtf8(source, {7, 900}, QUtf8StringView("x")) &&
              !rejected.setSharedTextFromUtf8(source, {7, 9}, QUtf8StringView("different")) &&
              rejected.text() == "unchanged",
          "failed source comparisons must preserve owned text");
  source.reset();
  value = InlineNode();
  require(!lifetime.expired() && segment.text() == QString::fromUtf8("文"),
          "slice must own its immutable source lifetime");
  segment.detachSharedText();
  require(lifetime.expired() && segment.text() == QString::fromUtf8("文"), "detaching final slice must release source");
}

void testAdapterByteColumnsMatchIndependentLineScanner() {
  // Query nonmonotonically, including columns inside UTF-8 sequences and a
  // surrogate pair crossing the sparse checkpoint boundary. The existing line
  // scanner is an independent oracle; no parser-generated ranges are reused.
  QString text = QStringLiteral("ascii\n\n") + QStringLiteral("x").repeated(63) +
                 QString::fromUtf8("\xf0\x9f\x98\x80中é") + QStringLiteral("z").repeated(67) +
                 QString::fromUtf8("中文 \xf0\x9f\x98\x80 ").repeated(25) + QStringLiteral("\r\nlast\n");
  LineStartOffsetCache offsets(text);
  CmarkNodeAdapter adapter(&offsets, text);
  cmark_node raw{};
  raw.type = CMARK_NODE_TEXT;
  unsigned char empty[] = "";
  raw.as.literal.data = empty;
  raw.as.literal.alloc = 1; // Already terminated: get_literal does not allocate.
  std::mt19937 random(0x4b1a231);
  for (int sample = 0; sample < 3000; ++sample) {
    const int line = 1 + int(random() % offsets.lineCount());
    const int maxColumn = int(offsets.lineText(text, line).toUtf8().size()) + 10;
    const int start = 1 + int(random() % maxColumn);
    const int end = start + int(random() % (maxColumn - start + 1));
    raw.start_line = raw.end_line = line;
    raw.start_column = start;
    raw.end_column = end - 1;
    const auto node = adapter.convertInline(&raw);
    require(node.sourceStart() == offsets.offsetForLineByteColumn(text, line, start) &&
                node.sourceEnd() == offsets.offsetForLineByteColumn(text, line, end),
            "sparse adapter byte columns must match the independent scanner at every Unicode boundary");
  }
}

void testCloneOwnsTextAndSurvivesDocumentReplacement() {
  DocumentSession session;
  const QString tableText = QStringLiteral("| Alpha | Beta |\n| --- | --- |\n| One | Two |");
  session.setMarkdownText(tableText, false);
  MarkdownNode* table = firstNodeOfType(session.document().root(), BlockType::Table);
  require(table != nullptr, "table parse failed");
  require(countShared(*table) > 0, "table inline text was not shared");

  std::unique_ptr<MarkdownNode> snapshot = table->clone(CloneMode::PreserveIds);
  require(countShared(*snapshot) == 0, "undo snapshot retained shared source slices");
  MarkdownSerializer serializer;
  const QString serialized = serializer.serializeBlock(*snapshot);

  session.setMarkdownText(QStringLiteral("replacement document"), false);
  require(serializer.serializeBlock(*snapshot) == serialized, "snapshot text changed with live document");
  require(serialized.contains(QStringLiteral("Alpha")) && serialized.contains(QStringLiteral("Two")),
          "snapshot serialization lost table cell text");
}

void testLocalReparseSharesSliceText() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbravo"), false);
  require(session.applyTextEdit(9, 0, QStringLiteral("X")) != muffin::TextEditResult::Rejected, "local edit failed");
  require(session.markdownText().toString() == QStringLiteral("alpha\n\nbrXavo"), "local edit text mismatch");
  require(countShared(session.document().root()) > 0, "local reparse did not bind shared slice text");
}

void testLazyMetadataCopiesPreserveDomainState() {
  InlineNode link = InlineNode::link(QStringLiteral("https://example.com"), QStringLiteral("Example"),
                                     {InlineNode::text(QStringLiteral("label"))});
  link.setMarker(QStringLiteral("[]"));
  link.setAutolink(true);
  InlineNode copiedLink = link;
  InlineNode assignedLink;
  assignedLink = link;

  link.setHref(QStringLiteral("https://changed.example"));
  link.children().first().setText(QStringLiteral("changed"));
  require(copiedLink.href() == QStringLiteral("https://example.com") &&
              copiedLink.title() == QStringLiteral("Example") && copiedLink.marker() == QStringLiteral("[]") &&
              copiedLink.isAutolink(),
          "InlineNode copy lost lazy metadata");
  require(copiedLink.children().first().text() == QStringLiteral("label"), "InlineNode copy shared mutable children");
  require(assignedLink.href() == QStringLiteral("https://example.com") &&
              assignedLink.children().first().text() == QStringLiteral("label"),
          "InlineNode assignment lost lazy metadata");

  MarkdownNode block(BlockType::CodeFence);
  block.setHeadingLevel(3);
  block.setSetext(true);
  block.setListKind(ListKind::Ordered);
  block.setListStart(7);
  block.setListTight(true);
  block.setTaskChecked(true);
  block.setTaskItem(true);
  block.setCodeLanguage(QStringLiteral("cpp"));
  block.setIndentedCode(true);
  block.setMathDelimiter(MathDelimiter::Bracket);
  block.setAlertKind(AlertKind::Warning);
  block.setFrontMatterFormat(FrontMatterFormat::Toml);
  block.setTableAlignments({TableAlignment::Left, TableAlignment::Right});
  block.setTableRowIsHeader(true);
  DefinitionBlock definition;
  definition.kind = DefinitionBlock::Kind::Link;
  definition.label = QStringLiteral("ref");
  definition.destination = QStringLiteral("https://example.com/ref");
  definition.markerRange = {0, 5};
  block.setDefinition(definition);

  std::unique_ptr<MarkdownNode> clone = block.clone();
  block.setCodeLanguage(QStringLiteral("rust"));
  block.setTableAlignments({TableAlignment::Center});
  definition.label = QStringLiteral("changed");
  block.setDefinition(definition);

  require(clone->headingLevel() == 3 && clone->setext() && clone->listKind() == ListKind::Ordered &&
              clone->listStart() == 7 && clone->listTight() && clone->taskChecked() && clone->isTaskItem(),
          "MarkdownNode clone lost packed scalar metadata");
  require(clone->isIndentedCode() && clone->mathDelimiter() == MathDelimiter::Bracket &&
              clone->alertKind() == AlertKind::Warning && clone->frontMatterFormat() == FrontMatterFormat::Toml &&
              clone->tableRowIsHeader(),
          "MarkdownNode clone lost packed flag metadata");
  require(clone->codeLanguage() == QStringLiteral("cpp") &&
              clone->tableAlignments() == QVector<TableAlignment>({TableAlignment::Left, TableAlignment::Right}) &&
              clone->definition().label == QStringLiteral("ref"),
          "MarkdownNode clone lost lazy metadata");
}

void testRangeStorageAndMarkerTransitions() {
  const auto equal = [](InlineSourceRanges a, InlineSourceRanges b) {
    const auto range = [](InlineRange x, InlineRange y) { return x.start == y.start && x.end == y.end; };
    return range(a.source, b.source) && range(a.content, b.content) && range(a.openMarker, b.openMarker) &&
           range(a.closeMarker, b.closeMarker);
  };
  // A long span has small edge deltas. Exercise both compact boundaries and
  // fallback, including -1 sentinels and negative but otherwise legal values.
  for (qsizetype delta : {-32769, -32768, -32767, -1, 0, 1, 32767, 32768, 32769}) {
    for (qsizetype base : {qsizetype(-100000), qsizetype(0), qsizetype(1000000)}) {
      InlineSourceRanges expected{{base, base + 1000000},
                                  {base + delta, base + 1000000 - delta},
                                  {-1, base + delta},
                                  {base + 1000000 - delta, -1}};
      InlineNode node(InlineType::Text);
      node.setSourceRanges(expected);
      require(equal(node.sourceRanges(), expected), "edge-relative coordinates retain sentinel and delta boundaries");
      InlineNode copy = node;
      node.setSourceEnd(base + 2000000);
      require(equal(copy.sourceRanges(), expected) && node.contentRange().end == expected.content.end,
              "changing an edge preserves other ranges and independent copies");
      node.setSourceRanges({{0, 5}, {0, 5}, {-1, -1}, {-1, -1}});
      require(node.sourceStart() == 0 && node.contentRange().end == 5,
              "exceptional coordinates can return to ordinary storage");
    }
  }
  const qsizetype limit = std::numeric_limits<qint32>::max();
  const QVector<qsizetype> offsets{-1,
                                   0,
                                   1,
                                   limit - 1,
                                   limit,
                                   limit + 1,
                                   std::numeric_limits<qsizetype>::max() - 10,
                                   std::numeric_limits<qsizetype>::min() + 10};
  InlineNode node = InlineNode::link("destination", "title", {InlineNode::text("label")});
  std::mt19937 random(821);
  for (int iteration = 0; iteration < 500; ++iteration) {
    const auto next = [&] { return offsets[random() % offsets.size()]; };
    InlineSourceRanges expected{{next(), next()}, {next(), next()}, {next(), next()}, {next(), next()}};
    node.setSourceRanges(expected);
    node.setMarker(iteration % 2 ? "**" : "custom syntax");
    InlineNode copy = node, assigned;
    assigned = node;
    node.setContentRange({0, 4});
    node.setMarker("_");
    require(equal(copy.sourceRanges(), expected) && equal(assigned.sourceRanges(), expected),
            "range copies must retain every full-width coordinate without aliasing");
    require(copy.marker() == (iteration % 2 ? "**" : "custom syntax") && copy.href() == "destination",
            "marker transitions must preserve independent range and link metadata");
    expected.content = {0, 4};
    require(equal(node.sourceRanges(), expected), "partial range mutation must retain all other coordinates");
    node.setSourceStart(3);
    node.setSourceEnd(7);
    node.setSourceRanges({{3, 7}, {3, 7}, {-1, -1}, {-1, -1}});
    node.setHref({});
    node.setTitle({});
    node.setMarker({});
    require(node.sourceStart() == 3 && node.sourceEnd() == 7 && node.marker().isEmpty(),
            "return from exceptional ranges and release optional metadata without losing coordinates");
    node.setHref("destination");
    node.setTitle("title");
  }
  QVector<InlineNode> nested{InlineNode::strong("__", {InlineNode::text("text")})};
  nested[0].setSourceRanges({{limit - 2, limit}, {limit - 2, limit}, {-1, -1}, {-1, -1}});
  nested[0].children()[0].setSourceRange({limit - 2, limit});
  shiftInlineSourcePositions(nested, 20);
  require(nested[0].sourceStart() == limit + 18 && nested[0].children()[0].sourceEnd() == limit + 20,
          "nested source shifts must promote range storage across the 32-bit boundary");
  shiftInlineSourcePositions(nested, -20);
  require(nested[0].sourceStart() == limit - 2 && nested[0].marker() == "__" && !nested[0].openMarkerRange().isValid(),
          "range demotion must preserve markers and absent ranges");
}

} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testLiveTreeSharesSourceText();
  testCloneOwnsTextAndSurvivesDocumentReplacement();
  testLocalReparseSharesSliceText();
  testLazyMetadataCopiesPreserveDomainState();
  testRangeStorageAndMarkerTransitions();
  testDirectSharingMatchesDecodedParserAndBinding();
  testDirectSourceSlicesRetainLifetimeAndRejectDecodedDifferences();
  testAdapterByteColumnsMatchIndependentLineScanner();
  return 0;
}
