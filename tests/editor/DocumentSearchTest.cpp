#include "editor/DocumentSearch.h"

#include "document/PieceTable.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>

#include <cstdlib>

using namespace muffin;

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  SearchResults literal = DocumentSearch::findAll(QStringLiteral("Alpha alpha ALPHA"), QStringLiteral("alpha"), {});
  require(literal.valid && literal.matches.size() == 3,
          QStringLiteral("Literal search should be case-insensitive by default"));

  SearchResults sensitive =
      DocumentSearch::findAll(QStringLiteral("Alpha alpha"), QStringLiteral("alpha"), {false, true});
  require(sensitive.matches.size() == 1 && sensitive.matches.first().start == 6,
          QStringLiteral("Case-sensitive search mismatch"));

  PieceTable fragmented(QStringLiteral("Alpha alpha ALPHA"));
  fragmented.replace(4, 8, QStringLiteral("a al"));
  SearchResults streamed = DocumentSearch::findAll(fragmented, QStringLiteral("alpha"), {});
  require(fragmented.toString() == QStringLiteral("Alpha alpha ALPHA"),
          QStringLiteral("Piece search fixture mismatch"));
  require(streamed.matches.size() == 3 && streamed.matches.at(1).start == 6,
          QStringLiteral("Piece search should cross fragmented boundaries"));

  QString replaced;
  SearchResults captures = DocumentSearch::replaceAll(QStringLiteral("one=1 two=22"), QStringLiteral("(\\w+)=(\\d+)"),
                                                      QStringLiteral("$2:$1"), {true, true}, &replaced);
  require(captures.valid && captures.matches.size() == 2, QStringLiteral("Regex matches missing"));
  require(replaced == QStringLiteral("1:one 22:two"), QStringLiteral("Capture replacement failed: %1").arg(replaced));

  SearchResults zeroWidth = DocumentSearch::replaceAll(QStringLiteral("aa"), QStringLiteral("(?=a)"),
                                                       QStringLiteral("x"), {true, true}, &replaced);
  require(zeroWidth.matches.size() == 2 && replaced == QStringLiteral("xaxa"),
          QStringLiteral("Zero-width regex replacement failed"));
  QVector<qsizetype> positions{0, 1, 3, 4, 7, -1};
  const auto mapped = DocumentSearch::replaceAll("foo foo", "foo", "xy", {}, &replaced, {}, &positions);
  require(mapped.changed && replaced == "xy xy" && positions == QVector<qsizetype>({0, 1, 2, 3, 5, -1}),
          "replacement maps source anchors inside and after multiple matches");
  positions = {0, 1, 2};
  DocumentSearch::replaceAll("aa", "(?=a)", "x", {true, true}, &replaced, {}, &positions);
  require(positions == QVector<qsizetype>({1, 3, 4}), "zero-width insertions map source anchors forward");
  require(!DocumentSearch::replaceAll("same", "same", "$0", {true, true}, &replaced).changed,
          "identity replacement cannot create a document edit");

  SearchResults invalid = DocumentSearch::findAll(QStringLiteral("text"), QStringLiteral("("), {true, true});
  require(!invalid.valid && !invalid.error.isEmpty(), QStringLiteral("Invalid regex should return an error"));
  const QString dense = QStringLiteral("a ").repeated(100000);
  QElapsedTimer timer;
  timer.start();
  const auto bulk = DocumentSearch::replaceAll(dense, "a", "longer replacement", {}, &replaced);
  require(bulk.totalMatches == 100000 && bulk.matches.size() == DocumentSearch::kCachedMatches &&
              replaced == QStringLiteral("longer replacement ").repeated(100000),
          "complete bounded bulk replacement");
  require(timer.elapsed() < 10000, "replacement must not repeatedly move the growing document tail");
  qInfo() << "100000 replacements:" << timer.elapsed() << "ms; cached matches:" << bulk.matches.size();
  const auto navigation = DocumentSearch::findAll(PieceTable(dense), "a", {}, {}, 150000, true, 150000);
  require(navigation.totalMatches == 100000 && navigation.matches.size() == DocumentSearch::kCachedMatches &&
              navigation.selectedMatch.start == 150002 && navigation.selectedIndex == 75001,
          "navigation past cache limit retains exact count and position");
  const auto millions = DocumentSearch::findAll(PieceTable(QString(4 * 1024 * 1024, u'a')), "a", {});
  require(millions.totalMatches == 4 * 1024 * 1024 && millions.matches.size() == DocumentSearch::kCachedMatches,
          "millions of hits must not create millions of result records");
  auto reverse = DocumentSearch::findAll(PieceTable("aa"), "(?=a)", {true, true}, {}, 1, false, 1);
  require(reverse.selectedMatch.start == 0, "backward zero-width navigation advances");
  const auto cancel = std::make_shared<std::atomic_bool>(true);
  replaced = "unchanged";
  const auto interrupted = DocumentSearch::replaceAll(dense, "a", "x", {}, &replaced, cancel);
  require(interrupted.canceled && replaced == "unchanged", "cancellation cannot publish partial replacement");
  timer.restart();
  const auto pathological = DocumentSearch::findAll(QString(100000, u'a') + "!", "(a+)+$", {true, true});
  require(!pathological.valid && !pathological.error.isEmpty() && timer.elapsed() < 10000,
          "pathological regex must fail explicitly within its resource budget");
  require(DocumentSearch::expandReplacement("$99999999999999999999999999999999999999", {}, true).isEmpty(),
          "oversized capture numbers cannot overflow");
  return 0;
}
