#include "editor/DocumentSearch.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>

using namespace muffin;

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  SearchResults literal = DocumentSearch::findAll(
      QStringLiteral("Alpha alpha ALPHA"), QStringLiteral("alpha"), {});
  require(literal.valid && literal.matches.size() == 3,
          QStringLiteral("Literal search should be case-insensitive by default"));

  SearchResults sensitive = DocumentSearch::findAll(
      QStringLiteral("Alpha alpha"), QStringLiteral("alpha"), {false, true});
  require(sensitive.matches.size() == 1 && sensitive.matches.first().start == 6,
          QStringLiteral("Case-sensitive search mismatch"));

  QString replaced;
  SearchResults captures = DocumentSearch::replaceAll(
      QStringLiteral("one=1 two=22"), QStringLiteral("(\\w+)=(\\d+)"),
      QStringLiteral("$2:$1"), {true, true}, &replaced);
  require(captures.valid && captures.matches.size() == 2,
          QStringLiteral("Regex matches missing"));
  require(replaced == QStringLiteral("1:one 22:two"),
          QStringLiteral("Capture replacement failed: %1").arg(replaced));

  SearchResults zeroWidth = DocumentSearch::replaceAll(
      QStringLiteral("aa"), QStringLiteral("(?=a)"), QStringLiteral("x"),
      {true, true}, &replaced);
  require(zeroWidth.matches.size() == 2 && replaced == QStringLiteral("xaxa"),
          QStringLiteral("Zero-width regex replacement failed"));

  SearchResults invalid = DocumentSearch::findAll(
      QStringLiteral("text"), QStringLiteral("("), {true, true});
  require(!invalid.valid && !invalid.error.isEmpty(),
          QStringLiteral("Invalid regex should return an error"));
  return 0;
}
