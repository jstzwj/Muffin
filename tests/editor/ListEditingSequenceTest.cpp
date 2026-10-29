#include "EditorTestUtils.h"
#include "document/ListItemSource.h"

#include <QClipboard>
#include <QInputMethodEvent>
#include <QMimeData>

using namespace muffin;

namespace {
struct Harness {
  DocumentSession session;
  EditorController controller;
  EditorView view;

  explicit Harness(const QString& text) {
    controller.attach(&session, &view);
    view.resize(900, 600);
    session.setMarkdownText(text, false);
    view.setDocument(session.document());
  }
  MarkdownNode* inner(int index = 0) {
    return childAt(firstChildOfType(listItemAt(session, 0, 0), BlockType::List), index);
  }
  void expect(const QString& text) {
    controller.brushQueue().flush();
    require(session.markdownText().toString() == text,
            QStringLiteral("source mismatch: expected [%1], got [%2]").arg(text, session.markdownText().toString()));
    require(controller.selection().hasCursor(), "each operation must retain a valid caret");
    require(view.hitForCursorPosition(controller.selection().cursorPosition()).isValid(),
            QStringLiteral("the source caret must remain renderable: text [%1], caret %2 source %3")
                .arg(text)
                .arg(controller.selection().cursorPosition().text.textOffset)
                .arg(controller.selection().cursorPosition().text.sourceOffset));
  }
};

void testSharedSourceRanges() {
  Harness h(QStringLiteral("3. 4. 正文\n      - child\n4. last"));
  const auto outer = listItemSource(h.session.markdownText(), *listItemAt(h.session, 0, 0));
  const auto inner = listItemSource(h.session.markdownText(), *h.inner());
  require(outer.isValid() && inner.isValid(), "both same-line list levels need source ranges");
  require(outer.content.start == 3 && outer.content.length() == 0 && outer.children.start == 3,
          "an empty parent must not own its child's source text");
  require(inner.item.start == 3 && inner.content.start == 6 && inner.content.end == 8,
          "nested content must start after its own marker");
  require(inner.children.start > inner.content.end && inner.children.end == inner.item.end,
          "descendants must be separate from the editable paragraph");
  setCursor(h.controller.selection(), h.inner(), 0);
  const BlockLayout* layout = h.view.blockLayoutForNode(h.inner()->id());
  require(layout && layout->contentSourceStart() == inner.content.start && layout->listMarker() == "4.",
          "rendering must consume the same marker and content bounds");
}

void testSourceOwnershipAndRevision() {
  Harness h(QStringLiteral("3. 4. - [x] 中文\n4. 后续"));
  const auto parent = listItemSource(h.session.markdownText(), *h.inner());
  auto* task = childAt(firstChildOfType(h.inner(), BlockType::List), 0);
  const auto source = listItemSource(h.session.markdownText(), *task);
  require(parent.content.length() == 0 && parent.children.start == source.item.start,
          "each empty level must stop before the next marker");
  require(source.marker.task && source.marker.taskChecked && source.item.start == 6 && source.content.start == 12,
          "third-level checkbox and content must have independent absolute bounds");
  setCursor(h.controller.selection(), task, 0);
  require(h.controller.inputController().insertText(QStringLiteral("新增")), "edit a third-level task");
  h.expect(QStringLiteral("3. 4. - [x] 新增中文\n4. 后续"));
  const auto updated =
      listItemSource(h.session.markdownText(), *childAt(firstChildOfType(h.inner(), BlockType::List), 0));
  require(updated.content.length() == 4 && updated.item.end == updated.content.end,
          "source bounds must reflect the current document revision");
  h.controller.undo();
  h.expect(QStringLiteral("3. 4. - [x] 中文\n4. 后续"));

  Harness blocks(QStringLiteral("1. # 标题\n\n   独立段落\n\n   ```text\n   2. literal\n   ```\n2. 后续"));
  const auto blockSource = listItemSource(blocks.session.markdownText(), *listItemAt(blocks.session, 0, 0));
  require(!blockSource.paragraph && blockSource.content.length() == 0 && blockSource.children.start == 3,
          "a heading, later paragraph and fence are child blocks, not the marker's inline body");
}

void testEnterSameLineNestedItem() {
  struct Case {
    int offset;
    QString expected;
  };
  const Case cases[] = {
      {0, "3. 4. \n   5. body\n   6. next\n4. last"},
      {2, "3. 4. bo\n   5. dy\n   6. next\n4. last"},
      {4, "3. 4. body\n   5. \n   6. next\n4. last"},
  };
  for (const auto& c : cases) {
    for (bool paragraphCursor : {false, true}) {
      const QString original = QStringLiteral("3. 4. body\n   5. next\n4. last");
      Harness h(original);
      setCursor(h.controller.selection(), paragraphCursor ? primaryParagraph(*h.inner()) : h.inner(), c.offset);
      require(h.controller.inputController().insertParagraphBreak(), "Enter must split within the inner list");
      h.expect(c.expected);
      h.controller.undo();
      h.expect(original);
      require(h.controller.selection().cursorPosition().text.textOffset == c.offset,
              "undo must restore the text caret");
      h.controller.redo();
      h.expect(c.expected);
    }
  }
}

void testDeleteAcrossNestedSiblings() {
  const QString original = QStringLiteral("3. 4. body\n   5. next\n4. last");
  Harness h(original);
  setCursor(h.controller.selection(), h.inner(), 4);
  require(h.controller.inputController().deleteForward(), "Delete must merge adjacent nested items");
  h.expect(QStringLiteral("3. 4. body next\n4. last"));
  h.controller.undo();
  h.expect(original);
  h.controller.redo();
  h.expect(QStringLiteral("3. 4. body next\n4. last"));
}

void testDeleteAtEmptyParent() {
  const QString original = QStringLiteral("3. 4. body\n4. last");
  Harness h(original);
  setCursor(h.controller.selection(), listItemAt(h.session, 0, 0), 0);
  require(h.controller.inputController().deleteForward(), "Delete must collapse an empty parent into its child");
  h.expect(QStringLiteral("3. body\n4. last"));
  h.controller.undo();
  h.expect(original);
}

void testNumberingDoesNotTouchLiteralBlocks() {
  const QString original = QStringLiteral("1. alpha\n\n   ```text\n   2. code\n   ```\n2. beta");
  Harness h(original);
  setCursor(h.controller.selection(), listItemAt(h.session, 0, 0), 5);
  require(h.controller.inputController().insertParagraphBreak(), "Enter must create a new list item");
  h.expect(QStringLiteral("1. alpha\n2. \n\n   ```text\n   2. code\n   ```\n3. beta"));
  h.controller.undo();
  h.expect(original);
}

void testCrossLevelCutUndoRedo() {
  SettingsOverride copyAsMarkdown("editor/copyAsMarkdown", true);
  const QString original = QStringLiteral("3. 4. 中文正文\n4. 后续正文");
  for (bool backward : {false, true}) {
    Harness h(original);
    auto* inner = h.inner();
    auto* next = listItemAt(h.session, 0, 1);
    setCrossSelection(h.controller.selection(), backward ? next : inner, 2, backward ? inner : next, 2);
    require(h.controller.clipboardController().cut(), "cross-level selection must support cut in both directions");
    const auto* mime = QApplication::clipboard()->mimeData();
    require(mime && mime->data("text/markdown") == QStringLiteral("4. 正文\n4. 后续").toUtf8(),
            "copy should use the inner marker, without adding an unselected ancestor");
    h.expect(QStringLiteral("3. 4. 中文正文"));
    h.controller.undo();
    h.expect(original);
    require(!h.controller.selection().selection().isCollapsed(), "undo cut must restore the cross-level selection");
    require(selectedMarkdown(h.session, h.controller.selection()) == QStringLiteral("4. 正文\n4. 后续"),
            "undo must restore both selection endpoints, including their list levels");
    h.controller.redo();
    h.expect(QStringLiteral("3. 4. 中文正文"));

    Harness target(QStringLiteral("目的"));
    setCursor(target.controller.selection(), blockAt(target.session, 0), 0);
    require(target.controller.clipboardController().paste(), "cut Markdown must paste through the actual clipboard");
    target.expect(QStringLiteral("4. 正文\n4. 后续目的"));
    target.controller.undo();
    target.expect(QStringLiteral("目的"));
  }
}

void testEmptyParentSelectionAndTaskClipboard() {
  Harness h(QStringLiteral("3. 4. body\n4. last"));
  setCrossSelection(h.controller.selection(), listItemAt(h.session, 0, 0), 0, h.inner(), 2);
  require(h.controller.clipboardController().cut(), "selection from an empty parent into its child must cut");
  h.expect(QStringLiteral("3. dy\n4. last"));
  h.controller.undo();
  h.expect(QStringLiteral("3. 4. body\n4. last"));

  Harness task(QStringLiteral("3. - [x] 中文正文\n4. last"));
  setSelection(task.controller.selection(), task.inner(), 0, 2);
  require(task.controller.clipboardController().copy(), "nested task text should copy");
  require(QApplication::clipboard()->mimeData()->data("text/markdown") == QStringLiteral("- [x] 中文").toUtf8(),
          "nested task clipboard must share the checkbox and text boundaries");
}

void testFormattedNestedClipboard() {
  Harness h(QStringLiteral("前言\n\n3. 4. **中文**后续\n4. last"));
  auto* item = childAt(firstChildOfType(listItemAt(h.session, 1, 0), BlockType::List), 0);
  auto* paragraph = primaryParagraph(*item);
  const auto source = listItemSource(h.session.markdownText(), *item);
  setSourceSelection(h.controller.selection(), item, paragraph, 0, source.content.start, 2, source.content.start + 6);
  require(selectedMarkdown(h.session, h.controller.selection()) == QStringLiteral("4. **中文**"),
          "formatted selections with a paragraph text-node ID must use their list owner's marker and inline base");
  require(h.controller.clipboardController().cut(), "formatted nested selection must cut");
  h.expect(QStringLiteral("前言\n\n3. 4. 后续\n4. last"));
  h.controller.undo();
  h.expect(QStringLiteral("前言\n\n3. 4. **中文**后续\n4. last"));
}

void testContinuousEnterTypeDeleteAndUndo() {
  const QString original = QStringLiteral("3. 4. 正文\n   5. 后续\n4. last");
  Harness h(original);
  setCursor(h.controller.selection(), h.inner(), 1);
  require(h.controller.inputController().insertParagraphBreak(), "Enter should split Chinese text");
  h.expect(QStringLiteral("3. 4. 正\n   5. 文\n   6. 后续\n4. last"));
  QInputMethodEvent commit;
  commit.setCommitString(QStringLiteral("新增"));
  require(h.controller.inputController().handleInputMethod(&commit),
          "Chinese IME commit should follow the split caret");
  h.expect(QStringLiteral("3. 4. 正\n   5. 新增文\n   6. 后续\n4. last"));
  setCursor(h.controller.selection(), h.inner(1), 0);
  require(h.controller.inputController().deleteBackward(), "Backspace should merge the split item");
  h.expect(QStringLiteral("3. 4. 正 新增文\n   6. 后续\n4. last"));
  h.controller.undo();
  h.expect(QStringLiteral("3. 4. 正\n   5. 新增文\n   6. 后续\n4. last"));
  h.controller.undo();
  h.expect(QStringLiteral("3. 4. 正\n   5. 文\n   6. 后续\n4. last"));
  h.controller.undo();
  h.expect(original);
}

MarkdownNode* firstList(MarkdownNode& node) {
  if (node.type() == BlockType::List)
    return &node;
  for (const auto& child : node.children())
    if (auto* list = firstList(*child))
      return list;
  return nullptr;
}

QString inlineShape(const InlineNode& node) {
  QString result = QStringLiteral("%1:%2:%3:").arg(int(node.type())).arg(node.sourceStart()).arg(node.sourceEnd());
  result += node.text() + QLatin1Char('|') + node.href() + QLatin1Char('[');
  for (const auto& child : node.children())
    result += inlineShape(child);
  return result + QLatin1Char(']');
}

QString treeShape(const MarkdownNode& node) {
  const auto range = node.sourceRange();
  QString result = QStringLiteral("%1:%2:%3:%4:%5:%6:%7:")
                       .arg(int(node.type()))
                       .arg(range.byteStart)
                       .arg(range.byteEnd)
                       .arg(range.lineStart)
                       .arg(range.lineEnd)
                       .arg(range.columnStart)
                       .arg(range.columnEnd);
  result += QStringLiteral("%1:%2:%3:%4:%5:")
                .arg(int(node.listKind()))
                .arg(node.listStart())
                .arg(node.listTight())
                .arg(node.isTaskItem())
                .arg(node.taskChecked());
  result += node.literal() + QLatin1Char('|') + node.codeLanguage() + QLatin1Char('[');
  for (const auto& text : node.inlines())
    result += inlineShape(text);
  for (const auto& child : node.children())
    result += treeShape(*child);
  return result + QLatin1Char(']');
}

void requireFreshTree(Harness& h) {
  DocumentSession fresh;
  fresh.setMarkdownText(h.session.markdownText().toString(), false);
  require(treeShape(h.session.document().root()) == treeShape(fresh.document().root()),
          "incremental tree, source ranges, inlines and list metadata must agree with a fresh parse");
}

QStringList literalBlocks(const MarkdownNode& node) {
  QStringList result;
  if (node.type() == BlockType::CodeFence || node.type() == BlockType::MathBlock || node.type() == BlockType::HtmlBlock)
    result += QString::number(int(node.type())) + QLatin1Char(':') + QString::number(node.isIndentedCode()) +
              QLatin1Char(':') + node.codeLanguage() + QLatin1Char(':') + node.literal();
  for (const auto& child : node.children())
    result += literalBlocks(*child);
  return result;
}

void testComplexSiblingMerges() {
  struct Case {
    const char* name;
    QString original;
    QString expected;
    QString forwardExpected;
  };
  const Case cases[] = {
      {"loose paragraphs, two fences, nested tasks and quote",
       "1. alpha\n   wrap\n\n   second paragraph\n\n   ```text\n   2. literal\n   ```\n\n"
       "   - child A\n     - grand A\n2. beta\n   continuation\n\n   # heading B\n\n"
       "   > quote B\n\n   ~~~cpp\n   int value = 4;\n   ~~~\n\n   - [x] child B\n3. last",
       "1. alpha\n   wrap beta\n   continuation\n\n   second paragraph\n\n   ```text\n   2. literal\n   ```\n\n"
       "   - child A\n     - grand A\n\n   # heading B\n\n   > quote B\n\n"
       "   ~~~cpp\n   int value = 4;\n   ~~~\n\n   - [x] child B\n3. last"},
      {"different marker widths",
       "9. left\n\n   ```cpp\n   first\n   ```\n10. right\n    next\n\n    # right heading\n\n"
       "    - [x] task\n        extra\n11. last",
       "9. left right\n   next\n\n   ```cpp\n   first\n   ```\n\n   # right heading\n\n"
       "   - [x] task\n       extra\n11. last"},
      {"formatted bodies after a document prefix",
       "前言\n\n9. **left**\n\n   - child\n10. [right](relative.md)\n    tail\n\n    ## heading\n11. last",
       "前言\n\n9. **left** [right](relative.md)\n   tail\n\n   - child\n\n   ## heading\n11. last"},
      {"empty left parent with same-line children",
       "3. 4. nested\n      continuation\n\n      ```text\n      9. literal\n      ```\n4. right\n\n   > quote\n5. last",
       "3. right\n\n   4. nested\n      continuation\n\n      ```text\n      9. literal\n      ```\n\n   > quote\n5. last",
       "3. nested\n   continuation\n\n   ```text\n   9. literal\n   ```\n4. right\n\n   > quote\n5. last"},
      {"right parent without primary paragraph", "1. alpha\n\n   prior\n2. 4. nested\n      tail\n3. last",
       "1. alpha\n\n   prior\n\n   4. nested\n      tail\n3. last"},
      {"two empty parents with same-line children", "1. - a\n2. - b\n3. last", "1. - a\n\n   - b\n3. last",
       "1. a\n2. - b\n3. last"},
      {"left heading followed by its own paragraph", "1. # title\n\n   prior\n2. beta\n3. last",
       "1. beta\n\n   # title\n\n   prior\n3. last"},
      {"right heading and table", "1. alpha\n2. # title\n\n   | a | b |\n   | - | - |\n   | c | d |\n3. last",
       "1. alpha\n\n   # title\n\n   | a | b |\n   | - | - |\n   | c | d |\n3. last"},
      {"fence beside empty marker", "1. ```text\n   first\n   ```\n2. beta\n3. last",
       "1. beta\n\n   ```text\n   first\n   ```\n3. last"},
      {"indented code block", "9. alpha\n10.     code\n        second\n11. last",
       "9. alpha\n\n       code\n       second\n11. last"},
      {"checkbox-looking code is not a task", "9. alpha\n10.     [x] code\n        second\n11. last",
       "9. alpha\n\n       [x] code\n       second\n11. last"},
      {"task checkbox is not continuation indentation",
       "9. [x] alpha\n\n   - left\n10. [ ] beta\n    wrap\n\n    ```text\n    body\n    ```\n11. last",
       "9. [x] alpha beta\n   wrap\n\n   - left\n\n   ```text\n   body\n   ```\n11. last"},
      {"empty marker padding with children", "1.   \n   - child\n2. beta\n3. last", "1. beta\n\n   - child\n3. last"},
      {"quote container and width change",
       "> 9. alpha\n>    - child\n> 10. beta\n>     tail\n>\n>     ```text\n>     2. literal\n>     ```\n> 11. last",
       "> 9. alpha beta\n>    tail\n>    - child\n>\n>    ```text\n>    2. literal\n>    ```\n> 11. last"},
      {"tabbed continuation and fence", "9. alpha\n10. beta\n\twrap\n\n\t```text\n\t2. literal\n\t```\n11. last",
       "9. alpha beta\n   wrap\n\n   ```text\n   2. literal\n   ```\n11. last"},
      {"tabbed indented code block", "9. alpha\n10.\t\tcode\n\t\tsecond\n11. last",
       "9. alpha\n\n       code\n       second\n11. last"},
      {"tabs in indented code payload", "9. alpha\n10.\t\t\tcode\n\t\t\tsecond\n11. last",
       "9. alpha\n\n       \tcode\n       \tsecond\n11. last"},
      {"tabs in fenced code payload", "9. alpha\n10. beta\n\n\t```text\n\t\tcode\n\t```\n11. last",
       "9. alpha beta\n\n   ```text\n   \tcode\n   ```\n11. last"},
      {"indented code in a nested child", "9. alpha\n10. beta\n    - child\n\n          code\n11. last",
       "9. alpha beta\n   - child\n\n         code\n11. last"},
      {"bare unordered marker", "-\n- beta\n- last", "- beta\n- last"},
      {"empty sibling after children", "1. alpha\n\n   # title\n\n   paragraph\n2. \n3. last",
       "1. alpha\n\n   # title\n\n   paragraph\n3. last"},
  };
  for (const auto& c : cases) {
    for (bool backward : {false, true}) {
      std::fprintf(stderr, "merge %s %s\n", c.name, backward ? "Backspace" : "Delete");
      Harness h(c.original);
      const auto literals = literalBlocks(h.session.document().root());
      auto* list = firstList(h.session.document().root());
      require(list && list->children().size() >= 2, "fixture must contain sibling list items");
      auto* left = childAt(list, 0);
      auto* right = childAt(list, 1);
      const auto source = listItemSource(h.session.markdownText(), *(backward ? right : left));
      const qsizetype offset = backward ? source.content.start : source.content.end;
      // Empty parent rows may share a source offset with a heading or sublist.
      // Keep the display owner, as a click on that row does.
      setSourceCursor(h.controller.selection(), backward ? right : left, 0, offset);
      require(backward ? h.controller.inputController().deleteBackward()
                       : h.controller.inputController().deleteForward(),
              "merge must be handled");
      // Delete on an empty parent immediately above its same-line sublist
      // removes one nesting level before considering the next sibling.
      const QString expected = !backward && !c.forwardExpected.isNull() ? c.forwardExpected : c.expected;
      h.expect(expected);
      requireFreshTree(h);
      require(literalBlocks(h.session.document().root()) == literals,
              "merging must preserve literal block kinds, languages and payload, including tabs");
      auto* merged = childAt(firstList(h.session.document().root()), 0);
      require(merged->type() == BlockType::ListItem, "merged item must remain a list item");
      h.controller.undo();
      h.expect(c.original);
      requireFreshTree(h);
      require(h.controller.selection().cursorPosition().text.sourceOffset == offset,
              "undo must restore the original source caret");
      h.controller.redo();
      h.expect(expected);
      requireFreshTree(h);
    }
  }
}

void testComplexContinuousEditing() {
  SettingsOverride copyAsMarkdown("editor/copyAsMarkdown", true);
  const QString children =
      QStringLiteral("\n\n   第二段\n\n   ```text\n   2. literal\n   ```\n\n   - [x] 子项\n     - 深层");
  const QString ending = QStringLiteral("\n\n   > 引用\n3. 末项");
  const QString original = QStringLiteral("1. 甲乙") + children + QStringLiteral("\n2. 丙丁") + ending;
  const QString merged = QStringLiteral("1. 甲乙 丙丁") + children + ending;
  const QString split = QStringLiteral("1. 甲\n2. 乙 丙丁") + children + ending;
  const QString typed = QStringLiteral("1. 甲\n2. 新增乙 丙丁") + children + ending;
  const QString cut = QStringLiteral("1. 甲\n2. 新增项\n     - 深层") + ending;

  for (bool backward : {false, true}) {
    Harness h(original);
    setCursor(h.controller.selection(), listItemAt(h.session, 0, 0), 2);
    require(h.controller.inputController().deleteForward(), "merge before continuous complex editing");
    h.expect(merged);
    requireFreshTree(h);
    setCursor(h.controller.selection(), listItemAt(h.session, 0, 0), 1);
    require(h.controller.inputController().insertParagraphBreak(), "split merged item with all child blocks");
    h.expect(split);
    requireFreshTree(h);
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("新增"));
    require(h.controller.inputController().handleInputMethod(&commit), "IME commit after complex split");
    h.expect(typed);
    requireFreshTree(h);

    const QString indented = QStringLiteral("1. 甲\n   1. 新增乙 丙丁\n\n      第二段\n\n      ```text\n"
                                            "      2. literal\n      ```\n\n      - [x] 子项\n        - 深层\n\n"
                                            "      > 引用\n2. 末项");
    require(h.controller.inputController().indentListItem(), "indent a mixed-block item after IME");
    h.expect(indented);
    requireFreshTree(h);
    require(h.controller.inputController().outdentListItem(), "outdent all child blocks as a unit");
    h.expect(typed);
    requireFreshTree(h);

    auto* item = listItemAt(h.session, 0, 1);
    auto* task = childAt(firstChildOfType(item, BlockType::List), 0);
    setCrossSelection(h.controller.selection(), backward ? task : item, backward ? 1 : 2, backward ? item : task,
                      backward ? 2 : 1);
    const QString selected = selectedMarkdown(h.session, h.controller.selection());
    require(selected.contains(QStringLiteral("```text\n")) && selected.contains(QStringLiteral("2. literal")) &&
                selected.contains(QStringLiteral("- [x] 子")),
            "cross-level selection must carry the fence and task marker");
    require(h.controller.clipboardController().cut(), "cut across paragraphs, code and nested task");
    h.expect(cut);
    requireFreshTree(h);
    require(QApplication::clipboard()->mimeData()->data("text/markdown") == selected.toUtf8(),
            "clipboard must retain the selected block structure");

    h.controller.undo();
    h.expect(typed);
    requireFreshTree(h);
    require(selectedMarkdown(h.session, h.controller.selection()) == selected,
            "undo must recover the entire cross-level selection");
    for (const auto& expected : {indented, typed, split, merged, original}) {
      h.controller.undo();
      h.expect(expected);
      requireFreshTree(h);
    }
    for (const auto& expected : {merged, split, typed, indented, typed, cut}) {
      h.controller.redo();
      h.expect(expected);
      requireFreshTree(h);
    }

    Harness target(QStringLiteral("目的"));
    setCursor(target.controller.selection(), blockAt(target.session, 0), 0);
    require(target.controller.clipboardController().paste(), "paste complex selection through real clipboard");
    target.expect(selected + QStringLiteral("目的"));
    requireFreshTree(target);
    target.controller.undo();
    target.expect(QStringLiteral("目的"));
  }
}
} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  SettingsOverride ordered("markdown/orderedList", 0);
  SettingsOverride align("editor/alignIndent", false);
  runTest("shared source ranges", testSharedSourceRanges);
  runTest("source ownership and revision", testSourceOwnershipAndRevision);
  runTest("Enter at same-line nested start/middle/end", testEnterSameLineNestedItem);
  runTest("Delete nested siblings", testDeleteAcrossNestedSiblings);
  runTest("Delete empty parent", testDeleteAtEmptyParent);
  runTest("renumber literal protection", testNumberingDoesNotTouchLiteralBlocks);
  runTest("cross-level cut undo/redo", testCrossLevelCutUndoRedo);
  runTest("empty parent and task clipboard", testEmptyParentSelectionAndTaskClipboard);
  runTest("formatted nested clipboard", testFormattedNestedClipboard);
  runTest("continuous editing", testContinuousEnterTypeDeleteAndUndo);
  runTest("complex sibling merges", testComplexSiblingMerges);
  runTest("complex continuous editing", testComplexContinuousEditing);
  QApplication::clipboard()->clear();
  return 0;
}
