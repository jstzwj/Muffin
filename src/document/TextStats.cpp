#include "document/TextStats.h"

#include "document/PieceTable.h"

namespace muffin::text_stats {
namespace {

class WordCounter {
public:
  void append(QStringView text) {
    for (const QChar ch : text) {
      const bool wordChar = ch.isLetterOrNumber() || ch == QLatin1Char('_');
      if (wordChar && !inWord_) {
        ++count_;
      }
      inWord_ = wordChar;
    }
  }

  int count() const { return count_; }

private:
  int count_ = 0;
  bool inWord_ = false;
};

}  // namespace

int countWords(QStringView text) {
  WordCounter counter;
  counter.append(text);
  return counter.count();
}

int countWords(const PieceTable& text) {
  WordCounter counter;
  text.forEachChunk([&counter](QStringView chunk) { counter.append(chunk); });
  return counter.count();
}

}  // namespace muffin::text_stats
