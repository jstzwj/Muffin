#include "editor/SourceLineHeightIndex.h"

#include "../TestUtils.h"

#include <QCoreApplication>

using namespace muffin;

void testCompressedDefaultsAndPointMeasurements() {
  SourceLineHeightIndex index;
  index.reset(1'000'000, 20);
  require(index.lineCount() == 1'000'000, "line height index count mismatch");
  require(index.totalHeight() == 20'000'000, "default total height mismatch");
  require(index.yForLine(500'000) == 10'000'000, "default prefix height mismatch");
  require(index.lineAtY(10'000'019) == 500'000, "default y lookup mismatch");

  index.setHeight(10, 60);
  index.setHeight(500'000, 40);
  index.setHeight(999'999, 80);
  require(index.heightForLine(10) == 60, "first measured height mismatch");
  require(index.heightForLine(500'000) == 40, "middle measured height mismatch");
  require(index.heightForLine(999'999) == 80, "last measured height mismatch");
  require(index.totalHeight() == 20'000'120, "measured total height mismatch");
  require(index.yForLine(11) == 260, "measured prefix after first line mismatch");
  require(index.yForLine(500'001) == 10'000'080, "measured middle prefix mismatch");
  require(index.lineAtY(index.yForLine(500'000)) == 500'000, "measured y lookup mismatch");
  require(index.lineAtY(index.totalHeight() - 1) == 999'999, "last y lookup mismatch");
}

void testResetIsIndependentOfPriorMeasurements() {
  SourceLineHeightIndex index;
  index.reset(100, 10);
  for (int line = 0; line < 100; line += 3) index.setHeight(line, 50);
  index.reset(7, 17);
  require(index.lineCount() == 7, "reset line count mismatch");
  require(index.totalHeight() == 119, "reset total height mismatch");
  for (int line = 0; line < 7; ++line) {
    require(index.heightForLine(line) == 17, "reset should discard measured heights");
    require(index.lineAtY(index.yForLine(line)) == line, "reset y round trip mismatch");
  }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  runTest("testCompressedDefaultsAndPointMeasurements", testCompressedDefaultsAndPointMeasurements);
  runTest("testResetIsIndependentOfPriorMeasurements", testResetIsIndependentOfPriorMeasurements);
  return 0;
}
