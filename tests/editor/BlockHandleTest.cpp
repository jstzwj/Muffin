#include "editor/EditorView.h"
#include "editor/EditorController.h"
#include "render/BlockLayout.h"
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QThread>
#include <QTemporaryDir>
#include <QScrollBar>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace muffin;
static void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
static void waitFor(DocumentSession& session, const EditorView* view = nullptr) {
  QElapsedTimer timer;
  timer.start();
  while ((session.isBatchPending() || (view && view->isLayoutPending())) && timer.elapsed() < 10000) {
    QApplication::processEvents();
    QThread::msleep(1);
  }
  QApplication::processEvents();
  require(!session.isBatchPending(), "block operation must finish");
  require(!view || !view->isLayoutPending(), "block layout must finish");
}
static void mouse(EditorView& view, QEvent::Type type, QPointF point, Qt::MouseButton button,
                  Qt::MouseButtons buttons) {
  QMouseEvent event(type, point, view.viewport()->mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &event);
}
static void interactionAndHistory() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  const QString original = QStringLiteral("Alpha\n\nBravo\n\nCharlie");
  session.setMarkdownText(original, false);
  view.resize(750, 450);
  view.setDocument(session.document());
  view.show();
  QApplication::processEvents();
  const auto first = controller.blockTarget(session.document().root().children()[0]->id());
  const auto second = controller.blockTarget(session.document().root().children()[1]->id());
  const auto third = controller.blockTarget(session.document().root().children()[2]->id());
  const auto point = [&](BlockTarget target) {
    return view.mapDocumentToViewport(view.blockLayoutForNode(target.id)->rect().topLeft() + QPointF(30, 8));
  };
  mouse(view, QEvent::MouseMove, point(second), Qt::NoButton, Qt::NoButton);
  require(view.blockHandleRect().isEmpty(), "handles must be disabled by default");
  controller.setCursorForSourceOffset(2);
  const auto caret = controller.selection().cursorPosition();
  view.setBlockHandlesEnabled(true);
  mouse(view, QEvent::MouseMove, point(second), Qt::NoButton, Qt::NoButton);
  require(view.blockHandleTarget().id == second.id && !view.blockHandleRect().isEmpty(),
          "hover must expose the complete block's handle");
  if (const auto path = qgetenv("MUFFIN_BLOCK_HANDLE_SNAPSHOT"); !path.isEmpty())
    require(view.viewport()->grab().save(QString::fromLocal8Bit(path)), "block handle snapshot must save");
  int menus = 0;
  QObject::connect(&view, &EditorView::blockMenuRequested, &view, [&](BlockTarget target, QPoint) {
    require(target.id == second.id, "menu must target the hovered block instead of the writing caret");
    ++menus;
  });
  auto handle = view.blockHandleRect().center();
  mouse(view, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
  mouse(view, QEvent::MouseButtonRelease, handle, Qt::LeftButton, Qt::NoButton);
  require(menus == 1 && SelectionRange{caret, controller.selection().cursorPosition()}.isCollapsed(),
          "opening a handle menu must not move the text caret");
  require(controller.performBlockAction(second, EditorController::BlockAction::Copy) &&
              QApplication::clipboard()->text() == "Bravo",
          "copy must act on the explicit target without modifying source");
  mouse(view, QEvent::MouseMove, point(first), Qt::NoButton, Qt::NoButton);
  handle = view.blockHandleRect().center();
  const auto* last = view.blockLayoutForNode(third.id);
  const auto drop = view.mapDocumentToViewport(last->rect().bottomLeft() + QPointF(30, -1));
  mouse(view, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
  mouse(view, QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton);
  require(session.markdownText().toString() == original && !controller.canUndo(),
          "drag preview must not modify source or history");
  mouse(view, QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton);
  waitFor(session);
  const auto moved = session.markdownText().toString();
  require(moved == "Bravo\n\nCharlie\n\nAlpha\n\n", "dragging must move the selected complete block");
  controller.undo();
  waitFor(session);
  require(session.markdownText().toString() == original && !controller.canUndo(),
          "one undo must restore exact source and history");
  controller.redo();
  waitFor(session);
  require(session.markdownText().toString() == moved, "one redo must restore the moved block");
  require(!controller.performBlockAction(first, EditorController::BlockAction::Delete),
          "a stale menu target must not delete a new-generation block");
}
static void cancellationAndReadOnly() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText("First\n\nSecond", false);
  view.resize(700, 400);
  view.setDocument(session.document());
  view.setBlockHandlesEnabled(true);
  view.show();
  QApplication::processEvents();
  const auto id = session.document().root().children().front()->id();
  auto target = controller.blockTarget(id);
  const auto hover = view.mapDocumentToViewport(view.blockLayoutForNode(id)->rect().topLeft() + QPointF(30, 8));
  mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
  auto handle = view.blockHandleRect().center();
  mouse(view, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
  mouse(view, QEvent::MouseMove, handle + QPointF(0, 60), Qt::NoButton, Qt::LeftButton);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(&view, &escape);
  require(!view.blockInteractionActive() && !controller.canUndo(), "Escape must cancel without history");
  session.setEditingReadOnly(true);
  view.setEditingLocked(true);
  require(view.blockHandleRect().isEmpty() &&
              !controller.performBlockAction(target, EditorController::BlockAction::Duplicate),
          "read-only must reject mutation at both UI and command layers");
  session.setEditingReadOnly(false);
  view.setEditingLocked(false);
  view.setLivePreview(true);
  mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
  require(view.blockHandleRect().isEmpty(), "split preview must not expose editing handles");
  view.setLivePreview(false);
  mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
  view.setBlockHandlesEnabled(false);
  require(view.blockHandleRect().isEmpty() && !view.blockInteractionActive(),
          "disabling the feature must clear its interaction state");
}
static void targetedActionsAndInsert() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText("Keep\n\nTarget **bold**", false);
  view.resize(700, 400);
  view.setDocument(session.document());
  auto target = controller.blockTarget(session.document().root().children().back()->id());
  controller.setCursorForSourceOffset(1);
  require(controller.performBlockAction(target, EditorController::BlockAction::Heading2),
          "targeted conversion must start");
  waitFor(session);
  require(session.markdownText().toString() == "Keep\n\n## Target **bold**",
          "conversion must preserve inline Markdown and leave the other paragraph alone");
  controller.undo();
  waitFor(session);
  target = controller.blockTarget(session.document().root().children().back()->id());
  require(controller.performBlockAction(target, EditorController::BlockAction::InsertAfter),
          "insert below must create an editable paragraph");
  require(controller.inputController().insertText(QStringLiteral("中文")),
          "typing after block insertion must target its new paragraph");
  require(session.markdownText().toString().endsWith(QStringLiteral("\n\n中文")),
          "insert below must not append typing to the old block");
}
static void nestedContainersAndAutoScroll() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  const QString html =
      QStringLiteral("<div align='center'>\n\n# Inner\n\n<section>\n\nNested text\n\n</section>\n\n</div>");
  const QString original = html + "\n\n- Parent\n  - Child\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n" +
                           QStringLiteral("After\n\n").repeated(60);
  session.setMarkdownText(original, false);
  view.resize(750, 500);
  view.setDocument(session.document());
  view.setBlockHandlesEnabled(true);
  view.show();
  QApplication::processEvents();
  const auto& structure = session.document().blockStructure();
  const auto container = structure.targetAt(0), list = structure.targetAt(1);
  const auto* child = session.document().root().children().at(1).get();
  const auto hover =
      view.mapDocumentToViewport(view.blockLayoutForNode(child->id())->rect().topLeft() + QPointF(30, 8));
  mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
  require(view.blockHandleTarget().id == container.id,
          "hover inside nested HTML must target its complete outer container");
  require(controller.moveBlock(container, list, true), "moving a nested HTML container must start");
  waitFor(session);
  require(session.markdownText().toString().startsWith("- Parent\n  - Child\n\n" + html),
          "moving containers must preserve lists and complete paired HTML");
  controller.undo();
  waitFor(session);
  require(session.markdownText().toString() == original, "undo must restore complex container source exactly");
  const auto fresh = controller.blockTarget(session.document().root().children().front()->id());
  const auto top = view.mapDocumentToViewport(view.blockLayoutForNode(fresh.id)->rect().topLeft() + QPointF(30, 8));
  mouse(view, QEvent::MouseMove, top, Qt::NoButton, Qt::NoButton);
  const auto handle = view.blockHandleRect().center();
  mouse(view, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
  const auto edge = QPointF(100, view.viewport()->height() - 2);
  mouse(view, QEvent::MouseMove, edge, Qt::NoButton, Qt::LeftButton);
  const auto previousScroll = view.verticalScrollBar()->value();
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 150) {
    QApplication::processEvents();
    QThread::msleep(1);
  }
  require(view.verticalScrollBar()->value() > previousScroll,
          "dragging at an edge must auto-scroll without modifying text");
  view.setBlockHandlesEnabled(false);
  require(session.markdownText().toString() == original && !view.blockInteractionActive(),
          "disabling during a drag must cancel without changing source");
}
static void themeGeometry() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText("# Heading\n\nA paragraph with **bold** text.", false);
  view.resize(750, 450);
  view.setDocument(session.document());
  view.setBlockHandlesEnabled(true);
  view.show();
  require(ThemeDefinition::builtIns().size() == 5, "theme geometry must exercise all five built-in themes");
  for (const auto& definition : ThemeDefinition::builtIns()) {
    view.setTheme(RenderTheme::fromDefinition(definition));
    QApplication::processEvents();
    const auto id = session.document().root().children().back()->id();
    const auto* block = view.blockLayoutForNode(id);
    const auto point = view.mapDocumentToViewport(block->rect().topLeft() + QPointF(30, 8));
    mouse(view, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
    require(!view.blockHandleRect().isEmpty() &&
                view.viewport()->rect().contains(view.blockHandleRect().toAlignedRect()),
            "block handles must remain visible within each built-in theme's gutter");
    view.cancelBlockInteraction();
    mouse(view, QEvent::MouseMove, QPointF(1, 1), Qt::NoButton, Qt::NoButton);
    require(view.viewport()->cursor().shape() != Qt::OpenHandCursor,
            "an empty handle must not create a false hit region at the viewport origin");
  }
}
static void headingGutterOwnership() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(850, 500);
  view.show();
  int menus = 0;
  QObject::connect(&view, &EditorView::blockMenuRequested, &view, [&](BlockTarget, QPoint) { ++menus; });
  const auto themes = ThemeDefinition::builtIns();
  for (const auto& theme : themes) {
    view.setTheme(RenderTheme::fromDefinition(theme));
    for (int level = 1; level <= 6; ++level) {
      const QString source = QString(level, u'#') + QStringLiteral(" A heading\n\nBody");
      session.setMarkdownText(source, false);
      view.setDocument(session.document());
      waitFor(session, &view);
      const auto id = session.document().root().children().front()->id();
      const auto* block = view.blockLayoutForNode(id);
      require(block, "heading gutter fixture must have a live layout");
      const auto rect = view.mapDocumentToViewport(block->rect().topLeft());
      // Compare the controls' first-line band, excluding text antialiasing
      // and CSS heading borders (which can land on fractional device pixels).
      const int right = int(std::floor(rect.x())) - 2;
      const int left = qMax(0, right - 48);
      const QRect gutter(left, qMax(0, int(std::floor(rect.y()))), right - left,
                         qMin(20, qMax(1, int(std::floor(block->rect().height())) - 4)));
      require(!gutter.isEmpty(), "theme must leave a gutter for the heading controls");
      const auto snapshot = [&] { return view.viewport()->grab(gutter).toImage(); };
      const auto hover = rect + QPointF(30, 8);
      view.setBlockHandlesEnabled(false);
      controller.setCursorForSourceOffset(source.indexOf("A heading") + 1);
      const auto badge = snapshot();
      if (level == 3 && theme.id == themes.front().id) {
        if (const auto path = qgetenv("MUFFIN_HEADING_GUTTER_SNAPSHOT"); !path.isEmpty())
          require(view.viewport()->grab().save(QString::fromLocal8Bit(path) + "-badge.png"),
                  "badge snapshot must save");
      }
      controller.setCursorForSourceOffset(source.indexOf("Body") + 1);
      const auto empty = snapshot();
      if ((level <= 2) != (badge == empty)) {
        std::cerr << "badge mismatch: theme=" << theme.id.toStdString() << " level=" << level << '\n';
        if (const auto path = qgetenv("MUFFIN_HEADING_GUTTER_SNAPSHOT"); !path.isEmpty()) {
          badge.save(QString::fromLocal8Bit(path) + "-badge-actual.png");
          empty.save(QString::fromLocal8Bit(path) + "-badge-empty.png");
        }
      }
      require((level <= 2) == (badge == empty), "active H3-H6 annotations must be visible before hovering");
      view.setBlockHandlesEnabled(true);
      mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
      const auto handleOnly = snapshot();
      require(!view.blockHandleRect().isEmpty() && handleOnly != empty,
              "heading hover must display a usable handle in the gutter");
      controller.setCursorForSourceOffset(source.indexOf("A heading") + 1);
      mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
      const auto activeHandle = snapshot();
      if (activeHandle != handleOnly) {
        std::cerr << "gutter mismatch: theme=" << theme.id.toStdString() << " level=" << level << " rect=" << gutter.x()
                  << ',' << gutter.y() << ',' << gutter.width() << ',' << gutter.height()
                  << " handle=" << view.blockHandleRect().x() << ',' << view.blockHandleRect().y() << '\n';
        if (const auto path = qgetenv("MUFFIN_HEADING_GUTTER_SNAPSHOT"); !path.isEmpty()) {
          activeHandle.save(QString::fromLocal8Bit(path) + "-actual.png");
          handleOnly.save(QString::fromLocal8Bit(path) + "-expected.png");
        }
      }
      require(activeHandle == handleOnly, "active heading badge must yield its gutter to the hovered handle");
      if (level == 3 && theme.id == themes.front().id) {
        if (const auto path = qgetenv("MUFFIN_HEADING_GUTTER_SNAPSHOT"); !path.isEmpty())
          require(view.viewport()->grab().save(QString::fromLocal8Bit(path) + "-handle.png"),
                  "handle snapshot must save");
      }
      const auto* body = view.blockLayoutForNode(session.document().root().children().back()->id());
      require(body, "body hover fixture must have a layout");
      mouse(view, QEvent::MouseMove, view.mapDocumentToViewport(body->rect().topLeft()) + QPointF(30, 8), Qt::NoButton,
            Qt::NoButton);
      require(snapshot() == badge, "hovering a different block must restore the active heading annotation");
      mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
      QEvent leave(QEvent::Leave);
      QApplication::sendEvent(view.viewport(), &leave);
      require(view.blockHandleRect().isEmpty() && snapshot() == badge,
              "leaving the viewport must restore the heading annotation without stale pixels");
      mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
      const auto handle = view.blockHandleRect().center();
      const auto caret = controller.selection().cursorPosition();
      const int before = menus;
      mouse(view, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
      mouse(view, QEvent::MouseButtonRelease, handle, Qt::LeftButton, Qt::NoButton);
      require(menus == before + 1 && SelectionRange{caret, controller.selection().cursorPosition()}.isCollapsed(),
              "gutter switching must preserve the handle hit target and writing caret");
      require(snapshot() == badge, "leaving the handle interaction must restore the heading annotation");
      mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
      view.setBlockHandlesEnabled(false);
      require(snapshot() == badge, "disabling handles must restore the original heading annotation");
    }
  }
}
static void nestedListDragAndContinuousHistory() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText("- Alpha\n\n  paragraph\n\n  ```cpp\n  code\n  ```\n\n  - Child A\n  - Child B\n"
                          "- Bravo\n- Charlie\n\nTail",
                          false);
  view.resize(850, 900);
  view.setDocument(session.document());
  QObject::connect(&view, &EditorView::layoutBusyChanged, &session, &DocumentSession::setPresentationBusy);
  QObject::connect(&session, &DocumentSession::parsed, &view, [&] {
    if (!session.lastParseWasLocalEdit() || session.lastChangeWasBatch())
      view.setDocument(session.document(), {}, true);
  });
  view.setBlockHandlesEnabled(true);
  view.show();
  QApplication::processEvents();
  controller.setCursorForSourceOffset(session.markdownText().toString().indexOf("Alpha") + 5);
  QInputMethodEvent commit;
  commit.setCommitString(QStringLiteral("中文"));
  QApplication::sendEvent(&view, &commit);
  const auto before = session.markdownText().toString();
  require(before.contains(QStringLiteral("Alpha中文")), "IME commit must precede a structural drag safely");
  const auto findItem = [&](const QString& word) {
    const auto cursor = controller.cursorForSourceOffset(session.markdownText().toString().indexOf(word));
    return controller.blockTarget(cursor.blockId);
  };
  const auto drag = [&](BlockTarget from, BlockTarget to, bool after) {
    const auto* source = view.blockLayoutForNode(from.id);
    require(source && from.isValid(), "a nested drag source must have a live layout");
    const auto hover = view.mapDocumentToViewport(source->rect().topLeft() + QPointF(50, 7));
    mouse(view, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
    require(view.blockHandleTarget().id == from.id, "hover must identify the individual list item");
    const auto handle = view.blockHandleRect().center();
    const auto* target = view.blockLayoutForNode(to.id);
    require(target && to.isValid(), "a nested drag destination must have a live layout");
    const auto drop = view.mapDocumentToViewport(
        QPointF(target->rect().left() + 50, after ? target->rect().bottom() - 1 : target->rect().top() + 1));
    mouse(view, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
    mouse(view, QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton);
    waitFor(session, &view);
  };
  drag(findItem("Child B"), findItem("Child A"), false);
  const auto childrenMoved = session.markdownText().toString();
  require(childrenMoved.indexOf("Child B") < childrenMoved.indexOf("Child A") && childrenMoved.contains("  code\n"),
          "child-item sorting must leave its parent's other blocks intact");
  drag(findItem("Alpha"), findItem("Charlie"), true);
  const auto parentMoved = session.markdownText().toString();
  require(parentMoved.startsWith("- Bravo\n- Charlie\n") &&
              parentMoved.indexOf("Alpha") < parentMoved.indexOf("code") &&
              parentMoved.indexOf("code") < parentMoved.indexOf("Child B"),
          "moving a parent must carry paragraphs, code and its reordered child list together");
  controller.undo();
  waitFor(session);
  require(session.markdownText().toString() == childrenMoved, "one undo restores the complete parent move");
  controller.undo();
  waitFor(session);
  require(session.markdownText().toString() == before,
          "second undo restores only the child move, retaining Chinese input");
  controller.redo();
  waitFor(session);
  controller.redo();
  waitFor(session);
  require(session.markdownText().toString() == parentMoved, "two redo operations restore nested reorder exactly");
  auto target = findItem("Bravo");
  require(controller.performBlockAction(target, EditorController::BlockAction::InsertAfter), "insert sibling starts");
  waitFor(session);
  require(controller.inputController().insertText(QStringLiteral("新项目")), "typing into a new sibling must succeed");
  require(session.markdownText().toString().startsWith(QStringLiteral("- Bravo\n- 新项目\n- Charlie")),
          "inserting through the handle must retain list structure and target the new item");
}
static void eofLiteralContinuousHistory() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  const QString original = "Alpha\n\nBeta\n\n```cpp\nint value;";
  session.setMarkdownText(original, false);
  view.resize(800, 600);
  view.setDocument(session.document());
  auto& document = session.document();
  require(controller.performBlockAction(document.blockStructure().targetAt(2), EditorController::BlockAction::MoveUp),
          "EOF code move must start");
  waitFor(session, &view);
  const auto moved = session.markdownText().toString();
  require(moved == "Alpha\n\n```cpp\nint value;\n```\n\nBeta\n\n",
          "moving EOF code must close it before its new following block");
  require(
      controller.performBlockAction(document.blockStructure().targetAt(1), EditorController::BlockAction::Duplicate),
      "duplicating moved code must start");
  waitFor(session, &view);
  const auto duplicated = session.markdownText().toString();
  int codeBlocks = 0;
  for (const auto& node : document.root().children())
    codeBlocks += node->type() == BlockType::CodeFence;
  require(codeBlocks == 2 && duplicated.count("int value;") == 2, "code copies must remain independent literal blocks");
  controller.undo();
  waitFor(session, &view);
  require(session.markdownText().toString() == moved, "undo duplication must retain the first move");
  controller.undo();
  waitFor(session, &view);
  require(session.markdownText().toString() == original, "undo move must remove its implicit closure too");
  controller.redo();
  waitFor(session, &view);
  controller.redo();
  waitFor(session, &view);
  require(session.markdownText().toString() == duplicated, "redo sequence must restore both independent code blocks");

  session.setMarkdownText(original, false);
  view.setDocument(document);
  require(
      controller.performBlockAction(document.blockStructure().targetAt(2), EditorController::BlockAction::InsertAfter),
      "insert below unterminated code must start");
  require(controller.inputController().insertText(QStringLiteral("Outside")),
          "typing after unterminated code must work");
  require(document.root().children().back()->type() == BlockType::Paragraph &&
              session.markdownText().toString() == original + "\n```\n\nOutside",
          "new paragraph and typed text must stay outside the original literal");
}
static void conversionContinuousHistory() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  const QString original = "Before\n\n## > **quoted** ##\n\nAfter";
  session.setMarkdownText(original, false);
  view.resize(800, 600);
  view.setDocument(session.document());
  auto target = session.document().blockStructure().targetAt(1);
  require(controller.performBlockAction(target, EditorController::BlockAction::Paragraph),
          "heading demotion must start");
  waitFor(session, &view);
  const auto paragraph = session.markdownText().toString();
  require(paragraph == "Before\n\n\\> **quoted**\n\nAfter" &&
              session.document().root().children()[1]->type() == BlockType::Paragraph,
          "heading conversion must retain a literal quote marker and inline formatting");
  target = session.document().blockStructure().targetAt(1);
  require(controller.performBlockAction(target, EditorController::BlockAction::BulletList),
          "paragraph to list must start");
  waitFor(session, &view);
  const auto list = session.markdownText().toString();
  require(list == "Before\n\n- \\> **quoted**\n\nAfter", "list conversion must share the protected inline source");
  controller.undo();
  waitFor(session, &view);
  require(session.markdownText().toString() == paragraph, "undo list must return to a paragraph");
  controller.undo();
  waitFor(session, &view);
  require(session.markdownText().toString() == original, "undo demotion must restore original closing ATX hashes");
  controller.redo();
  waitFor(session, &view);
  controller.redo();
  waitFor(session, &view);
  require(session.markdownText().toString() == list, "redo conversions must reproduce exact protected source");
}
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("MuffinTest");
  QCoreApplication::setApplicationName("BlockHandleTest");
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  interactionAndHistory();
  cancellationAndReadOnly();
  targetedActionsAndInsert();
  nestedContainersAndAutoScroll();
  themeGeometry();
  headingGutterOwnership();
  nestedListDragAndContinuousHistory();
  eofLiteralContinuousHistory();
  conversionContinuousHistory();
  QApplication::clipboard()->clear();
}
