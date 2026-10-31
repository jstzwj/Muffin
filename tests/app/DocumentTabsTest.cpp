#include "app/MainWindow.h"
#include "app/FileOpenDispatcher.h"
#include "app/NoteLinks.h"
#include "app/QuickOpenDialog.h"
#include "app/SearchPanelWidget.h"
#include "app/LanguageManager.h"
#include "app/FileTreeModel.h"
#include "app/FileOpenConfirmation.h"
#include "app/FileOpenProtection.h"
#include "app/PrefsFilesPage.h"
#include "app/PreferencesDialog.h"
#include "app/PrefsExportPage.h"
#include "app/PrefsShortcutsPage.h"
#include "app/SearchSettings.h"
#include "app/CommandDeclarations.h"
#include "theme/ChromeStyleSheet.h"
#include "editor/ShortcutSettings.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"
#include "editor/VirtualSourceEdit.h"
#include "render/BlockLayout.h"
#include "render/ImageLoader.h"
#include "render/LayoutResources.h"
#include "projection/SelectionSerializer.h"

#include "../TestUtils.h"
#include "../AtomicFileWrite.h"
#include "../../scripts/MemoryLifecycleAudit.h"
#include "../../scripts/DocumentMemoryBenchmark.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QFileDialog>
#include <QSaveFile>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QInputMethodEvent>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QListWidget>
#include <QPushButton>
#include <QProgressDialog>
#include <QPlainTextEdit>
#include <QProxyStyle>
#include <QRadioButton>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSettings>
#include <QDoubleSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSpinBox>
#include <QStyleFactory>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest/QTest>

using namespace muffin;

namespace {

void waitUntil(const std::function<bool()>& ready, const char* message) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 15000) {
    QApplication::processEvents();
    QTest::qWait(5);
  }
  require(ready(), message);
}

QAction* action(MainWindow& window, const QString& text) {
  for (QAction* candidate : window.findChildren<QAction*>()) {
    if (candidate->text() == text) {
      return candidate;
    }
  }
  require(false, QStringLiteral("Missing action: %1").arg(text));
  return nullptr;
}

void acceptSaveFile(QFileDialog& dialog, const QString& path) {
  require(dialog.acceptMode() == QFileDialog::AcceptSave, "file chooser is a save dialog");
  dialog.setOption(QFileDialog::DontConfirmOverwrite, true);
  auto* filename = dialog.findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
  require(filename, "non-native file dialog exposes its filename input");
  // selectFile() deliberately leaves a visible, focused filename editor alone
  // (Qt 6 qfiledialog.cpp). Write the intended path as user input instead of
  // relying on platform-dependent initial focus or the directory model's timing.
  filename->setFocus();
  filename->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
  const auto selected = dialog.selectedFiles();
  const auto expected = QFileInfo(path).absoluteFilePath();
  require(selected.size() == 1 && QFileInfo(selected.first()).absoluteFilePath() == expected,
          QStringLiteral("save dialog must select %1, got %2").arg(expected, selected.join(QStringLiteral(", "))));
  QString emitted;
  const auto connection =
      QObject::connect(&dialog, &QFileDialog::fileSelected, &dialog, [&](const QString& file) { emitted = file; });
  const bool invoked = QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection);
  QObject::disconnect(connection);
  require(invoked && dialog.result() == QDialog::Accepted && !emitted.isEmpty() &&
              QFileInfo(emitted).absoluteFilePath() == expected,
          QStringLiteral("save dialog must accept %1, result=%2, emitted=%3")
              .arg(expected)
              .arg(dialog.result())
              .arg(emitted));
}

void focusedSaveDialogSelectsExactPath() {
  QTemporaryDir directory;
  require(directory.isValid(), "save dialog fixture directory");
  require(QDir(directory.path()).mkdir(QStringLiteral("中文 folder")), "save dialog alternative directory");
  const auto target = directory.filePath(QStringLiteral("中文 folder/chosen file.md"));
  for (bool existing : {false, true}) {
    if (existing) {
      QFile file(target);
      require(file.open(QIODevice::WriteOnly) && file.write("original") == 8, "existing save dialog fixture");
    }
    QFileDialog dialog(nullptr, QStringLiteral("Save"), directory.filePath("suggested.md"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.open();
    auto* filename = dialog.findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
    require(filename, "focused save dialog filename input");
    filename->setFocus();
    waitUntil([&] { return filename->hasFocus(); }, "filename input receives initial focus");
    // Force the state which made selectFile() a no-op, for both a new path
    // and an overwrite, including a different directory, spaces and Unicode.
    acceptSaveFile(dialog, target);
  }
}

struct Fixture {
  QTemporaryDir directory;
  SettingsOverride sourceMode{"view/sourceMode", false};
  MainWindow window;
  QTabWidget* tabs = nullptr;

  Fixture() {
    require(directory.isValid(), "temporary directory");
    tabs = window.findChild<QTabWidget*>(QStringLiteral("DocumentTabs"));
    require(tabs != nullptr, "document tabs exist");
    window.resize(1000, 700);
    window.show();
    QApplication::processEvents();
  }

  ~Fixture() {
    // Tests own their dirty buffers and recovery snapshots; do not leave drafts behind.
    DraftRecovery recovery;
    for (int i = 0; i < tabs->count(); ++i) {
      auto* document = static_cast<DocumentTab*>(tabs->widget(i));
      document->session.document().setModified(false);
      recovery.markClean(document->session.filePath(), document->draftKey);
    }
  }

  DocumentTab* current() { return static_cast<DocumentTab*>(tabs->currentWidget()); }

  QString file(const QString& name, const QString& text) {
    const QString path = directory.filePath(name);
    QFile out(path);
    require(out.open(QIODevice::WriteOnly), "create test file");
    require(out.write(text.toUtf8()) > 0, "write test file");
    return path;
  }

  DocumentTab* open(const QString& path) {
    require(window.openFile(path), "open file");
    DocumentTab* document = current();
    waitUntil([document] { return !document->session.isAsyncParseInProgress(); }, "file finished loading");
    QApplication::processEvents(); // drain queued path/base updates before editing or scrolling
    return document;
  }

  void close(DocumentTab* document) { tabs->tabCloseRequested(tabs->indexOf(document)); }
};

QList<QMessageBox*> reloadQuestions(MainWindow& window) {
  QList<QMessageBox*> visible;
  // QMessageBox deliberately ignores window titles on macOS. Locate this
  // application-owned dialog by identity, independently of platform or locale.
  for (auto* dialog : window.findChildren<QMessageBox*>(QStringLiteral("ExternalFileChangeDialog")))
    if (dialog->isVisible())
      visible.append(dialog);
  return visible;
}

void externalWrite(DocumentTab* document, const QString& text) {
  const auto bytes = text.toUtf8();
  QString error;
  const bool committed = muffin::test::writeFileAtomically(document->session.filePath(), bytes, &error);
  require(committed, QStringLiteral("external atomic replacement of %1 (%2) succeeds: %3")
                         .arg(document->session.filePath(), text.left(40), error));
  // Exercise the real baseline/deduplication path without depending on when a
  // platform's watcher delivers each event. The dedicated file-watch test
  // separately covers notification delivery and re-arming after atomic writes.
  require(QMetaObject::invokeMethod(&document->session, "onFileChanged", Qt::DirectConnection),
          "check the externally changed file");
}

void externalFilesOpenInTabsByPreference() {
  SettingsOverride preference(file_open_preferences::kOpenInTabs, true);
  SettingsOverride session("session/window", QVariantMap{});
  QSettings().remove(QLatin1String(file_open_preferences::kOpenInTabs));
  PrefsFilesPage preferences;
  auto* check = preferences.findChild<QCheckBox*>(QStringLiteral("openFilesInTabs"));
  require(check && check->isChecked() && file_open_preferences::openInTabs(), "tab reuse is enabled by default");
  require(LanguageManager::instance().setLanguage(QStringLiteral("zh_CN")), "file-open preference language loads");
  preferences.retranslateUi();
  require(check->text() == QString::fromUtf8("默认以标签页打开文件"), "the new preference is translated at runtime");
  LanguageManager::instance().setLanguage(QStringLiteral("en"));
  preferences.retranslateUi();
  check->setChecked(false);
  require(!file_open_preferences::openInTabs(), "preference immediately changes the open policy");
  PrefsFilesPage reloadedPreferences;
  require(!reloadedPreferences.findChild<QCheckBox*>(QStringLiteral("openFilesInTabs"))->isChecked(),
          "file-open preference persists across preferences windows");
  check->setChecked(true);

  Fixture f;
  FileOpenDispatcher dispatcher;
  dispatcher.setInitialWindow(&f.window);
  auto* unsaved = f.current();
  unsaved->session.setMarkdownText("Unsaved original work\n", true);
  const auto first = f.file(QString::fromUtf8("中文 first.md"), "# First\n");
  const auto second = f.file("second.md", "# Second\n");
  dispatcher.openFiles({first, second});
  waitUntil(
      [&] {
        return f.window.hasOpenFile(first) && f.window.hasOpenFile(second) && !f.current()->session.isOpenInProgress();
      },
      "external files load as tabs");
  require(f.tabs->count() == 3 && unsaved->session.markdownText().toString() == "Unsaved original work\n" &&
              unsaved->session.document().isModified(),
          "external opens preserve the dirty untitled buffer");
  dispatcher.openFiles({first});
  require(f.tabs->count() == 3 && f.current()->session.filePath() == first, "reopening selects the existing tab");
  auto* edited = f.current();
  waitUntil([&] { return !edited->session.isOpenInProgress(); }, "first tab finishes loading");
  edited->session.setMarkdownText("Unsaved first-file changes\n", true);
  f.window.showMinimized();
  dispatcher.openFiles({first});
  require(!f.window.isMinimized() && edited->session.markdownText().toString() == "Unsaved first-file changes\n" &&
              edited->session.document().isModified(),
          "reopening restores the window without reloading unsaved edits");

  // Native FileOpen events (Finder on macOS) must follow the same policy.
  QFileOpenEvent event(second);
  QCoreApplication::sendEvent(qApp, &event);
  require(f.tabs->count() == 3 && f.current()->session.filePath() == second, "native file events also reuse tabs");

  QDialog modal(&f.window);
  modal.setModal(true);
  modal.show();
  QApplication::processEvents();
  dispatcher.openFiles({first});
  require(f.current()->session.filePath() == second, "external requests wait while a modal operation is active");
  modal.close();
  waitUntil([&] { return f.current()->session.filePath() == first; }, "queued request opens after the modal closes");

  check->setChecked(false);
  const auto third = f.file("new-window.md", "# Third\n");
  dispatcher.openFiles({third});
  QPointer<MainWindow> other;
  for (auto* widget : QApplication::topLevelWidgets())
    if (auto* window = qobject_cast<MainWindow*>(widget); window && window != &f.window && window->hasOpenFile(third))
      other = window;
  require(other && f.tabs->count() == 3 && !f.window.hasOpenFile(third), "disabling tab reuse opens a separate window");
  waitUntil(
      [&] {
        return other->hasOpenFile(third) &&
               !static_cast<DocumentTab*>(
                    other->findChild<QTabWidget*>(QStringLiteral("DocumentTabs"))->currentWidget())
                    ->session.isOpenInProgress();
      },
      "separate window finishes loading");
  check->setChecked(true);
  QEvent activate(QEvent::WindowActivate);
  QCoreApplication::sendEvent(other, &activate);
  dispatcher.openFiles({first});
  require(!other->hasOpenFile(first) && f.current()->session.filePath() == first,
          "an existing tab in another window is activated without duplicating its document");
  other->close();
  waitUntil([&] { return !other; }, "separate window is destroyed on close");
  dispatcher.openFiles({second});
  require(f.current()->session.filePath() == second, "routing survives closing the last active window");
}

void closedInitialWindowDoesNotCaptureFileOpens() {
  SettingsOverride preference(file_open_preferences::kOpenInTabs, true);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture f;
  const auto path = f.file("closed-window.md", "# Previously opened\n");
  f.open(path);
  FileOpenDispatcher dispatcher;
  dispatcher.setInitialWindow(&f.window);
  MainWindow remaining;
  remaining.show();
  QApplication::processEvents();
  require(f.window.close() && !f.window.isVisible(), "the original window closes while another remains open");
  dispatcher.openFiles({path});
  auto* tabs = remaining.findChild<QTabWidget*>(QStringLiteral("DocumentTabs"));
  auto* document = static_cast<DocumentTab*>(tabs->currentWidget());
  waitUntil([&] { return !document->session.isOpenInProgress(); }, "file opens in the remaining window");
  require(remaining.hasOpenFile(path) && !f.window.isVisible(),
          "a closed stack-owned window never captures requests intended for an existing window");
}

void earlyNativeFileEventsAreRetained() {
  SettingsOverride preference(file_open_preferences::kOpenInTabs, true);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture f;
  FileOpenDispatcher dispatcher;
  const auto first = f.file("early-one.md", "# One\n");
  const auto second = f.file("early-two.md", "# Two\n");
  QFileOpenEvent one(first);
  QFileOpenEvent two(second);
  QCoreApplication::sendEvent(qApp, &one);
  QCoreApplication::sendEvent(qApp, &two);
  require(f.tabs->count() == 1 && !f.window.hasOpenFile(first), "events wait for the initial window");
  require(dispatcher.setInitialWindow(&f.window), "early events suppress unrelated session restoration");
  waitUntil([&] { return f.window.hasOpenFile(first) && f.window.hasOpenFile(second); }, "all early events replay");
  require(f.tabs->count() == 2, "early events do not overwrite one another or create an extra blank tab");
}

void externalFileChangesCoalesceAndReloadLatest() {
  SettingsOverride autoSave("files/autoSave", true);
  Fixture f;
  auto* document = f.open(f.file("agent-edited.md", "# Original\n"));
  document->session.setMarkdownText("Unsaved local edits\n", true);
  require(document->autoSaveTimer->isActive(), "local edits schedule auto-save before the external conflict");
  // Fail promptly on the old synchronous question(), rather than leave the
  // regression hanging forever in a modal event loop.
  QTimer watchdog;
  watchdog.setInterval(1000);
  QObject::connect(&watchdog, &QTimer::timeout, &f.window, [&] {
    for (auto* question : reloadQuestions(f.window))
      question->button(QMessageBox::No)->click();
  });
  watchdog.start();
  externalWrite(document, "Agent revision 0\n");
  watchdog.stop();
  require(!document->autoSaveTimer->isActive(), "pending reload decision suspends the background auto-save");
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "one asynchronous reload question appears");
  QPointer<QMessageBox> first = reloadQuestions(f.window).first();
  // Exercise macOS's titleless message boxes on every platform as well.
  first->setWindowTitle(QString());
  require(reloadQuestions(f.window).size() == 1 && reloadQuestions(f.window).first() == first,
          "reload question detection also works without a window title");
  QString latest;
  for (int i = 1; i <= 12; ++i) {
    latest = QStringLiteral("Agent revision %1\n%2\n").arg(i).arg(QString(i, QLatin1Char('x')));
    externalWrite(document, latest);
    QApplication::processEvents();
    require(reloadQuestions(f.window).size() == 1 && reloadQuestions(f.window).first() == first,
            "repeated writes reuse the existing question instead of stacking or queuing duplicates");
  }
  require(document->session.markdownText().toString() == "Unsaved local edits\n" &&
              document->session.document().isModified(),
          "external writes never discard unsaved edits before confirmation");
  first->button(QMessageBox::Yes)->click();
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty() && document->session.markdownText().toString() == latest &&
              !document->session.document().isModified(),
          "one confirmation reloads the latest revision and dismisses the entire burst");

  document->session.setMarkdownText("Keep my new local work\n", true);
  externalWrite(document, "Another external revision after reloading\n");
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "a later edit starts a new question");
  externalWrite(document, "Latest external revision while declining to reload\n");
  reloadQuestions(f.window).first()->button(QMessageBox::No)->click();
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty() &&
              document->session.markdownText().toString() == "Keep my new local work\n" &&
              document->session.document().isModified(),
          "declining once preserves local edits without replaying older notifications");
  QFile disk(document->session.filePath());
  require(disk.open(QIODevice::ReadOnly) && disk.readAll() == "Latest external revision while declining to reload\n",
          "declining does not overwrite the agent's disk changes");
  disk.close();
  externalWrite(document, "A genuinely new change must still be reported\n");
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "declining does not disable later notifications");
  reloadQuestions(f.window).first()->button(QMessageBox::No)->click();
}

void backgroundExternalFileChangesWaitForActivation() {
  SettingsOverride autoSave("files/autoSave", true);
  SettingsOverride saveOnSwitch("files/autoSaveOnSwitch", true);
  Fixture f;
  auto* foreground = f.open(f.file("foreground.md", "Foreground\n"));
  auto* background = f.open(f.file("background.md", "Background\n"));
  auto* other = f.open(f.file("other.md", "Other\n"));
  f.tabs->setCurrentWidget(foreground);
  background->session.setMarkdownText("Unsaved background work\n", true);
  QString latest;
  for (int i = 0; i < 12; ++i) {
    latest = QStringLiteral("Background revision %1\n%2\n").arg(i).arg(QString(i, QLatin1Char('x')));
    externalWrite(background, latest);
    QApplication::processEvents();
    require(reloadQuestions(f.window).isEmpty() && f.current() == foreground,
            "background file changes never interrupt the active tab");
  }
  externalWrite(other, "Other external revision\n");
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty() && !background->autoSaveTimer->isActive() &&
              background->session.markdownText().toString() == "Unsaved background work\n",
          "background conflict preserves local edits and suspends auto-save silently");
  f.tabs->setCurrentWidget(other);
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "activating a changed file shows its question");
  require(reloadQuestions(f.window).first()->text().contains("other.md"),
          "older background changes do not block the newly active file");
  reloadQuestions(f.window).first()->button(QMessageBox::Yes)->click();
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty() &&
              other->session.markdownText().toString() == "Other external revision\n",
          "confirming the active file does not pop questions for inactive files");

  // Rapid activation superseded before the deferred callback must not flash a question.
  f.tabs->setCurrentWidget(background);
  f.tabs->setCurrentWidget(foreground);
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty(), "a tab deactivated before dispatch never opens a question");
  f.tabs->setCurrentWidget(background);
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "background changes prompt on activation");
  require(reloadQuestions(f.window).first()->text().contains("background.md"), "question belongs to the active tab");
  // Programmatic activation (e.g. navigation/close) must retire the now-irrelevant
  // question without treating it as an explicit decision to discard the reminder.
  f.tabs->setCurrentWidget(foreground);
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty(), "deactivation closes the question without saving or reloading");
  f.tabs->setCurrentWidget(background);
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "returning to an unanswered tab asks again");
  reloadQuestions(f.window).first()->button(QMessageBox::Yes)->click();
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty() && background->session.markdownText().toString() == latest &&
              !background->session.document().isModified(),
          "one confirmation after activation reads the latest background revision");
  f.tabs->setCurrentWidget(foreground);
  f.tabs->setCurrentWidget(background);
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty(), "resolved changes are not repeated on later activation");
}

void externalFileQuestionsRetireWithTheirTab() {
  Fixture f;
  auto* first = f.open(f.file("first.md", "First\n"));
  auto* second = f.open(f.file("second.md", "Second\n"));
  auto* third = f.open(f.file("third.md", "Third\n"));
  f.tabs->setCurrentWidget(first);
  externalWrite(first, "First external edit\n");
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "first file asks to reload");
  externalWrite(second, "Second external edit\n");
  externalWrite(second, "Latest second external edit\n");
  externalWrite(third, "Third external edit\n");
  QApplication::processEvents();
  require(reloadQuestions(f.window).size() == 1 && reloadQuestions(f.window).first()->text().contains("first.md"),
          "background changes do not stack over the active file's question");
  require(second->fileController.reload(second->session, &f.window), "resolve a background file's external change");
  reloadQuestions(f.window).first()->button(QMessageBox::No)->click();
  f.tabs->setCurrentWidget(second);
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty(), "resolved background changes are skipped on activation");
  f.tabs->setCurrentWidget(third);
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "unresolved file asks on activation");
  require(reloadQuestions(f.window).first()->text().contains("third.md"), "activation targets only that file");
  QPointer<DocumentTab> closed = third;
  f.close(third);
  QApplication::processEvents();
  require(!closed && reloadQuestions(f.window).isEmpty(), "closing a tab retires its active question safely");

  f.tabs->setCurrentWidget(first);
  externalWrite(first, "First changes again with a longer revision\n");
  waitUntil([&] { return reloadQuestions(f.window).size() == 1; }, "queue remains usable after closing a tab");
  externalWrite(second, "Second pending edit before closing\n");
  f.close(second);
  first->session.newDocument();
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty() && first->session.filePath().isEmpty() &&
              first->session.markdownText().isEmpty(),
          "path changes cancel active questions and closed queued tabs are ignored");

  // Destroying a window with a question still open must retire its callbacks
  // while the window-owned queue and services still exist.
  Fixture closing;
  auto* active = closing.open(closing.file("closing.md", "Before closing\n"));
  externalWrite(active, "External revision before window destruction\n");
  waitUntil([&] { return reloadQuestions(closing.window).size() == 1; }, "window has an active question at teardown");
}

void exportPreferencesAndOutputs() {
  Fixture f;
  f.open(f.file("export.md", "# First\n\nExport **content**\n\n# Second\n\nEnd\n"));
  QSettings().setValue("preferences/lastCategory", "export");
  PreferencesDialog preferences;
  preferences.show();
  auto* page = preferences.findChild<PrefsExportPage*>();
  auto* formats = page->findChild<QListWidget*>("ExportFormats");
  require(formats && formats->count() == 14, "general and all available export formats have settings pages");
  formats->setCurrentRow(1);
  auto* paper = page->findChild<QComboBox*>("ExportPdfPaper");
  require(paper && !page->findChild<QLineEdit*>("ExportPandocArguments"), "PDF shows native page settings");
  paper->setCurrentIndex(paper->findData("Custom"));
  page->findChild<QDoubleSpinBox*>("ExportPdfWidth")->setValue(174.2);
  page->findChild<QDoubleSpinBox*>("ExportPdfHeight")->setValue(230.5);
  paper->setCurrentIndex(paper->findData("A5"));
  require(page->findChild<QDoubleSpinBox*>("ExportPdfWidth")->value() == 148.0, "preset dimensions update in the form");
  paper->setCurrentIndex(paper->findData("Custom"));
  require(page->findChild<QDoubleSpinBox*>("ExportPdfWidth")->value() == 174.2 &&
              page->findChild<QDoubleSpinBox*>("ExportPdfHeight")->value() == 230.5,
          "paper presets preserve custom dimensions");
  paper->setCurrentIndex(paper->findData("A5"));
  page->findChild<QCheckBox*>("ExportPdfLandscape")->setChecked(true);
  formats->setCurrentRow(2);
  page->findChild<QCheckBox*>("ExportHtmlOutline")->setChecked(true);
  page->findChild<QComboBox*>("ExportTheme")->setCurrentIndex(1);
  page->findChild<QPlainTextEdit*>("ExportHtmlHead")->setPlainText("<meta name=\"author\" content=\"Muffin\">");
  formats->setCurrentRow(3);
  require(!page->findChild<QCheckBox*>("ExportHtmlOutline")->isChecked() && !page->findChild<QComboBox*>("ExportTheme"),
          "unstyled HTML has independent settings and no theme picker");
  formats->setCurrentRow(4);
  page->findChild<QSpinBox*>("ExportImageWidth")->setValue(640);
  page->findChild<QComboBox*>("ExportImageScale")->setCurrentIndex(1);
  formats->setCurrentRow(5);
  require(page->findChild<QLineEdit*>("ExportReferenceDocument") && !page->findChild<QLineEdit*>("ExportEpubCss"),
          "Word shows a reference template");
  formats->setCurrentRow(8);
  require(page->findChild<QLineEdit*>("ExportEpubCss") && page->findChild<QSpinBox*>("ExportTocDepth") &&
              !page->findChild<QLineEdit*>("ExportReferenceDocument"),
          "EPUB has CSS and TOC options");
  formats->setCurrentRow(1);
  require(page->findChild<QComboBox*>("ExportPdfPaper")->currentData() == "A5" &&
              page->findChild<QCheckBox*>("ExportPdfLandscape")->isChecked(),
          "switching formats retains independent settings");
  const auto captures = qEnvironmentVariable("MUFFIN_EXPORT_SCREENSHOT_DIR");
  if (!captures.isEmpty()) {
    QDir().mkpath(captures);
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh_CN")}) {
      LanguageManager::instance().setLanguage(language);
      for (int row : {0, 1, 2, 3, 4, 5, 8}) {
        formats->setCurrentRow(row);
        QApplication::processEvents();
        require(preferences.grab().save(QDir(captures).filePath(language + QString::number(row) + ".png")),
                "capture real export settings");
      }
    }
    LanguageManager::instance().setLanguage("en");
  }
  preferences.hide();
  const auto exportTo = [&](const char* name, const QString& path) {
    auto* command = action(f.window, name);
    // Content-dependent actions refresh when the File menu opens. Exercise
    // that path instead of relying on incidental updates from screenshots.
    auto* fileMenu = f.window.menuBar()->actions().first()->menu();
    fileMenu->popup(f.window.menuBar()->mapToGlobal(QPoint(0, f.window.menuBar()->height())));
    QApplication::processEvents();
    fileMenu->hide();
    require(command->isEnabled(), "export action is enabled when the File menu opens");
    QTimer accept;
    accept.setInterval(5);
    bool selected = false;
    QObject::connect(&accept, &QTimer::timeout, &f.window, [&] {
      if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        if (!selected) {
          selected = true;
          acceptSaveFile(*dialog, path);
        }
      } else if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        require(false, "Unexpected export error: " + message->text());
      }
    });
    accept.start();
    command->trigger();
    accept.stop();
    require(selected && QFileInfo(path).isFile(),
            QStringLiteral("export %1 creates the selected output %2 (dialog handled=%3)")
                .arg(QString::fromUtf8(name), path)
                .arg(selected));
    if (!captures.isEmpty()) {
      const auto copy = QDir(captures).filePath(QFileInfo(path).fileName());
      QFile::remove(copy);
      require(QFile::copy(path, copy), "retain exported samples for inspection");
    }
  };
  const auto html = f.directory.filePath("note.html");
  exportTo("HTML", html);
  QFile out(html);
  require(out.open(QIODevice::ReadOnly), "read menu-exported HTML");
  const auto contents = out.readAll();
  out.close();
  require(contents.contains("muffin-heading-1") && contents.contains("name=\"author\""),
          "HTML menu consumes its saved options");
  require(contents.contains("--side-bar-bg-color"), "default light theme uses the bundled theme stylesheet");
  exportTo("PDF", f.directory.filePath("note.pdf"));
  const auto image = f.directory.filePath("note.png");
  exportTo("Image (PNG)", image);
  require(QImage(image).width() == 1280, "image menu consumes saved width and scale");
  QSettings().setValue("export/pandocPath", QCoreApplication::applicationFilePath());
  QSettings().setValue("export/docx/arguments", "--metadata title=Test");
  const auto word = f.directory.filePath("note.docx");
  exportTo("Word (.docx)", word);
  QFile converted(word);
  require(converted.open(QIODevice::ReadOnly), "read fake converter output");
  require(converted.readAll().contains("title=Test"), "Word menu passes its saved arguments");
  converted.close();
  QSettings().setValue("export/docx/arguments", "--fake-export-failure");
  QTimer dismiss;
  dismiss.setInterval(5);
  QObject::connect(&dismiss, &QTimer::timeout, &f.window, [] {
    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
      box->accept();
  });
  // Choose the existing file and suppress only the expected failure message.
  QTimer choose;
  choose.setInterval(5);
  QObject::connect(&choose, &QTimer::timeout, &f.window, [&] {
    if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
      choose.stop();
      acceptSaveFile(*dialog, word);
    }
  });
  dismiss.start();
  choose.start();
  action(f.window, "Word (.docx)")->trigger();
  dismiss.stop();
  choose.stop();
  require(converted.open(QIODevice::ReadOnly) && converted.readAll().contains("title=Test"),
          "failed conversion preserves an existing destination");
  QSettings().remove("export/pandocPath");
  QSettings().remove("export/docx/arguments");
}

void searchPreferences() {
  SettingsOverride extensions("search/extensions", "md"), ignored("search/ignorePatterns", "build/"),
      hidden("search/includeHidden", false), git("search/readGitIgnore", true), ignore("search/readIgnore", true),
      size("search/maxFileBytes", 100 * 1024 * 1024), regex("search/maxRegexBytes", 32 * 1024 * 1024),
      category("preferences/lastCategory", "search"), openingExtensions("files/textExtensions", "cpp"),
      openingLimit("files/sizeLimitBytes", 1024);
  PreferencesDialog preferences;
  auto* extensionEdit = preferences.findChild<QLineEdit*>("SearchExtensions");
  auto* patterns = preferences.findChild<QPlainTextEdit*>("SearchIgnorePatterns");
  auto* maxSize = preferences.findChild<QSpinBox*>("SearchMaxFileMiB");
  auto* maxRegex = preferences.findChild<QSpinBox*>("SearchMaxRegexMiB");
  require(extensionEdit && patterns && maxSize && maxRegex, "search preference controls exist");
  preferences.show();
  QApplication::processEvents();
  require(extensionEdit->isVisible() && patterns->isVisible(), "search category restores by stable key");
  extensionEdit->setText("*.MD, txt");
  patterns->setPlainText("drafts/\n*.bak");
  maxSize->setValue(12);
  maxRegex->setValue(6);
  preferences.findChild<QCheckBox*>("SearchReadGitIgnore")->setChecked(false);
  preferences.findChild<QCheckBox*>("SearchIncludeHidden")->setChecked(true);
  const auto settings = SearchSettings::load();
  require(settings.extensions == QStringList({"md", "txt"}) && settings.scan.ignorePatterns == "drafts/\n*.bak" &&
              settings.scan.includeHidden && !settings.scan.readGitIgnore && settings.scan.readIgnore &&
              settings.maxFileBytes == 12 * 1024 * 1024 && settings.maxRegexBytes == 6 * 1024 * 1024,
          "all search controls save immediately without changing opening protection");
  require(QSettings().value("files/textExtensions").toString() == "cpp" &&
              QSettings().value("files/sizeLimitBytes").toLongLong() == 1024,
          "file protection preferences remain independent");
  const auto screenshots = qEnvironmentVariable("MUFFIN_SEARCH_PREFS_SCREENSHOT_DIR");
  for (const auto& theme : {QStringLiteral("github"), QStringLiteral("night")}) {
    const auto definition = ThemeDefinition::builtIn(theme).value();
    preferences.setThemeDefinition(definition);
    QApplication::processEvents();
    require(patterns->palette().color(QPalette::Text) == definition.colors.chromeText &&
                patterns->palette().color(QPalette::Base) == definition.colors.surface,
            "multiline rules follow the theme");
    if (!screenshots.isEmpty()) {
      QDir().mkpath(screenshots);
      preferences.grab().save(QDir(screenshots).filePath(theme + ".png"));
    }
  }
  preferences.findChild<QPushButton*>("SearchResetDefaults")->click();
  const auto reset = SearchSettings::load();
  require(reset.maxFileBytes == 100 * 1024 * 1024 && reset.maxRegexBytes == 32 * 1024 * 1024 &&
              reset.scan.readGitIgnore && reset.scan.readIgnore && !reset.scan.includeHidden &&
              reset.scan.ignorePatterns == WorkspaceScanOptions::defaultIgnorePatterns() &&
              reset.extensions.contains("md") && !reset.extensions.contains("cpp"),
          "restore every search default");
}

void exportNumberControls() {
  QSettings().setValue("preferences/lastCategory", "export");
  QSettings().setValue("export/lastPage", 1);
  QSettings().setValue("export/pdf/paper", "A4");
  const auto captures = qEnvironmentVariable("MUFFIN_EXPORT_NUMBER_SCREENSHOT_DIR");
  const auto originalLanguage = LanguageManager::instance().currentLanguageCode();
  const auto restoreLanguage = qScopeGuard([&] { LanguageManager::instance().setLanguage(originalLanguage); });
  if (!captures.isEmpty()) {
    QDir().mkpath(captures);
    LanguageManager::instance().setLanguage("zh_CN");
  }
  PreferencesDialog preferences;
  auto* page = preferences.findChild<PrefsExportPage*>();
  auto* paper = page->findChild<QComboBox*>("ExportPdfPaper");
  auto* margin = page->findChild<QDoubleSpinBox*>("ExportPdfMargin");
  auto* font = page->findChild<QSpinBox*>("ExportPdfFontSize");
  auto* width = page->findChild<QDoubleSpinBox*>("ExportPdfWidth");
  for (const auto& theme : {QStringLiteral("github"), QStringLiteral("night")}) {
    preferences.setThemeDefinition(ThemeDefinition::builtIn(theme).value());
    preferences.show();
    QApplication::processEvents();
    paper->setCurrentIndex(paper->findData("A4"));
    require(!width->isEnabled(), "preset paper dimensions remain disabled");
    margin->setValue(15);
    font->setValue(16);
    for (auto* spin : {static_cast<QAbstractSpinBox*>(margin), static_cast<QAbstractSpinBox*>(font)}) {
      QStyleOptionSpinBox option;
      option.initFrom(spin);
      option.buttonSymbols = spin->buttonSymbols();
      const QRect up = spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin);
      const QRect down = spin->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, spin);
      require(spin->rect().contains(up) && spin->rect().contains(down) && !up.intersects(down) &&
                  up.center().y() < down.center().y(),
              "stacked step buttons fit the field and have distinct click targets");
      const auto value = [spin, margin, font] { return spin == margin ? margin->value() : font->value(); };
      const double before = value();
      QTest::mouseClick(spin, Qt::LeftButton, Qt::NoModifier, up.center());
      require(value() == before + 1, "upper chevron increments the value");
      QTest::mouseClick(spin, Qt::LeftButton, Qt::NoModifier, down.center());
      require(value() == before, "lower chevron decrements the value");
      QTest::keyClick(spin, Qt::Key_Up);
      require(value() == before + 1, "keyboard stepping is retained");
      QTest::keyClick(spin, Qt::Key_Down);
      require(value() == before, "keyboard stepping reverses the change");
    }
    require(QSettings().value("export/pdf/margin").toDouble() == margin->value() &&
                QSettings().value("export/pdf/fontSize").toInt() == font->value(),
            "button and keyboard edits retain immediate preference persistence");
    margin->setValue(margin->minimum());
    QTest::keyClick(margin, Qt::Key_Down);
    require(margin->value() == margin->minimum(), "stepping still respects numeric bounds");
    margin->setValue(15);
    font->clearFocus();
    preferences.setFocus();
    QTest::mouseMove(&preferences, QPoint(5, 5));
    QApplication::processEvents();
    if (!captures.isEmpty())
      require(preferences.grab().save(QDir(captures).filePath(theme + ".png")), "capture export number controls");
    paper->setCurrentIndex(paper->findData("Custom"));
    require(width->isEnabled(), "custom page dimensions remain editable");
  }
}

void exportPathRowsFitControls() {
  QSettings().setValue("preferences/lastCategory", "export");
  QSettings().setValue("export/lastPage", 0);
  PreferencesDialog preferences;
  auto* page = preferences.findChild<PrefsExportPage*>();
  auto* formats = page->findChild<QListWidget*>("ExportFormats");
  const auto originalLanguage = LanguageManager::instance().currentLanguageCode();
  const auto restoreLanguage = qScopeGuard([&] {
    LanguageManager::instance().setLanguage(originalLanguage);
    refreshDeclarations();
  });
  const auto captures = qEnvironmentVariable("MUFFIN_EXPORT_PATH_SCREENSHOT_DIR");
  if (!captures.isEmpty())
    QDir().mkpath(captures);
  for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh_CN")}) {
    LanguageManager::instance().setLanguage(language);
    QApplication::processEvents();
    for (const auto& theme : {QStringLiteral("github"), QStringLiteral("night")}) {
      preferences.setThemeDefinition(ThemeDefinition::builtIn(theme).value());
      for (const auto size : {QSize(1040, 720), QSize(880, 620)}) {
        preferences.resize(size);
        preferences.show();
        for (int row : {0, 5, 6, 8, 0}) {
          formats->setCurrentRow(row);
          if (row == 0) {
            auto* folder = page->findChild<QComboBox*>("ExportDefaultFolder");
            folder->setCurrentIndex(folder->currentIndex() == 0 ? 1 : 0);
          }
          QTest::qWait(20);
          for (const auto* name :
               {"ExportCustomFolder", "ExportPandocPath", "ExportReferenceDocument", "ExportEpubCss"}) {
            auto* field = page->findChild<QLineEdit*>(name);
            if (!field)
              continue;
            auto* container = field->parentWidget();
            auto* browse = container->findChild<QPushButton*>();
            require(field->isVisible() && browse, "rebuilt path field is visible with its browse action");
            for (const auto* control : {static_cast<QWidget*>(field), static_cast<QWidget*>(browse)}) {
              require(container->rect().contains(control->geometry()),
                      QStringLiteral("%1 fits its row (%2, %3, width=%4): row height=%5, control y=%6 height=%7")
                          .arg(name, language, theme)
                          .arg(size.width())
                          .arg(container->height())
                          .arg(control->y())
                          .arg(control->height()));
              require(control->height() >= control->minimumSizeHint().height(),
                      "path controls retain their style-derived minimum height");
            }
          }
          if (!captures.isEmpty() && row == 0 && size.width() == 1040)
            require(preferences.grab().save(QDir(captures).filePath(language + '-' + theme + ".png")),
                    "capture export path controls");
        }
      }
    }
  }
}

void noteLinkRewriting() {
  const QString root = QDir::tempPath() + QStringLiteral("/muffin-notes");
  const auto oldPath = root + QStringLiteral("/notes/方案.md");
  const auto newPath = root + QStringLiteral("/archive/new name.md");
  const auto source = root + QStringLiteral("/index.md");
  const QString before = QStringLiteral("# 项目总览\n\n[方案](notes/方案.md#第一节 \"保留标题\")\n\n"
                                        "[引用][plan]\n\n[plan]: <notes/方案.md#第一节> \"定义标题\"\n\n"
                                        "![图片](notes/方案.md)\n\n[网页](https://example.com/notes/方案.md)\n\n"
                                        "`[代码](notes/方案.md)`\n\n```md\n[代码](notes/方案.md)\n```\n\n"
                                        "[本页](#项目总览)\n\n[附件](assets/a.png)\n");
  int count = 0;
  const auto after = NoteLinks::rewrite(before, source, oldPath, newPath, &count);
  require(count == 3, QStringLiteral("rewrite inline, definition and image only, got %1: %2").arg(count).arg(after));
  require(after.contains(QStringLiteral("[方案](archive/new%20name.md#%E7%AC%AC%E4%B8%80%E8%8A%82 \"保留标题\")")),
          "encoded path and fragment keep link title");
  require(after.contains(QStringLiteral("[plan]: <archive/new%20name.md#%E7%AC%AC%E4%B8%80%E8%8A%82> \"定义标题\"")),
          "reference definition keeps angle delimiters and title");
  require(after.contains(QStringLiteral("`[代码](notes/方案.md)`")) &&
              after.contains(QStringLiteral("```md\n[代码](notes/方案.md)")),
          "code examples unchanged");
  const auto outgoing = NoteLinks::rewrite(QStringLiteral("[总览](../index.md)\n\n![图](images/a.png)\n"), oldPath,
                                           oldPath, newPath, &count);
  require(count == 1 && outgoing.contains(QStringLiteral("../notes/images/a.png")),
          "moving a note rebases its outgoing images");
  const auto folder =
      NoteLinks::rewrite(QStringLiteral("[内链](方案.md)\n\n[外链](../index.md)\n"), oldPath,
                         root + QStringLiteral("/notes"), root + QStringLiteral("/deep/archive"), &count);
  require(count == 1 && folder.contains(QStringLiteral("[内链](方案.md)")) &&
              folder.contains(QStringLiteral("../../index.md")),
          "folder move preserves internal links and rebases outgoing ones");
  const auto nested =
      NoteLinks::rewrite(QStringLiteral("[![a](notes/方案.md)](notes/方案.md)\n\n[x\\] y](notes/方案.md)\n"), source,
                         oldPath, newPath, &count);
  require(count == 3, QStringLiteral("nested images and escaped labels use source destinations: %1").arg(nested));
  const auto codeLabels =
      NoteLinks::rewrite(QStringLiteral("[`a](notes/方案.md) b`](notes/方案.md)\n\n[`[unbalanced`](notes/方案.md)\n"),
                         source, oldPath, newPath, &count);
  require(count == 2 && codeLabels.contains(QStringLiteral("`a](notes/方案.md) b`")) &&
              codeLabels.contains(QStringLiteral("`[unbalanced`")),
          "bracket-like code inside link labels is never rewritten as a destination");
  QTemporaryDir encodedFolder;
  const auto encodedPath = encodedFolder.filePath("index.md");
  TextFileFormat format;
  format.encodingName = QStringLiteral("UTF-16LE");
  format.writeBom = true;
  format.lineEnding = TextLineEnding::Crlf;
  QByteArray originalBytes;
  require(FileController::encodeTextBytes(QStringLiteral("# Note\n\n[link](old.md)\n"), format, &originalBytes),
          "encode UTF-16 note");
  QFile encodedFile(encodedPath);
  require(encodedFile.open(QIODevice::WriteOnly), "create encoded note");
  require(encodedFile.write(originalBytes) == originalBytes.size(), "write UTF-16 note");
  encodedFile.close();
  const auto encoded = NoteLinks::prepare(encodedFolder.path(), encodedFolder.filePath("old.md"),
                                          encodedFolder.filePath(QStringLiteral("新 方案.md")), {}, {});
  require(encoded.complete && encoded.changes.size() == 1 && encoded.changes[0].file.before == originalBytes,
          "encoding-aware preview retains exact original bytes");
  QString decoded;
  TextFileFormat detected;
  require(FileController::decodeTextBytes(encoded.changes[0].file.after, &decoded, &detected) &&
              detected.encodingName == format.encodingName && detected.writeBom &&
              detected.lineEnding == TextLineEnding::Crlf,
          "link updates preserve UTF-16 BOM and CRLF");
}

void noteLinkNavigation() {
  Fixture f;
  const auto aPath =
      f.file(QStringLiteral("总览.md"),
             QStringLiteral("# 总览\n\n") + QStringLiteral("阅读段落 [方案](方案%20一.md#重复-1)\n\n").repeated(180));
  const auto bPath = f.file(QStringLiteral("方案 一.md"),
                            QStringLiteral("# 开头\n\n") + QStringLiteral("背景\n\n").repeated(160) +
                                QStringLiteral("## 重复\n\n一\n\n## 重复\n\n二\n\n## Hello World!\n\n结尾\n"));
  auto* a = f.open(aPath);
  a->editorController.setCursorForSourceOffset(200);
  a->renderView->verticalScrollBar()->setValue(a->renderView->verticalScrollBar()->maximum() / 2);
  QApplication::processEvents();
  const auto original = a->renderView->viewportPosition();
  QPoint linkPoint(-1, -1);
  for (int y = 4; y < a->renderView->viewport()->height() && linkPoint.x() < 0; y += 8)
    for (int x = 4; x < a->renderView->viewport()->width(); x += 8)
      if (!a->renderView->hitTest(QPointF(x, y)).linkHref.isEmpty()) {
        linkPoint = QPoint(x, y);
        break;
      }
  require(linkPoint.x() >= 0, "find rendered link in reading viewport");
  QTest::mouseClick(a->renderView->viewport(), Qt::LeftButton, Qt::ControlModifier, linkPoint);
  waitUntil(
      [&] {
        return f.current() != a && !f.current()->session.isOpenInProgress() && action(f.window, "Back")->isEnabled();
      },
      "local link opens and completes navigation");
  auto* b = f.current();
  require(b->session.filePath() == bPath && !b->backend->isSourceMode(), "local Markdown uses live preview in a tab");
  const auto heading = NoteLinks::headingTarget(b->session.document().outline(), QStringLiteral("重复-1"));
  require(b->editorController.selection().cursorPosition().blockId == heading,
          "duplicate Chinese heading anchor selected");
  action(f.window, "Back")->trigger();
  waitUntil([&] { return f.current() == a && action(f.window, "Forward")->isEnabled(); },
            "back returns to source note");
  const auto restored = a->renderView->viewportPosition();
  require(qAbs(restored.sourceOffset - original.sourceOffset) < 2 &&
              qAbs(restored.screenOffset - original.screenOffset) < 2,
          "back preserves reading paragraph and pixel offset");
  action(f.window, "Forward")->trigger();
  waitUntil([&] { return f.current() == b && action(f.window, "Back")->isEnabled(); },
            "forward returns to destination");
  b->renderView->linkActivated(QStringLiteral("#hello-world"));
  QApplication::processEvents();
  require(b->editorController.selection().cursorPosition().blockId ==
              NoteLinks::headingTarget(b->session.document().outline(), "hello-world"),
          "current-file heading anchor resolves formatted slug");
  b->renderView->linkActivated(QStringLiteral("missing.md#标题"));
  QApplication::processEvents();
  require(f.current() == b && f.tabs->count() == 2, "missing local target does not open an empty tab");
  f.close(a);
  action(f.window, "Back")->trigger();
  QApplication::processEvents();
  action(f.window, "Back")->trigger();
  waitUntil([&] { return f.current()->session.filePath() == aPath && action(f.window, "Forward")->isEnabled(); },
            "history reopens a closed note");
  auto* reopened = f.current();
  action(f.window, "Source Code Mode")->trigger();
  require(reopened->backend->isSourceMode(), "switch origin to source mode");
  reopened->editor->setCursorPosition(800);
  auto* sourceView = reopened->editor->findChild<VirtualSourceEdit*>();
  sourceView->verticalScrollBar()->setValue(sourceView->verticalScrollBar()->maximum() / 3);
  const auto sourcePosition = reopened->editor->viewportPosition();
  reopened->renderView->linkActivated(QStringLiteral("方案%20一.md#Hello%20World!"));
  waitUntil([&] { return f.current() == b && action(f.window, "Back")->isEnabled(); },
            "raw heading title and existing destination tab resolve");
  action(f.window, "Back")->trigger();
  waitUntil([&] { return f.current() == reopened && action(f.window, "Forward")->isEnabled(); },
            "return from live preview to source note");
  require(reopened->backend->isSourceMode() &&
              qAbs(reopened->editor->viewportPosition().sourceOffset - sourcePosition.sourceOffset) < 2,
          "history preserves source mode and reading location");
}

void noteLinkRenamePreview() {
  for (bool sourceMode : {false, true}) {
    Fixture f;
    const auto target = f.file(QStringLiteral("方案.md"), QStringLiteral("# 方案\n\n正文\n"));
    const auto index = f.file(QStringLiteral("总览.md"), QStringLiteral("# 总览\n\n[方案](方案.md#方案)\n"));
    auto* source = f.open(index);
    if (sourceMode)
      action(f.window, "Source Code Mode")->trigger();
    f.window.openFolderAtPath(f.directory.path());
    auto* sidebar = f.window.findChild<SidebarWidget*>();
    const auto answer = [&](const QString& button) {
      auto* timer = new QTimer(&f.window);
      timer->setInterval(5);
      QObject::connect(timer, &QTimer::timeout, &f.window, [&, timer, button] {
        auto* dialog = f.window.findChild<QDialog*>(QStringLiteral("NoteLinkPreview"));
        if (!dialog || !dialog->isVisible())
          return;
        require(dialog->findChild<QTreeWidget*>("NoteLinkFiles")->topLevelItemCount() == 1,
                "preview lists affected note");
        require(!dialog->findChild<QPlainTextEdit*>("NoteLinkDiff")->toPlainText().isEmpty(),
                "preview shows actual changed links");
        if (qEnvironmentVariableIsSet("MUFFIN_NOTE_LINK_SCREENSHOT"))
          dialog->grab().save(qEnvironmentVariable("MUFFIN_NOTE_LINK_SCREENSHOT"));
        timer->stop();
        if (button.isEmpty())
          dialog->reject();
        else
          dialog->findChild<QPushButton*>(button)->click();
        timer->deleteLater();
      });
      timer->start();
    };
    answer({});
    sidebar->inlineCommitRequested(InlineEditContext{target, false, false}, QStringLiteral("新方案.md"));
    require(QFileInfo::exists(target), "canceling preview leaves filename unchanged");
    answer(QStringLiteral("NoteLinkApply"));
    sidebar->inlineCommitRequested(InlineEditContext{target, false, false}, QStringLiteral("新方案.md"));
    const auto renamed = f.directory.filePath(QStringLiteral("新方案.md"));
    require(QFileInfo::exists(renamed) && !QFileInfo::exists(target), "rename completes after update preview");
    require(source->session.markdownText().toString().contains(
                QStringLiteral("%E6%96%B0%E6%96%B9%E6%A1%88.md#%E6%96%B9%E6%A1%88")),
            "open clean note reloads updated link");
    require(!source->session.document().isModified(), "clean note stays clean after disk update");
    if (sourceMode) {
      source->editor->moveDocumentEnd();
      source->editor->insertText(QStringLiteral("\n未保存草稿\n"));
    } else {
      source->editorController.applyMarkdownTextWithUndo(
          source->session.markdownText().toString() + QStringLiteral("\n未保存草稿\n"), "draft");
    }
    answer(QStringLiteral("NoteLinkApply"));
    const auto priorAnchor = source->editor->anchorPosition(), priorFocus = source->editor->cursorPosition();
    sidebar->inlineCommitRequested(InlineEditContext{renamed, false, false}, QStringLiteral("最终.md"));
    require(source->session.document().isModified() &&
                source->session.markdownText().toString().contains(QStringLiteral("未保存草稿")),
            "dirty note updates in memory without losing draft");
    QFile disk(index);
    require(disk.open(QIODevice::ReadOnly), "read note on disk");
    const auto diskText = QString::fromUtf8(disk.readAll());
    require(!diskText.contains(QStringLiteral("未保存草稿")) && diskText.contains(QStringLiteral("%E6%96%B0")),
            "dirty note is not silently saved");
    source->backend->undo();
    require(source->session.markdownText().toString().contains(QStringLiteral("%E6%96%B0")),
            "unsaved link update is one undoable step");
    if (sourceMode)
      require(source->editor->anchorPosition() == priorAnchor && source->editor->cursorPosition() == priorFocus,
              "source undo restores the selection before the batch update");
  }
}

void noteLinkMoveAndConflicts() {
  Fixture f;
  require(QDir().mkpath(f.directory.filePath("old/images")) && QDir().mkpath(f.directory.filePath("deep/archive")),
          "create move folders");
  const auto target =
      f.file("old/plan.md", QStringLiteral("# Plan\n\n[Home](../index.md#home)\n\n![image](images/a.png)\n"));
  const auto index = f.file("index.md", QStringLiteral("# Home\n\n[plan](old/plan.md#plan)\n"));
  auto* moved = f.open(target);
  f.window.openFolderAtPath(f.directory.path());
  const auto destination = f.directory.filePath("deep/archive/plan.md");
  QTimer choose;
  choose.setInterval(5);
  QObject::connect(&choose, &QTimer::timeout, &f.window, [&] {
    if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
      dialog->selectFile(destination);
      QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
      choose.stop();
    }
  });
  QTimer apply;
  apply.setInterval(5);
  QObject::connect(&apply, &QTimer::timeout, &f.window, [&] {
    auto* dialog = f.window.findChild<QDialog*>("NoteLinkPreview");
    if (!dialog || !dialog->isVisible())
      return;
    require(dialog->findChild<QTreeWidget*>("NoteLinkFiles")->topLevelItemCount() == 2,
            "move preview includes incoming and outgoing links");
    apply.stop();
    dialog->findChild<QPushButton*>("NoteLinkApply")->click();
  });
  choose.start();
  apply.start();
  action(f.window, "Move To...")->trigger();
  require(moved->session.filePath() == destination && QFileInfo::exists(destination) && !QFileInfo::exists(target),
          "move updates the open tab and disk path");
  require(moved->session.markdownText().toString().contains("../../old/images/a.png") &&
              moved->session.markdownText().toString().contains("../../index.md#home"),
          "moved note retains image and outgoing heading targets");
  QFile incoming(index);
  require(incoming.open(QIODevice::ReadOnly), "read incoming links after move");
  require(incoming.readAll().contains("deep/archive/plan.md#plan"), "incoming links follow moved note");
  incoming.close();

  // Change a note on disk after preview; this must abort before renaming anything.
  QTimer conflict;
  conflict.setInterval(5);
  QObject::connect(&conflict, &QTimer::timeout, &f.window, [&] {
    if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
      require(message->text().contains("changed on disk"), "conflicting disk changes are explained");
      conflict.stop();
      message->accept();
      return;
    }
    auto* dialog = f.window.findChild<QDialog*>("NoteLinkPreview");
    if (!dialog || !dialog->isVisible())
      return;
    QFile changed(index);
    require(changed.open(QIODevice::WriteOnly | QIODevice::Truncate), "edit disk after preview");
    changed.write("external edit\n");
    changed.close();
    dialog->findChild<QPushButton*>("NoteLinkApply")->click();
  });
  conflict.start();
  f.window.findChild<SidebarWidget*>()->inlineCommitRequested(InlineEditContext{destination, false, false}, "other.md");
  require(QFileInfo::exists(destination) && !QFileInfo::exists(f.directory.filePath("deep/archive/other.md")),
          "stale preview leaves path unchanged");
  require(incoming.open(QIODevice::ReadOnly) && incoming.readAll() == "external edit\n",
          "stale preview preserves externally edited bytes");
}

void shortcutPreferencesFollowThemeAndConfirmReset() {
  const QPalette originalPalette = QApplication::palette();
  const auto restorePalette = qScopeGuard([&] { QApplication::setPalette(originalPalette); });
  auto& shortcuts = ShortcutSettings::instance();
  const auto custom = QKeySequence(QStringLiteral("Ctrl+Alt+F12"));
  const QString captureDirectory = qEnvironmentVariable("MUFFIN_SHORTCUT_SCREENSHOT_DIR");
  const auto originalLanguage = LanguageManager::instance().currentLanguageCode();
  const auto restoreLanguage = qScopeGuard([&] {
    LanguageManager::instance().setLanguage(originalLanguage);
    refreshDeclarations();
  });
  if (!captureDirectory.isEmpty()) {
    QDir().mkpath(captureDirectory);
    LanguageManager::instance().setLanguage(QStringLiteral("zh_CN"));
    refreshDeclarations();
  }

  // Keep the same dialog alive through a theme change, with an OS palette
  // deliberately opposed to each document theme. Test both its view and popup.
  PreferencesDialog preferences;
  preferences.findChild<QListWidget*>()->setCurrentRow(8);
  auto* filter = preferences.findChild<QLineEdit*>("ShortcutFilter");
  auto* commands = preferences.findChild<QTreeWidget*>("ShortcutCommands");
  auto* sequence = preferences.findChild<QKeySequenceEdit*>("ShortcutSequence");
  auto* apply = preferences.findChild<QPushButton*>("ShortcutApply");
  auto* reset = preferences.findChild<QPushButton*>("ShortcutReset");
  require(filter && commands && sequence && apply && reset, "shortcut preference controls exist");
  for (const auto& themeId : {QStringLiteral("github"), QStringLiteral("night"), QStringLiteral("newsprint")}) {
    const auto definition = ThemeDefinition::builtIn(themeId).value();
    const auto& colors = definition.colors;
    QApplication::setPalette(dialogPalette(
        ThemeDefinition::builtIn(colors.isDark ? QStringLiteral("github") : QStringLiteral("night")).value()));
    preferences.setThemeDefinition(definition);
    preferences.resize(1040, 760);
    preferences.show();
    QApplication::processEvents();
    require(commands->palette().color(QPalette::Text) == colors.chromeText &&
                commands->viewport()->palette().color(QPalette::Base) == colors.surface,
            "command list uses the document theme instead of the operating system palette");
    require(commands->palette().color(QPalette::Highlight) == colors.chromeSelection &&
                commands->palette().color(QPalette::HighlightedText) == colors.chromeSelectionText,
            "selected command uses a readable paired background and text");
    require(commands->visualItemRect(commands->topLevelItem(0)).height() >= commands->fontMetrics().height() + 8,
            "command rows have readable vertical spacing");
    const auto* input = sequence->findChild<QLineEdit*>();
    require(input && input->palette().color(QPalette::Text) == colors.chromeText &&
                input->palette().color(QPalette::Base) == colors.surface,
            "shortcut recorder follows the same theme as the command list");
    if (!captureDirectory.isEmpty())
      require(preferences.grab().save(QDir(captureDirectory).filePath(themeId + ".png")),
              "capture themed shortcut preferences");

    preferences.resize(880, 620);
    QApplication::processEvents();
    auto* editor = preferences.findChild<QWidget*>("ShortcutEditor");
    require(editor->rect().contains(QRect(apply->mapTo(editor, QPoint()), apply->size())) &&
                editor->rect().contains(QRect(sequence->mapTo(editor, QPoint()), sequence->size())),
            "recorder and apply action fit the minimum preferences width");

    shortcuts.setOverride("format.bold", custom);
    for (bool confirm : {false, true}) {
      bool handled = false;
      QTimer::singleShot(0, &preferences, [&] {
        auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(question && question->objectName() == "ShortcutResetConfirmation", "reset opens one confirmation");
        require(question->parentWidget() == &preferences, "reset confirmation belongs to the themed top-level dialog");
        require(question->width() >= 340, "reset confirmation leaves room for its explanation and action labels");
        if (!captureDirectory.isEmpty() && !confirm)
          require(question->grab().save(QDir(captureDirectory).filePath(themeId + ".reset.png")),
                  "capture themed reset confirmation");
        require(question->palette().color(QPalette::Window).rgba() == colors.canvas.rgba(),
                QStringLiteral("%1 reset surface: expected %2, got %3")
                    .arg(themeId, colors.canvas.name(QColor::HexArgb),
                         question->palette().color(QPalette::Window).name(QColor::HexArgb)));
        const auto* text = question->findChild<QLabel*>("qt_msgbox_label");
        require(text && text->palette().color(QPalette::WindowText) == colors.chromeText,
                "reset confirmation text remains readable in either theme");
        require(question->defaultButton() == question->button(QMessageBox::Cancel),
                "restoring all shortcuts requires an explicit confirmation");
        auto* restore = question->findChild<QPushButton*>("ShortcutResetAccept");
        require(restore && question->buttonRole(restore) == QMessageBox::AcceptRole,
                "restore is an affirmative action next to Cancel");
        handled = true;
        if (confirm)
          restore->click();
        else
          QTest::keyClick(question, Qt::Key_Escape);
      });
      reset->click();
      require(handled, "reset confirmation was handled");
      require(shortcuts.hasOverride("format.bold") != confirm,
              "cancel preserves custom shortcuts and confirmation removes them");
      require(QSettings().contains("shortcuts/format.bold") != confirm,
              "reset decision is also reflected in persisted settings");
    }
    filter->setText(QStringLiteral("no-such-shortcut-command"));
    require(commands->topLevelItemCount() == 0 && !sequence->isEnabled() && !apply->isEnabled(),
            "empty search cannot modify an unrelated command");
    filter->clear();
  }
}

void lockedEditingPreservesUndoAndRejectsChanges() {
  SettingsOverride enabled("editor/lockEditingEnabled", true);
  SettingsOverride scope("editor/lockEditingScope", 0);
  for (bool source : {false, true}) {
    Fixture f;
    auto* document = f.open(f.file("locked-history.md", "Alpha\n"));
    if (source)
      action(f.window, "Source Code Mode")->trigger();
    QWidget* input = source ? static_cast<QWidget*>(document->editor->findChild<VirtualSourceEdit*>())
                            : static_cast<QWidget*>(document->renderView);
    if (source) {
      document->editor->setCursorPosition(5);
      document->editor->insertText("B");
    } else {
      document->editorController.setCursorForSourceOffset(5);
      require(document->editorController.inputController().insertText("B"), "type before locking");
    }
    require(document->session.markdownText().toString() == "AlphaB\n" && document->backend->canUndo(),
            "typing produces one undo step");
    auto* lock = f.window.findChild<QAction*>("edit.lock_editing");
    require(lock && lock->shortcut() == QKeySequence("F12"), "lock is a registered command");
    lock->trigger();
    require(f.window.editingLocked() && document->session.editingReadOnly() && document->editor->isReadOnly(),
            "both editors observe session lock");
    const auto selection = document->editorController.selection().selection();
    const auto sourceCursor = document->editor->cursorPosition();
    require(!action(f.window, "Undo")->isEnabled() && !action(f.window, "Paste")->isEnabled() &&
                !action(f.window, "Bold")->isEnabled() && action(f.window, "Copy")->isEnabled() &&
                action(f.window, "Find...")->isEnabled(),
            "lock disables editing commands and preserves reading commands");
    require(!action(f.window, "Bold")->toolTip().isEmpty(), "disabled editing commands explain the lock");
    action(f.window, "Undo")->trigger();
    document->editorController.undo(); // Defensive boundary, even when bypassing the menu.
    document->editor->undo();
    document->editorController.applyMarkdownTextWithUndo("Rejected\n", "Rejected replacement");
    document->editor->replaceAllWithUndo("Rejected\n");
    require(!document->session.applyMarkdownText("Rejected\n", true) &&
                !document->session.applyTextDelta(0, 0, "Rejected", true),
            "full and incremental edits explicitly report rejection");
    input->setFocus();
    QApplication::processEvents();
    QTest::keyClick(input, Qt::Key_Backspace);
    QTest::keyClick(input, Qt::Key_Return);
    QTest::keyClick(input, Qt::Key_Tab);
    QTest::keyClick(input, Qt::Key_A);
    QInputMethodEvent ime;
    ime.setCommitString(QStringLiteral("中文"));
    QApplication::sendEvent(input, &ime);
    require(document->session.markdownText().toString() == "AlphaB\n" &&
                document->editor->cursorPosition() == sourceCursor &&
                document->editorController.selection().selection().focus.blockId == selection.focus.blockId &&
                document->editorController.selection().selection().focus.text.sourceOffset ==
                    selection.focus.text.sourceOffset,
            "rejected typing, IME, deletion and replacement do not move the caret or change text");
    lock->trigger();
    require(document->backend->canUndo() && !document->backend->canRedo(), "rejected undo preserves history");
    action(f.window, "Undo")->trigger();
    require(document->session.markdownText().toString() == "Alpha\n", "unlock permits the original undo");
    lock->trigger();
    action(f.window, "Redo")->trigger();
    lock->trigger();
    action(f.window, "Redo")->trigger();
    require(document->session.markdownText().toString() == "AlphaB\n", "redo inserts the character exactly once");
    if (!source) {
      document->editorController.applyMarkdownTextWithUndo("Snapshot\n", "Snapshot replacement");
      lock->trigger();
      document->editorController.undo();
      lock->trigger();
      document->editorController.undo();
      require(document->session.markdownText().toString() == "AlphaB\n", "snapshot history also survives locking");
    }
  }
}

void lockedEditingScopesSurviveLoading() {
  SettingsOverride enabled("editor/lockEditingEnabled", true);
  for (bool perTab : {true, false}) {
    SettingsOverride scope("editor/lockEditingScope", perTab ? 0 : 1);
    Fixture f;
    auto* first = f.open(f.file("locked-a.md", "First\n"));
    action(f.window, "Source Code Mode")->trigger();
    f.window.setEditingLocked(true);
    auto* second = f.open(f.file("locked-b.md", "Second\n"));
    require(second->session.editingReadOnly() == !perTab && second->editor->isReadOnly() == !perTab,
            "newly loaded tab respects the configured lock scope");
    f.tabs->setCurrentWidget(first);
    require(f.window.editingLocked() && first->editor->isReadOnly(), "returning to a locked tab preserves its lock");
    first->session.setOpenConfirmationPending(true);
    f.window.setEditingLocked(false);
    require(first->editor->isReadOnly() && !first->session.isEditingAllowed(),
            "unlocking does not remove loading protection");
    require(!first->session.applyMarkdownText("Wrong buffer", true), "loading refuses full-parse edit fallback");
    first->session.setOpenConfirmationPending(false);
    require(!first->editor->isReadOnly(), "loading protection releases after confirmation ends");
    first->session.openDocumentAsync("Reloaded\n");
    f.window.setEditingLocked(true);
    waitUntil([&] { return !first->session.isAsyncParseInProgress(); }, "locked reload finishes");
    require(first->editor->isReadOnly() && first->session.markdownText().toString() == "Reloaded\n",
            "async completion retains the user lock while allowing file loading");
    const auto cursor = first->editor->cursorPosition();
    first->editor->insertText("Rejected");
    require(!first->editor->canUndo() && first->editor->cursorPosition() == cursor,
            "locked source input cannot create phantom undo records");
    f.window.setEditingLocked(false);
    require(!first->editor->isReadOnly(), "explicit unlock restores editing");
  }
}

void lockedEditingPreferencesSyncAcrossWindows() {
  SettingsOverride enabled("editor/lockEditingEnabled", true);
  SettingsOverride scope("editor/lockEditingScope", 0);
  Fixture first;
  Fixture second;
  first.open(first.file("first-window.md", "First\n"));
  first.open(first.file("other-tab.md", "Other\n"));
  first.window.setEditingLocked(true);
  second.window.setEditingLocked(true);
  const auto editPreferences = [&](const std::function<void(PreferencesDialog&)>& change) {
    QTimer::singleShot(0, &first.window, [&] {
      auto* dialog = first.window.findChild<PreferencesDialog*>();
      require(dialog, "lock preferences dialog exists");
      change(*dialog);
      dialog->accept();
    });
    action(first.window, "Preferences...")->trigger();
  };
  const auto selectScope = [&](const QString& label) {
    editPreferences([&](PreferencesDialog& dialog) {
      for (auto* radio : dialog.findChildren<QRadioButton*>()) {
        if (radio->text() == label) {
          radio->setChecked(true);
          return;
        }
      }
      require(false, "lock scope radio exists");
    });
  };
  selectScope("One lock for the whole window");
  require(!first.window.lockEditingPerTab() && !second.window.lockEditingPerTab(),
          "scope changes apply to all existing windows");
  for (int i = 0; i < first.tabs->count(); ++i)
    require(static_cast<DocumentTab*>(first.tabs->widget(i))->session.editingReadOnly(),
            "window scope locks every tab from the active tab's state");
  selectScope("Each tab locks on its own");
  require(first.window.lockEditingPerTab() && second.window.lockEditingPerTab() && first.window.editingLocked(),
          "returning to per-tab mode retains visible lock states in each window");
  editPreferences([](PreferencesDialog& dialog) {
    for (auto* check : dialog.findChildren<QCheckBox*>()) {
      if (check->text() == "Enable locked editing") {
        check->setChecked(false);
        return;
      }
    }
    require(false, "lock preference checkbox exists");
  });
  require(!first.window.lockEditingEnabled() && !second.window.lockEditingEnabled() && !first.window.editingLocked() &&
              !second.window.editingLocked(),
          "disabling the feature releases every window");
  for (auto* window : {&first.window, &second.window}) {
    auto* tabs = window->findChild<QTabWidget*>("DocumentTabs");
    for (int i = 0; i < tabs->count(); ++i)
      require(!static_cast<DocumentTab*>(tabs->widget(i))->editor->isReadOnly(), "all tabs release the user lock");
    require(!window->findChild<QAction*>("edit.lock_editing")->isEnabled(), "disabled lock command is unavailable");
  }
  Fixture later;
  require(!later.window.lockEditingEnabled() && !later.window.findChild<QAction*>("edit.lock_editing")->isEnabled(),
          "new windows inherit a disabled feature before command initialization");
}

void lockedEditingUsesShortcutPreferences() {
  SettingsOverride enabled("editor/lockEditingEnabled", true);
  SettingsOverride scope("editor/lockEditingScope", 0);
  auto& shortcuts = ShortcutSettings::instance();
  const auto cleanup = qScopeGuard([&] {
    for (const auto& id : {"edit.lock_editing", "edit.select_all", "edit.copy"})
      shortcuts.clearOverride(QLatin1String(id));
  });
  for (bool source : {false, true}) {
    Fixture f;
    auto* document = f.open(f.file("locked-shortcuts.md", "Read this\n"));
    if (source)
      action(f.window, "Source Code Mode")->trigger();
    else
      document->editorController.setCursorForSourceOffset(0);
    QWidget* input = source ? static_cast<QWidget*>(document->editor->findChild<VirtualSourceEdit*>())
                            : static_cast<QWidget*>(document->renderView);
    PrefsShortcutsPage page;
    auto* filter = page.findChild<QLineEdit*>("ShortcutFilter");
    auto* sequence = page.findChild<QKeySequenceEdit*>("ShortcutSequence");
    auto* apply = page.findChild<QPushButton*>("ShortcutApply");
    const auto assign = [&](const QString& id, const QKeySequence& key) {
      filter->setText(id);
      sequence->setKeySequence(key);
      require(apply->isEnabled(), "shortcut preference accepts the new combination");
      apply->click();
    };
    assign("edit.lock_editing", QKeySequence("Ctrl+Alt+F9"));
    assign("edit.select_all", QKeySequence("Ctrl+Alt+F10"));
    assign("edit.copy", QKeySequence("Ctrl+Alt+F11"));
    input->setFocus();
    QApplication::processEvents();
    QTest::keyClick(input, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
    require(f.window.editingLocked(), "custom lock shortcut activates reading mode");
    QTest::keyClick(input, Qt::Key_F12);
    require(f.window.editingLocked(), "old lock shortcut is inactive after reassignment");
    QTest::keyClick(input, Qt::Key_F10, Qt::ControlModifier | Qt::AltModifier);
    QApplication::clipboard()->setText("sentinel");
    QTest::keyClick(input, Qt::Key_F11, Qt::ControlModifier | Qt::AltModifier);
    require(QApplication::clipboard()->text() == "Read this\n", "custom select-all and copy work while locked");
    assign("edit.lock_editing", QKeySequence());
    input->setFocus();
    QTest::keyClick(input, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
    require(f.window.editingLocked(), "clearing the shortcut removes it without releasing the lock");
    assign("edit.lock_editing", QKeySequence("F12"));
    input->setFocus();
    QTest::keyClick(input, Qt::Key_F12);
    require(!f.window.editingLocked(), "restoring the default shortcut unlocks the document");
    for (const auto& id : {"edit.lock_editing", "edit.select_all", "edit.copy"})
      shortcuts.clearOverride(QLatin1String(id));
  }
}

void lockedSourceInputPreservesReadingPosition() {
  SettingsOverride enabled("editor/lockEditingEnabled", true);
  Fixture f;
  const QString text = QStringLiteral("Read this paragraph.\n\n").repeated(1000);
  auto* document = f.open(f.file("locked-reading.md", text));
  action(f.window, "Source Code Mode")->trigger();
  auto* source = document->editor->findChild<VirtualSourceEdit*>();
  document->editor->setCursorPosition(0);
  f.window.setEditingLocked(true);
  source->setFocus();
  QApplication::processEvents();
  source->verticalScrollBar()->setValue(source->verticalScrollBar()->maximum() / 2);
  QApplication::processEvents();
  const int scroll = source->verticalScrollBar()->value();
  require(scroll > 0, "reading position is away from the source caret");
  for (auto key : {Qt::Key_A, Qt::Key_Backspace, Qt::Key_Return, Qt::Key_Tab})
    QTest::keyClick(source, key);
  require(document->session.markdownText().toString() == text && document->editor->cursorPosition() == 0 &&
              source->verticalScrollBar()->value() == scroll && !document->backend->canUndo(),
          "rejected input leaves text, history, caret and reading position unchanged");
}

void lockedEditingRetranslatesItsState() {
  SettingsOverride enabled("editor/lockEditingEnabled", true);
  const auto restore = qScopeGuard([] { LanguageManager::instance().setLanguage("en"); });
  Fixture f;
  f.open(f.file("translated-lock.md", "Text\n"));
  auto* lock = f.window.findChild<QAction*>("edit.lock_editing");
  require(lock->text() == "Lock Editing", "English menu starts with an English action");
  lock->trigger();
  require(lock->text() == "Unlock Editing" && f.window.windowTitle().endsWith(" (Editing Locked)"),
          "English lock state is translatable");
  LanguageManager::instance().setLanguage("zh_CN");
  QApplication::processEvents();
  require(lock->isChecked() && lock->text() == QStringLiteral("解锁编辑") &&
              !f.window.windowTitle().contains("Editing Locked"),
          "language change keeps the current action and title state");
  lock->trigger();
  LanguageManager::instance().setLanguage("en");
  QApplication::processEvents();
  require(!lock->isChecked() && lock->text() == "Lock Editing" && !f.window.windowTitle().contains("Editing Locked"),
          "unlocked state also survives retranslation");
}

void configurableShortcuts() {
  auto& shortcuts = ShortcutSettings::instance();
  for (bool source : {false, true}) {
    Fixture f;
    auto* document = f.open(f.file("shortcuts.md", QStringLiteral("中文 value\n")));
    if (source)
      action(f.window, "Source Code Mode")->trigger();
    QWidget* input = source ? static_cast<QWidget*>(document->editor->findChild<VirtualSourceEdit*>())
                            : static_cast<QWidget*>(document->renderView);
    const auto select = [&] {
      if (source)
        document->editor->setSelection(0, 2);
      else
        document->editorController.selection().setSelection(
            {document->editorController.cursorForSourceOffset(0), document->editorController.cursorForSourceOffset(2)});
      input->setFocus();
      QApplication::processEvents();
    };
    PrefsShortcutsPage page;
    auto* filter = page.findChild<QLineEdit*>("ShortcutFilter");
    auto* sequence = page.findChild<QKeySequenceEdit*>("ShortcutSequence");
    auto* apply = page.findChild<QPushButton*>("ShortcutApply");
    require(filter && sequence && apply, "shortcut preferences have searchable command and recorder");
    filter->setText("format.bold");
    const QKeySequence custom(Qt::CTRL | Qt::ALT | Qt::Key_Left);
    sequence->setKeySequence(custom);
    require(apply->isEnabled(), "valid custom shortcut can be applied");
    apply->click();
    require(action(f.window, "Bold")->shortcut() == custom, "shortcut updates existing action immediately");
    select();
    QTest::keyClick(input, Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier);
    require(document->session.markdownText().toString().startsWith(QStringLiteral("**中文**")),
            QStringLiteral("custom shortcut formats in %1 editing: %2; focus=%3 enabled=%4")
                .arg(source ? "source" : "render", document->session.markdownText().toString(),
                     QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "none")
                .arg(action(f.window, "Bold")->isEnabled()));
    action(f.window, "Undo")->trigger();

    filter->setText("format.italic");
    sequence->setKeySequence(custom);
    require(apply->text() == "Reassign", "conflict is shown before reassignment");
    apply->click();
    require(action(f.window, "Bold")->shortcut().isEmpty() && action(f.window, "Italic")->shortcut() == custom,
            "explicit reassignment clears previous owner");
    sequence->setKeySequence(QKeySequence(Qt::Key_A));
    require(!apply->isEnabled(), "typing keys cannot be stolen by a window shortcut");
    sequence->setKeySequence(QKeySequence("Ctrl+Left"));
    require(!apply->isEnabled(), "editor navigation keys remain available");
    shortcuts.clearOverride("format.bold");
    shortcuts.clearOverride("format.italic");

    const QKeySequence copyKey(Qt::CTRL | Qt::ALT | Qt::Key_F12);
    shortcuts.setOverride("edit.copy", copyKey);
    select();
    QApplication::clipboard()->setText("sentinel");
    QTest::keyClick(input, Qt::Key_C, Qt::ControlModifier);
    require(QApplication::clipboard()->text() == "sentinel", "old source editor fallback shortcut is disabled");
    QTest::keyClick(input, Qt::Key_F12, Qt::ControlModifier | Qt::AltModifier);
    require(QApplication::clipboard()->text() == QStringLiteral("中文"), "new copy shortcut works in both editors");
    shortcuts.clearOverride("edit.copy");
    require(action(f.window, "Copy")->shortcut() == QKeySequence(QKeySequence::Copy), "restore default action");
    shortcuts.setOverride("edit.select_all", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F10));
    select();
    const auto selectedText = [&] {
      if (source)
        return document->editor->selectedText();
      return SelectionSerializer()
          .exportSelection({&document->session.document(), document->editorController.selection().selection(),
                            SelectionExportFormat::Markdown})
          .text;
    };
    QTest::keyClick(input, Qt::Key_A, Qt::ControlModifier);
    require(selectedText() == QStringLiteral("中文"), "old select-all shortcut no longer selects");
    QTest::keyClick(input, Qt::Key_F10, Qt::ControlModifier | Qt::AltModifier);
    require(selectedText() == document->session.markdownText().toString(), "custom select-all covers the document");
    shortcuts.clearOverride("edit.select_all");
  }
}

void continuousEditingWorkflow() {
  // Exercise the same document through real widget input and window commands,
  // without resetting its AST or history between individual editing operations.
  for (bool source : {false, true})
    for (bool reverse : {false, true}) {
      Fixture f;
      SettingsOverride copyMarkdown{"editor/copyAsMarkdown", true};
      SettingsOverride indentSize{"editor/indentSize", 0};
      const QString original =
          QString::fromUtf8("第一段开头。\n\n第二段内容。\n\n- 第一项\n- 第二项\n\n结尾。\n\n"
                            "## 标题\n\n> 引用 **加粗** 与 [链接](https://example.com)\n\n"
                            "| 列一 | 列二 |\n|---|---|\n| 中文 | 🙂 |\n\n```cpp\nint value = 123;\n```\n");
      auto* document = f.open(f.file("workflow.md", original));
      auto* mode = action(f.window, "Source Code Mode");
      if (document->backend->isSourceMode() != source)
        mode->trigger();
      QWidget* input = source ? static_cast<QWidget*>(document->editor->findChild<VirtualSourceEdit*>())
                              : static_cast<QWidget*>(document->renderView);
      require(input, "workflow has an input surface");
      const auto select = [&](qsizetype start, qsizetype end) {
        if (source)
          document->editor->setSelection(start, end);
        else
          document->editorController.selection().setSelection({document->editorController.cursorForSourceOffset(start),
                                                               document->editorController.cursorForSourceOffset(end)});
        input->setFocus();
        QApplication::processEvents();
      };
      const auto text = [&] { return document->session.markdownText().toString(); };
      const auto expect = [&](const QString& expected, const char* step) {
        require(text() == expected, QString::fromLatin1(step) + "\nExpected: " + expected + "\nActual: " + text());
      };
      select(5, 5);
      QInputMethodEvent composing(QStringLiteral("zhongwen"), {});
      QApplication::sendEvent(input, &composing);
      expect(original, "preedit must not change document");
      QInputMethodEvent commit;
      const QString committed = QString::fromUtf8("中文🙂");
      commit.setCommitString(committed);
      QApplication::sendEvent(input, &commit);
      QString typed = original;
      typed.insert(5, committed);
      expect(typed, "Chinese IME commits once at caret");

      const auto start = typed.indexOf(committed);
      const auto end = typed.indexOf(QString::fromUtf8("内容"));
      select(reverse ? end : start, reverse ? start : end);
      action(f.window, "Cut")->trigger();
      QString cut = typed;
      cut.remove(start, end - start);
      expect(cut, "cross-paragraph cut removes the exact UTF-16 range");
      require(QApplication::clipboard()->text() == typed.mid(start, end - start),
              "cut preserves Chinese, emoji and paragraph breaks");
      action(f.window, "Paste")->trigger();
      expect(typed, "paste restores the cut range at the collapsed caret");

      const auto listCaret = text().indexOf(QString::fromUtf8(source ? "- 第二项" : "第二项"));
      select(listCaret, listCaret);
      QTest::keyClick(input, Qt::Key_Tab);
      require(QApplication::focusWidget() == input || QApplication::focusWidget() == input->focusProxy(),
              "plain Tab keeps focus in the editing surface");
      QString indented = typed;
      indented.insert(typed.indexOf(QString::fromUtf8("- 第二项")), source ? "\t" : "  ");
      expect(indented, "Tab nests the second list item");
      action(f.window, "Undo")->trigger();
      expect(typed, "undo list indent");
      action(f.window, "Undo")->trigger();
      expect(cut, "undo paste");
      action(f.window, "Undo")->trigger();
      expect(typed, "undo cut");
      action(f.window, "Undo")->trigger();
      expect(original, "undo Chinese commit");
      for (const auto& expected : {typed, cut, typed, indented}) {
        action(f.window, "Redo")->trigger();
        expect(expected, "redo workflow in order");
      }
      select(text().indexOf(committed), text().indexOf(committed) + committed.size());
      const auto viewport = source ? document->editor->viewportPosition() : document->renderView->viewportPosition();
      auto* other = f.open(f.file("other.md", "Untouched document\n"));
      require(other != document && other->session.markdownText().toString() == "Untouched document\n",
              "second tab remains independent");
      f.tabs->setCurrentWidget(document);
      QApplication::processEvents();
      expect(indented, "returning to tab retains unsaved workflow edits");
      qsizetype selectedStart = -1, selectedEnd = -1;
      if (source) {
        selectedStart = document->editor->selectionStart();
        selectedEnd = document->editor->selectionEnd();
      } else {
        require(BlockEditContextResolver(&document->session, &document->editorController.selection())
                    .selectionSourceRange(selectedStart, selectedEnd),
                "returned render selection has source range");
      }
      require(document->backend->isSourceMode() == source &&
                  text().mid(selectedStart, selectedEnd - selectedStart) == committed,
              "returning retains editor mode and UTF-16 selection");
      const auto returned = source ? document->editor->viewportPosition() : document->renderView->viewportPosition();
      require(returned.sourceOffset == viewport.sourceOffset, "returning retains reading anchor");
      action(f.window, "Undo")->trigger();
      expect(typed, "history survives tab switch");
      action(f.window, "Redo")->trigger();
      expect(indented, "redo survives tab switch");
      QTest::keyClick(input, Qt::Key_A, Qt::ControlModifier);
      QTest::keyClick(input, Qt::Key_Backspace);
      expect({}, "select-all Backspace clears document after the entire editing workflow");
      action(f.window, "Undo")->trigger();
      expect(indented, "undo restores the complete mixed document");
      action(f.window, "Redo")->trigger();
      expect({}, "redo clears the complete mixed document again");
      action(f.window, "Undo")->trigger();
      expect(indented, "document remains recoverable after repeated delete/undo");
    }
}

void imeSelectionReplacementWorkflow() {
  for (bool source : {false, true})
    for (bool reverse : {false, true}) {
      Fixture f;
      const QString selected = QString::fromUtf8("中文🙂");
      const QString original = QString::fromUtf8("开头：") + selected + QString::fromUtf8("，结尾。\n");
      auto* document = f.open(f.file("ime-selection.md", original));
      if (source)
        action(f.window, "Source Code Mode")->trigger();
      QWidget* input = source ? static_cast<QWidget*>(document->editor->findChild<VirtualSourceEdit*>())
                              : static_cast<QWidget*>(document->renderView);
      const auto start = original.indexOf(selected), end = start + selected.size();
      if (source)
        document->editor->setSelection(reverse ? end : start, reverse ? start : end);
      else
        document->editorController.selection().setSelection(
            {document->editorController.cursorForSourceOffset(reverse ? end : start),
             document->editorController.cursorForSourceOffset(reverse ? start : end)});
      input->setFocus();
      QInputMethodEvent composing(QStringLiteral("xinde"), {});
      QApplication::sendEvent(input, &composing);
      QInputMethodEvent cancel;
      QApplication::sendEvent(input, &cancel);
      require(document->session.markdownText().toString() == original && !document->backend->canUndo(),
              "canceling Chinese composition preserves selected text and creates no undo entry");
      QApplication::sendEvent(input, &composing);
      QInputMethodEvent commit;
      const QString replacement = QString::fromUtf8("新的文字");
      commit.setCommitString(replacement);
      QApplication::sendEvent(input, &commit);
      const auto expected = QString(original).replace(start, end - start, replacement);
      require(document->session.markdownText().toString() == expected,
              "Chinese commit replaces selection exactly once");
      action(f.window, "Undo")->trigger();
      require(document->session.markdownText().toString() == original && !document->backend->canUndo(),
              "one undo restores replaced Chinese text and emoji");
      qsizetype anchor = -1, focus = -1;
      if (source) {
        anchor = document->editor->anchorPosition();
        focus = document->editor->cursorPosition();
      } else {
        const auto selection = document->editorController.selection().selection();
        anchor = selection.anchor.text.sourceOffset;
        focus = selection.focus.text.sourceOffset;
      }
      require(
          anchor == (reverse ? end : start) && focus == (reverse ? start : end),
          QStringLiteral("IME undo restores selection and direction: source=%1 reverse=%2 expected=%3,%4 actual=%5,%6")
              .arg(source)
              .arg(reverse)
              .arg(reverse ? end : start)
              .arg(reverse ? start : end)
              .arg(anchor)
              .arg(focus));
      action(f.window, "Redo")->trigger();
      require(document->session.markdownText().toString() == expected, "Chinese replacement redo is exact");
      QTest::keyClicks(input, "x");
      const auto continued = QString(expected).insert(start + replacement.size(), QLatin1Char('x'));
      require(document->session.markdownText().toString() == continued,
              "typing continues at the committed Chinese caret");
      action(f.window, "Undo")->trigger();
      require(document->session.markdownText().toString() == expected,
              "replacement does not merge with following typing");
      action(f.window, "Undo")->trigger();
      require(document->session.markdownText().toString() == original,
              "replacement remains a separate recoverable undo step");
    }
}

void restrictedFormattingActions() {
  Fixture f;
  const QString original =
      "alpha\n\nbeta\n\n- first\n- second\n\n| A | B |\n|---|---|\n| one | two |\n\n```cpp\ncode\n```\n";
  auto* document = f.open(f.file("format-availability.md", original));
  auto* link = action(f.window, "Hyperlink");
  auto* underline = action(f.window, "Underline");
  const auto select = [&](qsizetype start, qsizetype end) {
    document->editorController.selection().setSelection({document->editorController.cursorForSourceOffset(start),
                                                         document->editorController.cursorForSourceOffset(end)});
    QApplication::processEvents();
  };
  select(1, original.indexOf("beta") + 2);
  require(!link->isEnabled() && !underline->isEnabled(),
          "single-block commands disabled for cross-paragraph selections");
  require(link->toolTip().contains("single paragraph") && underline->statusTip().contains("single paragraph"),
          "unavailable commands explain how to make the selection valid");
  for (auto* menu : f.window.findChildren<QMenu*>()) {
    if (menu->actions().contains(link))
      require(menu->toolTipsVisible(), "format menu exposes disabled command explanations");
  }
  link->trigger();
  underline->trigger();
  require(document->session.markdownText().toString() == original && !document->backend->canUndo(),
          "disabled actions do not mutate text or history");
  select(original.indexOf("one"), original.indexOf("two") + 2);
  require(!link->isEnabled() && !underline->isEnabled(), "different table cells do not share one editable selection");
  select(original.indexOf("code") + 1, original.indexOf("code") + 1);
  require(!link->isEnabled() && !underline->isEnabled(), "literal code caret cannot invoke prose formatting");
  select(original.indexOf("one"), original.indexOf("one") + 3);
  require(link->isEnabled() && underline->isEnabled() && action(f.window, "Bold")->isEnabled(),
          "single table cell supports inline formatting");
  link->trigger();
  require(document->session.markdownText().toString() == QString(original).replace("one", "[one](url)"),
          "table-cell hyperlink edits the cell without replacing neighbours");
  action(f.window, "Undo")->trigger();
  require(document->session.markdownText().toString() == original, "table-cell link is one undoable edit");
  select(original.indexOf("one") + 1, original.indexOf("one") + 1);
  require(link->isEnabled(), "table-cell caret supports inserting a link");
  link->trigger();
  require(document->session.markdownText().toString() == QString(original).replace("one", "o[](url)ne"),
          "table-cell caret inserts a link at its actual text offset");
  action(f.window, "Undo")->trigger();
  require(document->session.markdownText().toString() == original, "table-cell caret link undoes cleanly");
  select(original.indexOf("one") + 1, original.indexOf("one") + 1);
  action(f.window, "Bold")->trigger();
  require(document->session.markdownText().toString() == QString(original).replace("one", "**one**"),
          QStringLiteral("table-cell caret formats the current word without touching neighbours; actual: %1")
              .arg(document->session.markdownText().toString()));
  action(f.window, "Undo")->trigger();
  require(document->session.markdownText().toString() == original, "table-cell caret formatting undoes cleanly");
  select(original.indexOf("second"), original.indexOf("second") + 6);
  require(link->isEnabled() && underline->isEnabled(), "nested list text supports single-block commands");
  select(1, 4);
  require(link->isEnabled() &&
              link->toolTip() !=
                  MainWindow::tr(
                      "Select text within a single paragraph or table cell, or place the caret in editable text."),
          "valid selection clears the unavailable explanation");
  action(f.window, "Source Code Mode")->trigger();
  QApplication::processEvents();
  require(underline->isEnabled() && !underline->toolTip().contains("live editing"),
          "source formatting commands are enabled and clear the obsolete mode limitation");
  document->editor->setSelection(4, 1); // backwards selection
  QApplication::processEvents();
  require(link->isEnabled(), "source single-line link enabled");
  link->trigger();
  require(document->session.markdownText().toString() == QString(original).replace(1, 3, "[lph](url)"),
          "source hyperlink preserves selected text");
  require(document->editor->selectedText() == "lph", "source link keeps its label selected");
  action(f.window, "Undo")->trigger();
  require(document->session.markdownText().toString() == original, "source link undo restores selected text");
  document->editor->setSelection(1, original.indexOf("beta") + 2);
  QApplication::processEvents();
  require(!link->isEnabled() && link->toolTip().contains("one line"), "source multiline link explains its limit");
  // State and explanations must follow the active tab, including a locale change.
  auto* other = f.open(f.file("format-other.md", "Other text\n"));
  other->editorController.selection().setCursorPosition(other->editorController.cursorForSourceOffset(2));
  QApplication::processEvents();
  require(link->isEnabled(), "unavailable state does not leak to another tab");
  f.tabs->setCurrentWidget(document);
  QApplication::processEvents();
  require(!link->isEnabled(), "returning refreshes the original restricted selection");
  LanguageManager::instance().setLanguage("zh_CN");
  QApplication::processEvents();
  require(link->toolTip() ==
              MainWindow::tr("Select text within one line, or place the caret where you want to insert the link."),
          "unavailable explanation follows UI language");
  LanguageManager::instance().setLanguage("en");
  QApplication::processEvents();
}

void workspaceSearchAndReplace() {
  Fixture f;
  const QString screenshotDirectory = qEnvironmentVariable("MUFFIN_SEARCH_SCREENSHOT_DIR");
  if (!screenshotDirectory.isEmpty())
    QDir().mkpath(screenshotDirectory);
  QDir(f.directory.path()).mkpath("04.Study/English");
  QDir(f.directory.path()).mkpath("06.Notes/English");
  const auto a = f.file("04.Study/English/words.md", "# Notes\n[link](old-term)\nold-term\n");
  const auto b = f.file("06.Notes/English/words.md", "old-term\n");
  f.window.openFolderAtPath(f.directory.path());
  auto* quick = action(f.window, "Quick Open...");
  require(quick->isEnabled(), "Ctrl+P enabled with only a directory open");
  QTimer::singleShot(0, [&] {
    auto* picker = qobject_cast<QuickOpenDialog*>(QApplication::activeModalWidget());
    require(picker, "Ctrl+P opens picker");
    auto* field = picker->findChild<QLineEdit*>("QuickOpenFilter");
    auto* matches = picker->findChild<QListWidget*>("QuickOpenResults");
    field->setText("04/words");
    waitUntil([&] { return matches->count() == 1; }, "find unexpanded deep file");
    require(picker->selectedPath() == a, "path query distinguishes duplicate names");
    if (!screenshotDirectory.isEmpty())
      picker->grab().save(QDir(screenshotDirectory).filePath("quick-open.png"));
    picker->accept();
  });
  quick->trigger();
  auto* first = f.current();
  waitUntil([&] { return !first->session.isOpenInProgress(); }, "quick-open file loaded");
  auto* dirty = f.open(b);
  dirty->session.setMarkdownText("unsaved old-term\n", true);
  auto* searchAction = action(f.window, QString::fromUtf8("Find in Folder…"));
  require(searchAction->shortcut() == QKeySequence("Ctrl+Shift+F"), "directory search shortcut");
  searchAction->trigger();
  auto* sidebar = f.window.findChild<SidebarWidget*>();
  auto* panel = sidebar->searchPanel();
  require(sidebar->panel() == SidebarWidget::Panel::Search, "search command selects sidebar");
  panel->findChild<QLineEdit*>("WorkspaceSearchQuery")->setText("old-term");
  panel->findChild<QPushButton*>("WorkspaceSearchRun")->click();
  auto* tree = panel->findChild<QTreeWidget*>("WorkspaceSearchResults");
  waitUntil([&] { return panel->result().complete && panel->result().matches == 3 && tree->topLevelItemCount() == 2; },
            "search disk and dirty buffer");
  auto* searchToggle = sidebar->findChild<QToolButton*>("SearchFilesButton");
  auto* filesTab = sidebar->findChild<QToolButton*>("FilesTabButton");
  auto* outlineTab = sidebar->findChild<QToolButton*>("OutlineTabButton");
  require(searchToggle && searchToggle->isVisible() && searchToggle->isChecked() && filesTab->isChecked(),
          "search is an active file toolbar toggle rather than a third tab");
  const auto expanded = sidebar->expandedFolders();
  searchToggle->click();
  require(sidebar->panel() == SidebarWidget::Panel::Files && !searchToggle->isChecked(), "magnifier returns to files");
  searchToggle->click();
  require(sidebar->panel() == SidebarWidget::Panel::Search && tree->topLevelItemCount() == 2 &&
              panel->result().matches == 3 && sidebar->expandedFolders() == expanded,
          "switching back to search preserves query results and file expansion");
  require(f.window.focusWidget() == panel->focusWidget(), "search toggle targets the query field for keyboard input");
  outlineTab->click();
  require(sidebar->panel() == SidebarWidget::Panel::Outline && !searchToggle->isVisible(),
          "outline keeps its own view");
  searchAction->trigger();
  filesTab->click();
  require(sidebar->panel() == SidebarWidget::Panel::Files, "file tab also exits search");
  searchAction->trigger();
  QTreeWidgetItem* item = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->toolTip(0) == a)
      item = tree->topLevelItem(i)->child(0);
  }
  require(item, "first file has a content hit");
  tree->itemClicked(item, 0);
  waitUntil([&] { return f.current()->backend->isSourceMode() && f.current()->editor->selectedText() == "old-term"; },
            "link target is exactly selected in source");
  require(f.current() == first && f.tabs->count() == 2, "search reuses existing tab");
  require(first->searchSourceFallback && !QSettings().value("view/sourceMode").toBool(),
          "hidden hit only temporarily enters source mode");
  tree->itemClicked(item->parent()->child(1), 0);
  waitUntil([&] { return !first->backend->isSourceMode() && first->editorController.selection().hasCursor(); },
            "ordinary text returns to live editing after a hidden hit");
  require(!first->searchSourceFallback &&
              first->editorController.selection().selection().anchor.text.sourceOffset == 25,
          "live selection retains the precise source range");
  if (!screenshotDirectory.isEmpty()) {
    f.window.grab().save(QDir(screenshotDirectory).filePath("sidebar.png"));
  }
  panel->findChild<QLineEdit*>("WorkspaceReplacement")->setText("new-term");
  int externalChanges = 0;
  QObject::connect(&first->session, &DocumentSession::externalFileChanged, &f.window, [&] { ++externalChanges; });
  QObject::connect(&dirty->session, &DocumentSession::externalFileChanged, &f.window, [&] { ++externalChanges; });
  panel->activate(true);
  auto* replaceButton = panel->findChild<QPushButton*>("WorkspaceReplacePreview");
  require(replaceButton && replaceButton->isVisible() && replaceButton->isEnabled(),
          QStringLiteral("folder replacement has an available sidebar button: visible=%1 enabled=%2 sidebar=%3 "
                         "panel=%4 complete=%5 matches=%6")
              .arg(replaceButton && replaceButton->isVisible())
              .arg(replaceButton && replaceButton->isEnabled())
              .arg(sidebar->isVisible())
              .arg(static_cast<int>(sidebar->panel()))
              .arg(panel->result().complete)
              .arg(panel->result().matches));
  bool previewCanceled = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    require(dialog && dialog->findChild<QPushButton*>("FolderReplaceConfirm"),
            "replacement button opens a preview with a confirmation action");
    previewCanceled = true;
    dialog->reject();
  });
  replaceButton->click();
  require(previewCanceled && panel->result().complete && panel->result().matches == 3 && replaceButton->isEnabled(),
          "canceling the preview preserves complete results and permits another replacement attempt");
  require(first->session.markdownText().toString().contains("old-term"),
          "canceling a preview preserves the open document");
  QFile unchanged(a);
  require(unchanged.open(QIODevice::ReadOnly), "read original after canceled preview");
  require(!unchanged.readAll().contains("new-term"), "preview does not write files before confirmation");
  unchanged.close();
  bool confirmed = false, completed = false;
  QTimer responder;
  responder.setInterval(5);
  QObject::connect(&responder, &QTimer::timeout, [&] {
    auto* modal = QApplication::activeModalWidget();
    if (!modal)
      return;
    if (auto* button = modal->findChild<QPushButton*>("FolderReplaceConfirm")) {
      if (!confirmed) {
        auto* files = modal->findChild<QTreeWidget*>("FolderReplaceFiles");
        require(files && files->topLevelItemCount() == 2, "preview includes editable and skipped files");
        require(files->topLevelItem(0)->checkState(0) == Qt::Checked &&
                    !(files->topLevelItem(1)->flags() & Qt::ItemIsEnabled),
                "dirty file excluded from preview");
        if (!screenshotDirectory.isEmpty())
          modal->grab().save(QDir(screenshotDirectory).filePath("preview.png"));
        confirmed = true;
        button->click();
      }
    } else if (auto* message = qobject_cast<QMessageBox*>(modal)) {
      // QMessageBox deliberately ignores window titles on macOS.
      require(message->objectName() == "FolderReplaceReport", "only the folder replacement report should be shown");
      completed = true;
      message->accept();
    }
  });
  responder.start();
  replaceButton->click();
  responder.stop();
  require(confirmed && completed, "replacement was explicitly confirmed and finished");
  require(externalChanges == 0, "folder replacement must not emit self-write external-change notifications");
  require(first->session.markdownText().toString().contains("new-term") && !first->session.document().isModified(),
          "open clean buffer updated after disk replacement");
  require(dirty->session.markdownText().toString() == "unsaved old-term\n" && dirty->session.document().isModified(),
          "dirty buffer preserved");
  QFile disk(a);
  require(disk.open(QIODevice::ReadOnly), "read replaced file");
  require(disk.readAll().contains("new-term"), "disk replacement applied");
  disk.close();
  bool restoreConfirmed = false, restoreCompleted = false;
  QObject::disconnect(&responder, nullptr, nullptr, nullptr);
  QObject::connect(&responder, &QTimer::timeout, [&] {
    auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (!message)
      return;
    if (message->icon() == QMessageBox::Question) {
      for (auto* button : message->buttons())
        if (button->text() == "Restore Originals") {
          restoreConfirmed = true;
          button->click();
          return;
        }
    } else {
      require(message->objectName() == "FolderRestoreReport", "only the folder restoration report should be shown");
      restoreCompleted = true;
      message->accept();
    }
  });
  responder.start();
  action(f.window, "Undo Folder Replacement")->trigger();
  responder.stop();
  require(restoreConfirmed && restoreCompleted, "restore explicitly confirmed and completed");
  require(externalChanges == 0, "restoring open files must not emit self-write external-change notifications");
  require(first->session.markdownText().toString().contains("old-term") && !first->session.document().isModified(),
          "restored open buffer is synchronized");
  require(disk.open(QIODevice::ReadOnly) && disk.readAll().contains("old-term"), "restore updates disk");
}

void fileTreeOpeningPreservesScrollPosition() {
  Fixture f;
  QStringList paths;
  for (int i = 0; i < 80; ++i)
    paths << f.file(QStringLiteral("note-%1.md").arg(i, 3, 10, QLatin1Char('0')), "# Note\n\nContent\n");
  f.open(paths.first());
  f.window.openFolderAtPath(f.directory.path());
  auto* sidebar = f.window.findChild<SidebarWidget*>();
  sidebar->setPanel(SidebarWidget::Panel::Files);
  auto* tree = sidebar->findChild<QTreeView*>("FileTree");
  auto* model = qobject_cast<FileTreeModel*>(tree->model());
  auto* bar = tree->verticalScrollBar();
  waitUntil([&] { return model->rowCount(tree->rootIndex()) == paths.size() && bar->maximum() > 0; },
            "file tree loads enough notes to scroll");
  bar->setValue(bar->maximum() / 2 + 3);
  QApplication::processEvents();
  const int rowHeight = tree->visualRect(model->index(paths[40])).height();
  for (int y : {2 * rowHeight, tree->viewport()->height() - 3 * rowHeight}) {
    const auto index = tree->indexAt(QPoint(40, y));
    const auto path = model->filePath(index);
    const auto row = tree->visualRect(index);
    require(index.isValid() && row.top() >= 0 && row.bottom() < tree->viewport()->height(),
            "target row is fully visible");
    const int position = bar->value();
    for (int repeat = 0; repeat < 2; ++repeat) {
      int scrollChanges = 0;
      const auto monitor = QObject::connect(bar, &QScrollBar::valueChanged, &f.window, [&](int) { ++scrollChanges; });
      const QPoint point(row.left() + 40, row.center().y());
      QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
      QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
      waitUntil(
          [&] { return f.current()->session.filePath() == path && !f.current()->session.isAsyncParseInProgress(); },
          "double-click opens the selected note in the editor");
      QTest::qWait(40); // include queued document/status updates after the open
      QObject::disconnect(monitor);
      require(scrollChanges == 0 && bar->value() == position && tree->visualRect(index).top() == row.top(),
              "opening or reactivating a visible file never shifts the tree, including transient scrolls");
    }
  }
  const auto distant = model->index(paths[70]);
  require(tree->visualRect(distant).top() >= tree->viewport()->height(),
          "external navigation target starts below the viewport");
  f.open(paths[70]);
  QApplication::processEvents();
  const auto revealed = tree->visualRect(distant);
  require(tree->currentIndex() == distant && revealed.top() >= 0 &&
              qAbs(revealed.bottom() - (tree->viewport()->height() - 1)) <= 1,
          "opening an offscreen file scrolls only far enough to reveal it");
  bar->setValue(bar->maximum() / 2 + 3);
  const int browsedPosition = bar->value();
  sidebar->setCurrentDocument("note-070.md", paths[70], true);
  QApplication::processEvents();
  require(bar->value() == browsedPosition, "status updates preserve the user's browsing position");
}

void workspaceSidebarWidth() {
  Fixture f;
  f.window.openFolderAtPath(f.directory.path());
  auto* sidebar = f.window.findChild<SidebarWidget*>();
  auto* splitter = qobject_cast<QSplitter*>(sidebar->parentWidget());
  auto* magnifier = sidebar->findChild<QToolButton*>("SearchFilesButton");
  auto* files = sidebar->findChild<QToolButton*>("FilesTabButton");
  auto* outline = sidebar->findChild<QToolButton*>("OutlineTabButton");
  auto* find = action(f.window, QString::fromUtf8("Find in Folder…"));
  auto* replace = action(f.window, QString::fromUtf8("Replace in Folder…"));
  require(splitter && magnifier, "sidebar is hosted in a resizable splitter");
  for (const int requested : {220, 260, 420}) {
    files->click();
    splitter->setSizes({requested, splitter->width() - requested - splitter->handleWidth()});
    QApplication::processEvents();
    const int width = sidebar->width();
    require(qAbs(width - requested) <= 1, "sidebar accepts both narrow and wide user sizes");
    const auto unchanged = [&] {
      QApplication::processEvents();
      require(sidebar->width() == width, "switching sidebar content preserves the user-selected width");
    };
    magnifier->click();
    unchanged();
    magnifier->click();
    unchanged();
    find->trigger();
    unchanged();
    replace->trigger();
    unchanged();
    outline->click();
    unchanged();
    find->trigger();
    unchanged();
    files->click();
    unchanged();
  }
  // An already visible sidebar must not replay its opening animation when a
  // menu command or shortcut selects a different panel.
  SettingsOverride motion("appearance/reducedMotion", false);
  SettingsOverride visibility("view/sidebarVisible", true);
  const int width = sidebar->width();
  find->trigger();
  require(sidebar->width() == width, "search command does not collapse an already visible sidebar");
  QTest::qWait(240);
  require(sidebar->width() == width, "search command preserves width with animations enabled");
  auto* toggle = action(f.window, "Show / Hide Sidebar");
  toggle->trigger();
  waitUntil([&] { return !sidebar->isVisible(); }, "sidebar hide animation completes");
  find->trigger();
  waitUntil([&] { return sidebar->isVisible() && sidebar->minimumWidth() == SidebarWidget::kMinimumWidth; },
            "sidebar search opening animation completes");
  require(sidebar->width() == width, "reopening search restores the user's sidebar width");
  files->click();
  require(sidebar->width() == width, "returning to files retains a wide search sidebar");
}

void documentFindRevealsMatchesInsideTallBlocks() {
  SettingsOverride typewriterSetting("appearance/typewriterMode", false);
  Fixture f;
  const QString needle = QString::fromUtf8("定位文本");
  QString text = "# Search positions\n\n| Column | Other |\n| --- | --- |\n";
  QVector<qsizetype> offsets;
  for (int row = 0; row < 120; ++row) {
    text += QStringLiteral("| Row %1 | ").arg(row);
    if (row == 30 || row == 90) {
      if (row == 30)
        text += "**";
      offsets.append(text.size());
      text += needle;
      if (row == 30)
        text += "**";
    } else {
      text += "ordinary cell";
    }
    text += " |\n";
  }
  text += "\n" + QStringLiteral("Wrapped paragraph text. ").repeated(900);
  offsets.append(text.size());
  text += needle + "\n\n```text\n";
  for (int line = 0; line < 100; ++line)
    text += QStringLiteral("Code line %1\n").arg(line);
  offsets.append(text.size());
  text += needle + "\n```\n\nEnd.\n";
  auto* document = f.open(f.file("find-tall-blocks.md", text));
  auto* view = document->renderView;
  require(!document->backend->isSourceMode(), "search fixture starts in live editing");
  action(f.window, "Find...")->trigger();
  document->findBar->setSearchText(needle);
  auto* query = document->findBar->findChild<QLineEdit*>();
  const auto button = [&](const QString& label) {
    for (auto* candidate : document->findBar->findChildren<QPushButton*>())
      if (candidate->text() == label)
        return candidate;
    require(false, "find navigation button exists");
    return static_cast<QPushButton*>(nullptr);
  };
  const auto check = [&](int index) {
    QApplication::processEvents();
    const auto range = document->editorController.selection().selection();
    require(!document->backend->isSourceMode() && range.anchor.text.sourceOffset == offsets[index] &&
                range.focus.text.sourceOffset == offsets[index] + needle.size(),
            QStringLiteral("find match %1 must select [%2, %3], got [%4, %5] (source mode: %6)")
                .arg(index)
                .arg(offsets[index])
                .arg(offsets[index] + needle.size())
                .arg(range.anchor.text.sourceOffset)
                .arg(range.focus.text.sourceOffset)
                .arg(document->backend->isSourceMode()));
    for (const auto position : {range.anchor, range.focus}) {
      const auto rect = view->hitForCursorPosition(position).cursorRect;
      const auto top = view->verticalScrollBar()->value();
      require(!rect.isEmpty() && rect.top() >= top && rect.bottom() <= top + view->viewport()->height(),
              QStringLiteral("match %1 inside a tall block must be fully visible").arg(index));
    }
    const auto* block = view->blockLayoutForNode(range.focus.blockId);
    if (block && block->type() == BlockType::Table)
      require(!block->selectionRects(range, view->theme()).isEmpty(), "table search must highlight the matched text");
  };
  for (const bool typewriter : {false, true}) {
    auto* mode = action(f.window, "Typewriter Mode");
    if (mode->isChecked() != typewriter)
      mode->trigger();
    document->lastFindOffset = -1;
    document->editorController.setCursorForSourceOffset(0);
    view->verticalScrollBar()->setValue(0);
    button("Next")->click();
    check(0);
    button("Next")->click();
    check(1);
    action(f.window, "Find Next")->trigger();
    check(2);
    QTest::keyClick(query, Qt::Key_Return);
    check(3);
    QTest::keyClick(query, Qt::Key_Return);
    check(0); // wrap forward from the code block back into the table
    button("Previous")->click();
    check(3);
    action(f.window, "Find Previous")->trigger();
    check(2);
    button("Previous")->click();
    check(1);
    QTest::keyClick(query, Qt::Key_Return, Qt::ShiftModifier);
    check(0);
    if (typewriter) {
      const int landing = view->verticalScrollBar()->value();
      QTest::qWait(350); // longer than the maximum typewriter scroll animation
      require(view->verticalScrollBar()->value() == landing, "search cancels stale typewriter scroll animations");
      check(0);
    }
    require(query->hasFocus(), "find navigation retains focus in the query input");
  }
  action(f.window, "Typewriter Mode")->trigger();
  action(f.window, "Source Code Mode")->trigger();
  document->lastFindOffset = -1;
  document->editor->setSelection(0, 0);
  QTest::keyClick(query, Qt::Key_Return);
  require(document->editor->selectionStart() == offsets[0] && document->editor->selectedText() == needle,
          "Enter advances exactly once in source mode too");
  QTest::keyClick(query, Qt::Key_Enter, Qt::ShiftModifier);
  require(document->editor->selectionStart() == offsets.back() && document->editor->selectedText() == needle,
          "Shift+keypad Enter searches backward and wraps in source mode");
  require(document->session.markdownText().toString() == text && !document->session.document().isModified(),
          "search navigation never changes the document");
}

void workspaceRenderedNavigation() {
  SettingsOverride startup("files/startupBehavior", 1);
  SettingsOverride sessionState("session/window", QVariantMap{});
  Fixture f;
  const QString text = QString::fromUtf8("# 标题 needle\n\n中文😀 **needle** [needle](https://example.test/needle)\n\n"
                                         "- list needle\n\n> quote needle\n\n| column |\n| --- |\n| needle |\n\n"
                                         "```cpp\n// needle\n```\n\n![needle](image.png)\n\n$needle$\n");
  const auto path = f.file("rendered.md", text);
  f.window.openFolderAtPath(f.directory.path());
  auto* panel = f.window.findChild<SidebarWidget*>()->searchPanel();
  const auto jump = [&](qsizetype start, bool source) {
    require(start >= 0, "fixture match exists");
    panel->resultActivated(path, WorkspaceSearch::fingerprint(text), start, 6);
    waitUntil(
        [&] { return !f.current()->session.isOpenInProgress() && f.current()->backend->isSourceMode() == source; },
        "search navigation completes in the intended mode");
    auto* document = f.current();
    if (source) {
      require(document->editor->selectionStart() == start && document->editor->selectedText() == "needle",
              "source fallback selects the exact hidden range");
    } else {
      const auto range = document->editorController.selection().selection();
      require(range.anchor.text.sourceOffset == start && range.focus.text.sourceOffset == start + 6,
              "live selection covers the exact Unicode source range");
      const auto rect = document->renderView->effectiveCursorRect();
      const auto scroll = document->renderView->verticalScrollBar()->value();
      require(!rect.isEmpty() && rect.bottom() >= scroll &&
                  rect.top() <= scroll + document->renderView->viewport()->height(),
              "live match is visible in the viewport");
    }
  };
  for (const auto& prefix : {QString::fromUtf8("标题 "), QString("**"), QString("["), QString("list "),
                             QString("quote "), QString("| "), QString("// ")}) {
    qsizetype start = text.indexOf(prefix) + prefix.size();
    if (prefix == "| ")
      start = text.indexOf("| needle") + 2;
    jump(start, false);
  }
  jump(text.indexOf("test/needle") + 5, true);
  jump(text.indexOf("list needle") + 5, false);
  jump(text.indexOf("![needle") + 2, true);
  jump(text.indexOf("quote needle") + 6, false);
  QSettings().remove("session/window");
  jump(text.indexOf("$needle") + 1, true);
  waitUntil(
      [&] {
        const auto snapshot = QSettings().value("session/window").toMap().value("views").toList();
        return snapshot.size() == 1 && !snapshot[0].toMap().value("sourceMode").toBool();
      },
      "temporary source fallback is not persisted as the user's preferred mode");
  // Explicit user mode choices override the temporary fallback policy.
  auto* source = action(f.window, "Source Code Mode");
  source->trigger();
  source->trigger();
  jump(text.indexOf("list needle") + 5, true);
  require(!f.current()->searchSourceFallback, "manual source mode is respected");
  source->trigger();
  jump(text.indexOf("list needle") + 5, false);
  // An async open must reveal a distant match after its layout is ready.
  QString distant;
  for (int i = 0; i < 500; ++i)
    distant += QStringLiteral("Paragraph %1 with ordinary text.\n\n").arg(i);
  distant += "Distant needle.\n";
  const auto deepPath = f.file("distant.md", distant);
  const auto offset = distant.indexOf("needle");
  jump(text.indexOf("test/needle") + 5, true);
  panel->resultActivated(deepPath, WorkspaceSearch::fingerprint(distant), offset, 6);
  waitUntil(
      [&] {
        return !f.current()->session.isOpenInProgress() &&
               f.current()->editorController.selection().selection().anchor.text.sourceOffset == offset;
      },
      "async navigation selects a distant live match");
  require(!f.current()->backend->isSourceMode(), "new files default to live editing");
}

void fileOpenProtectionSettingsAndBoundaries() {
  SettingsOverride extensionsOn{"files/extensionProtection", true};
  SettingsOverride extensions{"files/textExtensions", file_open_protection::defaultTextExtensions()};
  SettingsOverride sizeOn{"files/sizeProtection", true};
  SettingsOverride limit{"files/sizeLimitMb", 100};
  SettingsOverride bytes{"files/sizeLimitBytes", QVariant()};
  SettingsOverride unit{"files/sizeLimitUnit", "MB"};
  QSettings().remove("files/extensionProtection");
  QSettings().remove("files/sizeProtection");
  QSettings().remove("files/sizeLimitMb");
  PrefsFilesPage page;
  auto* extensionCheck = page.findChild<QCheckBox*>("extensionProtection");
  auto* sizeCheck = page.findChild<QCheckBox*>("sizeProtection");
  auto* edit = page.findChild<QLineEdit*>("textExtensions");
  auto* addExtension = page.findChild<QPushButton*>("addTextExtension");
  auto* resetExtensions = page.findChild<QPushButton*>("resetTextExtensions");
  auto* chips = page.findChild<QWidget*>("textExtensionChips");
  auto* spin = page.findChild<QDoubleSpinBox*>("sizeLimitValue");
  auto* units = page.findChild<QComboBox*>("sizeLimitUnit");
  require(extensionCheck->isChecked() && sizeCheck->isChecked() && spin->value() == 100,
          "both guards default on with a 100 MB limit");
  require(units->currentText() == "MB" && units->count() == 3, "size units default to MB with KB and GB available");
  Fixture f;
  const QString text = f.file("notes.MD", "text\n");
  const QString unknown = f.file("unknown.binary", "text\n");
  const QString noSuffix = f.file("README", "text\n");
  require(!file_open_protection::warningsForFile(text).any(), "known extension ignores case");
  require(file_open_protection::warningsForFile(unknown).extension, "unknown extension needs confirmation");
  require(file_open_protection::warningsForFile(noSuffix).extension, "extensionless file needs confirmation");
  edit->setText(QStringLiteral("*.MD; .BINARY，log txt、.TXT"));
  addExtension->click();
  require(!file_open_protection::warningsForFile(unknown).extension,
          "custom extensions accept dots, wildcards and separators");
  require(edit->text().isEmpty() && !addExtension->isEnabled(), "adding clears the field and prevents empty additions");
  const int count = chips->layout()->count();
  edit->setText(QStringLiteral(".binary *.BINARY"));
  edit->returnPressed();
  require(chips->layout()->count() == count, "duplicate extensions normalize to one chip");
  while (chips->layout()->count()) {
    chips->layout()->itemAt(0)->widget()->findChild<QToolButton*>("removeTextExtension")->click();
  }
  require(file_open_protection::warningsForFile(text).extension, "empty allowlist warns for every extension");
  resetExtensions->click();
  require(
      chips->layout()->count() ==
              file_open_protection::normalizedTextExtensions(file_open_protection::defaultTextExtensions()).size() &&
          !file_open_protection::warningsForFile(text).extension,
      "restore defaults restores the allowed extensions");
  QFile large(unknown);
  require(large.open(QIODevice::ReadWrite), "open sparse size fixture");
  require(large.resize(100LL * 1024 * 1024), "resize to exactly the default limit");
  require(!file_open_protection::warningsForFile(unknown).size, "exact size limit does not warn");
  require(large.resize(100LL * 1024 * 1024 + 1), "resize beyond limit");
  large.close();
  const auto warnings = file_open_protection::warningsForFile(unknown);
  require(warnings.extension && warnings.size, "both guards can trigger together");
  spin->setValue(200);
  require(!file_open_protection::warningsForFile(unknown).size, "custom size limit applies immediately");
  extensionCheck->setChecked(false);
  sizeCheck->setChecked(false);
  require(!edit->isEnabled() && !spin->isEnabled() && !units->isEnabled(), "disabled guards disable their fields");
  require(!resetExtensions->isEnabled() && !chips->isEnabled(),
          "disabled extension protection also disables chips and reset");
  require(!file_open_protection::warningsForFile(unknown).any(),
          "guards can be disabled independently of their values");
  PrefsFilesPage reopened;
  require(!reopened.findChild<QCheckBox*>("extensionProtection")->isChecked() &&
              !reopened.findChild<QCheckBox*>("sizeProtection")->isChecked() &&
              reopened.findChild<QDoubleSpinBox*>("sizeLimitValue")->value() == 200,
          "guard settings persist");
  if (const auto capture = qEnvironmentVariable("MUFFIN_OPEN_GUARD_SCREENSHOT"); !capture.isEmpty()) {
    SettingsOverride captureExtensions{"files/extensionProtection", true};
    SettingsOverride captureSize{"files/sizeProtection", true};
    SettingsOverride captureLimit{"files/sizeLimitMb", 100};
    SettingsOverride captureBytes{"files/sizeLimitBytes", qint64(100) * 1024 * 1024};
    LanguageManager::instance().setLanguage(QStringLiteral("zh_CN"));
    PreferencesDialog preferences;
    preferences.findChild<QListWidget*>()->setCurrentRow(5);
    preferences.resize(1040, 840);
    preferences.show();
    QApplication::processEvents();
    require(preferences.grab().save(capture + ".settings.png"), "capture protection preferences");
    auto* protection = preferences.findChild<QWidget*>("fileOpenProtection");
    const auto captureProtection = [&](const QString& suffix) {
      const QRect area(protection->mapTo(&preferences, QPoint()), protection->size());
      require(preferences.grab().copy(area).save(capture + suffix),
              "capture protection layout with its dialog background");
    };
    captureProtection(QStringLiteral(".protection.light.png"));
    auto* capturedUnits = preferences.findChild<QComboBox*>("sizeLimitUnit");
    capturedUnits->setCurrentText("GB");
    QApplication::processEvents();
    captureProtection(QStringLiteral(".protection.gb.png"));
    capturedUnits->setCurrentText("MB");
    preferences.setThemeDefinition(ThemeDefinition::builtIn(QStringLiteral("night")).value());
    QApplication::processEvents();
    captureProtection(QStringLiteral(".protection.dark.png"));
    preferences.resize(880, 840);
    QApplication::processEvents();
    captureProtection(QStringLiteral(".protection.narrow.png"));
    LanguageManager::instance().setLanguage(QStringLiteral("en"));
  }
}

void unsavedMermaidRendersAfterLeavingSource() {
  SettingsOverride diagrams{"markdown/diagrams", true};
  SettingsOverride source{"editor/showMermaidAsSource", false};
  Fixture f;
  auto* tab = f.current();
  QApplication::clipboard()->setText(QStringLiteral("```mermaid\nflowchart TD\nA[开始] --> B{是否保存？}\n"
                                                    "B -->|是| C[保存文件]\nB -->|否| D[继续编辑]\n```\n\nafter\n"));
  QTest::keyClick(tab->renderView, Qt::Key_V, Qt::ControlModifier);
  QApplication::processEvents();
  auto& root = tab->session.document().root();
  require(root.children().size() == 2 && root.children().front()->type() == BlockType::CodeFence,
          "pasted Markdown creates a Mermaid fence and following paragraph");
  const NodeId fence = root.children().front()->id();
  auto* paragraph = root.children().back().get();
  CursorPosition cursor;
  cursor.blockId = cursor.text.nodeId = fence;
  tab->editorController.selection().setCursorPosition(cursor);
  waitUntil(
      [&] {
        const auto* block = tab->renderView->blockLayoutForNode(fence);
        return block && block->mermaidState() == BlockLayout::MermaidState::Ready;
      },
      "focused Mermaid validates its source asynchronously");
  require(!tab->renderView->blockLayoutForNode(fence)->isMermaidRendered(),
          "the focused Mermaid fence retains editable source");
  cursor.blockId = cursor.text.nodeId = paragraph->id();
  cursor.text.textOffset = 1;
  tab->editorController.selection().setCursorPosition(cursor);
  waitUntil(
      [&] {
        const auto* block = tab->renderView->blockLayoutForNode(fence);
        return block && block->isMermaidRendered();
      },
      "unsaved Mermaid renders after the cursor leaves its source");
  require(tab->session.filePath().isEmpty() && tab->session.document().isModified(),
          "Mermaid renders without saving or assigning a document path");

  // The empty line after the final block shares its node ID, but is outside
  // the source editor. It must allow the diagram to replace the source too.
  Fixture atEnd;
  auto* endTab = atEnd.current();
  QApplication::clipboard()->setText(QStringLiteral("```mermaid\nflowchart TD\nA --> B\n```\n"));
  QTest::keyClick(endTab->renderView, Qt::Key_V, Qt::ControlModifier);
  QApplication::processEvents();
  const NodeId endFence = endTab->session.document().root().children().front()->id();
  const QRectF rect = endTab->renderView->nodeRect(endFence);
  const QPointF trailing(rect.left(), rect.bottom() + endTab->renderView->theme().blockSpacing() + 5);
  QTest::mouseClick(endTab->renderView->viewport(), Qt::LeftButton, Qt::NoModifier,
                    (trailing - QPointF(0, endTab->renderView->verticalScrollBar()->value())).toPoint());
  require(endTab->editorController.selection().cursorPosition().afterBlock,
          "click below the final Mermaid fence reaches the trailing empty line");
  waitUntil(
      [&] {
        const auto* block = endTab->renderView->blockLayoutForNode(endFence);
        return block && block->isMermaidRendered();
      },
      "unsaved Mermaid at document end renders from the trailing empty line");
  require(endTab->session.filePath().isEmpty(), "trailing-line rendering needs no saved path");
}

void fileSizeUnitsConvertPersistAndProtectAtByteBoundaries() {
  SettingsOverride sizeOn{"files/sizeProtection", true};
  SettingsOverride legacy{"files/sizeLimitMb", 137};
  SettingsOverride bytes{"files/sizeLimitBytes", QVariant()};
  SettingsOverride unit{"files/sizeLimitUnit", "MB"};
  PrefsFilesPage page;
  auto* spin = page.findChild<QDoubleSpinBox*>("sizeLimitValue");
  auto* units = page.findChild<QComboBox*>("sizeLimitUnit");
  const qint64 legacyBytes = qint64(137) * 1024 * 1024;
  require(spin->value() == 137 && file_open_protection::sizeLimitBytes() == legacyBytes,
          "existing MB settings retain their threshold");
  for (const auto& name : {"GB", "KB", "MB", "GB", "KB"}) {
    units->setCurrentText(QString::fromLatin1(name));
    require(file_open_protection::sizeLimitBytes() == legacyBytes,
            "switching display units preserves the byte threshold");
  }
  require(spin->value() == 137 * 1024, "MB converts exactly to KB");
  spin->setValue(513);
  require(file_open_protection::sizeLimitBytes() == 513 * 1024, "KB edits persist in bytes");
  units->setCurrentText("GB");
  units->setCurrentText("KB");
  require(spin->value() == 513 && file_open_protection::sizeLimitBytes() == 513 * 1024,
          "fractional GB display does not round an existing KB threshold");
  PrefsFilesPage reopened;
  require(reopened.findChild<QComboBox*>("sizeLimitUnit")->currentText() == "KB" &&
              reopened.findChild<QDoubleSpinBox*>("sizeLimitValue")->value() == 513,
          "both the value and selected unit persist");
  QTemporaryDir directory;
  QFile file(directory.filePath("boundary.md"));
  require(file.open(QIODevice::ReadWrite) && file.resize(513 * 1024), "create KB boundary fixture");
  require(!file_open_protection::warningsForFile(file.fileName()).size, "exact KB limit does not warn");
  require(file.resize(513 * 1024 + 1), "extend KB boundary fixture");
  require(file_open_protection::warningsForFile(file.fileName()).size, "one byte above KB limit warns");
  units->setCurrentText("GB");
  spin->setValue(3.25);
  require(file_open_protection::sizeLimitBytes() == qint64(13) * 1024 * 1024 * 1024 / 4,
          "fractional GB limits above 2 GB use 64-bit bytes");
  spin->findChild<QLineEdit*>()->setText(spin->locale().toString(2.5, 'f', 1));
  units->setCurrentText("MB");
  require(spin->value() == 2560 && file_open_protection::sizeLimitBytes() == qint64(2560) * 1024 * 1024,
          "switching units commits unsubmitted input using its original unit");
  QSettings().setValue("files/sizeLimitBytes", 0);
  require(file_open_protection::sizeLimitBytes() == 1024, "minimum threshold is one KB");
  QSettings().setValue("files/sizeLimitBytes", file_open_protection::kMaximumSizeLimitBytes + 1);
  require(file_open_protection::sizeLimitBytes() == file_open_protection::kMaximumSizeLimitBytes,
          "oversized persisted thresholds clamp safely");
}

void fileOpenConfirmationDefersReadingAndPreservesTabs() {
  SettingsOverride extensionsOn{"files/extensionProtection", true};
  SettingsOverride extensions{"files/textExtensions", file_open_protection::defaultTextExtensions()};
  SettingsOverride sizeOn{"files/sizeProtection", true};
  SettingsOverride limit{"files/sizeLimitMb", 1};
  Fixture f;
  auto* original = f.open(f.file("original.md", "Original document\n"));
  original->session.applyTextDelta(0, 0, "Unsaved ", true);
  const auto originalText = original->session.markdownText().toString();
  const QString path = f.file("guard.binary", "Before confirmation\n");
  require(f.window.openFile(path), "guard accepts open request");
  auto* pending = f.current();
  require(pending != original && pending->pendingOpenPath == path, "guard has its own pending tab");
  require(pending->session.isOpenInProgress() && !pending->session.isAsyncParseInProgress() &&
              pending->session.filePath().isEmpty() && pending->session.markdownText().isEmpty(),
          "no file identity, text or parser before confirmation");
  require(pending->openConfirmation->isVisible(), "confirmation is displayed inside the document area");
  require(pending->openConfirmation->findChild<QLabel*>("extensionWarning")->isVisible() &&
              !pending->openConfirmation->findChild<QLabel*>("sizeWarning")->isVisible(),
          "extension warning alone");
  if (const auto capture = qEnvironmentVariable("MUFFIN_OPEN_GUARD_SCREENSHOT"); !capture.isEmpty()) {
    require(f.window.grab().save(capture + ".extension.png"), "capture extension warning");
    LanguageManager::instance().setLanguage(QStringLiteral("zh_CN"));
    QApplication::processEvents();
    require(f.window.grab().save(capture + ".zh.png"), "capture translated warning");
    action(f.window, MainWindow::tr("Night"))->trigger();
    QApplication::processEvents();
    require(f.window.grab().save(capture + ".night.png"), "capture dark warning");
    LanguageManager::instance().setLanguage(QStringLiteral("en"));
  }
  require(pending->fileController.save(pending->session, &f.window) == SaveOutcome::SkippedBusy,
          "save cannot create an empty file while awaiting consent");
  require(!pending->session.applyTextDelta(0, 0, "stray edit", true), "model rejects editing while awaiting consent");
  ParseOptions options;
  options.enableAutolink = false;
  pending->session.setParseOptions(options);
  require(!pending->session.isAsyncParseInProgress(), "preferences do not start an unconfirmed parse");
  require(f.window.openFile(path) && f.tabs->count() == 2 && f.current() == pending,
          "duplicate open reuses confirmation");
  f.tabs->setCurrentWidget(original);
  require(original->session.markdownText().toString() == originalText, "other documents remain usable");
  f.tabs->setCurrentWidget(pending);
  pending->openConfirmation->findChild<QPushButton*>("cancelFileOpen")->click();
  waitUntil([&] { return f.tabs->count() == 1; }, "cancel removes pending tab");
  require(f.current() == original && original->session.markdownText().toString() == originalText,
          "cancel returns to original document with unsaved changes intact");
  require(f.window.openFile(path), "reopen for acceptance");
  pending = f.current();
  // Replacing the bytes after the warning proves the file was not read speculatively.
  f.file("guard.binary", "Read only after confirmation\n");
  pending->openConfirmation->findChild<QPushButton*>("confirmFileOpen")->click();
  waitUntil([&] { return !pending->session.isOpenInProgress(); }, "confirmed file finishes loading");
  require(pending->session.filePath() == path &&
              pending->session.markdownText().toString() == "Read only after confirmation\n",
          "confirmation opens current bytes");
  require(pending->pendingOpenPath.isEmpty() && !pending->openConfirmation->isVisible(), "guard clears after opening");
}

void fileOpenSizeConfirmationAndStartupCancellation() {
  SettingsOverride extensionsOn{"files/extensionProtection", true};
  SettingsOverride extensions{"files/textExtensions", file_open_protection::defaultTextExtensions()};
  SettingsOverride sizeOn{"files/sizeProtection", true};
  SettingsOverride limit{"files/sizeLimitMb", 1};
  Fixture f;
  auto* startup = f.current();
  const QString path = f.file("large.md", "Large text\n");
  QFile file(path);
  require(file.open(QIODevice::ReadWrite) && file.resize(1024 * 1024 + 1), "large fixture");
  file.close();
  require(f.window.openFile(path) && f.current() == startup, "guard reuses pristine startup tab");
  require(!startup->openConfirmation->findChild<QLabel*>("extensionWarning")->isVisible() &&
              startup->openConfirmation->findChild<QLabel*>("sizeWarning")->isVisible(),
          "size warning alone");
  if (const auto capture = qEnvironmentVariable("MUFFIN_OPEN_GUARD_SCREENSHOT"); !capture.isEmpty()) {
    require(f.window.grab().save(capture + ".size.png"), "capture size warning");
  }
  action(f.window, "Source Code Mode")->trigger();
  require(startup->openConfirmation->isVisible() && startup->editor->isReadOnly(),
          "source mode keeps guard visible and read-only");
  startup->openConfirmation->findChild<QPushButton*>("cancelFileOpen")->click();
  require(f.tabs->count() == 1 && f.current() == startup && !startup->session.isOpenInProgress() &&
              startup->pendingOpenPath.isEmpty() && !startup->editor->isReadOnly(),
          "startup cancel restores editable empty document");
  const QString combined = f.file("large.binary", "Combined warning\n");
  file.setFileName(combined);
  require(file.open(QIODevice::ReadWrite) && file.resize(1024 * 1024 + 1), "combined warning fixture");
  file.close();
  require(f.window.openFile(combined), "request file triggering both guards");
  require(startup->openConfirmation->findChild<QLabel*>("extensionWarning")->isVisible() &&
              startup->openConfirmation->findChild<QLabel*>("sizeWarning")->isVisible(),
          "both warnings share one confirmation page");
  f.file("large.binary", "Confirmed combined warning\n");
  startup->openConfirmation->findChild<QPushButton*>("confirmFileOpen")->click();
  waitUntil([&] { return !startup->session.isOpenInProgress(); }, "combined confirmation loads");
  require(startup->backend->isSourceMode() && !startup->editor->isReadOnly() &&
              startup->session.markdownText().toString() == "Confirmed combined warning\n",
          "confirmed open preserves source mode");
  const QString pendingClose = f.file("close.binary", "Never loaded\n");
  require(f.window.openFile(pendingClose), "create pending tab to close");
  f.close(f.current());
  require(f.current() == startup && f.tabs->count() == 1, "closing an unconfirmed tab preserves loaded document");
  SettingsOverride extensionOff{"files/extensionProtection", false};
  SettingsOverride sizeOff{"files/sizeProtection", false};
  const QString unknown = f.file("allowed.binary", "Guard disabled\n");
  auto* opened = f.open(unknown);
  require(opened->pendingOpenPath.isEmpty() && opened->session.filePath() == unknown, "disabled guards open directly");
}

void sessionRestoreKeepsUnconfirmedFilesSeparate() {
  SettingsOverride extensionsOn{"files/extensionProtection", true};
  SettingsOverride extensions{"files/textExtensions", file_open_protection::defaultTextExtensions()};
  SettingsOverride startupMode{"files/startupBehavior", 1};
  Fixture f;
  const QString unknown = f.file("restore.binary", "Unconfirmed text\n");
  const QString known = f.file("restore.md", "Confirmed text\n");
  QSettings().setValue("session/window",
                       QVariantMap{{"version", 1}, {"files", QStringList{unknown, known}}, {"active", 0}});
  f.window.restoreStartupFile();
  require(f.tabs->count() == 2, "session restore creates exactly one tab per file");
  auto* pending = f.current();
  require(pending->pendingOpenPath == unknown && pending->session.markdownText().isEmpty(),
          "session restore waits for consent on protected files");
  auto* loaded = static_cast<DocumentTab*>(f.tabs->widget(1));
  waitUntil([&] { return !loaded->session.isOpenInProgress(); }, "normal session file loads in background");
  require(loaded->session.markdownText().toString() == "Confirmed text\n", "other session files load independently");
  pending->openConfirmation->findChild<QPushButton*>("cancelFileOpen")->click();
  require(!pending->session.isOpenInProgress() && pending->pendingOpenPath.isEmpty(), "restored guard cancels safely");
}

// Interact with the real modal prompt while its nested event loop is running.
class AnswerPrompts {
public:
  QTimer timer;
  int count = 0;
  explicit AnswerPrompts(QMessageBox::StandardButton answer) {
    timer.setInterval(5);
    QObject::connect(&timer, &QTimer::timeout, &timer, [this, answer] {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        ++count;
        require(box->button(answer) != nullptr,
                QStringLiteral("Unexpected dialog: %1 — %2").arg(box->windowTitle(), box->text()));
        box->button(answer)->click();
      }
      require(count < 10, "unexpected repeated prompts");
    });
    timer.start();
  }
};

QString contents(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "read saved document");
  return QString::fromUtf8(file.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
}

QToolButton* tabControlButton(QTabBar* bar, int index) {
  auto* controls = bar->tabButton(index, QTabBar::RightSide);
  require(controls && controls->objectName() == "TabControls", "tab owns its trailing status and control group");
  auto* button = controls->findChild<QToolButton*>();
  require(button, "tab has one close or pin button");
  return button;
}

QAction* tabMenuAction(QMenu& menu, const QString& text) {
  for (auto* item : menu.actions())
    if (item->text() == MainWindow::tr(text.toUtf8().constData()))
      return item;
  require(false, QStringLiteral("Missing tab menu action: %1").arg(text));
  return nullptr;
}

void withTabMenu(MainWindow& window, DocumentTab* document, const std::function<void(QMenu&)>& inspect) {
  auto* tabs = window.findChild<QTabWidget*>("DocumentTabs");
  auto* bar = tabs->tabBar();
  const QPoint pos = bar->tabRect(tabs->indexOf(document)).center();
  bool visited = false;
  QTimer::singleShot(0, &window, [&] {
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    require(menu && menu->objectName() == "TabContextMenu", "tab context menu opens");
    inspect(*menu);
    visited = true;
    menu->close();
  });
  QContextMenuEvent event(QContextMenuEvent::Mouse, pos, bar->mapToGlobal(pos));
  QApplication::sendEvent(bar, &event);
  require(visited, "tab context event was handled");
}

void triggerTabMenu(MainWindow& window, DocumentTab* document, const QString& text) {
  withTabMenu(window, document, [&](QMenu& menu) {
    auto* item = tabMenuAction(menu, text);
    require(item->isEnabled(), "tab menu action is enabled");
    item->trigger();
  });
}

void tabMenuLocksTheClickedDocument() {
  SettingsOverride enabled("editor/lockEditingEnabled", true);
  auto& shortcuts = ShortcutSettings::instance();
  const auto restoreShortcut = qScopeGuard([&] { shortcuts.clearOverride("edit.lock_editing"); });
  const QKeySequence custom("Ctrl+Alt+F9");
  shortcuts.setOverride("edit.lock_editing", custom);
  for (bool perTab : {true, false}) {
    SettingsOverride scope("editor/lockEditingScope", perTab ? 0 : 1);
    Fixture f;
    auto* background = f.open(f.file("background.md", "Background\n"));
    auto* active = f.open(f.file("active.md", "Active\n"));
    auto* command = f.window.findChild<QAction*>("edit.lock_editing");
    const auto checkIndicators = [&] {
      for (auto* document : {background, active}) {
        auto* controls = f.tabs->tabBar()->tabButton(f.tabs->indexOf(document), QTabBar::RightSide);
        auto* indicator = controls->findChild<QWidget*>("TabLockIndicator");
        require(indicator && indicator->isHidden() == !document->session.editingReadOnly(),
                "each tab's lock marker tracks its effective read-only state");
      }
    };
    checkIndicators();
    withTabMenu(f.window, background, [&](QMenu& menu) {
      auto* lock = tabMenuAction(menu, "Lock Editing");
      require(lock->isEnabled() && lock->isCheckable() && !lock->isChecked() && lock->shortcut() == custom,
              "tab menu exposes the unlocked target and configured shortcut");
      lock->trigger();
    });
    require(f.current() == active && background->session.editingReadOnly() && background->editor->isReadOnly() &&
                active->session.editingReadOnly() == !perTab && f.window.editingLocked() == !perTab &&
                command->isChecked() == !perTab,
            "background locking preserves the active document and respects the configured scope");
    checkIndicators();
    withTabMenu(f.window, background, [&](QMenu& menu) {
      auto* unlock = tabMenuAction(menu, "Unlock Editing");
      require(unlock->isChecked(), "tab menu reflects the clicked tab rather than the active one");
    });
    withTabMenu(f.window, active, [&](QMenu& menu) {
      require(tabMenuAction(menu, perTab ? "Lock Editing" : "Unlock Editing")->isChecked() == !perTab,
              "the active tab's menu also reflects the effective scope");
    });
    f.tabs->setCurrentWidget(background);
    require(command->isChecked() && f.window.editingLocked(),
            "activating a context-locked tab synchronizes the command");
    command->trigger();
    require(!background->session.editingReadOnly() && !active->session.editingReadOnly(),
            "the existing edit command can unlock a context-locked document");
    checkIndicators();
    command->trigger();
    f.tabs->setCurrentWidget(active);
    triggerTabMenu(f.window, background, "Unlock Editing");
    require(f.current() == active && !background->session.editingReadOnly() && !active->session.editingReadOnly() &&
                !command->isChecked(),
            "context unlock targets a background tab and synchronizes the active command");
    checkIndicators();

    command->trigger();
    SettingsOverride disabled("editor/lockEditingEnabled", false);
    f.window.applyLockEditingSettings();
    checkIndicators();
    withTabMenu(f.window, background, [&](QMenu& menu) {
      auto* lock = tabMenuAction(menu, "Lock Editing");
      require(!lock->isEnabled() && !lock->isChecked(), "tab menu respects a disabled lock preference");
      lock->trigger();
    });
    require(!background->session.editingReadOnly() && !active->session.editingReadOnly(),
            "disabled context command cannot lock a document");
  }
}

void tabLockIndicatorPreservesControlsAndFollowsTheme() {
  SettingsOverride enabled("editor/lockEditingEnabled", true);
  SettingsOverride scope("editor/lockEditingScope", 0);
  Fixture f;
  auto* locked = f.open(f.file(QStringLiteral("a-very-long-file-name-for-reading-中文.md"), "Read\n"));
  auto* active = f.open(f.file("active.md", "Active\n"));
  auto* bar = f.tabs->tabBar();
  auto* controls = bar->tabButton(f.tabs->indexOf(locked), QTabBar::RightSide);
  const auto unlockedWidth = controls->sizeHint().width();
  locked->session.setMarkdownText("Read updated\n", true);
  triggerTabMenu(f.window, locked, "Lock Editing");
  QApplication::processEvents();
  auto* indicator = controls->findChild<QWidget*>("TabLockIndicator");
  auto* close = tabControlButton(bar, f.tabs->indexOf(locked));
  require(indicator->isVisible() && !indicator->visibleRegion().isEmpty() && close->isVisible() &&
              controls->sizeHint().width() > unlockedWidth &&
              indicator->geometry().right() < close->geometry().left() && close->width() == 20,
          "the lock has its own trailing space before the unchanged close target");
  require(f.tabs->tabText(f.tabs->indexOf(locked)) == QFileInfo(locked->session.filePath()).fileName() + " *" &&
              bar->accessibleTabName(f.tabs->indexOf(locked)).contains("Editing Locked") &&
              f.tabs->tabToolTip(f.tabs->indexOf(locked)).contains("Editing is locked"),
          "file name and dirty marker stay intact with an accessible lock status");
  indicator->ensurePolished();
  const auto lightInk = indicator->palette().color(QPalette::WindowText);
  action(f.window, "Night")->trigger();
  QApplication::processEvents();
  const auto darkInk = indicator->palette().color(QPalette::WindowText);
  require(lightInk != darkInk && darkInk.lightness() > lightInk.lightness(),
          "lock ink follows the light and dark chrome palettes");
  if (const auto capture = qEnvironmentVariable("MUFFIN_LOCK_TAB_SCREENSHOT"); !capture.isEmpty()) {
    require(f.window.grab().save(capture + ".night.png"), "capture dark lock indicator");
    action(f.window, "GitHub")->trigger();
    QApplication::processEvents();
    require(f.window.grab().save(capture + ".png"), "capture light lock indicator");
  }
  triggerTabMenu(f.window, locked, "Pin Tab");
  controls = bar->tabButton(f.tabs->indexOf(locked), QTabBar::RightSide);
  require(!controls->findChild<QWidget*>("TabLockIndicator")->isHidden() &&
              tabControlButton(bar, f.tabs->indexOf(locked))->objectName() == "PinTabButton",
          "lock and pin indicators coexist after reordering");
  QTest::mouseClick(tabControlButton(bar, f.tabs->indexOf(locked)), Qt::LeftButton);
  require(!locked->pinned && locked->session.editingReadOnly() && f.current() == active,
          "the pin button still unpins without unlocking or activating the document");
  triggerTabMenu(f.window, locked, "Unlock Editing");
  controls = bar->tabButton(f.tabs->indexOf(locked), QTabBar::RightSide);
  require(controls->findChild<QWidget*>("TabLockIndicator")->isHidden() &&
              controls->sizeHint().width() == unlockedWidth &&
              !bar->accessibleTabName(f.tabs->indexOf(locked)).contains("Editing Locked"),
          "unlock removes the indicator and releases its space");
  locked->session.document().setModified(false);
  QTest::mouseClick(tabControlButton(bar, f.tabs->indexOf(locked)), Qt::LeftButton);
  require(f.tabs->count() == 1 && f.current() == active, "close button still closes the clicked background tab");
}

class TabCloseSideStyle final : public QProxyStyle {
public:
  explicit TabCloseSideStyle(QTabBar::ButtonPosition side)
      : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))), side_(side) {}

  int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr,
                QStyleHintReturn* data = nullptr) const override {
    if (hint == SH_TabBar_CloseButtonPosition)
      return side_;
    return QProxyStyle::styleHint(hint, option, widget, data);
  }

private:
  QTabBar::ButtonPosition side_;
};

void tabButtonsHaveSingleOwnerAcrossPlatformStyles() {
  const QString originalStyle = QApplication::style()->objectName();
  const auto restoreStyle = qScopeGuard([&] {
    if (auto* style = QStyleFactory::create(originalStyle))
      QApplication::setStyle(style);
  });
  // macOS creates native close buttons on the left and paints them on hover.
  // Exercise that style hint here too, rather than relying on the host OS.
  for (const auto side : {QTabBar::LeftSide, QTabBar::RightSide}) {
    QApplication::setStyle(new TabCloseSideStyle(side));
    Fixture f;
    auto* bar = f.tabs->tabBar();
    require(bar->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, bar) == side,
            "fixture should use the requested native close-button side");
    const auto checkButtons = [&] {
      QApplication::processEvents();
      require(!f.tabs->tabsClosable() && !bar->tabsClosable(),
              "custom tab controls must own closing without Qt creating another button");
      for (int index = 0; index < f.tabs->count(); ++index) {
        auto* document = static_cast<DocumentTab*>(f.tabs->widget(index));
        require(bar->tabButton(index, QTabBar::LeftSide) == nullptr,
                "tab must not retain a native left-side close button");
        auto* button = tabControlButton(bar, index);
        require(button && button->objectName() == (document->pinned ? "PinTabButton" : "CloseTabButton"),
                "each tab should expose exactly its custom close or pin control");
        if (bar->isVisible()) {
          QTest::mouseMove(bar, bar->tabRect(index).center());
          require(bar->tabButton(index, QTabBar::LeftSide) == nullptr, "hover must not expose a second close control");
        }
      }
    };
    checkButtons();
    auto* a = f.open(f.file("buttons-a.md", "A\n"));
    auto* b = f.open(f.file("buttons-b.md", "B\n"));
    auto* c = f.open(f.file("buttons-c.md", "C\n"));
    checkButtons();
    triggerTabMenu(f.window, b, "Pin Tab");
    checkButtons();
    auto* pin = tabControlButton(bar, f.tabs->indexOf(b));
    QTest::mouseClick(pin, Qt::LeftButton);
    require(!b->pinned && f.tabs->count() == 3, "pin control should unpin without closing the document");
    checkButtons();
    f.tabs->setCurrentWidget(c);
    bar->moveTab(f.tabs->indexOf(a), 0);
    auto* close = tabControlButton(bar, f.tabs->indexOf(a));
    QTest::mouseClick(close, Qt::LeftButton);
    require(f.tabs->count() == 2 && f.tabs->indexOf(a) < 0 && f.current() == c,
            "custom close should target the reordered background tab exactly once");
    checkButtons();
    action(f.window, "Night")->trigger();
    checkButtons();
    action(f.window, "GitHub")->trigger();
    checkButtons();
    QApplication::setStyle(new TabCloseSideStyle(side == QTabBar::LeftSide ? QTabBar::RightSide : QTabBar::LeftSide));
    checkButtons();
  }
}

MainWindow* otherWindow(const QList<QWidget*>& before) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (!before.contains(widget))
      if (auto* window = qobject_cast<MainWindow*>(widget))
        return window;
  }
  require(false, "new editor window exists");
  return nullptr;
}

void deleteTestWindow(MainWindow* window) {
  DraftRecovery recovery;
  auto* tabs = window->findChild<QTabWidget*>("DocumentTabs");
  for (int i = 0; i < tabs->count(); ++i) {
    auto* document = static_cast<DocumentTab*>(tabs->widget(i));
    document->session.document().setModified(false);
    recovery.markClean(document->session.filePath(), document->draftKey);
  }
  delete window;
}

void tabMenuTargetsBackgroundTabsAndStopsOnCancel() {
  Fixture f;
  auto* a = f.open(f.file("a.md", "A\n"));
  auto* b = f.open(f.file("b.md", "B\n"));
  auto* c = f.open(f.file("c.md", "C\n"));
  auto* d = f.open(f.file("d.md", "D\n"));
  c->editorController.applyMarkdownTextWithUndo("Edited C\n", "edit");
  triggerTabMenu(f.window, a, "Copy Path");
  require(QApplication::clipboard()->text() == QDir::toNativeSeparators(a->session.filePath()) && f.current() == d,
          "right-click actions target the clicked tab without switching documents");
  {
    AnswerPrompts answer(QMessageBox::Cancel);
    triggerTabMenu(f.window, b, "Close Tabs to the Right");
    require(answer.count == 1 && f.tabs->count() == 4 && f.current() == d,
            "cancel stops the batch before closing later tabs and retains active document");
  }
  {
    AnswerPrompts answer(QMessageBox::Save);
    triggerTabMenu(f.window, b, "Close Tabs to the Right");
    require(answer.count == 1 && f.tabs->count() == 2 && f.tabs->widget(0) == a && f.tabs->widget(1) == b,
            "close-to-right closes exactly the visible tabs after the target");
    require(contents(f.directory.filePath("c.md")) == "Edited C\n", "batch close saves the correct buffer");
  }
  withTabMenu(f.window, b, [&](QMenu& menu) {
    require(!tabMenuAction(menu, "Close Tabs to the Right")->isEnabled(), "no right-hand tabs disables close-to-right");
#ifdef Q_OS_MACOS
    tabMenuAction(menu, "Reveal in Finder");
#elif defined(Q_OS_WIN)
    tabMenuAction(menu, "Reveal in File Explorer");
#else
    tabMenuAction(menu, "Reveal in File Manager");
#endif
  });
  // A drag changes the meaning of "to the right" immediately.
  f.tabs->tabBar()->moveTab(0, 1);
  triggerTabMenu(f.window, b, "Close Tabs to the Right");
  require(f.tabs->count() == 1 && f.current() == b, "close-to-right follows dragged order");
}

void pinnedTabsSurviveBulkCloseAndRestore() {
  SettingsOverride startupMode{"files/startupBehavior", 1};
  Fixture f;
  auto* a = f.open(f.file("a.md", "A\n"));
  auto* b = f.open(f.file("b.md", "B\n"));
  auto* pinned = f.open(f.file("pinned.md", "Pinned\n"));
  b->editorController.applyMarkdownTextWithUndo("Edited B\n", "edit");
  triggerTabMenu(f.window, pinned, "Pin Tab");
  require(pinned->pinned && f.tabs->widget(0) == pinned, "pin puts the tab at the front");
  require(tabControlButton(f.tabs->tabBar(), 0)->objectName() == "PinTabButton",
          "pin replaces accidental close button");
  if (const auto capture = qEnvironmentVariable("MUFFIN_TAB_MENU_SCREENSHOT"); !capture.isEmpty()) {
    LanguageManager::instance().setLanguage(QStringLiteral("zh_CN"));
    withTabMenu(f.window, b, [&](QMenu& menu) {
      require(menu.grab().save(capture + ".menu.png"), "capture translated tab menu");
      require(f.window.grab().save(capture + ".window.png"), "capture pinned tab appearance");
    });
    LanguageManager::instance().setLanguage(QStringLiteral("en"));
  }
  f.tabs->tabBar()->moveTab(0, 2);
  require(f.tabs->widget(0) == pinned, "dragging keeps fixed tabs before other tabs");
  QTest::mouseClick(f.tabs->tabBar(), Qt::MiddleButton, Qt::NoModifier, f.tabs->tabBar()->tabRect(0).center());
  require(f.tabs->indexOf(pinned) == 0, "middle click preserves pinned tabs");
  {
    Fixture restored;
    restored.window.restoreStartupFile();
    auto* restoredPin = static_cast<DocumentTab*>(restored.tabs->widget(0));
    require(restoredPin->pinned && restoredPin->session.filePath() == pinned->session.filePath(),
            "session restores pin and order");
  }
  action(f.window, "New")->trigger();
  auto* blank = f.current();
  triggerTabMenu(f.window, b, "Close Saved Tabs");
  require(f.tabs->count() == 3 && f.tabs->indexOf(a) < 0 && f.tabs->indexOf(blank) >= 0 && f.tabs->indexOf(b) >= 0,
          "close-saved skips dirty, untitled and pinned tabs");
  triggerTabMenu(f.window, b, "Close Others");
  require(f.tabs->count() == 2 && f.tabs->indexOf(pinned) == 0 && f.tabs->indexOf(b) == 1,
          "close-others preserves clicked tab and pinned tabs");
  {
    AnswerPrompts answer(QMessageBox::Discard);
    triggerTabMenu(f.window, b, "Close All Tabs");
    require(answer.count == 1 && f.tabs->count() == 1 && f.current() == pinned && f.tabs->tabBar()->isVisible(),
            "close-all preserves a visible pinned tab");
  }
  withTabMenu(f.window, pinned, [&](QMenu& menu) {
    require(!tabMenuAction(menu, "Close All Tabs")->isEnabled(), "bulk close disables when only pinned tabs remain");
  });
  triggerTabMenu(f.window, pinned, "Unpin Tab");
  require(!pinned->pinned && !f.tabs->tabBar()->isVisible(), "unpin restores ordinary single-document appearance");
}

void movingTabsPreservesHistoryAndRebindsWindowLifetime() {
  Fixture f;
  const QString path = f.file("move.md", QStringLiteral("Paragraph.\n\n").repeated(140));
  auto* document = f.open(path);
  action(f.window, "Source Code Mode")->trigger();
  document->editor->setCursorPosition(50);
  document->editor->insertText("Unsaved move");
  document->editor->setSelection(32, 64);
  auto* source = document->editor->findChild<VirtualSourceEdit*>();
  source->verticalScrollBar()->setValue(source->verticalScrollBar()->maximum() / 2);
  const auto scroll = source->verticalScrollBar()->value();
  const QString edited = document->session.markdownText().toString();
  auto* other = f.open(f.file("other.md", "Other\n"));
  const auto before = QApplication::topLevelWidgets();
  triggerTabMenu(f.window, document, "Move Tab to New Window");
  auto* movedWindow = otherWindow(before);
  auto* movedTabs = movedWindow->findChild<QTabWidget*>("DocumentTabs");
  require(movedTabs->currentWidget() == document && f.tabs->count() == 1 && f.current() == other,
          "move transfers the same editing page and preserves the source's other tab");
  waitUntil([&] { return !document->pendingViewState; }, "moved viewport restored");
  require(document->editor->canUndo() && document->editor->anchorPosition() == 32 &&
              document->editor->cursorPosition() == 64 && source->verticalScrollBar()->value() == scroll,
          "move retains source undo, selection and scroll");
  triggerTabMenu(*movedWindow, document, "Pin Tab");
  const auto beforeAgain = QApplication::topLevelWidgets();
  triggerTabMenu(*movedWindow, document, "Move Tab to New Window");
  auto* finalWindow = otherWindow(beforeAgain);
  require(movedTabs->count() == 1 && static_cast<DocumentTab*>(movedTabs->widget(0))->session.filePath().isEmpty(),
          "moving the last document leaves a fresh page in its old window");
  deleteTestWindow(movedWindow);
  QApplication::processEvents();
  action(*finalWindow, "Undo")->trigger();
  require(document->session.markdownText().toString() != edited &&
              !document->session.markdownText().toString().contains("Unsaved move"),
          "undo still works after the previous owning window is destroyed");
  action(*finalWindow, "Redo")->trigger();
  require(finalWindow->saveCurrentDocument() && contents(path) == edited, "save is rebound to the destination window");
  require(finalWindow->windowTitle().startsWith("move.md -"), "clean state updates the destination title");
  deleteTestWindow(finalWindow);
}

void pendingExternalChangeFollowsMovedTab() {
  Fixture f;
  auto* document = f.open(f.file("moving-conflict.md", "Original\n"));
  auto* other = f.open(f.file("staying.md", "Staying\n"));
  externalWrite(document, "Latest revision before moving the background tab\n");
  QApplication::processEvents();
  require(reloadQuestions(f.window).isEmpty(), "moving fixture starts with a silent background change");
  const auto before = QApplication::topLevelWidgets();
  triggerTabMenu(f.window, document, "Move Tab to New Window");
  auto* moved = otherWindow(before);
  waitUntil([&] { return reloadQuestions(*moved).size() == 1; }, "moved tab asks when activated in the new window");
  require(f.current() == other && reloadQuestions(f.window).isEmpty(), "old window retains no stale question");
  reloadQuestions(*moved).first()->button(QMessageBox::Yes)->click();
  QApplication::processEvents();
  require(document->session.markdownText().toString() == "Latest revision before moving the background tab\n" &&
              reloadQuestions(*moved).isEmpty(),
          "moved tab reloads the latest pending revision once");
  deleteTestWindow(moved);
}

void copyingTabsIncludesUnsavedContentAndIndependentBuffers() {
  Fixture f;
  auto* document = f.open(f.file("copy.md", "Original\r\n"));
  document->editorController.applyMarkdownTextWithUndo("Unsaved copy\n", "edit");
  document->editorController.moveDocumentEnd();
  auto* other = f.open(f.file("other.md", "Other\n"));
  const auto before = QApplication::topLevelWidgets();
  triggerTabMenu(f.window, document, "Copy Tab to New Window");
  auto* copiedWindow = otherWindow(before);
  auto* copyTabs = copiedWindow->findChild<QTabWidget*>("DocumentTabs");
  auto* copy = static_cast<DocumentTab*>(copyTabs->currentWidget());
  waitUntil([&] { return !copy->pendingViewState; }, "copied caret restored");
  require(copy != document && f.current() == other && f.tabs->count() == 2 && copy->session.document().isModified() &&
              copy->session.markdownText().toString() == "Unsaved copy\n",
          "copy includes unsaved text without removing the original");
  require(copy->session.filePath() == document->session.filePath() &&
              copy->session.fileFormat().lineEnding == TextLineEnding::Crlf &&
              copy->session.fileBaselineMtime() == document->session.fileBaselineMtime() &&
              copy->session.fileBaselineSize() == document->session.fileBaselineSize(),
          "copy retains path, encoding and disk conflict baseline");
  copy->editorController.applyMarkdownTextWithUndo("Independent copy\n", "edit copy");
  require(document->session.markdownText().toString() == "Unsaved copy\n",
          "editing the copy leaves the original buffer intact");
  deleteTestWindow(copiedWindow);
  action(f.window, "New")->trigger();
  auto* untitled = f.current();
  action(f.window, "Source Code Mode")->trigger();
  untitled->editor->insertText("Untitled copy");
  const auto beforeUntitled = QApplication::topLevelWidgets();
  triggerTabMenu(f.window, untitled, "Copy Tab to New Window");
  auto* untitledWindow = otherWindow(beforeUntitled);
  auto* untitledCopy =
      static_cast<DocumentTab*>(untitledWindow->findChild<QTabWidget*>("DocumentTabs")->currentWidget());
  require(untitledCopy->session.filePath().isEmpty() && untitledCopy->session.document().isModified() &&
              untitledCopy->session.markdownText().toString() == "Untitled copy" &&
              untitledCopy->backend->isSourceMode(),
          "untitled copies retain text and mode and still require Save As");
  withTabMenu(*untitledWindow, untitledCopy, [&](QMenu& menu) {
    require(!tabMenuAction(menu, "Copy Path")->isEnabled(), "untitled copy has no path to copy");
  });
  deleteTestWindow(untitledWindow);
}

void pendingTabsDisableWindowTransfer() {
  SettingsOverride extensionsOn{"files/extensionProtection", true};
  SettingsOverride extensions{"files/textExtensions", file_open_protection::defaultTextExtensions()};
  Fixture f;
  f.open(f.file("loaded.md", "Loaded\n"));
  const QString path = f.file("unknown.binary", "Not loaded\n");
  require(f.window.openFile(path), "request protected file");
  auto* pending = f.current();
  withTabMenu(f.window, pending, [&](QMenu& menu) {
    require(!tabMenuAction(menu, "Move Tab to New Window")->isEnabled() &&
                !tabMenuAction(menu, "Copy Tab to New Window")->isEnabled(),
            "unconfirmed buffers cannot transfer as empty documents");
    require(tabMenuAction(menu, "Copy Path")->isEnabled(), "pending file still has a path to copy");
  });
  triggerTabMenu(f.window, pending, "Close Tab");
  require(f.tabs->count() == 1, "context close cancels the protected open without reading it");
}

void closeFolder(MainWindow& window) {
  auto* sidebar = window.findChild<SidebarWidget*>();
  auto* tree = sidebar->findChild<QTreeView*>("FileTree");
  bool triggered = false;
  QTimer::singleShot(0, &window, [&] {
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    require(menu != nullptr, "file tree context menu opens");
    QAction* close = nullptr;
    bool collapse = false, expand = false, search = false;
    for (auto* item : menu->actions()) {
      if (item->text() == "Expand All Folders")
        expand = true;
      if (item->text() == QString::fromUtf8("Find in Folder…"))
        search = true;
      if (item->text() == "Close Folder")
        close = item;
      if (item->text() == "Collapse All Folders")
        collapse = true;
    }
    require(close && collapse && expand && search, "context menu exposes folder search, expand, collapse, and close");
    close->trigger();
    triggered = true;
    menu->close();
  });
  sidebar->fileTreeContextMenuRequested(sidebar->folderRoot(), true, false, tree->mapToGlobal(tree->rect().center()));
  require(triggered, "closing the workspace remains accessible from the context menu");
}

void tabBarOnlyForMultipleDocuments() {
  Fixture f;
  auto* bar = f.tabs->tabBar();
  auto* plus = f.window.findChild<QToolButton*>(QStringLiteral("NewTabButton"));
  auto* sidebar = f.window.findChild<SidebarWidget*>();
  auto checkSingle = [&] {
    QApplication::processEvents();
    require(f.tabs->count() == 1 && !bar->isVisible() && !plus->isVisible(),
            "single document hides tab bar and new-tab button");
    require(f.current()->mapTo(f.tabs, QPoint(0, 0)).y() <= 1, "hidden tab strip leaves no empty row above the editor");
  };
  checkSingle();
  const QString first =
      f.file(QStringLiteral("README.md"), QStringLiteral("# Single document\n\nWrite without a tab strip.\n"));
  const QString second = f.file(QStringLiteral("example.md"), QStringLiteral("# Second document\n"));
  sidebar->fileOpenRequested(first);
  DocumentTab* a = f.current();
  checkSingle();
  require(a->session.filePath() == first, "sidebar opens the first document");
  if (const QString screenshot = qEnvironmentVariable("MUFFIN_TAB_SCREENSHOT"); !screenshot.isEmpty()) {
    require(f.window.grab().save(screenshot + QStringLiteral(".single.png")),
            "save single-document appearance capture");
  }
  sidebar->fileOpenRequested(second);
  DocumentTab* b = f.current();
  QApplication::processEvents();
  require(a != b && f.tabs->count() == 2 && bar->isVisible() && plus->isVisible(),
          "opening the second sidebar document shows the tab strip");
  require(b->mapTo(f.tabs, QPoint(0, 0)).y() >= bar->height(), "visible tab strip sits above the editor");
  sidebar->fileOpenRequested(first);
  require(f.tabs->count() == 2 && f.current() == a, "reopening a sidebar document selects its tab");
  f.close(b);
  checkSingle();
  sidebar->fileOpenRequested(second);
  QApplication::processEvents();
  require(bar->isVisible() && plus->isVisible(), "tab strip reappears when a second document is reopened");
  f.close(f.current());
  checkSingle();
  {
    AnswerPrompts answer(QMessageBox::Ok);
    require(!f.window.openFile(f.directory.filePath(QStringLiteral("missing.md"))),
            "failed second open reports failure");
    checkSingle();
  }
}

void formatActionsFollowSelectionAndDocumentChanges() {
  Fixture f;
  auto* document = f.current();
  const QString text = QStringLiteral("**bold** *italic* <u>under</u> `code` $x$ ~~strike~~ plain\n");
  document->session.setMarkdownText(text, true);
  auto* paragraph = document->session.document().root().children().front().get();
  const QStringList names{"Bold", "Italic", "Underline", "Inline Code", "Inline Formula", "Strikethrough"};
  const QStringList tokens{"bold", "italic", "under", "code", "x", "strike", "plain"};
  auto at = [&](const QString& token) {
    CursorPosition position;
    position.blockId = position.text.nodeId = paragraph->id();
    position.text.sourceOffset = text.indexOf(token) + 1;
    position.text.textOffset = position.text.sourceOffset;
    document->editorController.selection().setCursorPosition(position);
    QApplication::processEvents();
  };
  for (int i = 0; i < tokens.size(); ++i) {
    at(tokens[i]);
    const auto expected = document->renderCommands.currentInlineFormats();
    const bool checked[]{expected.bold, expected.italic,     expected.underline,
                         expected.code, expected.inlineMath, expected.strikethrough};
    for (int j = 0; j < names.size(); ++j)
      require(action(f.window, names[j])->isChecked() == checked[j],
              "format menu must match current selection snapshot");
  }
  at("bold");
  auto* second = f.open(f.file("plain.md", "plain second document\n"));
  CursorPosition plain;
  plain.blockId = plain.text.nodeId = second->session.document().root().children().front()->id();
  second->editorController.selection().setCursorPosition(plain);
  QApplication::processEvents();
  for (const auto& name : names)
    require(!action(f.window, name)->isChecked(), "format snapshot must not leak across tabs");
  f.tabs->setCurrentWidget(document);
  QApplication::processEvents();
  require(action(f.window, "Bold")->isChecked(), "reactivating tab refreshes formatting");
}

void sharedCursorQueriesMatchIndependentQueries() {
  Fixture f;
  auto* document = f.current();
  const QStringList sources{"**bold** *italic* <u>under</u> `code` $x$ ~~strike~~ plain\n",
                            "prefix ![alt](image.png) and <img src=\"other.png\"> suffix\n",
                            "<img src=\"standalone.png\">\n",
                            "- **nested** ![alt](list.png)\n",
                            "| A | B |\n|---|---|\n| **bold** | ![alt](cell.png) |\n",
                            "```cpp\ntext\n```\n"};
  bool sawImage = false, sawBold = false;
  for (const auto& source : sources) {
    document->session.setMarkdownText(source, true);
    document->editorController.selection().clear();
    require(document->renderCommands.currentCursorState().imageSource.isEmpty(), "empty cursor has no image");
    const auto& children = document->session.document().root().children();
    require(!children.empty(), "cursor query fixture has a block");
    auto* block = children.front().get();
    for (qsizetype offset = 0; offset < source.size(); ++offset) {
      CursorPosition cursor;
      cursor.blockId = cursor.text.nodeId = block->id();
      cursor.text.sourceOffset = offset;
      cursor.text.textOffset = offset;
      document->editorController.selection().setCursorPosition(cursor);
      const auto shared = document->renderCommands.currentCursorState();
      sawImage |= !shared.imageSource.isEmpty();
      sawBold |= shared.formats.bold;
      const auto formats = document->renderCommands.currentInlineFormats();
      require(shared.formats.bold == formats.bold && shared.formats.italic == formats.italic &&
                  shared.formats.underline == formats.underline && shared.formats.code == formats.code &&
                  shared.formats.strikethrough == formats.strikethrough &&
                  shared.formats.inlineMath == formats.inlineMath,
              "shared formats match individual query at each source position");
      require(shared.imageSource == document->renderCommands.imageSrcAtCursor(),
              "shared image query matches individual query");
    }
    auto range = document->editorController.selection().selection();
    range.anchor.text.textOffset = 0;
    range.anchor.text.sourceOffset = 0;
    document->editorController.selection().setSelection(range);
    const auto shared = document->renderCommands.currentCursorState();
    const auto formats = document->renderCommands.currentInlineFormats();
    require(shared.formats.bold == formats.bold && shared.formats.italic == formats.italic &&
                shared.formats.underline == formats.underline && shared.formats.code == formats.code &&
                shared.formats.strikethrough == formats.strikethrough &&
                shared.formats.inlineMath == formats.inlineMath,
            "selection queries retain independent format semantics");
  }
  require(sawImage && sawBold, "query fixtures exercise images and formatted spans");
}

void retainsDocumentsAndRenderState() {
  Fixture f;
  const QString original = QStringLiteral("# First\n\n") + QStringLiteral("A paragraph of text.\n\n").repeated(180);
  const QString first = f.file(QStringLiteral("first.md"), original);
  const QString second = f.file(QStringLiteral("second.md"), QStringLiteral("# Second\n\nOther content.\n"));
  DocumentTab* a = f.open(first);
  require(f.tabs->count() == 1, "pristine startup tab reused");
  a->editorController.applyMarkdownTextWithUndo(original + QStringLiteral("Edited A.\n"), QStringLiteral("edit"));
  require(a->editorController.moveDocumentEnd(), "move caret to end");
  QApplication::processEvents();
  a->renderView->verticalScrollBar()->setValue(a->renderView->verticalScrollBar()->maximum() / 2);
  const int scroll = a->renderView->verticalScrollBar()->value();
  const qsizetype caret = a->editorController.selection().cursorPosition().text.sourceOffset;
  require(scroll > 0, "nonzero render scroll");
  require(f.tabs->tabText(0).endsWith(QStringLiteral(" *")), "dirty marker on tab");

  DocumentTab* b = f.open(second);
  require(f.tabs->count() == 2 && a != b, "opening a file adds a document");
  require(!b->editorController.canUndo(), "new file has independent undo history");
  require(a->session.markdownText().toString().endsWith(QStringLiteral("Edited A.\n")), "opening B retains unsaved A");
  require(f.window.openFile(QFileInfo(first).absolutePath() + QStringLiteral("/./first.md")), "reopen equivalent path");
  require(f.tabs->count() == 2 && f.current() == a, "equivalent path activates existing tab");
  QApplication::processEvents();
  require(a->renderView->verticalScrollBar()->value() == scroll, "switch preserves render scroll");
  require(a->editorController.selection().cursorPosition().text.sourceOffset == caret, "switch preserves render caret");
  action(f.window, QStringLiteral("Undo"))->trigger();
  require(a->session.markdownText().toString() == original, "undo still belongs to A");
  require(b->session.markdownText().toString().contains(QStringLiteral("Second")), "undo leaves B intact");
  f.tabs->tabBar()->moveTab(0, 1);
  require(f.window.openFile(first) && f.current() == a, "reordered tabs still resolve by document");
}

void sourceLayoutUsesAvailableWorkspace() {
  SettingsOverride readingWidth{"appearance/contentWidthPx", 640};
  Fixture f;
  const QString text = QStringLiteral(
      "# CLAUDE.md\n\n"
      "## Build\n\n"
      "This project uses Conan-generated CMake presets. Use the Release preset for local verification:\n\n"
      "```powershell\n"
      "conan install . -s build_type=Release -s compiler.cppstd=20 --build=missing\n"
      "cmake --preset conan-default\n"
      "cmake --build --preset conan-release\n"
      "```\n\n"
      "Do not use plain `cmake --build build` for verification on this workspace. The generated Visual Studio project may default to Debug, while the Conan Qt environment is configured for Release.\n\n"
      "## Test\n\n"
      "Run the full test suite with:\n\n"
      "```powershell\nctest --preset conan-release --output-on-failure\n```\n\n"
      "## Notes\n\n"
      "- Source mode uses the available workspace.\n"
      "- Markdown headings keep the same font size as body text.\n"
      "- Rendered mode keeps its own reading width.\n");
  auto* document = f.open(f.file(QStringLiteral("CLAUDE.md"), text));
  action(f.window, QStringLiteral("Source Code Mode"))->trigger();
  auto* source = document->editor->findChild<VirtualSourceEdit*>();
  require(source != nullptr && document->backend->isSourceMode(), "source workspace is visible");
  for (int width : {1680, 680}) {
    f.window.resize(width, 800);
    QApplication::processEvents();
    require(source->width() == document->editor->width() && source->x() == 0,
            "source canvas must fill wide and narrow workspaces regardless of the rendered reading width");
    require(source->viewport()->width() > 0, "source text remains usable in a narrow workspace");
  }
  if (const QString screenshot = qEnvironmentVariable("MUFFIN_SOURCE_SCREENSHOT"); !screenshot.isEmpty()) {
    f.window.resize(1600, 900);
    QApplication::processEvents();
    require(f.window.grab().save(screenshot), "save source-mode appearance capture");
  }
}

void sourceStateAndKeyboardNavigation() {
  Fixture f;
  DocumentTab* a = f.open(f.file(QStringLiteral("a.md"), QStringLiteral("Source A\n\n").repeated(150)));
  action(f.window, QStringLiteral("Source Code Mode"))->trigger();
  require(a->backend->isSourceMode(), "switch A to source mode");
  a->editor->setCursorPosition(450);
  a->editor->insertText(QStringLiteral("edited"));
  auto* source = a->editor->findChild<VirtualSourceEdit*>();
  require(source != nullptr, "source canvas");
  source->verticalScrollBar()->setValue(source->verticalScrollBar()->maximum() / 2);
  const int scroll = source->verticalScrollBar()->value();
  const qsizetype caret = a->editor->cursorPosition();
  DocumentTab* b = f.open(f.file(QStringLiteral("b.md"), QStringLiteral("Source B\n")));
  require(b->backend->isSourceMode(), "new tab inherits current view mode");
  b->editor->insertText(QStringLiteral("B edit"));
  b->viewStack->currentWidget()->setFocus();
  QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab, Qt::ControlModifier);
  require(f.current() == a, "Ctrl+Tab switches from source canvas");
  require(a->editor->cursorPosition() == caret && source->verticalScrollBar()->value() == scroll,
          "source caret and scroll retained");
  action(f.window, QStringLiteral("Undo"))->trigger();
  require(!a->session.markdownText().toString().contains(QStringLiteral("edited")), "source undo retained");
  require(b->session.markdownText().toString().contains(QStringLiteral("B edit")), "source undo isolated");
  action(f.window, QStringLiteral("Source Code Mode"))->trigger();
  require(!a->backend->isSourceMode(), "A returns to render mode");
  f.tabs->setCurrentWidget(b);
  require(action(f.window, QStringLiteral("Source Code Mode"))->isChecked(), "mode action follows active tab");
  f.tabs->setCurrentWidget(a);
  a->renderView->setFocus();
  QTest::keyClick(a->renderView, Qt::Key_Tab, Qt::ControlModifier);
  require(f.current() == b, "Ctrl+Tab switches from render canvas");
  QTest::keyClick(QApplication::focusWidget(), Qt::Key_N, Qt::ControlModifier);
  require(f.tabs->count() == 3 && f.current()->session.filePath().isEmpty(), "Ctrl+N creates an untitled tab");
  f.current()->editor->insertText(QStringLiteral("Untitled edits"));
  f.open(f.file(QStringLiteral("c.md"), QStringLiteral("C\n")));
  require(f.tabs->count() == 4, "edited untitled tab is retained when opening a file");
  action(f.window, QStringLiteral("New"))->trigger();
  DocumentTab* empty = f.current();
  empty->editor->insertText(QStringLiteral("Undoable draft"));
  empty->editor->undo();
  require(empty->session.markdownText().isEmpty() && empty->editor->canRedo(), "empty draft retains redo history");
  f.open(f.file(QStringLiteral("d.md"), QStringLiteral("D\n")));
  require(f.tabs->count() == 6 && f.tabs->indexOf(empty) >= 0,
          "opening a file preserves an empty draft's redo history");
}

void closesWithSaveDiscardAndCancel() {
  Fixture f;
  const QString path = f.file(QStringLiteral("a.md"), QStringLiteral("A\n"));
  DocumentTab* a = f.open(path);
  a->editorController.applyMarkdownTextWithUndo(QStringLiteral("Edited A\n"), QStringLiteral("edit"));
  DocumentTab* b = f.open(f.file(QStringLiteral("b.md"), QStringLiteral("B\n")));
  {
    AnswerPrompts answer(QMessageBox::Cancel);
    f.close(a);
    require(answer.count == 1 && f.tabs->count() == 2 && f.current() == b,
            "cancel background close preserves tabs and active document");
  }
  {
    AnswerPrompts answer(QMessageBox::Save);
    f.close(a);
    require(answer.count == 1 && f.tabs->count() == 1 && f.current() == b, "save closes just A");
    require(contents(path) == QStringLiteral("Edited A\n"), "close saves the correct document");
  }
  b->editorController.applyMarkdownTextWithUndo(QStringLiteral("Unsaved B\n"), QStringLiteral("edit"));
  {
    AnswerPrompts answer(QMessageBox::Discard);
    f.close(b);
    require(answer.count == 1 && f.tabs->count() == 1 && f.current()->session.filePath().isEmpty(),
            "closing last tab leaves a fresh untitled page");
  }
  auto* newButton = f.window.findChild<QToolButton*>(QStringLiteral("NewTabButton"));
  f.window.activateWindow();
  f.current()->renderView->setFocus();
  QApplication::processEvents();
  QTest::keyClick(f.current()->renderView, Qt::Key_N, Qt::ControlModifier);
  QApplication::processEvents();
  require(f.tabs->count() == 2, "Ctrl+N opens a tab while the tab strip is hidden");
  require(newButton->isVisible(), "new-tab button appears after creating a second tab");
  QTest::mouseClick(newButton, Qt::LeftButton);
  require(f.tabs->count() == 3, "visible plus opens a tab");
  auto* bar = f.tabs->tabBar();
  QTest::mouseClick(bar, Qt::MiddleButton, Qt::NoModifier, bar->tabRect(0).center());
  require(f.tabs->count() == 2, "middle click closes a tab");
  action(f.window, QStringLiteral("Close Tab"))->trigger();
  require(f.tabs->count() == 1 && !bar->isVisible(), "closing down to one document hides the tab strip");
  action(f.window, QStringLiteral("Close Tab"))->trigger();
  require(f.tabs->count() == 1 && f.window.isVisible(), "Ctrl+W action closes tab and keeps window usable");
}

void backgroundLoadingAndPersistence() {
  Fixture f;
  const QString first = f.file(QStringLiteral("large.md"),
                               QStringLiteral("# Large\n\n") + QStringLiteral("Long paragraph.\n\n").repeated(12000));
  const QString second = f.file(QStringLiteral("small.md"), QStringLiteral("# Small\n\nSmall body.\n"));
  require(f.window.openFile(first), "start async large open");
  DocumentTab* a = f.current();
  require(a->session.isAsyncParseInProgress(), "large document still loading");
  require(f.window.openFile(second), "open second while first is parsing");
  DocumentTab* b = f.current();
  waitUntil([a, b] { return !a->session.isAsyncParseInProgress() && !b->session.isAsyncParseInProgress(); },
            "both parses finish");
  require(f.current() == b && f.window.windowTitle().startsWith(QStringLiteral("small.md")),
          "background parse leaves active title intact");
  require(a->session.markdownText().toString().contains(QStringLiteral("Large")) &&
              b->editor->text().contains(QStringLiteral("Small")),
          "parses update the owning documents");
  a->session.setMarkdownText(QStringLiteral("Dirty A\n"), true);
  b->session.setMarkdownText(QStringLiteral("Dirty B\n"), true);
  QMetaObject::invokeMethod(a->draftTimer, "timeout", Qt::DirectConnection);
  QMetaObject::invokeMethod(b->draftTimer, "timeout", Qt::DirectConnection);
  DraftRecovery recovery;
  waitUntil(
      [&] {
        return a->lastDraftSnapshotRevision == a->session.document().revision() &&
               b->lastDraftSnapshotRevision == b->session.document().revision();
      },
      "background drafts committed");
  const auto drafts = recovery.pendingDrafts();
  require(drafts.size() == 2 && a->draftKey != b->draftKey, "each tab has its own recovery snapshot");
  {
    SettingsOverride autoSave("files/autoSave", true);
    QMetaObject::invokeMethod(a->autoSaveTimer, "timeout", Qt::DirectConnection);
    require(contents(first) == QStringLiteral("Dirty A\n"), "background autosave writes A");
    require(b->session.document().isModified() && contents(second).contains(QStringLiteral("Small")),
            "autosave leaves B unsaved");
    waitUntil([&] { return recovery.pendingDrafts().size() == 1; }, "saving A leaves B recovery snapshot");
  }
  a->session.setMarkdownText(QStringLiteral("Save All A\n"), true);
  action(f.window, QStringLiteral("Save All Open Files..."))->trigger();
  require(contents(first) == QStringLiteral("Save All A\n") && contents(second) == QStringLiteral("Dirty B\n"),
          "Save All includes inactive tabs");
  require(f.current() == b, "Save All retains active tab");
  waitUntil([&] { return recovery.pendingDrafts().isEmpty(); }, "Save All clears saved drafts asynchronously");

  // Closing an in-flight parse must not leave a worker referring to a destroyed session.
  const QString closing = f.file(QStringLiteral("closing.md"), QStringLiteral("Closing worker.\n\n").repeated(20000));
  require(f.window.openFile(closing), "start parse to close");
  f.close(f.current());
  QThreadPool::globalInstance()->waitForDone();
  QApplication::processEvents();
  require(f.tabs->count() == 2, "async tab closed safely");

  a->session.setMarkdownText(QStringLiteral("Exit dirty A\n"), true);
  {
    AnswerPrompts answer(QMessageBox::Cancel);
    require(!f.window.close() && answer.count == 1 && f.window.isVisible(), "window close checks inactive dirty tabs");
  }
  {
    AnswerPrompts answer(QMessageBox::Discard);
    require(f.window.close() && answer.count == 1, "window close can discard remaining dirty tabs");
  }
}

void openFailuresAndConflictingSaveAs() {
  Fixture f;
  DocumentTab* a = f.open(f.file(QStringLiteral("a.md"), QStringLiteral("A\n")));
  const QString second = f.file(QStringLiteral("b.md"), QStringLiteral("B\n"));
  DocumentTab* b = f.open(second);
  a->session.setMarkdownText(QStringLiteral("Unsaved A\n"), true);
  f.tabs->setCurrentWidget(a);
  {
    AnswerPrompts answer(QMessageBox::Ok);
    require(!f.window.openFile(f.directory.filePath(QStringLiteral("missing.md"))), "failed open reports failure");
    require(answer.count == 1 && f.tabs->count() == 2 && f.current() == a, "failed open leaves no extra tab");
  }
  {
    QTimer timer;
    timer.setInterval(5);
    QObject::connect(&timer, &QTimer::timeout, &timer, [] {
      if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        dialog->reject();
      }
    });
    timer.start();
    require(!f.window.openFile({}) && f.tabs->count() == 2 && f.current() == a, "cancel open leaves documents intact");
  }
  {
    AnswerPrompts answer(QMessageBox::Ok);
    QTimer choose;
    choose.setInterval(5);
    QObject::connect(&choose, &QTimer::timeout, &choose, [&] {
      if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        choose.stop();
        acceptSaveFile(*dialog, second);
      }
    });
    choose.start();
    action(f.window, QStringLiteral("Save As..."))->trigger();
    require(answer.count == 1 && a->session.document().isModified(),
            QStringLiteral("Save As refuses another open document (prompts=%1, modified=%2, path=%3)")
                .arg(answer.count)
                .arg(a->session.document().isModified())
                .arg(a->session.filePath()));
    require(contents(second) == QStringLiteral("B\n") && b->session.markdownText().toString() == QStringLiteral("B\n"),
            "conflicting Save As preserves both disk and open B");
  }
  {
    SettingsOverride saveOnSwitch("files/autoSaveOnSwitch", true);
    f.tabs->setCurrentWidget(b);
    require(!a->session.document().isModified() && contents(a->session.filePath()) == QStringLiteral("Unsaved A\n"),
            "save-on-switch preference still applies to retained documents");
    b->session.setMarkdownText(QStringLiteral("Discard B\n"), true);
    AnswerPrompts answer(QMessageBox::Discard);
    f.close(b);
    require(answer.count == 1 && contents(second) == QStringLiteral("B\n"),
            "closing with Discard never saves on switch");
  }
}

void renameFolderAndTranslateTabs() {
  Fixture f;
  const QString folder = f.directory.filePath(QStringLiteral("folder"));
  require(QDir().mkpath(folder), "create folder for rename");
  DocumentTab* a = f.open(f.file(QStringLiteral("folder/a.md"),
                                 QStringLiteral("# First document\n\nEditing one document keeps the others open.\n")));
  DocumentTab* b = f.open(f.file(QStringLiteral("folder/b.md"),
                                 QStringLiteral("# Second document\n\nSwitch tabs without losing your place.\n")));
  a->session.setMarkdownText(QStringLiteral("# First document\n\nUnsaved changes in another tab.\n"), true);
  auto* sidebar = f.window.findChild<SidebarWidget*>();
  sidebar->inlineCommitRequested(InlineEditContext{folder, false, true}, QStringLiteral("renamed"));
  const QString renamed = f.directory.filePath(QStringLiteral("renamed"));
  require(a->session.filePath() == renamed + QStringLiteral("/a.md") &&
              b->session.filePath() == renamed + QStringLiteral("/b.md"),
          "folder rename updates both active and inactive paths");
  require(f.window.saveCurrentDocument(), "renamed active file saves");
  require(f.window.openFile(a->session.filePath()) && f.tabs->count() == 2 && f.current() == a,
          "renamed inactive file reuses its tab");

  for (const LanguageInfo& language : LanguageManager::instance().availableLanguages()) {
    if (language.code == QStringLiteral("system")) {
      continue;
    }
    require(LanguageManager::instance().setLanguage(language.code), "tab language loads");
    const QString close = MainWindow::tr("Close Tab");
    require(!close.isEmpty(), "translated close label");
    if (language.code != QStringLiteral("en")) {
      require(close != QStringLiteral("Close Tab"), "tab controls translated in every locale");
    }
    auto* button = tabControlButton(f.tabs->tabBar(), 0);
    require(button->toolTip() == close && button->accessibleName() == close, "existing close button retranslates");
    auto* plus = f.window.findChild<QToolButton*>(QStringLiteral("NewTabButton"));
    require(plus->toolTip().startsWith(MainWindow::tr("New Tab")), "new-tab tooltip retranslates");
    require(f.tabs->tabBar()->accessibleName() == MainWindow::tr("Open Documents"),
            "tab bar accessible name retranslates");
  }
  LanguageManager::instance().setLanguage(QStringLiteral("en"));
  if (const QString screenshot = qEnvironmentVariable("MUFFIN_TAB_SCREENSHOT"); !screenshot.isEmpty()) {
    f.tabs->setCurrentWidget(b);
    QApplication::processEvents();
    require(f.window.grab().save(screenshot), "save tab appearance capture");
    action(f.window, QStringLiteral("Night"))->trigger();
    require(f.window.grab().save(screenshot + QStringLiteral(".night.png")), "save dark tab appearance capture");
  }
}

void hiddenTabsDeferThemeChanges() {
  Fixture f;
  auto* a = f.open(f.file("theme-a.md", "# A\n\ntext\n"));
  auto* b = f.open(f.file("theme-b.md", "# B\n\ntext\n"));
  const auto previous = a->renderView->theme().backgroundColor();
  const auto revision = a->session.document().revision();
  const auto cursor = a->renderView->cursorPosition();
  action(f.window, QStringLiteral("Night"))->trigger();
  require(a->pendingTheme.has_value() && !b->pendingTheme, "only hidden tab defers theme application");
  require(a->renderView->theme().backgroundColor() == previous, "hidden editor retains previous layout snapshot");
  const auto first = a->pendingTheme->backgroundColor();
  action(f.window, QStringLiteral("GitHub"))->trigger();
  require(a->pendingTheme && a->pendingTheme->backgroundColor() != first, "repeated changes coalesce to latest theme");
  const auto expected = b->renderView->theme().backgroundColor();
  f.tabs->setCurrentWidget(a);
  require(!a->pendingTheme && a->renderView->theme().backgroundColor() == expected,
          "activation applies latest snapshot once");
  require(a->session.document().revision() == revision &&
              a->renderView->cursorPosition().text.nodeId == cursor.text.nodeId,
          "deferred theme preserves document and cursor identity");
}

void hiddenTabsMergeThemeAndResourceChanges() {
  Fixture f;
  auto& loader = ImageLoader::instance();
  auto& resources = LayoutResources::instance();
  const QString url = "https://example.invalid/tab-resource.png";
  const auto store = [&](int height) {
    QImage image(60, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::blue);
    loader.store(url, image);
  };
  store(30);
  auto* a = f.open(f.file("resources-a.md", "![image](" + url + ")\n\n" +
                                                QString("Text with **bold** and wrapping words. ").repeated(30)));
  const auto imageId = a->session.document().root().children()[0]->id();
  const auto textId = a->session.document().root().children()[1]->id();
  CursorPosition cursor;
  cursor.blockId = cursor.text.nodeId = textId;
  cursor.text.textOffset = 5;
  cursor.text.sourceOffset = a->session.document().root().children()[1]->sourceRange().byteStart + 5;
  a->editorController.selection().setCursorPosition(cursor);
  auto* b = f.open(f.file("resources-b.md", "# Active\n\nsecond tab"));
  const auto oldHeight = a->renderView->nodeRect(imageId).height();
  const auto oldFont = a->renderView->blockLayoutForNode(textId)->reuse.resources.value(LayoutResources::fontKey());
  action(f.window, QStringLiteral("Night"))->trigger();
  store(90);
  resources.publish(LayoutResources::fontKey(), "tab-hidden-font-generation-1");
  action(f.window, QStringLiteral("GitHub"))->trigger();
  store(170);
  resources.publish(LayoutResources::fontKey(), "tab-hidden-font-generation-2");
  QApplication::processEvents();
  require(a->pendingTheme.has_value() && !a->renderView->isVisible(), "resource changes leave hidden theme deferred");
  require(a->renderView->nodeRect(imageId).height() == oldHeight &&
              a->renderView->blockLayoutForNode(textId)->reuse.resources.value(LayoutResources::fontKey()) == oldFont,
          "hidden tabs retain geometry and font dependencies across repeated resource and theme changes");
  const auto revision = a->session.document().revision();
  f.tabs->setCurrentWidget(a);
  require(!a->pendingTheme && a->renderView->theme().backgroundColor() == b->renderView->theme().backgroundColor(),
          "activation installs the newest deferred theme");
  require(a->renderView->nodeRect(imageId).height() >= 170 &&
              a->renderView->blockLayoutForNode(textId)->reuse.resources.value(LayoutResources::fontKey()) ==
                  resources.read(LayoutResources::fontKey()),
          "activation merges latest font and image resources before returning");
  const auto* activated = a->renderView->blockLayoutForNode(textId);
  QApplication::processEvents();
  require(a->renderView->blockLayoutForNode(textId) == activated,
          "queued show refresh does not discard the already current themed layout");
  // Let Qt settle the theme's scrollbar/style geometry before comparing views.
  const auto caret = a->renderView->effectiveCursorRect();
  const auto hit = a->renderView->hitTest(a->renderView->mapDocumentToViewport(caret.center()));
  // Inherit the same window stylesheet/font environment as the tab view.
  // A standalone macOS scroll area has different native scrollbar geometry.
  EditorView fresh(a);
  fresh.resize(a->renderView->size());
  fresh.setZoomPercent(a->renderView->theme().zoomPercent());
  fresh.setFontSizePx(a->renderView->theme().fontSizePx());
  fresh.setContentWidthPx(a->renderView->theme().contentWidthPx());
  fresh.setTheme(a->renderView->theme());
  fresh.setDocument(a->session.document(), a->session.filePath());
  fresh.show();
  fresh.setCursorPosition(cursor);
  QApplication::processEvents();
  if (fresh.viewport()->size() != a->renderView->viewport()->size() || fresh.effectiveCursorRect() != caret) {
    qWarning() << "Activated viewport/caret" << a->renderView->viewport()->size() << caret << "fresh viewport/caret"
               << fresh.viewport()->size() << fresh.effectiveCursorRect();
  }
  require(fresh.viewport()->size() == a->renderView->viewport()->size() && fresh.effectiveCursorRect() == caret,
          "activated layout and font metrics agree with a newly built view");
  const auto expectedHit = fresh.hitTest(fresh.mapDocumentToViewport(fresh.effectiveCursorRect().center()));
  require(hit.blockId == textId && hit.sourceOffset == expectedHit.sourceOffset &&
              hit.sourceOffset == cursor.text.sourceOffset &&
              a->renderView->cursorPosition().text.sourceOffset == cursor.text.sourceOffset &&
              a->session.document().revision() == revision,
          "coalesced activation preserves source cursor and fresh-view hit coordinates");
  fresh.hide();

  // The rendered view can also be hidden inside the active tab's source stack.
  action(f.window, QStringLiteral("Source Code Mode"))->trigger();
  require(!a->renderView->isVisible(), "source mode hides rendered resources");
  store(220);
  QApplication::processEvents();
  require(a->renderView->nodeRect(imageId).height() < 220, "source mode also defers image reflow");
  action(f.window, QStringLiteral("Source Code Mode"))->trigger();
  require(a->renderView->nodeRect(imageId).height() >= 220,
          "return to rendered mode consumes resources before scrolling");
}

QStringList openPaths(Fixture& f) {
  QStringList paths;
  for (int i = 0; i < f.tabs->count(); ++i)
    paths.append(static_cast<DocumentTab*>(f.tabs->widget(i))->session.filePath());
  return paths;
}

void directoryTreeFollowsTabsUnlessPinned() {
  Fixture f;
  require(QDir(f.directory.path()).mkpath("A/Sub") && QDir(f.directory.path()).mkpath("C"), "create tab directories");
  const auto a = f.file("A/B.md", "needle A\n");
  const auto c = f.file("C/D.md", "needle C\n");
  auto* first = f.open(a);
  auto* sidebar = f.window.findChild<SidebarWidget*>();
  auto* pin = sidebar->findChild<QToolButton*>("FolderPinButton");
  const auto rootA = f.directory.filePath("A"), rootC = f.directory.filePath("C");
  require(sidebar->folderRoot() == rootA && !pin->isChecked(), "direct file open follows its parent");
  auto* second = f.open(c);
  require(sidebar->folderRoot() == rootC, "opening another directory follows its active tab");
  f.tabs->setCurrentWidget(first);
  require(sidebar->folderRoot() == rootA, "switch back follows first tab");
  auto* query = sidebar->searchPanel()->findChild<QLineEdit*>("WorkspaceSearchQuery");
  require(query, "search exposes query field");
  query->setText("needle");
  sidebar->searchPanel()->startSearch({});
  waitUntil([&] { return sidebar->searchPanel()->result().complete; }, "directory A search completes");
  f.tabs->setCurrentWidget(second);
  require(sidebar->folderRoot() == rootC && sidebar->searchPanel()->root() == rootA &&
              sidebar->searchPanel()->result().query.root == rootA && sidebar->searchPanel()->result().complete,
          "tab navigation changes the tree without discarding an existing search or replacement scope");
  pin->click();
  f.tabs->setCurrentWidget(first);
  require(sidebar->folderRoot() == rootC && pin->isChecked(), "manual pin retains root across tabs");
  pin->click();
  require(sidebar->folderRoot() == rootA && !pin->isChecked(), "unpin immediately follows active tab");
  f.window.openFolderAtPath(f.directory.path());
  f.tabs->setCurrentWidget(second);
  require(sidebar->folderPinned() && sidebar->folderRoot() == f.directory.path(), "Open Folder pins the library root");
  const auto state = sidebar->folderState();
  require(state.value("pinned").toBool(), "pinned mode is serializable");
  pin->click();
  require(sidebar->folderRoot() == rootC && !sidebar->folderPinned(), "unpin library switches to document directory");
  action(f.window, "New")->trigger();
  require(sidebar->folderRoot() == rootC, "new unsaved tab retains directory");
}

void followingDirectorySurvivesRestart() {
  SettingsOverride startup("files/startupBehavior", 1);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture original;
  require(QDir(original.directory.path()).mkpath("A/Branch/Deep") && QDir(original.directory.path()).mkpath("C"),
          "create followed session folders");
  const auto a = original.file("A/a.md", "a\n");
  const auto c = original.file("C/c.md", "c\n");
  auto* first = original.open(a);
  auto* sidebar = original.window.findChild<SidebarWidget*>();
  auto* tree = sidebar->findChild<QTreeView*>("FileTree");
  auto* model = qobject_cast<FileTreeModel*>(tree->model());
  sidebar->restoreExpandedFolders({"Branch", "Branch/Deep"});
  waitUntil([&] { return tree->isExpanded(model->index(original.directory.filePath("A/Branch/Deep"))); },
            "followed session expansion loads");
  original.open(c);
  original.tabs->setCurrentWidget(first);
  require(original.window.close(), "close followed session");
  Fixture restored;
  restored.window.restoreStartupFile();
  auto* restoredSidebar = restored.window.findChild<SidebarWidget*>();
  auto* restoredTree = restoredSidebar->findChild<QTreeView*>("FileTree");
  auto* restoredModel = qobject_cast<FileTreeModel*>(restoredTree->model());
  waitUntil(
      [&] { return restoredTree->isExpanded(restoredModel->index(original.directory.filePath("A/Branch/Deep"))); },
      "followed session restores per-root expansion");
  require(!restoredSidebar->folderPinned() && restoredSidebar->folderRoot() == original.directory.filePath("A"),
          "restart retains follow mode instead of pinning the saved root");
  restored.tabs->setCurrentIndex(1);
  require(restoredSidebar->folderRoot() == original.directory.filePath("C"), "restored tabs continue following");
  restored.tabs->setCurrentIndex(0);
  waitUntil(
      [&] { return restoredTree->isExpanded(restoredModel->index(original.directory.filePath("A/Branch/Deep"))); },
      "switching restored roots retains their expansion");
}

void sessionRestoresOrderActiveFolderAndExpansion() {
  SettingsOverride startup("files/startupBehavior", 1);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture original;
  require(QDir(original.directory.path()).mkpath("Folder/Nested/Deep"), "create session folders");
  const QString a = original.file("a.md", "# First\n");
  const QString b = original.file("Folder/b.md", "# Second\n");
  const QString c = original.file("c.md", "# Third\n");
  original.open(a);
  original.window.openFolderAtPath(original.directory.path());
  auto* second = original.open(b);
  original.open(c);
  original.tabs->tabBar()->moveTab(2, 0);
  original.tabs->tabBar()->moveTab(1, 2);
  original.tabs->setCurrentWidget(second);
  auto* sidebar = original.window.findChild<SidebarWidget*>();
  sidebar->restoreExpandedFolders({"Folder", "Folder/Nested"});
  auto* tree = sidebar->findChild<QTreeView*>("FileTree");
  auto* model = qobject_cast<FileTreeModel*>(tree->model());
  waitUntil([&] { return tree->isExpanded(model->index(original.directory.filePath("Folder/Nested"))); },
            "session folders expanded before exit");
  tree->setExpanded(model->index(original.directory.filePath("Folder")), false);
  const QStringList expected{c, b, a};
  require(original.window.close(), "clean session closes");
  const auto snapshot = QSettings().value("session/window").toMap();
  require(snapshot.value("files").toStringList() == expected && snapshot.value("active").toInt() == 1,
          "exit preserves tab order and original active tab rather than the last prompted tab");
  require(snapshot.value("folder").toString() == original.directory.path() &&
              snapshot.value("expanded").toStringList() == QStringList({"Folder/Nested"}),
          "exit stores the workspace and relative expanded folder paths");

  Fixture restored;
  restored.window.restoreStartupFile();
  waitUntil(
      [&] {
        for (int i = 0; i < restored.tabs->count(); ++i)
          if (static_cast<DocumentTab*>(restored.tabs->widget(i))->session.isAsyncParseInProgress())
            return false;
        return true;
      },
      "restored documents finish loading");
  require(openPaths(restored) == expected && restored.current()->session.filePath() == b,
          "startup restores ordered tabs and active file");
  auto* restoredSidebar = restored.window.findChild<SidebarWidget*>();
  auto* restoredTree = restoredSidebar->findChild<QTreeView*>("FileTree");
  auto* restoredModel = qobject_cast<FileTreeModel*>(restoredTree->model());
  waitUntil(
      [&] { return restoredTree->isExpanded(restoredModel->index(original.directory.filePath("Folder/Nested"))); },
      "startup restores asynchronously loaded nested folder expansion");
  require(!restoredTree->isExpanded(restoredModel->index(original.directory.filePath("Folder"))),
          "exit prompts and queued parse notifications preserve a collapsed folder containing the active file");
  require(restoredSidebar->folderRoot() == original.directory.path(), "startup restores the workspace root");
}

void closedFilesAndFolderStayClosed() {
  SettingsOverride startup("files/startupBehavior", 1);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture f;
  const QString a = f.file("a.md", "first\n");
  const QString b = f.file("b.md", "second\n");
  auto* first = f.open(a);
  auto* second = f.open(b);
  f.close(first);
  require(QSettings().value("session/window").toMap().value("files").toStringList() == QStringList{b},
          "closing a file immediately removes it from the persisted session");
  closeFolder(f.window);
  auto* sidebar = f.window.findChild<SidebarWidget*>();
  require(sidebar->folderRoot().isEmpty() && f.current() == second, "closing a workspace retains its open documents");
  sidebar->setCurrentDocument("b.md", b, false);
  require(sidebar->folderRoot().isEmpty(), "document refresh does not reopen an explicitly closed workspace");
  Fixture filesOnly;
  filesOnly.window.restoreStartupFile();
  require(openPaths(filesOnly) == QStringList{b} &&
              filesOnly.window.findChild<SidebarWidget*>()->folderRoot().isEmpty(),
          "files can be restored with no workspace");
  action(f.window, "Close Tab")->trigger();
  const auto closed = QSettings().value("session/window").toMap();
  require(closed.value("files").toStringList().isEmpty() && closed.value("folder").toString().isEmpty(),
          "closing the last file and workspace immediately stores an empty session");
  Fixture blank;
  blank.window.restoreStartupFile();
  require(blank.tabs->count() == 1 && blank.current()->session.filePath().isEmpty() &&
              blank.current()->session.markdownText().isEmpty() && !blank.tabs->tabBar()->isVisible() &&
              blank.window.findChild<SidebarWidget*>()->folderRoot().isEmpty(),
          "an empty session starts clean and ignores recent-file history");
}

void startupPreferenceAndMissingFiles() {
  SettingsOverride startup("files/startupBehavior", 0);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture source;
  const QString a = source.file("available.md", "# Available\n");
  const QVariantMap snapshot{{"version", 1},
                             {"files", QStringList{source.directory.filePath("gone.md"), a}},
                             {"active", 1},
                             {"folder", source.directory.filePath("removed")}};
  QSettings().setValue("session/window", snapshot);
  Fixture fresh;
  fresh.window.restoreStartupFile();
  require(fresh.current()->session.filePath().isEmpty() && fresh.tabs->count() == 1,
          "always start fresh ignores the saved session");
  QSettings().remove("files/startupBehavior");
  Fixture restored;
  restored.window.restoreStartupFile();
  require(openPaths(restored) == QStringList{a} && restored.current()->session.filePath() == a &&
              restored.window.findChild<SidebarWidget*>()->folderRoot().isEmpty(),
          "default restores valid files while silently skipping removed files and folders");
  QSettings().setValue("session/window",
                       QVariantMap{{"version", 1}, {"files", QStringList{}}, {"folder", source.directory.path()}});
  Fixture folderOnly;
  folderOnly.window.restoreStartupFile();
  require(folderOnly.current()->session.filePath().isEmpty() && folderOnly.tabs->count() == 1 &&
              folderOnly.window.findChild<SidebarWidget*>()->folderRoot() == source.directory.path(),
          "a workspace can be restored with no open files");
}

void canceledClosePreservesSessionAndActiveTab() {
  SettingsOverride session("session/window", QVariantMap{});
  Fixture f;
  auto* dirty = f.open(f.file("dirty.md", "before\n"));
  auto* active = f.open(f.file("active.md", "active\n"));
  dirty->session.document().setModified(true);
  const auto before = QSettings().value("session/window");
  {
    AnswerPrompts answer(QMessageBox::Cancel);
    f.close(dirty);
    require(answer.count == 1, "closing a dirty background tab prompts");
  }
  require(f.current() == active && QSettings().value("session/window") == before,
          "canceling tab close preserves the active tab and session");
  {
    AnswerPrompts answer(QMessageBox::Cancel);
    require(!f.window.close() && answer.count == 1, "canceling window close keeps it open");
  }
  require(f.current() == active && QSettings().value("session/window") == before,
          "canceling exit does not persist temporary prompt activations");
}

void sessionPreservesEmptyTabAndExplicitReveal() {
  SettingsOverride startup("files/startupBehavior", 1);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture f;
  const QString a = f.file("first.md", "first\n");
  const QString b = f.file("last.md", "last\n");
  f.open(a);
  action(f.window, "New")->trigger();
  auto* blank = f.current();
  f.open(b);
  // Opening a file may reuse an empty tab. Create the placeholder after both
  // real files are open, then move it between them.
  if (blank->session.filePath() == b)
    action(f.window, "New")->trigger();
  f.tabs->tabBar()->moveTab(2, 1);
  f.tabs->setCurrentIndex(1);
  require(f.current()->session.filePath().isEmpty(), "middle tab is an empty placeholder");
  require(f.window.close(), "session with an empty tab closes");
  Fixture restored;
  restored.window.restoreStartupFile();
  require(openPaths(restored) == QStringList({a, QString(), b}) && restored.tabs->currentIndex() == 1,
          "empty tabs retain their order and active selection among named files");
  restored.tabs->setCurrentIndex(0);
  closeFolder(restored.window);
  action(restored.window, "Show in Sidebar")->trigger();
  require(restored.window.findChild<SidebarWidget*>()->folderRoot() == f.directory.path(),
          "explicit Show in Sidebar can reopen a closed workspace");
}

QString readingFixtureText() {
  QString text;
  for (int i = 0; i < 180; ++i)
    text += QStringLiteral(
                "Paragraph %1 中文 😀 with **bold**, *italic*, $$E=mc^2$$ and words that wrap across the viewport. ")
                .arg(i)
                .repeated(3) +
            "\n\n";
  return text;
}

void waitForRestoredView(DocumentTab* document) {
  waitUntil([&] { return !document->session.isAsyncParseInProgress() && !document->pendingViewState; },
            "parsed visible tab consumes its pending view state");
  QApplication::processEvents();
}

void requireReadingPosition(DocumentTab* document, const ViewportPosition& expected) {
  if (document->backend->isSourceMode()) {
    const auto actual = document->editor->viewportPosition();
    require(actual.sourceOffset == expected.sourceOffset && qAbs(actual.screenOffset - expected.screenOffset) <= 2,
            "source reading anchor and screen offset survive restart independently of the caret");
  } else {
    const auto cursor = document->editorController.cursorForSourceOffset(expected.sourceOffset);
    const auto hit = document->renderView->hitForCursorPosition(cursor);
    const auto* node = document->session.document().topLevelBlockAtOffset(expected.sourceOffset);
    const qreal y = expected.textAnchor ? hit.cursorRect.top() : document->renderView->nodeRect(node->id()).top();
    const qreal actual = document->renderView->mapDocumentToViewport(QPointF(0, y)).y();
    require(qAbs(actual - expected.screenOffset) <= 2,
            "rendered source anchor stays at its saved screen offset after reparse or rewrap");
  }
}

void sessionRestoresSelectionsModesAndIndependentReadingPositions() {
  SettingsOverride startup("files/startupBehavior", 1);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture original;
  const QString text = readingFixtureText();
  auto* rendered = original.open(original.file("rendered.md", text));
  const auto focus = text.indexOf("Paragraph 20") + 6;
  const auto anchor = focus + 14;
  rendered->editorController.selection().setSelection({rendered->editorController.cursorForSourceOffset(anchor),
                                                       rendered->editorController.cursorForSourceOffset(focus)});
  const auto savedSelection = rendered->editorController.selection().selection();
  const auto readingOffset = text.indexOf("Paragraph 130");
  rendered->renderView->scrollToNode(rendered->session.document().topLevelBlockAtOffset(readingOffset)->id());
  rendered->renderView->verticalScrollBar()->setValue(rendered->renderView->verticalScrollBar()->value() + 41);
  QApplication::processEvents();
  const auto renderedPosition = rendered->renderView->viewportPosition();
  require(!renderedPosition.atStart && renderedPosition.sourceOffset > anchor,
          "rendered viewport is far below its selection");

  auto* source = original.open(original.file("source.md", text));
  action(original.window, "Source Code Mode")->trigger();
  source->editor->setCursorPosition(readingOffset);
  source->editor->centerCursor();
  QApplication::processEvents();
  const auto sourcePosition = source->editor->viewportPosition();
  source->editor->setSelection(anchor, focus);
  source->editor->restoreViewportPosition(sourcePosition);
  const auto savedSourcePosition = source->editor->viewportPosition();
  // Leave the rendered page active; the source page must remain deferred after restart.
  original.tabs->setCurrentWidget(rendered);
  require(original.window.close(), "mixed-mode session closes");
  const auto saved = QSettings().value("session/window").toMap();
  const auto views = saved.value("views").toList();
  require(views.size() == 2 && !views[0].toMap().value("sourceMode").toBool() &&
              views[1].toMap().value("sourceMode").toBool(),
          "session persists each tab's own mode");

  Fixture restored;
  restored.window.resize(760, 620);
  restored.window.restoreStartupFile();
  auto* active = restored.current();
  waitForRestoredView(active);
  const auto actualSelection = active->editorController.selection().selection();
  require(actualSelection.anchor.text.sourceOffset == savedSelection.anchor.text.sourceOffset &&
              actualSelection.focus.text.sourceOffset == savedSelection.focus.text.sourceOffset &&
              actualSelection.anchor.text.sourceOffset > actualSelection.focus.text.sourceOffset,
          "rendered reverse selection restores with newly parsed node ids");
  require(!active->backend->isSourceMode() && !action(restored.window, "Source Code Mode")->isChecked(),
          "active tab's restored mode updates the shared mode controls");
  requireReadingPosition(active, renderedPosition);
  auto* hidden = static_cast<DocumentTab*>(restored.tabs->widget(1));
  require(hidden->pendingViewState.has_value() && hidden->backend->isSourceMode(),
          "background page defers geometry restoration");
  // A session save before first activation must keep the unconsumed state.
  closeFolder(restored.window);
  require(QSettings().value("session/window").toMap().value("views").toList()[1] == views[1],
          "saving an unopened hidden page retains its original pending view state");
  restored.tabs->setCurrentWidget(hidden);
  waitForRestoredView(hidden);
  require(hidden->editor->anchorPosition() == anchor && hidden->editor->cursorPosition() == focus &&
              action(restored.window, "Source Code Mode")->isChecked(),
          "source mode preserves reverse selection and updates shared controls on activation");
  // Width changes can move this source character to another wrapped row; use
  // its geometry at the new width rather than an absolute scrollbar value.
  const auto recaptured = hidden->editor->viewportPosition();
  require(qAbs(hidden->session.markdownText().lineForOffset(recaptured.sourceOffset) -
               hidden->session.markdownText().lineForOffset(savedSourcePosition.sourceOffset)) <= 1,
          "source viewport returns to the saved logical line after width changes");
  const auto before = hidden->editor->viewportPosition();
  restored.tabs->setCurrentWidget(active);
  restored.tabs->setCurrentWidget(hidden);
  QApplication::processEvents();
  require(hidden->editor->viewportPosition().sourceOffset == before.sourceOffset,
          "later tab activations do not replay the old persisted viewport");
  hidden->editor->insertText(QStringLiteral("replacement"));
  QString edited = text;
  edited.replace(focus, anchor - focus, QStringLiteral("replacement"));
  require(hidden->session.markdownText().toString() == edited,
          "typing replaces the restored source selection at the original coordinates");
}

void sessionViewRestorationWaitsForShowAndBoundsChangedFiles() {
  SettingsOverride startup("files/startupBehavior", 1);
  SettingsOverride session("session/window", QVariantMap{});
  SettingsOverride geometry("window/geometry", QByteArray{});
  Fixture files;
  const auto path = files.file("shortened.md", QStringLiteral("Hello 中文 😀\n"));
  const QVariantMap view{
      {"version", 1},      {"sourceMode", true},
      {"hasCursor", true}, {"anchor", qlonglong(9999999)},
      {"focus", -8},       {"viewport", QVariantMap{{"source", qlonglong(9999999)}, {"screenY", 0}, {"start", false}}}};
  QSettings().setValue(
      "session/window",
      QVariantMap{{"version", 1}, {"files", QStringList{path}}, {"active", 0}, {"views", QVariantList{view}}});
  MainWindow restored;
  restored.resize(900, 660);
  restored.restoreStartupFile();
  auto* tabs = restored.findChild<QTabWidget*>("DocumentTabs");
  auto* document = static_cast<DocumentTab*>(tabs->currentWidget());
  require(document->session.isAsyncParseInProgress() && document->pendingViewState.has_value(),
          "restoration retains view state while the worker still owns the unpublished AST");
  QApplication::processEvents();
  require(document->pendingViewState.has_value(), "startup before show keeps saved state pending");
  restored.show();
  waitForRestoredView(document);
  require(document->editor->anchorPosition() == document->session.markdownText().size() &&
              document->editor->cursorPosition() == 0,
          "changed or shortened documents clamp selection endpoints to valid UTF-16 source positions");
  require(!document->session.document().isModified() && !document->editor->canUndo(),
          "restoration never edits text or adds undo history");
}

void sessionRestoresNestedAndLiteralCoordinates() {
  SettingsOverride startup("files/startupBehavior", 1);
  SettingsOverride session("session/window", QVariantMap{});
  Fixture original;
  const QStringList samples{QStringLiteral("# Heading\n\n- outer\n  - Nested 中文 **bold** 😀 text\n\nTail\n"),
                            QStringLiteral("| Column | Other |\n| --- | --- |\n| Cell 中文 text | value |\n"),
                            QStringLiteral("```cpp\n// Literal 中文 😀 text\nint x = 2;\n```\n"),
                            QStringLiteral("$$\n\\text{中文} + x\n$$\n"),
                            QStringLiteral("---\ntitle: 中文\n---\n\nBody\n")};
  QList<QPair<qsizetype, qsizetype>> expected;
  for (int i = 0; i < samples.size(); ++i) {
    auto* document = original.open(original.file(QStringLiteral("nested-%1.md").arg(i), samples[i]));
    const auto focus = samples[i].indexOf(QStringLiteral("中文"));
    const auto anchor = focus + 2;
    SelectionRange range{document->editorController.cursorForSourceOffset(anchor),
                         document->editorController.cursorForSourceOffset(focus)};
    if (i >= 2) {
      // Typing through a literal controller also yields block-local source
      // offsets; persistence must canonicalize these rather than save them raw.
      range.anchor.text.sourceOffset = range.anchor.text.textOffset;
      range.focus.text.sourceOffset = range.focus.text.textOffset;
    }
    document->editorController.selection().setSelection(range);
    require(document->editorController.selection().hasCursor(), "nested and literal fixture selection resolves");
    expected.append({anchor, focus});
  }
  require(original.window.close(), "nested and literal session closes");
  Fixture restored;
  restored.window.restoreStartupFile();
  for (int i = 0; i < samples.size(); ++i) {
    restored.tabs->setCurrentIndex(i);
    auto* document = restored.current();
    waitForRestoredView(document);
    const auto selection = document->editorController.selection().selection();
    require(selection.anchor.text.sourceOffset == expected[i].first &&
                selection.focus.text.sourceOffset == expected[i].second,
            "nested list, table cell and code fence selections retain UTF-16 coordinates after restart");
    require(document->editorController.inputController().insertText(QStringLiteral("重启")),
            "restored literal or nested selection accepts input");
    QString edited = samples[i];
    edited.replace(expected[i].second, 2, QStringLiteral("重启"));
    require(document->session.markdownText().toString() == edited,
            "typing into a restored nested or literal selection targets its original source");
  }
}

} // namespace

void runWindowsUpdateTests();

int main(int argc, char** argv) {
  // A deterministic converter process for export plumbing tests. Actual
  // Pandoc need not be installed on CI, and can fail after partial output.
  if (argc == 2 && QByteArray(argv[1]) == "--version")
    return 0;
  QStringList converterArgs;
  for (int i = 1; i < argc; ++i)
    converterArgs << QString::fromLocal8Bit(argv[i]);
  if (converterArgs.contains("-o") && converterArgs.contains("-t")) {
    QFile output(converterArgs.value(converterArgs.indexOf("-o") + 1));
    if (!output.open(QIODevice::WriteOnly))
      return 1;
    output.write(converterArgs.join('\n').toUtf8());
    return converterArgs.contains("--fake-export-failure") ? 1 : 0;
  }
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  installMuffinTestCrashHandler();
  QStandardPaths::setTestModeEnabled(true);
  QApplication::setOrganizationName(QStringLiteral("MuffinTests"));
  QTemporaryDir settings;
  // Failed/aborted runs must not leave drafts that change the next run's
  // recovery counts or file-watcher prompts. Each process owns its data root.
  QApplication::setApplicationName(QStringLiteral("DocumentTabsTest-") + QFileInfo(settings.path()).fileName());
  const auto cleanupData = qScopeGuard([] {
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).removeRecursively();
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
  });
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  QSettings().setValue(QStringLiteral("update/autoCheck"), false);
  QSettings().setValue(QStringLiteral("appearance/reducedMotion"), true);
  QSettings().setValue(QStringLiteral("files/autoSaveOnSwitch"), false);
  QSettings().setValue(QStringLiteral("files/autoSave"), false);
  LanguageManager::instance().setLanguage(QStringLiteral("en"));
  const auto runLockedEditingTests = [] {
    runTest("tabMenuLocksTheClickedDocument", tabMenuLocksTheClickedDocument);
    runTest("tabLockIndicatorPreservesControlsAndFollowsTheme", tabLockIndicatorPreservesControlsAndFollowsTheme);
    runTest("lockedEditingPreservesUndoAndRejectsChanges", lockedEditingPreservesUndoAndRejectsChanges);
    runTest("lockedEditingScopesSurviveLoading", lockedEditingScopesSurviveLoading);
    runTest("lockedEditingPreferencesSyncAcrossWindows", lockedEditingPreferencesSyncAcrossWindows);
    runTest("lockedEditingUsesShortcutPreferences", lockedEditingUsesShortcutPreferences);
    runTest("lockedSourceInputPreservesReadingPosition", lockedSourceInputPreservesReadingPosition);
    runTest("lockedEditingRetranslatesItsState", lockedEditingRetranslatesItsState);
  };
  if (QCoreApplication::arguments().contains(QStringLiteral("--locked-editing"))) {
    runLockedEditingTests();
    return 0;
  }
  if (QCoreApplication::arguments().contains(QStringLiteral("--shortcut-preferences"))) {
    runTest("shortcutPreferencesFollowThemeAndConfirmReset", shortcutPreferencesFollowThemeAndConfirmReset);
    runTest("configurableShortcuts", configurableShortcuts);
    return 0;
  }
  if (QCoreApplication::arguments().contains(QStringLiteral("--export-path-layout"))) {
    runTest("exportPathRowsFitControls", exportPathRowsFitControls);
    return 0;
  }
  if (QCoreApplication::arguments().contains(QStringLiteral("--export-number-controls"))) {
    runTest("exportNumberControls", exportNumberControls);
    return 0;
  }
  if (QCoreApplication::arguments().contains(QStringLiteral("--search-preferences"))) {
    runTest("searchPreferences", searchPreferences);
    return 0;
  }
  if (QCoreApplication::arguments().contains(QStringLiteral("--windows-update"))) {
    runWindowsUpdateTests();
    return 0;
  }
  if (QCoreApplication::arguments().contains(QStringLiteral("--document-memory"))) {
    muffin::diag::runDocumentMemoryBenchmark<Fixture>();
    return 0;
  }
  if (QCoreApplication::arguments().contains(QStringLiteral("--memory-audit"))) {
    muffin::diag::runMemoryLifecycleAudit<Fixture>();
    return 0;
  }
  const auto runSessionViewTests = [] {
    runTest("sessionRestoresSelectionsModesAndIndependentReadingPositions",
            sessionRestoresSelectionsModesAndIndependentReadingPositions);
    runTest("sessionViewRestorationWaitsForShowAndBoundsChangedFiles",
            sessionViewRestorationWaitsForShowAndBoundsChangedFiles);
    runTest("sessionRestoresNestedAndLiteralCoordinates", sessionRestoresNestedAndLiteralCoordinates);
  };
  if (QCoreApplication::arguments().contains(QStringLiteral("--session-view-state"))) {
    // The dedicated lazy-layout test uses Qt's minimal platform. It only needs
    // the session view cases, not unrelated diagram/font rendering scenarios.
    runSessionViewTests();
    return 0;
  }
  runTest("externalFilesOpenInTabsByPreference", externalFilesOpenInTabsByPreference);
  runTest("closedInitialWindowDoesNotCaptureFileOpens", closedInitialWindowDoesNotCaptureFileOpens);
  runTest("earlyNativeFileEventsAreRetained", earlyNativeFileEventsAreRetained);
  if (QCoreApplication::arguments().contains(QStringLiteral("--file-opening")))
    return 0;
  runTest("externalFileChangesCoalesceAndReloadLatest", externalFileChangesCoalesceAndReloadLatest);
  runTest("backgroundExternalFileChangesWaitForActivation", backgroundExternalFileChangesWaitForActivation);
  runTest("externalFileQuestionsRetireWithTheirTab", externalFileQuestionsRetireWithTheirTab);
  runTest("pendingExternalChangeFollowsMovedTab", pendingExternalChangeFollowsMovedTab);
  if (QCoreApplication::arguments().contains(QStringLiteral("--external-file-changes")))
    return 0;
  runTest("focusedSaveDialogSelectsExactPath", focusedSaveDialogSelectsExactPath);
  if (QCoreApplication::arguments().contains(QStringLiteral("--save-dialog")))
    return 0;
  runTest("directoryTreeFollowsTabsUnlessPinned", directoryTreeFollowsTabsUnlessPinned);
  runTest("followingDirectorySurvivesRestart", followingDirectorySurvivesRestart);
  runTest("fileTreeOpeningPreservesScrollPosition", fileTreeOpeningPreservesScrollPosition);
  if (QCoreApplication::arguments().contains(QStringLiteral("--sidebar-file-open")))
    return 0;
  runTest("exportPreferencesAndOutputs", exportPreferencesAndOutputs);
  runTest("exportNumberControls", exportNumberControls);
  runTest("searchPreferences", searchPreferences);
  runTest("exportPathRowsFitControls", exportPathRowsFitControls);
  if (QCoreApplication::arguments().contains(QStringLiteral("--export-options")))
    return 0;
  runTest("noteLinkRewriting", noteLinkRewriting);
  runTest("noteLinkNavigation", noteLinkNavigation);
  runTest("noteLinkRenamePreview", noteLinkRenamePreview);
  runTest("noteLinkMoveAndConflicts", noteLinkMoveAndConflicts);
  if (QCoreApplication::arguments().contains(QStringLiteral("--note-links")))
    return 0;
  runTest("continuousEditingWorkflow", continuousEditingWorkflow);
  runLockedEditingTests();
  runTest("configurableShortcuts", configurableShortcuts);
  runTest("shortcutPreferencesFollowThemeAndConfirmReset", shortcutPreferencesFollowThemeAndConfirmReset);
  runTest("imeSelectionReplacementWorkflow", imeSelectionReplacementWorkflow);
  runTest("restrictedFormattingActions", restrictedFormattingActions);
  if (QCoreApplication::arguments().contains(QStringLiteral("--editing-workflow")))
    return 0;
  runTest("workspaceSearchAndReplace", workspaceSearchAndReplace);
  runTest("workspaceSidebarWidth", workspaceSidebarWidth);
  runTest("documentFindRevealsMatchesInsideTallBlocks", documentFindRevealsMatchesInsideTallBlocks);
  runTest("workspaceRenderedNavigation", workspaceRenderedNavigation);
  if (QCoreApplication::arguments().contains(QStringLiteral("--workspace-search")))
    return 0;
  runTest("tabButtonsHaveSingleOwnerAcrossPlatformStyles", tabButtonsHaveSingleOwnerAcrossPlatformStyles);
  if (QCoreApplication::arguments().contains(QStringLiteral("--tab-buttons")))
    return 0;
  runTest("fileOpenProtectionSettingsAndBoundaries", fileOpenProtectionSettingsAndBoundaries);
  runTest("unsavedMermaidRendersAfterLeavingSource", unsavedMermaidRendersAfterLeavingSource);
  if (QCoreApplication::arguments().contains(QStringLiteral("--unsaved-mermaid")))
    return 0;
  runTest("fileSizeUnitsConvertPersistAndProtectAtByteBoundaries",
          fileSizeUnitsConvertPersistAndProtectAtByteBoundaries);
  runTest("fileOpenConfirmationDefersReadingAndPreservesTabs", fileOpenConfirmationDefersReadingAndPreservesTabs);
  runTest("fileOpenSizeConfirmationAndStartupCancellation", fileOpenSizeConfirmationAndStartupCancellation);
  runTest("sessionRestoreKeepsUnconfirmedFilesSeparate", sessionRestoreKeepsUnconfirmedFilesSeparate);
  if (QCoreApplication::arguments().contains(QStringLiteral("--file-open-protection")))
    return 0;
  runSessionViewTests();
  runTest("tabMenuTargetsBackgroundTabsAndStopsOnCancel", tabMenuTargetsBackgroundTabsAndStopsOnCancel);
  runTest("pinnedTabsSurviveBulkCloseAndRestore", pinnedTabsSurviveBulkCloseAndRestore);
  runTest("movingTabsPreservesHistoryAndRebindsWindowLifetime", movingTabsPreservesHistoryAndRebindsWindowLifetime);
  runTest("copyingTabsIncludesUnsavedContentAndIndependentBuffers",
          copyingTabsIncludesUnsavedContentAndIndependentBuffers);
  runTest("pendingTabsDisableWindowTransfer", pendingTabsDisableWindowTransfer);
  if (QCoreApplication::arguments().contains(QStringLiteral("--tab-context-menu")))
    return 0;
  runTest("sessionRestoresOrderActiveFolderAndExpansion", sessionRestoresOrderActiveFolderAndExpansion);
  runTest("closedFilesAndFolderStayClosed", closedFilesAndFolderStayClosed);
  runTest("startupPreferenceAndMissingFiles", startupPreferenceAndMissingFiles);
  runTest("canceledClosePreservesSessionAndActiveTab", canceledClosePreservesSessionAndActiveTab);
  runTest("sessionPreservesEmptyTabAndExplicitReveal", sessionPreservesEmptyTabAndExplicitReveal);
  runTest("tabBarOnlyForMultipleDocuments", tabBarOnlyForMultipleDocuments);
  runTest("formatActionsFollowSelectionAndDocumentChanges", formatActionsFollowSelectionAndDocumentChanges);
  runTest("sharedCursorQueriesMatchIndependentQueries", sharedCursorQueriesMatchIndependentQueries);
  runTest("retainsDocumentsAndRenderState", retainsDocumentsAndRenderState);
  runTest("sourceStateAndKeyboardNavigation", sourceStateAndKeyboardNavigation);
  runTest("sourceLayoutUsesAvailableWorkspace", sourceLayoutUsesAvailableWorkspace);
  runTest("closesWithSaveDiscardAndCancel", closesWithSaveDiscardAndCancel);
  runTest("backgroundLoadingAndPersistence", backgroundLoadingAndPersistence);
  runTest("openFailuresAndConflictingSaveAs", openFailuresAndConflictingSaveAs);
  runTest("renameFolderAndTranslateTabs", renameFolderAndTranslateTabs);
  runTest("hiddenTabsDeferThemeChanges", hiddenTabsDeferThemeChanges);
  runTest("hiddenTabsMergeThemeAndResourceChanges", hiddenTabsMergeThemeAndResourceChanges);
  return 0;
}
