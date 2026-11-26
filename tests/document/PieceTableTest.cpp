// Phase 0 fuzz/edge tests for PieceTable. The PieceTable is a standalone class (not yet wired into
// MarkdownDocument); these tests pin its QString-like facade by mirroring random replace sequences
// against a real QString and asserting byte/QChar-identical results, plus explicit edge cases
// (empty, boundary insert, delete, replace-whole, CJK, surrogate-pair emoji).
#include "document/PieceTable.h"
#include "document/DocumentSession.h"
#include "document/PendingBlockMarker.h"
#include "document/SourcePositionIndex.h"
#include "document/SourceRangeUtil.h"

#include "../TestUtils.h"

#include <QChar>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <random>
#include <limits>

using namespace muffin;

static void testIndexedSourceRangeLookups() {
  QString text = QStringLiteral("第一行🙂\n\nlast\n");
  PieceTable table(text);
  for (int edit = 0; edit < 20; ++edit) {
    for (int line = -1; line <= text.count('\n') + 3; ++line) {
      require(sourceOffsetForLineEnd(table, line) == sourceOffsetForLineEnd(text, line),
              "indexed line ends retain QString boundary semantics");
      for (int column : {-1, 0, 1, 3, 100})
        require(sourceOffsetForLineColumn(table, line, column) == sourceOffsetForLineColumn(text, line, column),
                "indexed source offsets retain invalid-line, Unicode and column clamping semantics");
    }
    const auto start = (edit * 7) % (text.size() + 1);
    const auto count = qMin<qsizetype>(edit % 4, text.size() - start);
    const auto inserted = edit % 3 ? QStringLiteral("x\n") : QString();
    text.replace(start, count, inserted);
    table.replace(start, start + count, inserted);
  }
}

static void testEmptyTable() {
  PieceTable pt;
  require(pt.size() == 0, "empty: size==0");
  require(pt.isEmpty(), "empty: isEmpty");
  require(pt.toString().isEmpty(), "empty: toString");

  pt.replace(0, 0, QStringLiteral("hello"));
  require(pt.size() == 5, "insert-into-empty: size");
  require(pt.toString() == QStringLiteral("hello"), "insert-into-empty: toString");
  require(pt.at(0) == QLatin1Char('h'), "at(0)");
  require(pt.at(4) == QLatin1Char('o'), "at(4)");

  pt.replace(0, 5, QString());
  require(pt.size() == 0, "delete-all: size==0");
  require(pt.toString().isEmpty(), "delete-all: toString");
}

static void testInsertAtBoundaries() {
  PieceTable pt(QStringLiteral("AB"));
  pt.replace(0, 0, QStringLiteral("x"));
  require(pt.toString() == QStringLiteral("xAB"), "insert at 0");
  pt.replace(pt.size(), pt.size(), QStringLiteral("z"));
  require(pt.toString() == QStringLiteral("xABz"), "insert at end");
  pt.replace(2, 2, QStringLiteral("-"));
  require(pt.toString() == QStringLiteral("xA-Bz"), "insert in middle");
  // append after the append (exercises changes_ growth + second piece into changes_)
  pt.replace(pt.size(), pt.size(), QStringLiteral("!"));
  require(pt.toString() == QStringLiteral("xA-Bz!"), "second append");
}

static void testDeleteAndReplaceWhole() {
  PieceTable pt(QStringLiteral("abcdef"));
  pt.replace(2, 4, QString()); // drop "cd" -> "abef"
  require(pt.toString() == QStringLiteral("abef"), "delete middle");
  pt.replace(1, 3, QStringLiteral("XYZ")); // "aXYZf"
  require(pt.toString() == QStringLiteral("aXYZf"), "replace range");
  pt.replace(0, pt.size(), QStringLiteral("done"));
  require(pt.toString() == QStringLiteral("done"), "replace whole");
  // mid clamping mirrors QString::mid
  require(pt.mid(2, 100) == QStringLiteral("ne"), "mid over-length clamp");
  require(pt.mid(100, 5).isEmpty(), "mid past end is empty");
  require(pt.mid(2, 0).isEmpty(), "mid len 0 is empty");
}

static void testCjkAndSurrogates() {
  // CJK (BMP, 1 QChar) + emoji (surrogate pairs, 2 QChars). The piece table is QChar-based, so it
  // must match QString exactly at the code-unit level (surrogates included).
  const QString seed = QStringLiteral("你好😀世界");
  PieceTable pt(seed);
  require(pt.size() == seed.size(), "cjk/emoji: size matches QString");
  require(pt.toString() == seed, "cjk/emoji: toString matches");
  require(pt.toUtf8() == seed.toUtf8(), "cjk/emoji: toUtf8 matches");

  QString ref = seed;
  pt.replace(0, 1, QString()); // drop '你'  (pt replace [0,1) ↔ QString replace(0,1,...))
  ref.replace(0, 1, QString());
  require(pt.toString() == ref, "cjk delete matches QString");

  // Replace the QChar at index 1 (😀's high surrogate in the post-delete string). pt.replace takes
  // [start,end); QString::replace takes (pos,count) — so [1,2) ↔ (1,1). Both land on the surrogate.
  pt.replace(1, 2, QStringLiteral("X"));
  ref.replace(1, 1, QStringLiteral("X"));
  require(pt.toString() == ref, "mid-surrogate-region replace matches QString");
}

// Consecutive single-char appends at one caret position must coalesce into ONE changes_ piece (the
// append-only buffer makes them contiguous), so a long typing session does not grow the piece list —
// and thus every later replace/locate — linearly with keystroke count. The fuzz test below already
// proves coalescing stays QChar-correct; this pins the piece-count invariant itself.
static void testCoalescesConsecutiveAppends() {
  PieceTable pt(QStringLiteral("AB"));
  const qsizetype initialPieces = pt.pieceCount(); // 1 (the single original_ piece)
  QString ref = QStringLiteral("AB");
  for (int i = 0; i < 1000; ++i) {
    const QChar ch = QLatin1Char(static_cast<char>('a' + (i % 26)));
    pt.replace(pt.size(), pt.size(), QStringView(&ch, 1));
    ref.append(ch);
  }
  require(pt.toString() == ref, "coalesce: 1000 appends produce the right text");
  // 1000 consecutive appends must fold into a single new piece, not 1000.
  require(pt.pieceCount() == initialPieces + 1,
          QStringLiteral("coalesce: expected %1 pieces after 1000 appends, got %2")
              .arg(initialPieces + 1)
              .arg(pt.pieceCount()));
}

static void testStreamingSearchAcrossPieces() {
  PieceTable table(QStringLiteral("alpha beta omega"));
  table.replace(6, 10, QStringLiteral("BE"));
  table.replace(8, 8, QStringLiteral("TA"));

  require(table.toString() == QStringLiteral("alpha BETA omega"), "search fixture");
  require(table.indexOf(QStringLiteral("alpha")) == 0, "search at start");
  require(table.indexOf(QStringLiteral("BETA")) == 6, "search spans change pieces");
  require(table.indexOf(QStringLiteral("TA o")) == 8, "search spans change/original boundary");
  require(table.indexOf(QStringLiteral("alpha"), 1) == -1, "search honors start offset");
  require(table.indexOf(QStringView(), 4) == 4, "empty search returns start offset");

  table.replace(table.size(), table.size(), QStringLiteral(" alpha BETA"));
  require(table.lastIndexOf(QStringLiteral("alpha")) == 17, "reverse search finds final match");
  require(table.lastIndexOf(QStringLiteral("BETA"), 7) == 6, "reverse search honors start");
  require(table.lastIndexOf(QStringLiteral("missing")) == -1, "reverse search missing");
}

static void requireLineIndexMatches(const PieceTable& table, const QString& text, const QString& label) {
  QVector<qsizetype> starts{0};
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text.at(i) == QLatin1Char('\n')) {
      starts.push_back(i + 1);
    }
  }
  require(table.lineCount() == starts.size(), label + QStringLiteral(": line count"));
  for (int line = 1; line <= starts.size(); ++line) {
    const qsizetype expectedEnd = line < starts.size() ? starts.at(line) - 1 : text.size();
    require(table.lineStartOffset(line) == starts.at(line - 1), label + QStringLiteral(": line %1 start").arg(line));
    require(table.lineEndOffset(line) == expectedEnd, label + QStringLiteral(": line %1 end").arg(line));
  }
  int expectedLine = 1;
  for (qsizetype offset = 0; offset <= text.size(); ++offset) {
    while (expectedLine < starts.size() && starts.at(expectedLine) <= offset) {
      ++expectedLine;
    }
    require(table.lineForOffset(offset) == expectedLine, label + QStringLiteral(": offset %1 line").arg(offset));
  }
}

// The core safety net: mirror a long random replace sequence against QString and assert the
// PieceTable stays QChar-identical at every step (size, at, mid, toString, toUtf8). Random edits
// include inserts (span 0), deletes (empty repl), CJK, and surrogate-pair emoji.
static void testFuzzMirrorsQString() {
  std::mt19937 rng(20260627u);
  QString ref = QStringLiteral("Hello, 世界!\nLine two here.\n😀 emoji.\n--end--\n");
  PieceTable pt(ref);

  const auto randText = [&](int maxLen) {
    QString s;
    const int len = static_cast<int>(rng() % (maxLen + 1));
    for (int i = 0; i < len; ++i) {
      const int r = static_cast<int>(rng() % 10);
      if (r < 5) {
        s += QChar(static_cast<char16_t>('a' + (rng() % 26)));
      } else if (r < 8) {
        s += QChar(static_cast<char16_t>(0x4e00 + (rng() % 0x100))); // CJK
      } else {
        s += QStringLiteral("😊"); // surrogate pair (2 QChars) — can land mid-pair, both sides agree
      }
    }
    return s;
  };

  for (int iter = 0; iter < 4000; ++iter) {
    const auto len = static_cast<unsigned long long>(ref.size());
    const qsizetype start = static_cast<qsizetype>(rng() % (len + 1));
    const qsizetype span = static_cast<qsizetype>(rng() % (len - start + 1));
    const QString repl = randText(4);

    ref.replace(start, span, repl);
    pt.replace(start, start + span, repl);

    requireLineIndexMatches(pt, ref, QStringLiteral("line index @ iter %1").arg(iter));

    require(pt.size() == ref.size(),
            QStringLiteral("size mismatch @ iter %1: pt=%2 ref=%3").arg(iter).arg(pt.size()).arg(ref.size()));
    if (pt.toString() != ref) {
      require(
          false,
          QStringLiteral("toString mismatch @ iter %1\n  pt:  %2\n  ref: %3").arg(iter).arg(pt.toString()).arg(ref));
    }
    if (ref.size() > 0) {
      const qsizetype i = static_cast<qsizetype>(rng() % static_cast<unsigned long long>(ref.size()));
      require(pt.at(i) == ref[i],
              QStringLiteral("at(%1) mismatch @ iter %2: pt=%3 ref=%4").arg(i).arg(iter).arg(pt.at(i)).arg(ref[i]));
      const qsizetype ms = static_cast<qsizetype>(rng() % (len + 1));
      const qsizetype ml = static_cast<qsizetype>(rng() % (len - ms + 1));
      require(pt.mid(ms, ml) == ref.mid(ms, ml),
              QStringLiteral("mid(%1,%2) mismatch @ iter %3").arg(ms).arg(ml).arg(iter));
    }
    if (iter % 500 == 0) {
      require(pt.toUtf8() == ref.toUtf8(), QStringLiteral("toUtf8 mismatch @ iter %1").arg(iter));
    }
  }
}

// Phase 1 dual-write invariant: MarkdownDocument now keeps a PieceTable (pieceText()) mirror in
// sync with markdownText_ across setMarkdownText + the real per-keystroke edit path (applyTextEdit
// → replaceTopLevelRange). This validates that mirror stays byte-identical, since Phase 2 flips the
// piece-table into the edit master relying on exactly this invariant.
static void testDualWriteStaysInSyncWithMarkdownText() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# Title\n\nHello world.\n"), false);
  require(session.document().pieceText().toString() == session.document().markdownText().toString(),
          "dual-write: in sync after setMarkdownText");

  const auto checkSync = [&](const char* label) {
    require(session.document().pieceText().toString() == session.document().markdownText().toString(),
            QStringLiteral("dual-write out of sync %1").arg(QString::fromUtf8(label)));
    const QString text = session.document().markdownText().toString();
    requireLineIndexMatches(session.document().pieceText(), text,
                            QStringLiteral("document line index %1").arg(QString::fromUtf8(label)));
    require(session.document().lineOffsets().lineCount() == session.document().pieceText().lineCount(),
            QStringLiteral("line-offset facade count %1").arg(QString::fromUtf8(label)));
  };

  require(session.applyTextEdit(0, 0, QStringLiteral("X"), {}) != muffin::TextEditResult::Rejected, "insert X at 0");
  checkSync("after insert");
  require(session.applyTextEdit(0, 1, QString(), {}) != muffin::TextEditResult::Rejected, "delete [0,1)");
  checkSync("after delete");
  require(session.applyTextEdit(0, 0, QStringLiteral("你好"), {}) != muffin::TextEditResult::Rejected,
          "insert CJK at 0");
  checkSync("after CJK insert");
  require(session.applyTextEdit(0, 2, QStringLiteral("AB"), {}) != muffin::TextEditResult::Rejected,
          "replace [0,2) with AB");
  checkSync("after replace");

  require(session.document().pieceText().size() == session.document().markdownText().toString().size(),
          "dual-write: final size match");
  require(session.document().pieceText().toUtf8() == session.document().markdownText().toString().toUtf8(),
          "dual-write: final toUtf8 match");
}

// The node recognizer is deliberately unfiltered and shared with editing and
// rendering. Use it as an oracle for the collector's paragraph-only fast path.
static QVector<qsizetype> unfilteredPendingMarkers(QStringView text, const MarkdownNode& root) {
  QVector<qsizetype> result;
  const auto visit = [&](const auto& self, const MarkdownNode& node) -> void {
    if (node.type() == BlockType::Paragraph) {
      const auto marker = detectPendingBlockMarkerForNode(text, node);
      if (marker.isValid()) {
        result.append(qMin(node.sourceRange().byteEnd, text.size()) - marker.prefixLength);
      }
    }
    for (const auto& child : node.children())
      if (child)
        self(self, *child);
  };
  visit(visit, root);
  return result;
}

static void testPendingMarkerCollectionMatchesUnfilteredRecognition() {
  MarkdownNode paragraph(BlockType::Paragraph);
  qsizetype positiveCases = 0;
  const auto check = [&](const QString& source) {
    // A nonzero offset and a surrogate pair catch accidental byte/UTF-16 mixing.
    const QString text = QStringLiteral("prefix \U0001f600\n") + source + QStringLiteral("\nunused suffix");
    const qsizetype start = text.indexOf(QLatin1Char('\n')) + 1;
    paragraph.setSourceRange({start, start + source.size(), 2, 2, 1, int(source.size() + 1)});
    const auto expected = unfilteredPendingMarkers(text, paragraph);
    positiveCases += !expected.isEmpty();
    require(collectPendingMarkerOffsets(text, paragraph) == expected,
            "pending marker prefilter changed recognition or source offsets");
  };
  const QStringList prefixes{QString(),
                             QStringLiteral(" "),
                             QStringLiteral("   "),
                             QStringLiteral("    "),
                             QStringLiteral("\t"),
                             QStringLiteral(" \t "),
                             QStringLiteral("\r"),
                             QStringLiteral("> "),
                             QStringLiteral("> > "),
                             QStringLiteral("- "),
                             QStringLiteral("1. "),
                             QStringLiteral("> - 12) "),
                             QStringLiteral("   > "),
                             QString(QChar(0x2003))};
  const QStringList bodies{QString(),
                           QStringLiteral("#"),
                           QStringLiteral("######"),
                           QStringLiteral("#######"),
                           QStringLiteral("# "),
                           QStringLiteral("# real heading"),
                           QStringLiteral("-"),
                           QStringLiteral("+"),
                           QStringLiteral("*"),
                           QStringLiteral("123."),
                           QStringLiteral("9)"),
                           QStringLiteral("1. "),
                           QStringLiteral("```"),
                           QStringLiteral("```cpp"),
                           QStringLiteral("````` a b"),
                           QStringLiteral("```a`"),
                           QStringLiteral("~~~lang`"),
                           QStringLiteral("$$"),
                           QStringLiteral("\\["),
                           QStringLiteral("\\]"),
                           QStringLiteral("ordinary paragraph"),
                           QStringLiteral("中文正文"),
                           QStringLiteral("\U0001f600 text"),
                           QStringLiteral("word\n#"),
                           QStringLiteral("\n$$"),
                           QStringLiteral("$$\n"),
                           QStringLiteral("0.42"),
                           QStringLiteral("https://example.com")};
  const QStringList endings{QString(), QStringLiteral("\r"), QStringLiteral("\n"), QString(QChar(0x2003))};
  for (const auto& prefix : prefixes)
    for (const auto& body : bodies)
      for (const auto& ending : endings)
        check(prefix + body + ending);
  for (uint code = 0; code < 65536; ++code) {
    check(QString(QChar(ushort(code))) + QStringLiteral("$$"));
    check(QString(QChar(ushort(code))) + QStringLiteral("> *"));
  }
  std::mt19937 random(0x4d41524b);
  const QString alphabet = QStringLiteral(" \t\r\n#-+*`~$\\[019.)abc中文>") + QChar(0x2003) + QChar(0x0661);
  for (int sample = 0; sample < 100000; ++sample) {
    QString source;
    const auto length = random() % 64;
    for (unsigned i = 0; i < length; ++i)
      source += alphabet.at(random() % alphabet.size());
    check(source);
  }
  require(positiveCases > 100, "pending marker comparison must include valid markers");
}

static void testPendingMarkerCollectionPreservesNestedAndRelativeOffsets() {
  const QString text = QStringLiteral("prefix \U0001f600\n\n> *\n\n- \\[\n\n###\n\n```cpp\n\nordinary paragraph");
  MarkdownNode root(BlockType::Document);
  const auto block = [&](BlockType type, const QString& source) {
    auto node = std::make_unique<MarkdownNode>(type);
    const qsizetype start = text.indexOf(source);
    require(start >= 0, "pending marker fixture source must exist");
    node->setSourceRange({start, start + source.size(), 1, 1, 1, int(source.size() + 1)});
    return node;
  };
  auto quote = block(BlockType::BlockQuote, QStringLiteral("> *"));
  quote->appendChild(block(BlockType::Paragraph, QStringLiteral("> *")));
  root.appendChild(std::move(quote));
  auto list = block(BlockType::List, QStringLiteral("- \\["));
  auto item = block(BlockType::ListItem, QStringLiteral("- \\["));
  item->appendChild(block(BlockType::Paragraph, QStringLiteral("- \\[")));
  list->appendChild(std::move(item));
  root.appendChild(std::move(list));
  root.appendChild(block(BlockType::Heading, QStringLiteral("###")));
  root.appendChild(block(BlockType::CodeFence, QStringLiteral("```cpp")));
  root.appendChild(block(BlockType::Paragraph, QStringLiteral("ordinary paragraph")));
  const QVector<qsizetype> expected{text.indexOf(QLatin1Char('*')), text.indexOf(QStringLiteral("\\["))};
  require(unfilteredPendingMarkers(text, root) == expected, "nested marker oracle must retain only paragraphs");
  require(collectPendingMarkerOffsets(text, root) == expected, "nested marker collection changed");
  for (const auto& child : root.children())
    child->relativizeDescendants();
  require(collectPendingMarkerOffsets(text, root) == expected, "relative marker source coordinates changed");
}

static void testPendingMarkerCacheTracksLocalEdits() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\n*\n\nomega"), false);
  const auto requireCache = [&](const char* label) {
    const QString text = session.markdownText().toString();
    const QVector<qsizetype> oracle = unfilteredPendingMarkers(QStringView(text), session.document().root());
    require(session.pendingMarkerOffsets() == oracle,
            QStringLiteral("pending marker cache mismatch %1").arg(QString::fromUtf8(label)));
  };
  requireCache("after full parse");
  require(session.applyTextEdit(0, 0, QStringLiteral("x")) != muffin::TextEditResult::Rejected,
          "pending marker prefix insert should apply locally");
  requireCache("after suffix shift");
  const qsizetype marker = session.markdownText().toString().indexOf(QLatin1Char('*'));
  require(session.applyTextEdit(marker + 1, 0, QStringLiteral(" item")) != muffin::TextEditResult::Rejected,
          "pending marker completion should apply locally");
  requireCache("after marker completion");
}

static void testSourcePositionIndexPreservesOrderAndTypeRanksAcrossSplices() {
  SourcePositionIndex index;
  const QVector<SourcePositionToken*> original = index.reset(QVector<quint8>{1, 2, 1, 3, 1});
  index.addSuffix(2, 100, 4);

  auto firstReplacement = index.makeToken(1);
  SourcePositionToken* firstReplacementPtr = firstReplacement.get();
  auto secondReplacement = index.makeToken(2);
  SourcePositionToken* secondReplacementPtr = secondReplacement.get();
  auto replacements = SourcePositionIndex::merge(std::move(firstReplacement), std::move(secondReplacement));
  index.replace(1, 2, std::move(replacements));

  const QVector<SourcePositionToken*> expectedOrder{original.at(0), firstReplacementPtr, secondReplacementPtr,
                                                    original.at(3), original.at(4)};
  const QVector<qsizetype> expectedTypeRanks{0, 1, 0, 0, 2};
  for (qsizetype i = 0; i < expectedOrder.size(); ++i) {
    require(index.rank(expectedOrder.at(i)) == i, QStringLiteral("source position rank mismatch at %1").arg(i));
    require(index.typeRank(expectedOrder.at(i)) == expectedTypeRanks.at(i),
            QStringLiteral("source position type rank mismatch at %1").arg(i));
  }
  require(index.adjustmentFor(firstReplacementPtr).bytes == 0,
          "fresh replacement must not inherit the removed range's source shift");
  require(index.adjustmentFor(original.at(3)).bytes == 100 && index.adjustmentFor(original.at(3)).lines == 4,
          "unchanged suffix token must preserve its lazy source shift through splice");
}

static void testCompactionPreservesSnapshotsAndIndexes() {
  const QString seed = QString::fromUtf8("alpha\n中文 😀 tail\n");
  PieceTable table(seed);
  const PieceTable snapshot = table;
  const QString insertion = QString("deleted words\n").repeated(50000);
  for (int i = 0; i < 8; ++i) {
    table.replace(3, 3, insertion);
    table.replace(3, 3 + insertion.size(), {});
    require(table.toString() == seed && snapshot.toString() == seed,
            "compaction preserves live text and independent snapshots");
    require(table.backingStoreBytes() < 2 * 1024 * 1024,
            "repeated deletions do not accumulate unbounded backing capacity");
    PieceTable oracle(seed);
    require(table.wordCount() == oracle.wordCount() && table.lineCount() == oracle.lineCount(),
            "rebasing preserves word and line indexes");
    for (qsizetype offset = 0; offset <= table.size(); ++offset)
      require(table.lineForOffset(offset) == oracle.lineForOffset(offset), "rebased source offsets remain unchanged");
    table.replace(1, 1, QStringLiteral("Z"));
    table.replace(1, 2, {});
  }
  table.replace(0, table.size(), {});
  require(table.backingStoreBytes() == 0, "deleting all text releases both backing buffers");
}

static void testCompactOffsetIndexPromotionAndSnapshots() {
  CompactOffsetIndex index;
  QVector<qsizetype> expected;
  for (qsizetype value : {0, 1, 7, 7, 1234}) {
    index.push_back(value);
    expected.push_back(value);
  }
  const auto narrowSnapshot = index;
  if constexpr (sizeof(qsizetype) > sizeof(quint32)) {
    for (qsizetype value :
         {qsizetype(std::numeric_limits<quint32>::max()), qsizetype(std::numeric_limits<quint32>::max()) + 1,
          std::numeric_limits<qsizetype>::max()}) {
      index.push_back(value);
      expected.push_back(value);
    }
  }
  const auto wideSnapshot = index;
  index.push_back(expected.last());
  require(narrowSnapshot.size() == 5 && narrowSnapshot.at(4) == 1234,
          "promotion and append preserve implicitly shared narrow snapshots");
  require(wideSnapshot.size() == expected.size(), "append preserves wide snapshots");
  for (qsizetype i = 0; i < expected.size(); ++i) {
    require(index.at(i) == expected.at(i), "index retains full-width offsets after promotion");
    const qsizetype first = std::lower_bound(expected.cbegin(), expected.cend(), expected.at(i)) - expected.cbegin();
    require(wideSnapshot.lowerBound(expected.at(i)) == first, "binary search preserves duplicate and boundary offsets");
  }
  require(index.lowerBound(-1) == 0 && index.lowerBound(8, 2) == 4,
          "binary search handles negative queries and a restricted suffix");
}

int main(int argc, char** argv) {
  runTest("indexed source range lookups", testIndexedSourceRangeLookups);
  QCoreApplication app(argc, argv);
  runTest("empty table", testEmptyTable);
  runTest("compact index promotion and snapshots", testCompactOffsetIndexPromotionAndSnapshots);
  runTest("compaction preserves snapshots and indexes", testCompactionPreservesSnapshotsAndIndexes);
  runTest("insert at boundaries", testInsertAtBoundaries);
  runTest("delete and replace whole", testDeleteAndReplaceWhole);
  runTest("cjk and surrogate pairs", testCjkAndSurrogates);
  runTest("coalesces consecutive appends", testCoalescesConsecutiveAppends);
  runTest("streaming search across pieces", testStreamingSearchAcrossPieces);
  runTest("fuzz mirrors QString", testFuzzMirrorsQString);
  runTest("dual-write stays in sync", testDualWriteStaysInSyncWithMarkdownText);
  runTest("pending marker collection matches full recognition",
          testPendingMarkerCollectionMatchesUnfilteredRecognition);
  runTest("pending marker nested and relative offsets", testPendingMarkerCollectionPreservesNestedAndRelativeOffsets);
  runTest("pending marker cache tracks local edits", testPendingMarkerCacheTracksLocalEdits);
  runTest("source position index preserves ranks", testSourcePositionIndexPreservesOrderAndTypeRanksAcrossSplices);
  return 0;
}
