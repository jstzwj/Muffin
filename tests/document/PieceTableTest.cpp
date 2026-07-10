// Phase 0 fuzz/edge tests for PieceTable. The PieceTable is a standalone class (not yet wired into
// MarkdownDocument); these tests pin its QString-like facade by mirroring random replace sequences
// against a real QString and asserting byte/QChar-identical results, plus explicit edge cases
// (empty, boundary insert, delete, replace-whole, CJK, surrogate-pair emoji).
#include "document/PieceTable.h"
#include "document/DocumentSession.h"
#include "document/PendingBlockMarker.h"
#include "document/SourcePositionIndex.h"

#include "../TestUtils.h"

#include <QChar>
#include <QCoreApplication>
#include <QString>
#include <QStringView>

#include <random>

using namespace muffin;

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
  pt.replace(2, 4, QString());  // drop "cd" -> "abef"
  require(pt.toString() == QStringLiteral("abef"), "delete middle");
  pt.replace(1, 3, QStringLiteral("XYZ"));  // "aXYZf"
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
  pt.replace(0, 1, QString());      // drop '你'  (pt replace [0,1) ↔ QString replace(0,1,...))
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
  const qsizetype initialPieces = pt.pieceCount();  // 1 (the single original_ piece)
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
              .arg(initialPieces + 1).arg(pt.pieceCount()));
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
    require(table.lineStartOffset(line) == starts.at(line - 1),
            label + QStringLiteral(": line %1 start").arg(line));
    require(table.lineEndOffset(line) == expectedEnd,
            label + QStringLiteral(": line %1 end").arg(line));
  }
  int expectedLine = 1;
  for (qsizetype offset = 0; offset <= text.size(); ++offset) {
    while (expectedLine < starts.size() && starts.at(expectedLine) <= offset) {
      ++expectedLine;
    }
    require(table.lineForOffset(offset) == expectedLine,
            label + QStringLiteral(": offset %1 line").arg(offset));
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
        s += QChar(static_cast<char16_t>(0x4e00 + (rng() % 0x100)));  // CJK
      } else {
        s += QStringLiteral("😊");  // surrogate pair (2 QChars) — can land mid-pair, both sides agree
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
      require(false, QStringLiteral("toString mismatch @ iter %1\n  pt:  %2\n  ref: %3")
                          .arg(iter).arg(pt.toString()).arg(ref));
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
// sync with markdownText_ across setMarkdownText + the real per-keystroke edit path (applyTextDelta
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

  require(session.applyTextDelta(0, 0, QStringLiteral("X"), false, {}), "insert X at 0");
  checkSync("after insert");
  require(session.applyTextDelta(0, 1, QString(), false, {}), "delete [0,1)");
  checkSync("after delete");
  require(session.applyTextDelta(0, 0, QStringLiteral("你好"), false, {}), "insert CJK at 0");
  checkSync("after CJK insert");
  require(session.applyTextDelta(0, 2, QStringLiteral("AB"), false, {}), "replace [0,2) with AB");
  checkSync("after replace");

  require(session.document().pieceText().size() == session.document().markdownText().toString().size(),
          "dual-write: final size match");
  require(session.document().pieceText().toUtf8() == session.document().markdownText().toString().toUtf8(),
          "dual-write: final toUtf8 match");
}

static void testPendingMarkerCacheTracksLocalEdits() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\n*\n\nomega"), false);
  const auto requireCache = [&](const char* label) {
    const QString text = session.markdownText().toString();
    const QVector<qsizetype> oracle =
        collectPendingMarkerOffsets(QStringView(text), session.document().root());
    require(session.pendingMarkerOffsets() == oracle,
            QStringLiteral("pending marker cache mismatch %1").arg(QString::fromUtf8(label)));
  };
  requireCache("after full parse");
  require(session.applyTextDelta(0, 0, QStringLiteral("x"), true),
          "pending marker prefix insert should apply locally");
  requireCache("after suffix shift");
  const qsizetype marker = session.markdownText().toString().indexOf(QLatin1Char('*'));
  require(session.applyTextDelta(marker + 1, 0, QStringLiteral(" item"), true),
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

  const QVector<SourcePositionToken*> expectedOrder{
      original.at(0), firstReplacementPtr, secondReplacementPtr, original.at(3), original.at(4)};
  const QVector<qsizetype> expectedTypeRanks{0, 1, 0, 0, 2};
  for (qsizetype i = 0; i < expectedOrder.size(); ++i) {
    require(index.rank(expectedOrder.at(i)) == i,
            QStringLiteral("source position rank mismatch at %1").arg(i));
    require(index.typeRank(expectedOrder.at(i)) == expectedTypeRanks.at(i),
            QStringLiteral("source position type rank mismatch at %1").arg(i));
  }
  require(index.adjustmentFor(firstReplacementPtr).bytes == 0,
          "fresh replacement must not inherit the removed range's source shift");
  require(index.adjustmentFor(original.at(3)).bytes == 100 &&
              index.adjustmentFor(original.at(3)).lines == 4,
          "unchanged suffix token must preserve its lazy source shift through splice");
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  runTest("empty table", testEmptyTable);
  runTest("insert at boundaries", testInsertAtBoundaries);
  runTest("delete and replace whole", testDeleteAndReplaceWhole);
  runTest("cjk and surrogate pairs", testCjkAndSurrogates);
  runTest("coalesces consecutive appends", testCoalescesConsecutiveAppends);
  runTest("fuzz mirrors QString", testFuzzMirrorsQString);
  runTest("dual-write stays in sync", testDualWriteStaysInSyncWithMarkdownText);
  runTest("pending marker cache tracks local edits", testPendingMarkerCacheTracksLocalEdits);
  runTest("source position index preserves ranks", testSourcePositionIndexPreservesOrderAndTypeRanksAcrossSplices);
  return 0;
}
