// Differential oracle for the block-relative offset model: after a DocumentSession holds a parsed
// (and thus relativized) tree, every node's RESOLVED-ABSOLUTE sourceRange / definition / inline
// range (via the accessors) must equal the absolute offsets a fresh full parse produces. If this
// passes for nested structures (lists, blockquotes, tables) the document model is correct and any
// remaining failures are in render/edit consumers, not the model.
#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"

#include "../TestUtils.h"

#include <QString>

using namespace muffin;

namespace {

QString fieldMismatch(const char* what, qsizetype got, qsizetype want) {
  return QStringLiteral("%1 mismatch: got=%2 want=%3").arg(QString::fromUtf8(what)).arg(got).arg(want);
}

// Compare a session (relativized) tree against a freshly-parsed (absolute) tree, node by node.
// Session inline offsets are relative-to-top-level-block, so resolve them via topLevelByteStart.
bool compareNode(const MarkdownNode& fresh, const MarkdownNode& session, QString& err, qsizetype topBase) {
  // topBase = the session node's owning top-level block's absolute byteStart (0 for fresh, which is
  // already absolute). Used to resolve session inline ranges.
  const SourceRange fr = fresh.sourceRange();
  const SourceRange sr = session.sourceRange();
  if (fr.byteStart != sr.byteStart || fr.byteEnd != sr.byteEnd) {
    err = QStringLiteral("node byteRange mismatch: got=[%1,%2] want=[%3,%4]")
              .arg(sr.byteStart).arg(sr.byteEnd).arg(fr.byteStart).arg(fr.byteEnd);
    return false;
  }
  if (fr.lineStart != sr.lineStart) {
    err = fieldMismatch("node lineStart", sr.lineStart, fr.lineStart);
    return false;
  }

  // Definition fields (resolved via accessor on both sides — accessor adds base on session).
  const DefinitionBlock fd = fresh.definition();
  const DefinitionBlock sd = session.definition();
  if (fd.isValid() || sd.isValid()) {
    const auto checkField = [&](const DefinitionFieldRange& f, const DefinitionFieldRange& s, const char* name) -> bool {
      if (f.start != s.start || f.end != s.end) {
        err = fieldMismatch(name, s.start, f.start);
        return false;
      }
      return true;
    };
    if (!checkField(fd.markerRange, sd.markerRange, "def.marker") ||
        !checkField(fd.labelRange, sd.labelRange, "def.label") ||
        !checkField(fd.destinationRange, sd.destinationRange, "def.dest") ||
        !checkField(fd.titleRange, sd.titleRange, "def.title") ||
        !checkField(fd.noteRange, sd.noteRange, "def.note") ||
        !checkField(fd.sourceRange, sd.sourceRange, "def.source")) {
      return false;
    }
  }

  // Inlines: session stores relative; resolve by + topBase.
  if (fresh.inlines().size() != session.inlines().size()) {
    err = QStringLiteral("inline count mismatch: got=%1 want=%2").arg(session.inlines().size()).arg(fresh.inlines().size());
    return false;
  }
  for (qsizetype i = 0; i < fresh.inlines().size(); ++i) {
    const InlineNode& fi = fresh.inlines().at(i);
    const InlineNode& si = session.inlines().at(i);
    // Unset ranges (sourceStart < 0, e.g. line breaks / markers) are not relativized — compare as-is.
    if (fi.sourceStart() < 0) {
      if (si.sourceStart() != fi.sourceStart() || si.sourceEnd() != fi.sourceEnd()) {
        err = QStringLiteral("inline %1 unset mismatch: got=[%2,%3] want=[%4,%5]")
                  .arg(i).arg(si.sourceStart()).arg(si.sourceEnd()).arg(fi.sourceStart()).arg(fi.sourceEnd());
        return false;
      }
    } else if (fi.sourceStart() != si.sourceStart() + topBase || fi.sourceEnd() != si.sourceEnd() + topBase) {
      err = QStringLiteral("inline %1 source mismatch: got=[%2,%3] want=[%4,%5]")
                .arg(i).arg(si.sourceStart() + topBase).arg(si.sourceEnd() + topBase).arg(fi.sourceStart()).arg(fi.sourceEnd());
      return false;
    }
  }

  if (fresh.children().size() != session.children().size()) {
    err = QStringLiteral("child count mismatch: got=%1 want=%2").arg(session.children().size()).arg(fresh.children().size());
    return false;
  }
  for (qsizetype i = 0; i < fresh.children().size(); ++i) {
    if (!compareNode(*fresh.children().at(i), *session.children().at(i), err, topBase)) {
      return false;
    }
  }
  return true;
}

// Compare a session document against a fresh parse of its markdownText. topBase for top-level
// blocks is their own absolute byteStart (each top-level block relativizes against itself).
bool documentMatchesFreshParse(const DocumentSession& session, QString& err) {
  CmarkGfmParser parser;
  const ParseResult fresh = parser.parseDocument(QStringView(session.markdownText()), ParseOptions{});
  const auto& freshChildren = fresh.root->children();
  const auto& sessChildren = session.document().root().children();
  if (freshChildren.size() != sessChildren.size()) {
    err = QStringLiteral("top-level count mismatch: got=%1 want=%2").arg(sessChildren.size()).arg(freshChildren.size());
    return false;
  }
  for (qsizetype i = 0; i < freshChildren.size(); ++i) {
    const qsizetype topBase = sessChildren.at(i)->sourceRange().byteStart;  // absolute (top-level)
    if (!compareNode(*freshChildren.at(i), *sessChildren.at(i), err, topBase)) {
      err = QStringLiteral("block %1 (type %2): %3")
                .arg(i).arg(static_cast<int>(sessChildren.at(i)->type())).arg(err);
      return false;
    }
  }
  return true;
}

void checkDoc(const char* name, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  QString err;
  const bool ok = documentMatchesFreshParse(session, err);  // capture first: arg eval order is unspecified
  require(ok, QStringLiteral("%1: %2").arg(QString::fromUtf8(name), err));
}

}  // namespace

void testParagraphAndHeading() {
  checkDoc("paragraph+heading",
           QStringLiteral("# Heading **bold** _it_\n\nPlain `code` and [link](http://x).\n"));
}

void testListNestedWithInlineMath() {
  checkDoc("list+math",
           QStringLiteral("- item $a+b$ **bold**\n  - nested $c$\n- two `code`\n"));
}

void testBlockquoteAndAlert() {
  checkDoc("blockquote",
           QStringLiteral("> quote $x$\n> > nested **b**\n\n> [!NOTE]\n> note text\n"));
}

void testTableWithFormattedCells() {
  checkDoc("table",
           QStringLiteral("| A **b** | B $x$ |\n| --- | --- |\n| 1 `c` | 2 |\n"));
}

void testDefinition() {
  checkDoc("definitions",
           QStringLiteral("[label]: http://dest \"Title\"\n\nText [label] ref.\n\n[^fn]: footnote text\n\nBody[^fn]\n"));
}

void testCodeFenceAndMathBlock() {
  checkDoc("code+mathblock",
           QStringLiteral("```\ncode line\n```\n\n$$\na = b\n$$\n\n    indented code\n"));
}

void testEditThenCompare() {
  // Under a length-changing edit, suffix top-level blocks (which are NOT re-parsed) get their OWN
  // sourceRange shifted by the edit delta, and their descendants resolve to absolute through that
  // shifted base — with no recursive sweep. Verify that invariant DIRECTLY: capture the suffix
  // blocks' resolved offsets, insert 3 bytes before them, and assert every suffix offset moved by
  // exactly +3 (bytes; line deltas are 0 since the insertion has no newline). This targets the lazy
  // shift itself and is decoupled from any slice-reparse trailing-extent quirk on the EDITED block
  // (which is re-parsed from a cmark slice and is not what the block-relative model is responsible
  // for). Multi-block document so the edit hits block 0 and shifts real suffix blocks.
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("para one\n\n- item $x$\n  - nested $y$\n\npara two\n"), false);
  const auto& childrenBefore = session.document().root().children();
  require(childrenBefore.size() == 3, "expected 3 top-level blocks (para, list, para)");

  const SourceRange listBefore = childrenBefore.at(1)->sourceRange();
  const SourceRange paraTwoBefore = childrenBefore.at(2)->sourceRange();
  // A descendant of the list (the nested item's paragraph) proves descendant RESOLUTION tracks the
  // shifted top-level base, not just the block's own range.
  const MarkdownNode* descBefore = childrenBefore.at(1)->findDescendant(
      [](const MarkdownNode& n) { return n.type() == BlockType::Paragraph; });
  require(descBefore != nullptr, "list should contain a paragraph descendant");
  const SourceRange descBeforeRange = descBefore->sourceRange();

  require(session.applyTextDelta(0, 0, QStringLiteral("ZZZ"), true), "edit should apply");

  const auto& childrenAfter = session.document().root().children();
  require(childrenAfter.size() == 3, "no-newline insertion should not change top-level structure");
  const SourceRange listAfter = childrenAfter.at(1)->sourceRange();
  const SourceRange paraTwoAfter = childrenAfter.at(2)->sourceRange();
  const MarkdownNode* descAfter = childrenAfter.at(1)->findDescendant(
      [](const MarkdownNode& n) { return n.type() == BlockType::Paragraph; });
  require(descAfter != nullptr, "list should still contain a paragraph descendant");
  const SourceRange descAfterRange = descAfter->sourceRange();

  require(listAfter.byteStart == listBefore.byteStart + 3, "suffix list byteStart must shift by +3");
  require(listAfter.byteEnd == listBefore.byteEnd + 3, "suffix list byteEnd must shift by +3");
  require(listAfter.lineStart == listBefore.lineStart, "no-newline insertion: list lineStart unchanged");
  require(paraTwoAfter.byteStart == paraTwoBefore.byteStart + 3, "suffix para byteStart must shift by +3");
  require(paraTwoAfter.byteEnd == paraTwoBefore.byteEnd + 3, "suffix para byteEnd must shift by +3");
  require(descAfterRange.byteStart == descBeforeRange.byteStart + 3,
          "descendant paragraph must resolve to absolute shifted by +3");
  require(descAfterRange.byteEnd == descBeforeRange.byteEnd + 3,
          "descendant paragraph byteEnd must resolve shifted by +3");
}

int main() {
  runTest("testParagraphAndHeading", testParagraphAndHeading);
  runTest("testListNestedWithInlineMath", testListNestedWithInlineMath);
  runTest("testBlockquoteAndAlert", testBlockquoteAndAlert);
  runTest("testTableWithFormattedCells", testTableWithFormattedCells);
  runTest("testDefinition", testDefinition);
  runTest("testCodeFenceAndMathBlock", testCodeFenceAndMathBlock);
  runTest("testEditThenCompare", testEditThenCompare);
  return 0;
}
