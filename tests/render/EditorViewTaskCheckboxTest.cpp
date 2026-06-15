#include "EditorViewTestUtils.h"

using namespace muffin;

// Regression for the rendering bug where an unchecked task item (isTaskItem()==true,
// taskChecked()==false) rendered as a plain bullet instead of an empty checkbox,
// because the layout's "is a task item" flag was driven off taskChecked().
void testUncheckedTaskItemRendersAsCheckbox() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);

  session.setMarkdownText(QStringLiteral("- [ ] a\n- [x] b"), false);
  view.resize(800, 240);
  view.setDocument(session.document());

  // Look up the list-item layout directly: blockAtViewportPos would return the
  // innermost block (the paragraph child), which is not the task-item layout.
  const BlockLayout* unchecked = view.blockLayoutForNode(listItemAt(session, 0, 0)->id());
  require(unchecked != nullptr, "unchecked item layout should exist");
  require(unchecked->isTaskListItem(), "an unchecked task item must render as a checkbox, not a bullet");
  require(!unchecked->taskChecked(), "unchecked item should be unchecked");

  const BlockLayout* checked = view.blockLayoutForNode(listItemAt(session, 0, 1)->id());
  require(checked != nullptr, "checked item layout should exist");
  require(checked->isTaskListItem(), "a checked task item must still render as a checkbox");
  require(checked->taskChecked(), "checked item should be checked");
}

// Clicking inside the checkbox affordance flags the hit so the view toggles the
// item; clicking the content text does not.
void testCheckboxClickTarget() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);

  session.setMarkdownText(QStringLiteral("- [ ] a"), false);
  view.resize(800, 240);
  view.setDocument(session.document());

  const NodeId itemId = listItemAt(session, 0, 0)->id();
  const BlockLayout* item = view.blockLayoutForNode(itemId);
  require(item != nullptr, "task item layout should exist");
  require(item->isTaskListItem(), "item should be a task item");

  const RenderTheme theme = view.theme();
  const QRectF box = item->taskCheckboxRect(theme);
  require(!box.isEmpty(), "checkbox rect should be non-empty");

  const HitTestResult checkboxHit = view.hitTest(box.center());
  require(checkboxHit.blockId == itemId, "checkbox hit should target the task item");
  require(checkboxHit.taskCheckboxHit, "a click inside the checkbox should be flagged as a toggle hit");

  // A point just past the marker gutter, inside the content text, must not toggle.
  const QRectF itemRect = view.nodeRect(itemId);
  const QPointF contentPoint(itemRect.left() + item->listContentIndent() + 4.0, box.center().y());
  const HitTestResult contentHit = view.hitTest(contentPoint);
  require(!contentHit.taskCheckboxHit, "a click in the content should not toggle");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testUncheckedTaskItemRendersAsCheckbox);
  RUN_TEST(testCheckboxClickTarget);
#undef RUN_TEST
  return 0;
}
