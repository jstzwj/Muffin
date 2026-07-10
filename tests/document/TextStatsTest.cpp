#include "document/PieceTable.h"
#include "document/TextStats.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

void testChunkBoundaryDoesNotSplitWord() {
  PieceTable text(QStringLiteral("ac def"));
  text.replace(1, 1, QStringLiteral("b"));
  require(text.toString() == QStringLiteral("abc def"), "piece setup mismatch");
  require(text.pieceCount() == 3, "test requires a word split over multiple pieces");
  require(text_stats::countWords(text) == 2, "chunk boundary split one logical word");
}

void testWordRulesMatchContiguousText() {
  const QString sample = QStringLiteral("one_two 42, caf\u00e9\nnext");
  PieceTable text(sample);
  text.replace(3, 3, QStringLiteral("_"));
  require(text_stats::countWords(text) == text_stats::countWords(QStringView(text.toString())),
          "piece and contiguous word counts differ");
  require(text_stats::countWords(text) == 4, "word rule count mismatch");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testChunkBoundaryDoesNotSplitWord();
  testWordRulesMatchContiguousText();
  return 0;
}
