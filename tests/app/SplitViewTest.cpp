#include "app/MainWindow.h"
#include "app/PrefsShortcutsPage.h"
#include "editor/EditorView.h"
#include "editor/DocumentSearch.h"
#include "editor/FindBarWidget.h"
#include "editor/ShortcutSettings.h"
#include "editor/SourceEditorWidget.h"
#include "editor/VirtualSourceEdit.h"
#include "render/ImageLoader.h"
#include "../TestUtils.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QInputMethodEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTimer>
#include <QWheelEvent>
#include <QtTest/QTest>

using namespace muffin;

namespace {
void waitFor(const std::function<bool()>& ready, const char* message) {
  QElapsedTimer timer;
  timer.start();
  // Dispatch queued document/brush/resource notifications before accepting an
  // idle state: a synchronous local edit can enqueue a later full refresh.
  do {
    QTest::qWait(5);
  } while (!ready() && timer.elapsed() < 15000);
  require(ready(), message);
}

struct Fixture {
  QTemporaryDir directory;
  SettingsOverride sourceMode{"view/sourceMode", false};
  SettingsOverride splitMode{"view/splitMode", false};
  SettingsOverride plainCopy{"editor/copyAsMarkdown", false};
  MainWindow window;
  QTabWidget* tabs = window.findChild<QTabWidget*>(QStringLiteral("DocumentTabs"));
  Fixture() {
    window.resize(1100, 760);
    window.show();
    QApplication::processEvents();
  }
  ~Fixture() {
    DraftRecovery recovery;
    for (int i = 0; i < tabs->count(); ++i) {
      auto* tab = static_cast<DocumentTab*>(tabs->widget(i));
      tab->session.document().setModified(false);
      recovery.markClean(tab->session.filePath(), tab->draftKey);
    }
  }
  DocumentTab* current() { return static_cast<DocumentTab*>(tabs->currentWidget()); }
  QAction* command(const QString& text) {
    for (auto* candidate : window.findChildren<QAction*>())
      if (candidate->text() == text)
        return candidate;
    require(false, QStringLiteral("Missing action: %1").arg(text));
    return nullptr;
  }
  DocumentTab* open(const QString& text) {
    const auto path = directory.filePath(QStringLiteral("split.md"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "create split fixture");
    file.write(text.toUtf8());
    file.close();
    require(window.openFile(path), "open split fixture");
    auto* tab = current();
    waitFor([&] { return !tab->session.isAsyncParseInProgress(); }, "load split fixture");
    QApplication::processEvents();
    return tab;
  }
};

void editingWorkflow() {
  SettingsOverride theme{"appearance/themeName", qEnvironmentVariable("MUFFIN_SPLIT_VIEW_CAPTURE_THEME", "github")};
  Fixture f;
  auto* tab = f.open(QStringLiteral("# Overview\n\nOriginal text\n\n- [ ] Task\n"));
  auto* split = f.command("Source and Preview");
  auto* source = f.command("Source Code Mode");
  auto* sourceButton = f.window.findChild<QToolButton*>(QStringLiteral("statusSourceMode"));
  auto* splitButton = f.window.findChild<QToolButton*>(QStringLiteral("statusSplitView"));
  require(sourceButton && splitButton && splitButton->x() > sourceButton->geometry().right(),
          "split toggle sits to the right of source toggle");
  require(!splitButton->icon().isNull(), "split has an embedded icon");
  QTest::mouseClick(splitButton, Qt::LeftButton);
  require(tab->viewMode() == DocumentViewMode::Split && split->isChecked() && !source->isChecked(),
          "split and source-only controls are mutually exclusive");
  require(tab->editor->isVisible() && tab->renderView->isVisible() && tab->renderView->editingLocked() &&
              !tab->editor->isReadOnly(),
          "only preview is read-only");
  auto* sourceHeader = tab->findChild<QWidget*>("SplitSourceHeader");
  auto* previewHeader = tab->findChild<QWidget*>("SplitPreviewHeader");
  auto* previewLabel = tab->findChild<QLabel*>("SplitPreviewLabel");
  auto* synchronize = tab->findChild<QToolButton*>("SplitSynchronizeScrolling");
  const auto verifyHeaders = [&] {
    require(sourceHeader->mapTo(tab, QPoint()).y() == previewHeader->mapTo(tab, QPoint()).y() &&
                sourceHeader->width() == tab->editor->width() && previewHeader->width() == tab->renderView->width(),
            "pane titles stay aligned with their own viewport");
    require(previewLabel->geometry().left() < 48 && synchronize->width() <= 28,
            "preview title is left aligned and scrolling control stays compact");
  };
  verifyHeaders();
  tab->viewSplitter->setSizes({300, 600});
  QApplication::processEvents();
  verifyHeaders();
  tab->viewSplitter->setSizes({1, 1});
  require(synchronize->toolButtonStyle() == Qt::ToolButtonIconOnly && !synchronize->icon().isNull() &&
              !synchronize->toolTip().isEmpty() && synchronize->accessibleName() == synchronize->toolTip(),
          "scroll linking has an icon, a tooltip and an accessible name");
  const auto linkedIcon = synchronize->icon().pixmap(16, 16, QIcon::Normal, QIcon::On).toImage();
  const auto unlinkedIcon = synchronize->icon().pixmap(16, 16, QIcon::Normal, QIcon::Off).toImage();
  require(linkedIcon != unlinkedIcon, "linked and independent scrolling have distinct icon states");
  require(tab->viewSplitter->handle(1)->width() >= 5, "split divider retains a usable drag target under chrome styles");
  const auto dividerImage = tab->viewSplitter->handle(1)->grab().toImage();
  require(dividerImage.pixelColor(dividerImage.width() / 2, dividerImage.height() / 2).rgb() !=
              tab->renderView->theme().backgroundColor().rgb(),
          "split divider is visible against the page background");
  const auto capture = qEnvironmentVariable("MUFFIN_SPLIT_VIEW_CAPTURE");
  if (!capture.isEmpty()) {
    QTest::qWait(150); // Capture the live panes after the mode-switch crossfade.
    require(f.window.grab().save(capture), "capture split view for visual review");
  }
  tab->editor->moveDocumentEnd();
  QInputMethodEvent commit;
  commit.setCommitString(QStringLiteral("\n中文输入与分屏预览\n"));
  QApplication::sendEvent(tab->editor->findChild<VirtualSourceEdit*>(), &commit);
  require(tab->session.isEditingAllowed(), "preview layout does not suspend source typing");
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview follows IME commit");
  require(tab->session.markdownText().toString().contains(QStringLiteral("中文输入与分屏预览")), "source IME commit");
  const auto* last = tab->session.document().root().children().back().get();
  require(!tab->renderView->nodeRect(last->id()).isEmpty(), "preview lays out the new block");
  require(tab->session.isEditingAllowed(), "preview leaves source editing responsive");
  tab->backend->undo();
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview follows undo");
  require(!tab->session.markdownText().toString().contains(QStringLiteral("中文输入与分屏预览")),
          "shared undo history");
  tab->backend->redo();
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview follows redo");
  for (int i = 0; i < 12; ++i) {
    tab->editor->insertText(QStringLiteral("\n\nBurst %1").arg(i));
    require(tab->session.isEditingAllowed(), "typing remains available while preview work is superseded");
  }
  tab->editor->insertText(QStringLiteral("\n\n# Burst complete"));
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview catches up with continuous edits");
  require(!tab->renderView->nodeRect(tab->session.document().root().children().back()->id()).isEmpty(),
          "preview uses the latest document after superseded layouts");
  split->trigger();
  require(tab->viewMode() == DocumentViewMode::Rendered && !tab->renderView->editingLocked(),
          "return to rendered mode");
  source->trigger();
  const auto position = tab->editor->cursorPosition();
  split->trigger();
  split->trigger();
  require(tab->viewMode() == DocumentViewMode::Source && tab->editor->cursorPosition() == position,
          "split return preserves source mode and caret");
  split->trigger();
  source->trigger();
  require(tab->viewMode() == DocumentViewMode::Source && !split->isChecked(), "source command exits split");
}

void previewCannotEditSource() {
  Fixture f;
  const auto text = QStringLiteral("# Heading\n\nSource stays editable\n\n- [ ] Task\n");
  auto* tab = f.open(text);
  f.command("Source and Preview")->trigger();
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "read-only preview layout completes");
  tab->renderView->setFocus();
  waitFor([&] { return tab->previewActive(); }, "preview receives command focus");
  tab->editorController.selectSourceRange(2, 9);
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview selection layout completes");
  tab->backend->copy();
  require(QApplication::clipboard()->text() == QStringLiteral("Heading"), "copy uses preview selection");
  const auto* item = tab->session.document().root().children().back()->children().front().get();
  const auto* itemLayout = tab->renderView->blockLayoutForNode(item->id());
  require(itemLayout && itemLayout->isTaskListItem(), "preview contains a task checkbox");
  const auto checkbox =
      tab->renderView->mapDocumentToViewport(itemLayout->taskCheckboxRect(tab->renderView->theme()).center());
  require(tab->renderView->hitTest(checkbox).taskCheckboxHit, "task hit matches the rendered checkbox");
  QTest::mouseClick(tab->renderView->viewport(), Qt::LeftButton, Qt::NoModifier, checkbox.toPoint());
  require(tab->session.markdownText().toString() == text, "preview task checkbox cannot change the document");
  QTest::keyClicks(tab->renderView, "forbidden");
  QTest::keyClick(tab->renderView, Qt::Key_Backspace);
  QInputMethodEvent commit;
  commit.setCommitString(QStringLiteral("不可写"));
  QApplication::sendEvent(tab->renderView, &commit);
  f.command("Paste")->trigger();
  require(tab->session.markdownText().toString() == text, "preview refuses keyboard, IME and paste");
  require(!f.command("Paste")->isEnabled() && tab->session.isEditingAllowed(),
          "preview lock is separate from document");
  tab->focusEditor();
  waitFor([&] { return !tab->previewActive(); }, "source regains command focus");
  require(f.command("Paste")->isEnabled(), "source commands enabled again");
  tab->editor->moveDocumentEnd();
  tab->editor->insertText(QStringLiteral("\nSource can still edit\n"));
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview follows resumed typing");
  require(tab->session.markdownText().toString().contains("Source can still edit"), "source remains editable");
}

void themedPaneChrome() {
  SettingsOverride savedTheme{"appearance/themeName", "github"};
  Fixture f;
  auto* tab = f.open(QStringLiteral("# Theme\n\nSource and preview\n"));
  f.command("Source and Preview")->trigger();
  for (const auto* name : {"github", "night", "newsprint"}) {
    bool changed = false;
    for (auto* action : f.window.findChildren<QAction*>()) {
      if (action->isCheckable() && action->data().toString() == QLatin1String(name)) {
        action->trigger();
        changed = true;
        break;
      }
    }
    require(changed, "switch the real application theme");
    QApplication::processEvents();
    const auto& theme = tab->renderView->theme();
    const auto sourceMargin = tab->editor->grab().toImage();
    require(sourceMargin.pixelColor(1, 1).rgb() == SourceEditorColors::fromTheme(theme).background.rgb(),
            QStringLiteral("source top margin follows theme %1: painted %2, expected %3, palette %4, fill %5")
                .arg(QLatin1String(name), sourceMargin.pixelColor(1, 1).name(),
                     SourceEditorColors::fromTheme(theme).background.name(),
                     tab->editor->palette().color(QPalette::Window).name())
                .arg(tab->editor->autoFillBackground()));
    const auto divider = tab->viewSplitter->handle(1)->grab().toImage();
    require(divider.pixelColor(0, divider.height() / 2).rgb() == theme.backgroundColor().rgb(),
            "split divider borders use the page background without a white underlay");
    for (const auto* labelName : {"SplitSourceLabel", "SplitPreviewLabel"}) {
      const auto* label = tab->findChild<QLabel*>(QLatin1String(labelName));
      require(label->palette().color(QPalette::WindowText).rgb() == theme.mutedTextColor().rgb(),
              "both pane labels follow the theme text color");
    }
  }
}

void scrollAndTabState() {
  Fixture f;
  QString text;
  for (int i = 0; i < 120; ++i)
    text +=
        QStringLiteral(
            "## Section %1\n\nParagraph %1 contains a long sentence to produce different wrapping in the two panes.\n\n")
            .arg(i);
  auto* tab = f.open(text);
  f.command("Source and Preview")->trigger();
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "split scroll fixture layout");
  auto* input = tab->editor->findChild<VirtualSourceEdit*>();
  input->verticalScrollBar()->setValue(input->verticalScrollBar()->maximum() / 2);
  QApplication::processEvents();
  const auto sourcePosition = tab->editor->viewportPosition();
  const auto previewPosition = tab->renderView->viewportPosition();
  require(sourcePosition.sourceOffset > 0 && qAbs(previewPosition.sourceOffset - sourcePosition.sourceOffset) < 400,
          "scroll synchronization uses source anchors");
  const auto caret = tab->editor->cursorPosition();
  tab->renderView->setFocus();
  waitFor([&] { return tab->previewActive(); }, "preview becomes scroll driver");
  tab->renderView->verticalScrollBar()->setValue(tab->renderView->verticalScrollBar()->maximum() / 3);
  QApplication::processEvents();
  require(qAbs(tab->editor->viewportPosition().sourceOffset - tab->renderView->viewportPosition().sourceOffset) < 400 &&
              tab->editor->cursorPosition() == caret,
          "preview scrolling follows source anchors without moving the source caret");
  tab->setSynchronizedScrolling(false);
  const auto previewScroll = tab->renderView->verticalScrollBar()->value();
  input->verticalScrollBar()->setValue(0);
  require(tab->renderView->verticalScrollBar()->value() == previewScroll, "independent scrolling when unlinked");
  tab->viewSplitter->setSizes({300, 600});
  f.command("New")->trigger();
  require(f.current() != tab && f.current()->viewMode() == DocumentViewMode::Split, "new tabs inherit split");
  f.command("Source Code Mode")->trigger();
  f.tabs->setCurrentWidget(tab);
  require(tab->viewMode() == DocumentViewMode::Split && f.command("Source and Preview")->isChecked() &&
              !tab->synchronizedScrolling(),
          "tabs retain independent mode and synchronization policy");
  const auto views = f.window.sessionState().value("views").toList();
  const auto saved = views[0].toMap();
  require(saved.value("splitView").toBool() && !saved.value("synchronizedScrolling").toBool() &&
              saved.value("splitSizes").toList().size() == 2,
          "session retains split geometry and scroll policy");
  f.command("Source and Preview")->trigger();
  tab->restoreViewMode(saved);
  require(tab->viewMode() == DocumentViewMode::Split && !tab->synchronizedScrolling(), "restore split view state");

  Fixture restored;
  restored.window.restoreSessionState(f.window.sessionState());
  auto* restoredTab = restored.current();
  waitFor([&] { return !restoredTab->session.isOpenInProgress() && !restoredTab->pendingViewState; },
          "restore split document and viewports");
  require(restoredTab->viewMode() == DocumentViewMode::Split && !restoredTab->synchronizedScrolling() &&
              restoredTab->renderView->editingLocked() && !restoredTab->editor->isReadOnly(),
          "session restores split layout and distinct input permissions");
  const auto sizes = tab->splitSizes();
  const auto restoredSizes = restoredTab->splitSizes();
  require(qAbs(double(sizes[0]) / (sizes[0] + sizes[1]) -
               double(restoredSizes[0]) / (restoredSizes[0] + restoredSizes[1])) < 0.03,
          "session restores divider ratio");
}

void findReplaceAndCustomShortcut() {
  Fixture f;
  auto* tab = f.open(QStringLiteral("# Heading\n\nFind needle and another needle\n"));
  auto& shortcuts = ShortcutSettings::instance();
  const auto cleanup = qScopeGuard([&] { shortcuts.clearOverride(QStringLiteral("view.split")); });
  PrefsShortcutsPage preferences;
  preferences.findChild<QLineEdit*>("ShortcutFilter")->setText(QStringLiteral("view.split"));
  preferences.findChild<QKeySequenceEdit*>("ShortcutSequence")->setKeySequence(QKeySequence("Ctrl+Alt+F8"));
  auto* apply = preferences.findChild<QPushButton*>("ShortcutApply");
  require(apply->isEnabled(), "split command is configurable in shortcut preferences");
  apply->click();
  tab->renderView->setFocus();
  QTest::keyClick(tab->renderView, Qt::Key_F8, Qt::ControlModifier | Qt::AltModifier);
  require(tab->viewMode() == DocumentViewMode::Split, "custom shortcut enters split");
  tab->renderView->setFocus();
  f.command("Replace...")->trigger();
  require(!tab->previewActive(), "split find and replacement target the source pane");
  tab->findBar->setSearchText("needle");
  tab->findBar->findRequested("needle", true, false, false);
  waitFor([&] { return tab->editor->selectedText() == QStringLiteral("needle"); }, "find selects source match");
  tab->renderView->setFocus();
  tab->findBar->replaceRequested("needle", "thread", false, false);
  waitFor([&] { return tab->session.markdownText().toString().contains("thread"); }, "replace through source pane");
  require(tab->viewMode() == DocumentViewMode::Split && tab->renderView->editingLocked(),
          "replacement retains split preview permissions");
  tab->findBar->replaceAllRequested("needle", "thread", false, false);
  waitFor([&] { return !tab->session.markdownText().toString().contains("needle") && !tab->session.isBatchPending(); },
          "replace all updates shared document");
  tab->focusEditor();
  tab->backend->undo();
  waitFor([&] { return tab->session.markdownText().toString().contains("needle") && !tab->session.isBatchPending(); },
          "batch undo survives split replacement");
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview catches up after replacement undo");
}

void unsavedSourceClose() {
  SettingsOverride autoSave{"files/autoSave", false}, saveOnSwitch{"files/autoSaveOnSwitch", false};
  for (const bool named : {false, true}) {
    Fixture f;
    auto* tab = named ? f.open(QStringLiteral("# Saved\n\nOriginal\n")) : f.current();
    f.command("Source and Preview")->trigger();
    tab->editor->moveDocumentEnd();
    tab->editor->insertText(QStringLiteral("\nUnsaved source edit\n"));
    require(tab->session.document().isModified() && !tab->session.undoStack().isClean(),
            "split source edits immediately mark the shared document dirty");
    int prompts = 0;
    QTimer answer;
    answer.setInterval(5);
    QObject::connect(&answer, &QTimer::timeout, &answer, [&] {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        require(box->button(QMessageBox::Cancel), "unsaved close offers cancellation");
        ++prompts;
        box->button(QMessageBox::Cancel)->click();
      }
    });
    answer.start();
    require(!f.window.close() && prompts == 1 && tab->session.document().isModified(),
            "closing a split document immediately after typing prompts and Cancel keeps edits");
    waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview settles without clearing dirty state");
    require(tab->session.document().isModified() && !f.window.close() && prompts == 2,
            "preview completion never clears unsaved changes or skips the close prompt");
    f.command("New")->trigger();
    require(f.current() != tab && tab->session.document().isModified(), "background split tab keeps its dirty state");
    f.command("Quit")->trigger();
    require(f.window.isVisible() && prompts == 3 && tab->session.document().isModified(),
            "File Quit also prompts for a modified background split tab and respects cancellation");
    f.tabs->setCurrentWidget(tab);
    if (named) {
      QFile disk(tab->session.filePath());
      require(disk.open(QIODevice::ReadOnly) && !disk.readAll().contains("Unsaved source edit"),
              "cancelling close does not silently save the source document");
      disk.close();
      f.command("Save")->trigger();
      require(!tab->session.document().isModified() && tab->session.undoStack().isClean(),
              "explicit save clears the dirty state of the shared source document");
      require(disk.open(QIODevice::ReadOnly) && disk.readAll().contains("Unsaved source edit"),
              "explicit save writes the source edits to disk");
      disk.close();
      require(f.window.close() && prompts == 3, "a saved split document closes without another prompt");
    }
    answer.stop();
  }
}

void closeDuringPreviewUpdates() {
  SettingsOverride autoSave{"files/autoSave", false}, saveOnSwitch{"files/autoSaveOnSwitch", false};
  for (const bool container : {false, true}) {
    Fixture f;
    const auto url = QStringLiteral("https://example.invalid/split-close-%1.png").arg(container);
    auto& images = ImageLoader::instance();
    const auto updateImage = [&](int height) {
      QImage image(40, height, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::red);
      images.store(url, image);
    };
    updateImage(30);
    QString text;
    for (int i = 0; i < 30; ++i) {
      if (container && i % 5 == 0)
        text += QStringLiteral("<div align=\"center\">\n\n");
      text += QStringLiteral("Paragraph %1 ![picture](%2)\n\n").arg(i).arg(url);
      if (container && i % 5 == 4)
        text += QStringLiteral("</div>\n\n");
    }
    f.command("Source and Preview")->trigger();
    auto* tab = f.open(text);
    // A cooperative preview uses lazy details on every test platform, matching
    // the native window's resource-refresh path rather than an eager snapshot.
    tab->renderView->setDocument(tab->session.document(), tab->session.filePath(), true);
    waitFor([&] { return !tab->renderView->isLayoutPending(); }, "close fixture preview ready");
    int prompts = 0;
    auto choice = QMessageBox::Cancel;
    QTimer answer;
    answer.setInterval(5);
    QObject::connect(&answer, &QTimer::timeout, &answer, [&] {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        ++prompts;
        box->button(choice)->click();
      }
    });
    answer.start();
    for (int i = 0; i < 4; ++i) {
      tab->editor->setCursorPosition(text.indexOf(QStringLiteral("Paragraph 0")));
      tab->editor->insertText(i % 2 ? QStringLiteral("prefix ") : QStringLiteral("\n\n"));
      updateImage(40 + i * 20);
      f.window.resize(1100 + i * 17, 760);
      require(!f.window.close() && prompts == i + 1 && tab->session.document().isModified(),
              "close during preview and image updates prompts and cancellation preserves unsaved changes");
      QApplication::processEvents();
    }
    choice = QMessageBox::Discard;
    QPointer<DocumentTab> closed = tab;
    tab->editor->insertText(QStringLiteral("\n\nfinal edit\n"));
    updateImage(140);
    f.tabs->tabCloseRequested(f.tabs->indexOf(tab));
    require(!closed && prompts == 5, "discard closes a split tab safely with preview callbacks pending");
    QApplication::processEvents();
    tab = f.open(text);
    tab->editor->insertText(QStringLiteral("\n\nwindow close\n"));
    updateImage(160);
    require(f.window.close() && prompts == 6, "discard also accepts window close during preview updates");
    QApplication::processEvents();
    answer.stop();
  }
}

void continuousPreviewEdits() {
  for (const bool htmlContainer : {false, true}) {
    Fixture f;
    QString text = htmlContainer ? QStringLiteral("<div align=\"center\">\n\n") : QString();
    for (int i = 0; i < 120; ++i)
      text += QStringLiteral("Paragraph %1 has some text.\n\n").arg(i);
    if (htmlContainer)
      text += QStringLiteral("</div>\n");
    auto* tab = f.open(text);
    f.command("Source and Preview")->trigger();
    waitFor([&] { return !tab->renderView->isLayoutPending(); }, "initial preview is ready");
    auto* input = tab->editor->findChild<VirtualSourceEdit*>();
    const auto offset = text.indexOf(QStringLiteral("Paragraph 60")) + QStringLiteral("Paragraph 60").size();
    tab->editor->setCursorPosition(offset);
    tab->editor->centerCursor();
    tab->synchronizeViewports(true);
    QApplication::processEvents();
    const int sourceScroll = input->verticalScrollBar()->value();
    require(sourceScroll > 0, "continuous typing fixture starts midway through the document");
    QSignalSpy builds(tab->renderView, &EditorView::layoutBusyChanged);
    QSignalSpy scrolls(input->verticalScrollBar(), &QScrollBar::valueChanged);
    QString typed;
    for (const auto character : QStringLiteral("updated")) {
      typed += character;
      tab->editor->insertText(QString(character));
      QApplication::processEvents();
      if (htmlContainer) {
        require(!builds.isEmpty(), "HTML edits use the cooperative preview path");
        waitFor([&] { return !tab->renderView->isLayoutPending(); }, "HTML preview catches up with typing");
      } else {
        require(!tab->renderView->isLayoutPending() && builds.isEmpty(),
                "ordinary Markdown typing retains the local preview path");
      }
      const auto* node = tab->session.document().topLevelBlockAtOffset(tab->editor->cursorPosition() - 1);
      const auto* block = tab->renderView->blockLayoutForNode(node->id());
      require(block && block->inlineLayout() &&
                  block->inlineLayout()->plainText().contains(QStringLiteral("Paragraph 60") + typed),
              "the preview contains newly typed text");
      require(input->verticalScrollBar()->value() == sourceScroll,
              QStringLiteral("preview layout does not move the source viewport while typing: %1 -> %2 (HTML %3)")
                  .arg(sourceScroll)
                  .arg(input->verticalScrollBar()->value())
                  .arg(htmlContainer));
    }
    for (const auto& scroll : scrolls)
      require(scroll.first().toInt() == sourceScroll,
              "source scrollbar never jumps to an intermediate preview position");
    tab->setSynchronizedScrolling(false);
    const auto previewPosition = tab->renderView->viewportPosition();
    tab->editor->insertText(QStringLiteral("!"));
    waitFor([&] { return !tab->renderView->isLayoutPending(); }, "independent preview edit completes");
    QApplication::processEvents();
    require(qAbs(tab->renderView->viewportPosition().sourceOffset - previewPosition.sourceOffset) <= 1,
            "independent preview updates preserve its reading position instead of following its old caret");
  }
}

void structuralPreviewEdits() {
  for (const bool linked : {true, false}) {
    Fixture f;
    QString text;
    for (int i = 0; i < 160; ++i)
      text += QStringLiteral("Paragraph %1 has some text.\n\n").arg(i);
    auto* tab = f.open(text);
    f.command("Source and Preview")->trigger();
    waitFor([&] { return !tab->renderView->isLayoutPending(); }, "structural preview is ready");
    auto* input = tab->editor->findChild<VirtualSourceEdit*>();
    const auto offset = text.indexOf(QStringLiteral("Paragraph 80")) + QStringLiteral("Paragraph 80").size();
    tab->editor->setCursorPosition(offset);
    tab->editor->centerCursor();
    tab->synchronizeViewports(true);
    QApplication::processEvents();
    tab->setSynchronizedScrolling(linked);
    if (!linked) {
      auto position = tab->editor->viewportPosition();
      position.sourceOffset = text.indexOf(QStringLiteral("Paragraph 120"));
      position.screenOffset = 0;
      position.textAnchor = true;
      tab->renderView->restoreViewportPosition(position,
                                               tab->editorController.cursorForSourceOffset(position.sourceOffset));
    }
    QApplication::processEvents();
    const auto frame = tab->renderView->viewport()->grab().toImage();
    const auto reading = tab->renderView->viewportPosition();
    const int sourceScroll = input->verticalScrollBar()->value();
    require(sourceScroll > 0 && reading.sourceOffset > 0, "structural fixture starts away from document start");
    QSignalSpy scrolls(input->verticalScrollBar(), &QScrollBar::valueChanged);

    // Exercise both source structure editing and full AST replacement. The
    // latter also covers source parse fallbacks and parse-option changes.
    tab->editor->insertText(QStringLiteral("\n\n```cpp\n"));
    require(tab->session.applyMarkdownText(tab->session.markdownText().toString(), true),
            "reparse the current source as a replacement AST");
    require(!tab->session.lastParseWasLocalEdit() && tab->renderView->isLayoutPending(),
            "structural source edit takes the cooperative full layout path");
    require(tab->renderView->viewport()->grab().toImage() == frame,
            "full preview rebuild retains its last native frame instead of painting a loading spinner");
    for (const auto* insertion : {"first", " second", " third"}) {
      tab->editor->insertText(QLatin1String(insertion));
      require(tab->session.isEditingAllowed() && tab->renderView->isLayoutPending(),
              "source accepts more input while preview generations are superseded");
      require(tab->renderView->viewport()->grab().toImage() == frame,
              "superseded preview builds keep the same stable frame");
    }
    waitFor([&] { return !tab->renderView->isLayoutPending(); }, "latest structural preview completes");
    QApplication::processEvents();
    require(input->verticalScrollBar()->value() == sourceScroll,
            "publishing the new preview does not move the source viewport");
    for (const auto& scroll : scrolls)
      require(scroll.first().toInt() == sourceScroll, "source never follows intermediate layout scroll positions");
    auto expected = linked ? tab->editor->viewportPosition() : reading;
    if (!linked)
      expected.sourceOffset += tab->session.markdownText().size() - text.size();
    const auto hit =
        tab->renderView->hitForCursorPosition(tab->editorController.cursorForSourceOffset(expected.sourceOffset));
    const auto screenY = tab->renderView->mapDocumentToViewport(hit.cursorRect.topLeft()).y();
    require(!hit.cursorRect.isEmpty() && qAbs(screenY - expected.screenOffset) <= 1,
            QStringLiteral("structural preview preserves the text's screen position (linked %1): %2 -> %3")
                .arg(linked)
                .arg(expected.screenOffset)
                .arg(screenY));
    require(tab->renderView->viewport()->grab().toImage() != frame || !linked,
            "completed preview publishes the changed visible content");
    const auto* node = tab->session.document().topLevelBlockAtOffset(tab->editor->cursorPosition());
    const auto* block = tab->renderView->blockLayoutForNode(node->id());
    require(block && block->type() == BlockType::CodeFence && node->literal().contains("first second third"),
            "preview commits the latest document rather than a superseded parse");

    tab->focusEditor();
    for (int i = 0; i < 4; ++i) {
      tab->backend->undo();
      waitFor([&] { return !tab->renderView->isLayoutPending(); }, "preview follows structural undo");
    }
    require(tab->session.markdownText().toString() == text, "structural preview refresh preserves source undo");
  }
}

void independentPreviewHistory() {
  for (const bool html : {false, true}) {
    Fixture f;
    QString original = html ? QStringLiteral("<div align=\"center\">\n\n") : QString();
    for (int i = 0; i < 160; ++i)
      original += QStringLiteral("Paragraph %1 has some text.\n\n").arg(i);
    if (html)
      original += QStringLiteral("</div>\n");
    auto* tab = f.open(original);
    f.command("Source and Preview")->trigger();
    waitFor([&] { return !tab->renderView->isLayoutPending(); }, "independent history initial layout");
    tab->setSynchronizedScrolling(false);
    const auto readOffset = original.indexOf(QStringLiteral("Paragraph 120"));
    tab->renderView->restoreViewportPosition({readOffset, 0, 0, true, false},
                                             tab->editorController.cursorForSourceOffset(readOffset));
    QApplication::processEvents();
    (void)tab->renderView->viewport()->grab();
    const auto reading = tab->renderView->viewportPosition();
    int operation = 0;
    const auto verifyReading = [&] {
      // Controller commands enqueue their BrushQueue refresh after the local
      // session notification. Resource completion may enqueue another generation.
      waitFor([&] { return !tab->renderView->isLayoutPending(); }, "independent history preview completes");
      const auto expected = tab->session.markdownText().indexOf(QStringView(u"Paragraph 120"));
      const auto hit = tab->renderView->hitForCursorPosition(tab->editorController.cursorForSourceOffset(expected));
      const auto y = tab->renderView->mapDocumentToViewport(hit.cursorRect.topLeft()).y();
      require(
          !hit.cursorRect.isEmpty() && qAbs(y - reading.screenOffset) <= 1,
          QStringLiteral(
              "unlinked preview keeps its reading row through edit/history: HTML=%1 y=%2 expected=%3 operation=%4 pending=%5")
              .arg(html)
              .arg(y)
              .arg(reading.screenOffset)
              .arg(operation++)
              .arg(tab->renderView->isLayoutPending()));
    };
    const auto editOffset = original.indexOf(QStringLiteral("Paragraph 40"));
    const auto inserted = QStringLiteral("```cpp\nint value = 1;\n```\n\n");
    // This document/controller entry bypasses VirtualSourceEdit input events.
    require(tab->editorController.replaceSourceRangeWithUndo(editOffset, 0, inserted, QStringLiteral("code")),
            "controller source edit succeeds");
    verifyReading();
    for (int i = 0; i < 3; ++i) {
      tab->editor->undo();
      require(tab->session.markdownText().toString() == original, "undo restores the shared document");
      verifyReading();
      tab->editor->redo();
      verifyReading();
    }
    // Two separated replacements must leave the anchor between them intact;
    // a single common-prefix/suffix replacement would map it into the edit.
    QString output;
    SourceTextChange change;
    const auto before = tab->session.markdownText().toString();
    const auto search = DocumentSearch::replaceAll(before, QStringLiteral("Paragraph (20|140)"),
                                                   QStringLiteral("Expanded paragraph $1"), {true, true}, &output, {},
                                                   nullptr, &change);
    require(search.changed && change.edits.size() == 2, "replace-all records exact separated changes");
    tab->session.applyBatchAsync(PieceTable(output), QStringLiteral("replace"), 0, 0, 0, 0, {}, {}, change);
    waitFor([&] { return !tab->session.isBatchPending(); }, "independent batch commits");
    verifyReading();
    tab->editor->undo();
    waitFor([&] { return !tab->session.isBatchPending(); }, "independent batch undo commits");
    verifyReading();
    tab->editor->redo();
    waitFor([&] { return !tab->session.isBatchPending(); }, "independent batch redo commits");
    verifyReading();
  }
}

void largeHtmlPreviewYieldsAndSupersedes() {
  Fixture f;
  QString original = QStringLiteral("<div align=\"center\">\n\n");
  for (int i = 0; i < 1800; ++i)
    original += QStringLiteral("Paragraph %1 contains **bold**, [a link](https://example.com), and text.\n\n").arg(i);
  original += QStringLiteral("</div>\n");
  auto* tab = f.open(original);
  f.command("Source and Preview")->trigger();
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "large HTML initial layout");
  QApplication::processEvents();
  const auto frame = tab->renderView->viewport()->grab().toImage();
  tab->editor->setCursorPosition(original.indexOf(QStringLiteral("Paragraph 5")));
  tab->editor->insertText(QStringLiteral("first "));
  require(tab->renderView->isLayoutPending() && tab->session.isEditingAllowed(),
          "HTML typing schedules layout and returns with source editing available");
  require(tab->renderView->viewport()->grab().toImage() == frame, "HTML rebuild keeps the last complete frame");
  int heartbeats = 0;
  bool superseded = false;
  QTimer heartbeat;
  heartbeat.setInterval(0);
  QObject::connect(&heartbeat, &QTimer::timeout, &f.window, [&] {
    if (tab->renderView->isLayoutPending()) {
      ++heartbeats;
      if (!superseded && heartbeats >= 2) {
        superseded = true;
        tab->editor->insertText(QStringLiteral("latest "));
      }
    }
  });
  heartbeat.start();
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "latest large HTML preview completes");
  heartbeat.stop();
  require(superseded && heartbeats > 3, "event loop runs and accepts new input between HTML layout slices");
  const auto offset = tab->session.markdownText().indexOf(QStringView(u"first latest Paragraph 5"));
  const auto* node = tab->session.document().topLevelBlockAtOffset(offset);
  const auto* block = tab->renderView->blockLayoutForNode(node->id());
  require(offset >= 0 && block && block->inlineLayout() &&
              block->inlineLayout()->plainText().startsWith(QStringLiteral("first latest Paragraph 5")),
          "only the latest HTML layout generation is published");
  QSignalSpy completions(tab->renderView, &EditorView::layoutReady);
  QTest::qWait(40);
  require(!tab->renderView->isLayoutPending() && completions.isEmpty(),
          "completed HTML preview stays idle when resource geometry has not changed");
}

void scrollAcrossSourceSyntaxAndGaps() {
  for (const bool container : {false, true}) {
    Fixture f;
    QString text = container ? QStringLiteral("<div align=\"center\">\n\n") : QString();
    text += QStringLiteral("First paragraph.\n\n\n\n\n");
    text += QStringLiteral("```cpp\n");
    for (int i = 0; i < 80; ++i)
      text += QStringLiteral("code_line_%1;\n").arg(i);
    text += QStringLiteral("```\n\n\n\n");
    for (int i = 0; i < 100; ++i)
      text += QStringLiteral("Paragraph %1 has some text.\n\n\n\n").arg(i);
    if (container)
      text += QStringLiteral("</div>\n");
    auto* tab = f.open(text);
    f.command("Source and Preview")->trigger();
    waitFor([&] { return !tab->renderView->isLayoutPending(); }, "source syntax scroll fixture layout");
    auto* input = tab->editor->findChild<VirtualSourceEdit*>();
    const auto caret = tab->editor->cursorPosition();
    // Every source row near the beginning, including blank gaps and the
    // opening code fence, must stay near the beginning of the preview.
    for (int y = 1; y < 210; y += 7) {
      input->verticalScrollBar()->setValue(y);
      QApplication::processEvents();
      require(tab->renderView->verticalScrollBar()->value() < tab->renderView->verticalScrollBar()->maximum() / 2,
              QStringLiteral(
                  "source syntax/gaps must not jump preview to the end: source y=%1 offset=%2 preview=%3/%4 HTML=%5")
                  .arg(y)
                  .arg(tab->editor->viewportPosition().sourceOffset)
                  .arg(tab->renderView->verticalScrollBar()->value())
                  .arg(tab->renderView->verticalScrollBar()->maximum())
                  .arg(container));
    }
    input->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    for (const int delta : {-120, -120, 120, -120, 120, 120}) {
      const QPointF point = input->viewport()->rect().center();
      QWheelEvent wheel(point, input->viewport()->mapToGlobal(point.toPoint()), {}, QPoint(0, delta), Qt::NoButton,
                        Qt::NoModifier, Qt::NoScrollPhase, false);
      QApplication::sendEvent(input->viewport(), &wheel);
      QApplication::processEvents();
      require(tab->renderView->verticalScrollBar()->value() < tab->renderView->verticalScrollBar()->maximum() / 2,
              "wheel scrolling back and forth over syntax and blank rows does not jump to the document end");
    }
    for (const int paragraph : {2, 15, 40}) {
      const auto start = text.indexOf(QStringLiteral("Paragraph %1 has").arg(paragraph));
      const auto gap = text.indexOf(QLatin1Char('\n'), start) + 2;
      tab->editor->restoreViewportPosition({gap, 0, 0, true, false});
      QApplication::processEvents();
      const auto following = text.indexOf(QStringLiteral("Paragraph %1 has").arg(paragraph + 1));
      const auto cursor = tab->editorController.cursorForSourceOffset(following);
      const auto rect = tab->renderView->nodeRect(cursor.blockId);
      const auto y = tab->renderView->mapDocumentToViewport(rect.topLeft()).y();
      require(!rect.isEmpty() && qAbs(y) < 100,
              QStringLiteral("an interior source gap follows nearby content, not EOF: paragraph=%1 y=%2 HTML=%3")
                  .arg(paragraph)
                  .arg(y)
                  .arg(container));
    }
    require(tab->editor->cursorPosition() == caret && !tab->session.document().isModified(),
            "scrolling does not move the source caret or edit the document");
  }
}

void resizeAndArrangePanes() {
  for (const bool html : {false, true}) {
    for (const bool linked : {false, true}) {
      Fixture f;
      QString text = html ? QStringLiteral("<div align=\"center\">\n\n") : QString();
      for (int i = 0; i < 180; ++i)
        text +=
            QStringLiteral(
                "Paragraph %1 has **bold** text and a [link](https://example.com). More words to wrap across several lines.\n\n")
                .arg(i);
      if (html)
        text += QStringLiteral("</div>\n");
      auto* tab = f.open(text);
      f.command("Source and Preview")->trigger();
      waitFor([&] { return !tab->renderView->isLayoutPending(); }, "arrangement initial preview");
      tab->setSynchronizedScrolling(linked);
      tab->editor->setSelection(12, 2);
      const auto sourceOffset = text.indexOf(QStringLiteral("Paragraph 120"));
      tab->editor->restoreViewportPosition({sourceOffset, 0, 0, true, false});
      tab->synchronizeViewports(true);
      const auto previewOffset = linked ? sourceOffset : text.indexOf(QStringLiteral("Paragraph 80"));
      tab->renderView->restoreViewportPosition({previewOffset, 0, 0, true, false},
                                               tab->editorController.cursorForSourceOffset(previewOffset));
      waitFor([&] { return !tab->renderView->isLayoutPending(); }, "arrangement reading window");
      (void)tab->renderView->viewport()->grab();
      const auto verify = [&] {
        waitFor([&] { return !tab->renderView->isLayoutPending(); }, "arrangement preview finishes");
        require(qAbs(tab->editor->viewportPosition().sourceOffset - sourceOffset) < 130,
                "resizing and rearranging keep source reading text away from its offscreen caret");
        const auto hit =
            tab->renderView->hitForCursorPosition(tab->editorController.cursorForSourceOffset(previewOffset));
        const auto y = tab->renderView->mapDocumentToViewport(hit.cursorRect.topLeft()).y();
        require(!hit.cursorRect.isEmpty() && qAbs(y) < (linked ? 50 : 2),
                QStringLiteral("pane layout preserves preview reading row: HTML=%1 linked=%2 y=%3")
                    .arg(html)
                    .arg(linked)
                    .arg(y));
        require(tab->editor->anchorPosition() == 12 && tab->editor->cursorPosition() == 2 &&
                    !tab->session.document().isModified(),
                "arranging panes leaves selection and document untouched");
      };
      for (const int width : {260, 620, 390}) {
        tab->viewSplitter->setSizes({width, 900 - width});
        verify();
      }
      auto* options = tab->findChild<QToolButton*>("SplitLayoutOptions");
      require(options && options->menu() && !options->icon().isNull() && !options->accessibleName().isEmpty(),
              "pane arrangement is available from a compact accessible menu");
      tab->findChild<QAction*>("SplitTopAndBottom")->trigger();
      verify();
      require(tab->splitOrientation() == Qt::Vertical &&
                  tab->findChild<QWidget*>("SplitPreviewHeader")->mapTo(tab, QPoint()).y() >
                      tab->findChild<QWidget*>("SplitSourceHeader")->mapTo(tab, QPoint()).y(),
              "top/bottom menu action lays preview below source");
      tab->findChild<QAction*>("SplitSwapPanes")->trigger();
      verify();
      require(tab->previewFirst(), "swap moves preview above source");
      tab->findChild<QAction*>("SplitSideBySide")->trigger();
      verify();
      require(tab->splitOrientation() == Qt::Horizontal && tab->previewFirst(),
              "orientation changes preserve pane order");
      tab->setSplitLayout(Qt::Vertical, true);
      verify();
      const auto saved = f.window.sessionState();
      Fixture restored;
      restored.window.restoreSessionState(saved);
      auto* copy = restored.current();
      waitFor([&] { return !copy->session.isOpenInProgress() && !copy->pendingViewState; },
              "restore arranged split session");
      require(copy->splitOrientation() == Qt::Vertical && copy->previewFirst() &&
                  copy->synchronizedScrolling() == linked,
              "session restores top/bottom split, swapped pane order and scrolling policy");
    }
  }
}

void writingModesBelongToTheEditingPane() {
  SettingsOverride focus{"appearance/focusMode", false};
  SettingsOverride typewriter{"appearance/typewriterMode", false};
  SettingsOverride middle{"editor/typewriterCursorMiddle", true};
  Fixture f;
  QString text;
  for (int i = 0; i < 120; ++i)
    text +=
        QStringLiteral("Paragraph %1 has enough words to wrap across multiple lines in the source pane.\n\n").arg(i);
  auto* tab = f.open(text);
  // Start a rendered cursor animation, then switch modes before it completes.
  f.command("Typewriter Mode")->trigger();
  tab->editorController.selectSourceRange(text.indexOf("Paragraph 60"), text.indexOf("Paragraph 60"));
  f.command("Source and Preview")->trigger();
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "writing mode preview layout");
  auto* input = tab->editor->findChild<VirtualSourceEdit*>();
  require(input->verticalScrollBar()->minimum() < 0 && tab->renderView->verticalScrollBar()->minimum() == 0,
          "typewriter boundary padding belongs only to the editable source");
  tab->setSynchronizedScrolling(false);
  const auto previewOffset = text.indexOf("Paragraph 30");
  tab->renderView->restoreViewportPosition({previewOffset, 0, 0, true, false},
                                           tab->editorController.cursorForSourceOffset(previewOffset));
  (void)tab->renderView->viewport()->grab();
  const auto previewBefore = tab->renderView->viewport()->grab().toImage();
  const auto sourceBefore = input->viewport()->grab().toImage();
  f.command("Focus Mode")->trigger();
  require(tab->renderView->viewport()->grab().toImage() == previewBefore,
          "focus mode leaves the read-only preview complete");
  require(input->viewport()->grab().toImage() != sourceBefore, "focus mode follows the source writing line");
  const auto previewScroll = tab->renderView->verticalScrollBar()->value();
  tab->focusEditor();
  tab->editor->setCursorPosition(text.indexOf("Paragraph 80") + 50);
  tab->editor->insertText(QStringLiteral("中文输入"));
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "typewriter source input updates preview");
  const auto caret = input->inputMethodQuery(Qt::ImCursorRectangle).toRect();
  require(qAbs(caret.center().y() - input->viewport()->height() / 2) <= 2,
          QStringLiteral("source input keeps its own visual caret row centered: y=%1 height=%2")
              .arg(caret.center().y())
              .arg(input->viewport()->height()));
  require(qAbs(tab->renderView->verticalScrollBar()->value() - previewScroll) <= 2,
          "source typing does not center an independent preview");
  tab->renderView->setFocus();
  waitFor([&] { return tab->previewActive(); }, "preview selection receives focus");
  tab->editorController.selectSourceRange(previewOffset, previewOffset + 9);
  const auto selectedScroll = tab->renderView->verticalScrollBar()->value();
  QTest::qWait(350);
  require(tab->renderView->verticalScrollBar()->value() == selectedScroll,
          "preview selections and a previous rendered animation cannot start typewriter scrolling");
  tab->focusEditor();
  tab->setSynchronizedScrolling(true);
  tab->editor->setCursorPosition(text.indexOf("Paragraph 90") + 20);
  QApplication::processEvents();
  require(qAbs(tab->renderView->viewportPosition().sourceOffset - tab->editor->viewportPosition().sourceOffset) < 200,
          "linked preview follows the source reading anchor under typewriter mode");
  tab->setSplitLayout(Qt::Vertical, true);
  waitFor([&] { return !tab->renderView->isLayoutPending(); }, "writing modes survive pane rearrangement");
  require(input->verticalScrollBar()->minimum() < 0 && tab->renderView->verticalScrollBar()->minimum() == 0,
          "swapping panes does not change writing mode ownership");
  f.command("Source and Preview")->trigger();
  require(tab->viewMode() == DocumentViewMode::Rendered && tab->renderView->verticalScrollBar()->minimum() < 0 &&
              input->verticalScrollBar()->minimum() == 0 && f.command("Focus Mode")->isChecked() &&
              f.command("Typewriter Mode")->isChecked(),
          "return to rendered editing restores writing modes without changing user preferences");
  f.command("Focus Mode")->trigger();
  f.command("Typewriter Mode")->trigger();
}
}

void runSplitViewTests() {
  runTest("splitViewResizeAndArrangePanes", resizeAndArrangePanes);
  runTest("splitViewWritingModesBelongToTheEditingPane", writingModesBelongToTheEditingPane);
  runTest("splitViewIndependentPreviewHistory", independentPreviewHistory);
  runTest("splitViewLargeHtmlPreviewYieldsAndSupersedes", largeHtmlPreviewYieldsAndSupersedes);
  runTest("splitViewCloseDuringPreviewUpdates", closeDuringPreviewUpdates);
  runTest("splitViewScrollAcrossSourceSyntaxAndGaps", scrollAcrossSourceSyntaxAndGaps);
  runTest("splitViewUnsavedSourceClose", unsavedSourceClose);
  runTest("splitViewContinuousPreviewEdits", continuousPreviewEdits);
  runTest("splitViewStructuralPreviewEdits", structuralPreviewEdits);
  runTest("splitViewEditingWorkflow", editingWorkflow);
  runTest("splitViewPreviewCannotEditSource", previewCannotEditSource);
  runTest("splitViewThemedPaneChrome", themedPaneChrome);
  runTest("splitViewScrollAndTabState", scrollAndTabState);
  runTest("splitViewFindReplaceAndCustomShortcut", findReplaceAndCustomShortcut);
}
