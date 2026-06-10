#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

void testUnorderedListMarkerKindsByDepth() {
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("- level one\n"
                     "    - level two\n"
                     "        - level three\n"
                     "            - level four\n"
                     "\n"
                     "1. ordered one\n"
                     "    - ordered child bullet"),
      false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 6, QStringLiteral("list marker fixture should parse six list items"));
  require(layout.block(items.at(0)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletDisc,
          QStringLiteral("first unordered level should use a filled disc marker"));
  require(layout.block(items.at(1)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletCircle,
          QStringLiteral("second unordered level should use a hollow circle marker"));
  require(layout.block(items.at(2)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletSquare,
          QStringLiteral("third unordered level should use a filled square marker"));
  require(layout.block(items.at(3)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletSquare,
          QStringLiteral("fourth unordered level should continue using a filled square marker"));
  require(layout.block(items.at(4)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::OrderedText,
          QStringLiteral("ordered list marker should remain text"));
  require(layout.block(items.at(5)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletDisc,
          QStringLiteral("unordered child inside ordered list should start its own bullet depth"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testUnorderedListMarkerKindsByDepth);
#undef RUN_TEST
  return 0;
}
