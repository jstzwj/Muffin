#include "unicode/WordBoundary.h"

#include "../TestUtils.h"
#include <QCoreApplication>

namespace {

void wordSegmentationHasRuntimeData() {
  const QString latin = QStringLiteral("hello world");
  for (qsizetype offset : {qsizetype(0), qsizetype(2), qsizetype(5)}) {
    const auto segment = muffin::findWordSegment(latin, offset);
    require(segment.isWord && segment.start == 0 && segment.end == 5,
            "ICU runtime data must support the word under or before the caret");
  }
  const auto last = muffin::findWordSegment(latin, latin.size());
  require(last.isWord && last.start == 6 && last.end == latin.size(),
          "ICU word boundaries must include the final word");

  const QString chinese = QStringLiteral("你好世界");
  const auto first = muffin::findWordSegment(chinese, 1);
  const auto second = muffin::findWordSegment(chinese, 3);
  require(first.isWord && first.start == 0 && first.end == 2 && second.isWord && second.start == 2 && second.end == 4,
          "ICU runtime data must include Chinese dictionary segmentation");
}

} // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  runTest("wordSegmentationHasRuntimeData", wordSegmentationHasRuntimeData);
  return 0;
}
