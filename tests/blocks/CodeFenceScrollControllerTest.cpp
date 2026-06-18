#include "blocks/code/CodeFenceScrollController.h"
#include "document/NodeId.h"
#include "render/BlockLayout.h"
#include "theme/RenderTheme.h"

#include "../TestUtils.h"

#include <QCoreApplication>

#include <iostream>

using namespace muffin;

// The controller lives outside the rebuilt BlockLayouts and is keyed by NodeId, so its state must
// survive both layout rebuilds and id-remapping reparses. These guard the rebuild-survival logic
// and the scroll clamp that keeps the offset inside [0, contentWidth - visibleWidth].
void testOffsetStorageAndClamp() {
  CodeFenceScrollController scroll;
  const NodeId a = NodeId::create();
  require(scroll.offsetFor(a) == 0.0, "absent offset defaults to 0");
  scroll.setContentWidth(a, 500.0);
  require(scroll.contentWidthFor(a) == 500.0, "content width round-trips");
  scroll.setOffset(a, 120.0);
  require(scroll.offsetFor(a) == 120.0, "offset round-trips");
  require(scroll.clampedOffset(a, 300.0) == 120.0, "in-range offset is unchanged by clamp");
  scroll.setOffset(a, 9999.0);
  require(scroll.clampedOffset(a, 300.0) == 200.0, "offset clamps to contentWidth - visibleWidth");
  scroll.setOffset(a, -5.0);
  require(scroll.clampedOffset(a, 300.0) == 0.0, "offset clamps at 0");
  require(scroll.clampedOffset(a, 600.0) == 0.0, "no overflow (visible >= content) forces offset 0");
}

void testRemapSurvivesReparse() {
  CodeFenceScrollController scroll;
  const NodeId old1 = NodeId::create();
  const NodeId old2 = NodeId::create();
  const NodeId new1 = NodeId::create();
  scroll.setContentWidth(old1, 400.0);
  scroll.setOffset(old1, 80.0);
  scroll.setContentWidth(old2, 100.0);
  scroll.setOffset(old2, 10.0);
  scroll.remapAfterReparse({{old1, new1}});  // old1 -> new1; old2 untouched
  require(scroll.offsetFor(new1) == 80.0, "remapped id keeps its offset");
  require(scroll.contentWidthFor(new1) == 400.0, "remapped id keeps its content width");
  require(scroll.offsetFor(old2) == 10.0, "an unmapped id is preserved as-is");
  require(scroll.offsetFor(old1) == 0.0, "the old id no longer holds the entry");
}

void testScrollBarStripHeight() {
  const RenderTheme theme = RenderTheme::github();
  const qreal h = BlockLayout::scrollBarStripHeight(theme);
  require(h >= 8.0, "scrollbar strip height has a sensible minimum");
}

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("CodeFenceScrollControllerTest"));
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testOffsetStorageAndClamp);
  RUN_TEST(testRemapSurvivesReparse);
  RUN_TEST(testScrollBarStripHeight);
#undef RUN_TEST
  return 0;
}
