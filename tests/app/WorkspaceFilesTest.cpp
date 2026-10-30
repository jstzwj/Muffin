#include "app/WorkspaceFiles.h"
#include "app/QuickOpenDialog.h"
#include "app/SearchSettings.h"
#include "theme/ThemeDefinition.h"
#include "../TestUtils.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QImage>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTemporaryDir>
#include <QThread>
#include <QSet>

using namespace muffin;
namespace {
void waitUntil(const std::function<bool()>& ready);
void write(const QString& path, const QByteArray& bytes = "note") {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "create fixture");
  file.write(bytes);
}
QSet<QString> relativeFiles(const QString& root, const WorkspaceFiles& files) {
  QSet<QString> paths;
  for (const auto& path : files.paths)
    paths.insert(QDir(root).relativeFilePath(path));
  return paths;
}
void ignoreFiles() {
  QTemporaryDir dir;
  auto cancel = std::make_shared<std::atomic_bool>(false);
  WorkspaceScanOptions options;
  options.ignorePatterns.clear();
  const QStringList names = {
      "visible.md",    "root.md",        "deep/root.md", "test.tmp",       "keep.tmp",
      "deep/test.tmp", "deep/nested.md", "cache/no.md",  "blocks/drop.md", "blocks/allowed/yes.md",
      "a/draft1.md",   "a/x/draft2.md",  "a/x/live.md",  "#hash.md",       "!literal.md",
      "space name.md", "trail.md",       "a.md",         "b.md",           "c.md",
      "1.md",          "q1.md",          "q12.md",       "build/kept.md"};
  for (const auto& name : names)
    write(dir.filePath(name));
  write(
      dir.filePath(".gitignore"),
      QByteArray::fromHex("efbbbf") +
          "# comment\n*.tmp\n!keep.tmp\n/root.md\ncache/\n!cache/no.md\nblocks/*\n!blocks/allowed/\n"
          "a/**/draft?.md\n\\#hash.md\n\\!literal.md\nspace\\ name.md\ntrail.md   \n[ab].md\n[[:digit:]].md\nq?.md\n");
  write(dir.filePath("deep/.gitignore"), "!test.tmp\nnested.md\n");
  write(dir.filePath(".ignore"), "!root.md\nkeep.tmp\n");
  write(dir.filePath("deep/.ignore"), "!nested.md\n");
  const auto found = WorkspaceFileIndex::scan(dir.path(), {"md", "tmp"}, cancel, options);
  const QSet<QString> expected = {
      "visible.md",  "root.md", "deep/root.md", "deep/test.tmp", "deep/nested.md", "blocks/allowed/yes.md",
      "a/x/live.md", "c.md",    "q12.md",       "build/kept.md"};
  require(relativeFiles(dir.path(), found) == expected,
          "nested ignore files, precedence, negation, anchoring, globstars, escaping and character classes");
  require(found.ignoredDirectories.contains(dir.filePath("cache")) &&
              found.ignoredFiles.contains(dir.filePath("test.tmp")),
          "excluded files and pruned directories are explained");
  options.readIgnore = false;
  const auto gitOnly = relativeFiles(dir.path(), WorkspaceFileIndex::scan(dir.path(), {"md", "tmp"}, cancel, options));
  require(!gitOnly.contains("root.md") && gitOnly.contains("keep.tmp") && !gitOnly.contains("deep/nested.md"),
          ".ignore can be disabled independently");
  options.readGitIgnore = false;
  require(WorkspaceFileIndex::scan(dir.path(), {"md", "tmp"}, cancel, options).paths.size() == names.size(),
          "turning off both ignore file formats includes every otherwise eligible file");
  options.ignorePatterns = "build/\n*.tmp\n!keep.tmp\n";
  const auto custom = relativeFiles(dir.path(), WorkspaceFileIndex::scan(dir.path(), {"md", "tmp"}, cancel, options));
  require(!custom.contains("build/kept.md") && custom.contains("keep.tmp") && !custom.contains("deep/test.tmp"),
          "editable search exclusions apply to both files and directories");
  options.ignorePatterns.clear();
  write(dir.filePath(".ignore"), QByteArray("invalid\0rule", 12));
  options.readIgnore = true;
  require(WorkspaceFileIndex::scan(dir.path(), {"md"}, cancel, options)
              .unreadableIgnoreFiles.contains(dir.filePath(".ignore")),
          "invalid ignore file is reported rather than silently changing scope");
}
void preferencesScope() {
  SettingsOverride extensions("search/extensions", "md"), ignored("search/ignorePatterns", "build/"),
      hidden("search/includeHidden", false), git("search/readGitIgnore", true), ignore("search/readIgnore", true),
      size("search/maxFileBytes", 100 * 1024 * 1024), regex("search/maxRegexBytes", 32 * 1024 * 1024),
      category("preferences/lastCategory", "search"), openingExtensions("files/textExtensions", "cpp"),
      openingLimit("files/sizeLimitBytes", 1024);
  QSettings().setValue("search/extensions", "*.MD, txt");
  QSettings().setValue("search/ignorePatterns", "drafts/\n*.bak");
  QSettings().setValue("search/maxFileBytes", 12 * 1024 * 1024);
  QSettings().setValue("search/maxRegexBytes", 6 * 1024 * 1024);
  QSettings().setValue("search/readGitIgnore", false);
  QSettings().setValue("search/includeHidden", true);
  const auto settings = SearchSettings::load();
  require(settings.extensions == QStringList({"md", "txt"}) && settings.scan.ignorePatterns == "drafts/\n*.bak" &&
              settings.scan.includeHidden && !settings.scan.readGitIgnore && settings.scan.readIgnore &&
              settings.maxFileBytes == 12 * 1024 * 1024 && settings.maxRegexBytes == 6 * 1024 * 1024,
          QStringLiteral("search snapshot: extensions=%1 rules=%2 hidden=%3 git=%4 ignore=%5 bytes=%6 regex=%7")
              .arg(settings.extensions.join(','), settings.scan.ignorePatterns)
              .arg(settings.scan.includeHidden)
              .arg(settings.scan.readGitIgnore)
              .arg(settings.scan.readIgnore)
              .arg(settings.maxFileBytes)
              .arg(settings.maxRegexBytes));
  QTemporaryDir dir;
  write(dir.filePath("note.txt"));
  write(dir.filePath("drafts/draft.txt"));
  write(dir.filePath("build/new.txt"));
  write(dir.filePath("old.md.bak"));
  write(dir.filePath("source.cpp"));
  QuickOpenDialog picker;
  picker.setCandidates({dir.filePath("drafts/draft.txt"), dir.filePath("source.cpp")});
  picker.setWorkspace(dir.path(), settings.extensions);
  auto* results = picker.findChild<QListWidget*>("QuickOpenResults");
  waitUntil([&] { return results->count() == 2; });
  for (int i = 0; i < results->count(); ++i)
    require(results->item(i)->data(Qt::UserRole).toString().endsWith("note.txt") ||
                results->item(i)->data(Qt::UserRole).toString().endsWith("new.txt"),
            "Quick Open obeys search scope and history cannot bypass exclusions");
  auto cancel = std::make_shared<std::atomic_bool>(false);
  const auto all = WorkspaceFileIndex::scan(dir.path(), {"*"}, cancel, settings.scan);
  require(all.paths.size() == 3, "wildcard extension includes all file types while honoring exclusions");
  require(WorkspaceFileIndex::scan(dir.path(), {}, cancel, settings.scan).paths.isEmpty(),
          "empty extension list searches no files");
}
void hiddenFiles() {
  QTemporaryDir dir;
  write(dir.filePath("visible.md"));
  write(dir.filePath(".hidden.md"));
  write(dir.filePath(".private/note.md"));
#ifdef Q_OS_WIN
  for (const auto& name : {QStringLiteral(".hidden.md"), QStringLiteral(".private")}) {
    const auto path = QDir::toNativeSeparators(dir.filePath(name));
    require(SetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_ATTRIBUTE_HIDDEN), "hide Windows fixture");
  }
#endif
  auto cancel = std::make_shared<std::atomic_bool>(false);
  WorkspaceScanOptions options;
  require(WorkspaceFileIndex::scan(dir.path(), {"md"}, cancel, options).paths.size() == 1,
          "hidden entries excluded by default");
  options.includeHidden = true;
  require(WorkspaceFileIndex::scan(dir.path(), {"md"}, cancel, options).paths.size() == 3,
          "hidden entries included on request");
}
void waitUntil(const std::function<bool()>& ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 10000) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
  require(ready(), "asynchronous picker should finish");
}
void matching() {
  const QString path = QString::fromUtf8("04.学习/英语/单词.md");
  require(PathMatcher::score(QString::fromUtf8("学习 单词"), path) >= 0, "AND terms across path");
  require(PathMatcher::score(QString::fromUtf8("04/单词"), path) >= 0, "skip intermediate directory");
  require(PathMatcher::score(QString::fromUtf8("单词/04"), path) < 0, "path segments stay ordered");
  require(PathMatcher::score(QString::fromUtf8("随记 单词"), path) < 0, "every term must match");
  require(PathMatcher::score(QString::fromUtf8("朝路"), QString::fromUtf8("阅读/朝圣之路.md")) >= 0,
          "Chinese subsequence");
  require(PathMatcher::score("readme", "README.md") > PathMatcher::score("readme", "readme/notes.md"),
          "filename ranks above directory");
  require(PathMatcher::score("ab/c", "axb/ab/c.md") >= 0, "best segment alignment");
  require(PathMatcher::score("foo\\note", "foo/deep/note.md") >= 0, "Windows query separators");
}
void scanAndPicker() {
  QTemporaryDir directory, outside;
  const auto a = directory.filePath(QString::fromUtf8("04.学习/英语/单词.md"));
  const auto b = directory.filePath(QString::fromUtf8("06.随记/英语/单词.md"));
  write(a);
  write(b);
  write(directory.filePath("build/ignored.md"));
  write(directory.filePath("node_modules/ignored.md"));
  write(directory.filePath("image.png"));
  write(outside.filePath("other.md"));
  auto cancel = std::make_shared<std::atomic_bool>(false);
  const auto files = WorkspaceFileIndex::scan(directory.path(), {"md"}, cancel);
  require(files.paths.size() == 2, "scan deep files, exclude cache and non-text types");
  require(!WorkspaceFileIndex::contains(directory.path(), outside.filePath("other.md")), "root boundary");
  require(WorkspaceFileIndex::scan(directory.path(), {"md"}, cancel, 1).truncated,
          "limited index is explicitly incomplete");
  cancel->store(true);
  require(WorkspaceFileIndex::scan(directory.path(), {"md"}, cancel).paths.isEmpty(), "cancel scan");
  QuickOpenDialog dialog;
  dialog.setCandidates({outside.filePath("other.md")});
  dialog.setWorkspace(directory.path(), {"md"});
  auto* list = dialog.findChild<QListWidget*>("QuickOpenResults");
  auto* filter = dialog.findChild<QLineEdit*>("QuickOpenFilter");
  waitUntil([&] { return list->count() == 2; });
  require(list->item(0)->data(Qt::UserRole + 1) != list->item(1)->data(Qt::UserRole + 1),
          "same filename shows distinct paths");
  filter->setText(QString::fromUtf8("04/单词"));
  waitUntil([&] { return list->count() == 1; });
  require(dialog.selectedPath() == a, "picker searches full workspace, excludes outside history");
  QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QApplication::sendEvent(filter, &enter);
  require(dialog.result() == QDialog::Accepted, "Enter opens matching file");
}
void pickerRanking() {
  QTemporaryDir directory;
  const auto weak = directory.filePath("alpha/notes.md"), strong = directory.filePath("alpha.md");
  write(weak);
  write(strong);
  QuickOpenDialog dialog;
  dialog.setCandidates({weak, strong});
  auto* filter = dialog.findChild<QLineEdit*>("QuickOpenFilter");
  filter->setText("alpha");
  QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QApplication::sendEvent(filter, &enter);
  require(dialog.selectedPath() == strong,
          "a new query selects the highest ranked match rather than the previous selection");
}
void pickerPresentation() {
  QTemporaryDir directory;
  const auto rootFile = directory.filePath("note.md"), nested = directory.filePath("deep/note.md");
  write(rootFile);
  write(nested);
  for (const auto& style : QStyleFactory::keys()) {
    QApplication::setStyle(QStyleFactory::create(style));
    QuickOpenDialog dialog;
    dialog.setWorkspace(directory.path(), {"md"});
    auto* list = dialog.findChild<QListWidget*>("QuickOpenResults");
    auto* filter = dialog.findChild<QLineEdit*>("QuickOpenFilter");
    waitUntil([&] { return list->count() == 2; });
    require(list->item(0)->data(Qt::UserRole + 1).toString().isEmpty(), "root file name is displayed once");
    require(list->item(1)->data(Qt::UserRole + 1).toString() == "deep", "secondary row shows only the parent path");
    filter->setText("note");
    filter->setCursorPosition(4);
    QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
    QApplication::sendEvent(filter, &home);
    require(filter->cursorPosition() == 0, "Home edits the filter rather than moving the result selection");
    for (const auto& theme : {QStringLiteral("github"), QStringLiteral("night")}) {
      const auto definition = ThemeDefinition::builtIn(theme).value();
      dialog.setThemeDefinition(definition);
      dialog.ensurePolished();
      list->ensurePolished();
      QImage image(400, 40, QImage::Format_ARGB32);
      image.fill(definition.colors.surface);
      QPainter painter(&image);
      QStyleOptionViewItem option;
      option.initFrom(list);
      option.rect = image.rect();
      option.widget = list;
      option.state |= QStyle::State_Selected;
      list->itemDelegate()->paint(&painter, option, list->model()->index(0, 0));
      painter.end();
      require(image.pixelColor(380, 20) == definition.colors.chromeSelection,
              QStringLiteral("%1/%2: selected fill %3, expected %4 (view palette %5)")
                  .arg(style, theme, image.pixelColor(380, 20).name(), definition.colors.chromeSelection.name(),
                       list->palette().color(QPalette::Highlight).name()));
      bool visibleInk = false;
      for (int y = 4; y < 34; ++y)
        for (int x = 10; x < 120; ++x) {
          if (image.pixelColor(x, y) == definition.colors.chromeSelectionText)
            visibleInk = true;
        }
      require(visibleInk, "selected filename uses the foreground paired with its actual background");
      if (const auto screenshots = qEnvironmentVariable("MUFFIN_SEARCH_SCREENSHOT_DIR"); !screenshots.isEmpty()) {
        QDir().mkpath(screenshots);
        dialog.grab().save(QDir(screenshots).filePath("quick-open-" + style + '-' + theme + ".png"));
      }
    }
  }
}
}
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir settingsDirectory;
  QCoreApplication::setOrganizationName("MuffinTests");
  QCoreApplication::setApplicationName("WorkspaceFiles");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
  runTest("path matching", matching);
  runTest("project and user ignore rules", ignoreFiles);
  runTest("hidden search entries", hiddenFiles);
  runTest("search preference scope and file picker", preferencesScope);
  runTest("recursive scan and picker", scanAndPicker);
  runTest("picker ranking", pickerRanking);
  runTest("picker native selection and text editing", pickerPresentation);
}
