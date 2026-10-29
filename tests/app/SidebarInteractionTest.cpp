#include "app/SidebarWidget.h"
#include "app/FileTreeModel.h"

#include "../TestUtils.h"

#include <QApplication>
#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemModel>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QMenu>
#include <QMimeData>
#include <QSettings>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeView>
#include <QToolButton>

#include "io/MuffinMime.h"

#include <memory>

using namespace muffin;

namespace {

void waitUntil(const std::function<bool()>& ready, const char* message) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 15000) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
  require(ready(), message);
}

void mouseEvent(QWidget* target, QEvent::Type type, const QPoint& pos, Qt::MouseButton button = Qt::LeftButton,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, QPointF(pos), QPointF(target->mapToGlobal(pos)), button,
                    type == QEvent::MouseButtonRelease ? Qt::NoButton : button, modifiers);
  QApplication::sendEvent(target, &event);
}

void click(QWidget* target, const QPoint& pos, Qt::MouseButton button = Qt::LeftButton,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  mouseEvent(target, QEvent::MouseButtonPress, pos, button, modifiers);
  mouseEvent(target, QEvent::MouseButtonRelease, pos, button, modifiers);
}

void doubleClick(QWidget* target, const QPoint& pos) {
  click(target, pos);
  mouseEvent(target, QEvent::MouseButtonDblClick, pos);
  mouseEvent(target, QEvent::MouseButtonRelease, pos);
}

struct FileTreeFixture {
  QTemporaryDir dir;
  SidebarWidget sidebar;
  QTreeView* tree = nullptr;
  FileTreeModel* model = nullptr;
  QPersistentModelIndex folder;
  int openedFiles = 0;
  QString openedPath;

  explicit FileTreeFixture(const std::function<void(const QTemporaryDir&)>& prepare = {}) {
    require(dir.isValid(), "temporary folder should exist");
    require(QDir(dir.path()).mkpath(QStringLiteral("Folder/Nested")), "nested folders should exist");
    QFile file(dir.filePath(QStringLiteral("document.md")));
    require(file.open(QIODevice::WriteOnly), "document fixture should open");
    file.write("# Document\n");
    file.close();
    if (prepare)
      prepare(dir);
    const auto rootRows = QDir(dir.path()).entryList(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot).size();
    sidebar.resize(320, 600);
    sidebar.setFolderRoot(dir.path());
    sidebar.show();
    tree = sidebar.findChild<QTreeView*>(QStringLiteral("FileTree"));
    require(tree != nullptr, "file tree should exist");
    model = qobject_cast<FileTreeModel*>(tree->model());
    require(model != nullptr, "file tree should use a filesystem model");
    waitUntil(
        [&] {
          folder = QPersistentModelIndex(model->index(dir.filePath(QStringLiteral("Folder"))));
          return model->rowCount(tree->rootIndex()) == rootRows && folder.isValid() &&
                 !tree->visualRect(folder).isEmpty();
        },
        "file tree should load its root rows");
    QObject::connect(&sidebar, &SidebarWidget::fileOpenRequested, &sidebar, [this](const QString& path) {
      ++openedFiles;
      openedPath = path;
    });
  }

  QPoint folderLabel() const {
    const QRect row = tree->visualRect(folder);
    return QPoint(row.left() + 40, row.center().y());
  }
};

void testFolderNameAndRowToggleOnSingleClick() {
  FileTreeFixture f;
  require(!f.tree->isExpanded(f.folder), "folder should start collapsed");
  click(f.tree->viewport(), f.folderLabel());
  require(f.tree->isExpanded(f.folder), "single-clicking the folder name should expand it");
  require(f.tree->currentIndex() == f.folder, "folder click should also select the row");
  const QPoint blank(f.tree->viewport()->width() - 8, f.tree->visualRect(f.folder).center().y());
  require(f.tree->indexAt(blank) == f.folder, "blank space at the right should belong to the folder row");
  click(f.tree->viewport(), blank);
  require(!f.tree->isExpanded(f.folder), "clicking the blank part of the folder row should collapse it");
  require(f.openedFiles == 0, "folder clicks must not open a document");
}

void testFolderDoubleClickTogglesOnlyOnce() {
  FileTreeFixture f;
  doubleClick(f.tree->viewport(), f.folderLabel());
  require(f.tree->isExpanded(f.folder), "folder double-click must expand once without immediately collapsing");
  doubleClick(f.tree->viewport(), f.folderLabel());
  require(!f.tree->isExpanded(f.folder), "folder double-click must collapse once without reopening");
  require(f.openedFiles == 0, "folder double-clicks must not open a document");
}

void testArrowAndNestedFolderClicks() {
  FileTreeFixture f;
  const QRect row = f.tree->visualRect(f.folder);
  const QPoint arrow(row.left() - f.tree->indentation() / 2, row.center().y());
  click(f.tree->viewport(), arrow);
  require(f.tree->isExpanded(f.folder), "arrow click must expand exactly once");
  waitUntil([&] { return f.model->rowCount(f.folder) == 1; }, "expanded folder should load its child");
  const QModelIndex nested = f.model->index(f.dir.filePath(QStringLiteral("Folder/Nested")));
  const QRect nestedRow = f.tree->visualRect(nested);
  require(!nestedRow.isEmpty(), "nested folder row should be visible");
  click(f.tree->viewport(), QPoint(nestedRow.left() + 40, nestedRow.center().y()));
  require(f.tree->isExpanded(nested), "nested folder name should expand too");
  click(f.tree->viewport(), arrow);
  require(!f.tree->isExpanded(f.folder), "arrow click must collapse exactly once");
}

void testFileDoubleClickStillOpensDocument() {
  FileTreeFixture f;
  const QModelIndex file = f.model->index(f.dir.filePath(QStringLiteral("document.md")));
  const QRect row = f.tree->visualRect(file);
  const QPoint pos(row.left() + 40, row.center().y());
  click(f.tree->viewport(), pos);
  require(f.openedFiles == 0, "single-clicking a file should only select it");
  doubleClick(f.tree->viewport(), pos);
  require(f.openedFiles == 1, "double-clicking a file should open it once");
  require(f.openedPath == f.dir.filePath(QStringLiteral("document.md")), "file open should target the clicked file");
}

void testRightClickAndInlineRenameDoNotToggleFolder() {
  FileTreeFixture f;
  click(f.tree->viewport(), f.folderLabel(), Qt::RightButton);
  require(!f.tree->isExpanded(f.folder), "right-click should leave folder expansion unchanged");
  f.sidebar.beginInlineRename(f.dir.filePath(QStringLiteral("Folder")));
  QLineEdit* editor = nullptr;
  waitUntil(
      [&] {
        editor = f.tree->viewport()->findChild<QLineEdit*>();
        return editor && editor->isVisible();
      },
      "inline rename editor should open");
  click(editor, editor->rect().center());
  require(!f.tree->isExpanded(f.folder), "clicking inside the rename editor must not toggle its folder");
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(editor, &escape);
  require(f.openedFiles == 0, "renaming a folder must not open a document");
}

void testToolbarIconsRetainTheirShapeAtHighDpi() {
  SidebarWidget sidebar;
  const auto paintedBounds = [](const QPixmap& pixmap) {
    const QImage image = pixmap.toImage();
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (qAlpha(image.pixel(x, y)) > 0)
          bounds |= QRect(x, y, 1, 1);
      }
    }
    const qreal dpr = pixmap.devicePixelRatio();
    return QRectF(bounds.x() / dpr, bounds.y() / dpr, bounds.width() / dpr, bounds.height() / dpr);
  };
  for (const QString& themeId : {QStringLiteral("github"), QStringLiteral("night")}) {
    const auto theme = ThemeDefinition::builtIn(themeId);
    require(theme.has_value(), "toolbar icon test theme should exist");
    sidebar.applyTheme(*theme);
    for (const auto* name : {"NewFileButton", "NewFolderButton", "RefreshFoldersButton", "SortFilesButton",
                             "CollapseFoldersButton", "SearchFilesButton", "FolderPinButton"}) {
      const auto* button = sidebar.findChild<QToolButton*>(QString::fromLatin1(name));
      require(button && !button->icon().isNull(), "toolbar button should have an icon");
      for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active}) {
        const QRectF reference = paintedBounds(button->icon().pixmap(QSize(16, 16), 1.0, mode));
        require(!reference.isEmpty(), "toolbar icon should contain visible strokes");
        for (const qreal dpr : {1.25, 1.5, 2.0, 3.0}) {
          const QPixmap pixmap = button->icon().pixmap(QSize(16, 16), dpr, mode);
          require(pixmap.width() >= qRound(16 * dpr) && pixmap.height() >= qRound(16 * dpr),
                  "toolbar icons should supply enough pixels for high-DPI screens");
          const QRectF bounds = paintedBounds(pixmap);
          require(
              !bounds.isEmpty() && qAbs(bounds.left() - reference.left()) <= 1.0 &&
                  qAbs(bounds.top() - reference.top()) <= 1.0 && qAbs(bounds.right() - reference.right()) <= 1.0 &&
                  qAbs(bounds.bottom() - reference.bottom()) <= 1.0,
              QStringLiteral(
                  "%1 icon must retain its full shape at DPR %2, theme %3, mode %4: reference=%5,%6,%7,%8 actual=%9,%10,%11,%12")
                  .arg(QString::fromLatin1(name))
                  .arg(dpr)
                  .arg(themeId)
                  .arg(mode)
                  .arg(reference.x())
                  .arg(reference.y())
                  .arg(reference.width())
                  .arg(reference.height())
                  .arg(bounds.x())
                  .arg(bounds.y())
                  .arg(bounds.width())
                  .arg(bounds.height()));
        }
      }
    }
  }
}

void testToolbarCollapsesFoldersAndRestoresState() {
  FileTreeFixture f;
  require(QDir(f.dir.path()).mkpath("Folder/Nested/Deep/Leaf"), "create unloaded deep folders");
  auto* collapse = f.sidebar.findChild<QToolButton*>("CollapseFoldersButton");
  require(collapse && !collapse->toolTip().isEmpty() && !collapse->accessibleName().isEmpty(),
          "folder toolbar exposes tooltips and accessible names");
  auto* toolbar = f.sidebar.findChild<QWidget*>("FolderToolbar");
  require(toolbar && toolbar->findChildren<QToolButton*>().size() == 7 &&
              !f.sidebar.findChild<QWidget*>("FolderName") && !f.sidebar.findChild<QToolButton*>("FolderMenuButton"),
          "compact toolbar contains file controls and the search toggle");
  const auto nestedPath = f.dir.filePath("Folder/Nested");
  const auto leafPath = f.dir.filePath("Folder/Nested/Deep/Leaf");
  f.sidebar.restoreExpandedFolders({"Folder", "Folder/Nested", "Folder/Nested/Deep", "Folder/Nested/Deep/Leaf"});
  waitUntil([&] { return f.tree->isExpanded(f.model->index(leafPath)); },
            "saved state restores asynchronously loaded descendants");
  const auto saved = f.sidebar.expandedFolders();
  require(saved.contains("Folder") && saved.contains("Folder/Nested/Deep/Leaf"), "expansion stores relative paths");
  collapse->click();
  QApplication::processEvents();
  require(f.sidebar.expandedFolders().isEmpty() && !f.tree->isExpanded(f.folder),
          "collapse all clears pending expansion");
  f.sidebar.setFolderRoot({});
  f.sidebar.setFolderRoot(f.dir.path());
  f.sidebar.restoreExpandedFolders(saved + QStringList{"../", "missing", f.dir.path()});
  waitUntil([&] { return f.tree->isExpanded(f.model->index(leafPath)); },
            "nested state restores after reopening a workspace");
  require(f.sidebar.expandedFolders() == saved, "restore excludes absolute, missing and outside workspace entries");
  f.tree->setExpanded(f.folder, false);
  const auto collapsedParent = f.sidebar.expandedFolders();
  f.sidebar.restoreExpandedFolders(collapsedParent);
  waitUntil([&] { return f.tree->isExpanded(f.model->index(nestedPath)); }, "hidden descendant expansion is retained");
  require(!f.tree->isExpanded(f.folder), "restoring descendants does not reopen their collapsed parent");
  const auto file = f.dir.filePath("Folder/Nested/inside.md");
  QFile out(file);
  require(out.open(QIODevice::WriteOnly), "create nested active document");
  out.write("text\n");
  out.close();
  f.sidebar.setCurrentDocument("inside.md", file, false);
  collapse->click();
  f.sidebar.setCurrentDocument("inside.md", file, true);
  require(!f.tree->isExpanded(f.folder), "status updates for the active file preserve collapsed folders");
  require(QDir(f.dir.path()).mkpath("Unloaded/More/Leaf"), "create fresh branch for expand all");
  f.sidebar.expandAllFolders();
  waitUntil([&] { return f.tree->isExpanded(f.model->index(f.dir.filePath("Unloaded/More/Leaf"))); },
            "expand all discovers and expands previously unloaded branches");
  collapse->click();
  QApplication::processEvents();
  require(f.sidebar.expandedFolders().isEmpty(), "collapse cancels recursive expansion");
  if (const auto screenshot = qEnvironmentVariable("MUFFIN_SIDEBAR_SCREENSHOT"); !screenshot.isEmpty()) {
    f.sidebar.restoreExpandedFolders(saved);
    QApplication::processEvents();
    require(f.sidebar.grab().save(screenshot), "capture explorer toolbar appearance");
    const auto night = ThemeDefinition::builtIn(QStringLiteral("night"));
    require(night.has_value(), "night appearance is available");
    f.sidebar.applyTheme(*night);
    QApplication::processEvents();
    require(f.sidebar.grab().save(screenshot + QStringLiteral(".night.png")),
            "capture dark explorer toolbar appearance");
  }
}

void testFolderPinAndFollowState() {
  QTemporaryDir dir;
  require(QDir(dir.path()).mkpath("A/Branch/Deep") && QDir(dir.path()).mkpath("C"), "create followed directories");
  for (const auto* name : {"A", "A/Branch/Deep", "C"}) {
    for (int i = 0; i < 80; ++i) {
      QFile file(dir.filePath(QStringLiteral("%1/note%2.md").arg(name).arg(i, 3, 10, QLatin1Char('0'))));
      require(file.open(QIODevice::WriteOnly), "create scrolling directory");
      file.write("note\n");
    }
  }
  SidebarWidget sidebar;
  sidebar.resize(SidebarWidget::kMinimumWidth, 310);
  sidebar.show();
  auto* tree = sidebar.findChild<QTreeView*>("FileTree");
  auto* model = qobject_cast<FileTreeModel*>(tree->model());
  auto* pin = sidebar.findChild<QToolButton*>("FolderPinButton");
  require(pin && !pin->isEnabled() && !pin->isChecked(), "empty tree has a disabled follow pin");
  const auto emptyState = sidebar.folderState();
  const QString a = dir.filePath("A"), c = dir.filePath("C");
  sidebar.setCurrentDocument("note000.md", a + "/note000.md", false);
  waitUntil([&] { return model->rowCount(tree->rootIndex()) == 81; }, "first followed directory loads");
  require(sidebar.folderRoot() == a && !pin->isChecked() && pin->isEnabled(), "direct file open follows its directory");
  sidebar.restoreExpandedFolders({"Branch", "Branch/Deep"});
  waitUntil(
      [&] {
        const auto deep = model->index(a + "/Branch/Deep");
        return tree->isExpanded(deep) && model->rowCount(deep) == 80;
      },
      "followed folder expansion loads");
  tree->verticalScrollBar()->setValue(700);
  QApplication::processEvents();
  const int scroll = tree->verticalScrollBar()->value();
  const auto anchor = QPersistentModelIndex(tree->indexAt({60, 0}));
  const int y = tree->visualRect(anchor).top();
  require(scroll > 0 && anchor.isValid(), "following fixture has a scrolled view");
  sidebar.setCurrentDocument("note000.md", c + "/note000.md", false);
  waitUntil([&] { return model->rowCount(tree->rootIndex()) == 80; }, "second followed directory loads");
  require(sidebar.folderRoot() == c, "another file changes the followed root");
  sidebar.setCurrentDocument("note000.md", a + "/note000.md", false);
  waitUntil(
      [&] {
        return tree->isExpanded(model->index(a + "/Branch/Deep")) && qAbs(tree->visualRect(anchor).top() - y) <= 1;
      },
      "return restores root expansion and visual scroll anchor");
  require(qAbs(tree->verticalScrollBar()->value() - scroll) <= 1, "return does not scroll to the active file");
  {
    SidebarWidget restored;
    restored.resize(sidebar.size());
    restored.show();
    restored.setCurrentDocument("note000.md", a + "/note000.md", false);
    restored.restoreFolderState(sidebar.folderState());
    auto* restoredTree = restored.findChild<QTreeView*>("FileTree");
    auto* restoredModel = qobject_cast<FileTreeModel*>(restoredTree->model());
    const QString anchorPath = model->filePath(anchor);
    require(anchorPath.startsWith(a + "/Branch/Deep/"), "saved viewport starts inside a nested branch");
    waitUntil(
        [&] {
          const auto restoredAnchor = restoredModel->index(anchorPath);
          return restoredModel->isDirectoryLoaded(restoredModel->index(a + "/Branch/Deep")) &&
                 !restoredTree->visualRect(restoredAnchor).isEmpty() &&
                 qAbs(restoredTree->visualRect(restoredAnchor).top() - y) <= 1;
        },
        "cold filesystem model restores the nested viewport after all saved branches load");
    require(!restored.folderPinned(), "cold restore preserves follow mode");
  }
  sidebar.setCurrentDocument("note000.md", c + "/note000.md", false);
  sidebar.setCurrentDocument("note000.md", a + "/note000.md", false);
  sidebar.setCurrentPath(a + "/note000.md");
  waitUntil(
      [&] {
        const auto revealed = tree->visualRect(model->index(a + "/note000.md"));
        return tree->isExpanded(model->index(a + "/Branch/Deep")) && !revealed.isEmpty() &&
               tree->viewport()->rect().intersects(revealed);
      },
      "explicit reveal wins over the saved viewport while switching roots");
  pin->click();
  require(sidebar.folderPinned() && pin->isChecked(), "click pins the current directory");
  sidebar.setCurrentDocument("note000.md", c + "/note000.md", false);
  require(sidebar.folderRoot() == a && !tree->currentIndex().isValid(),
          "pinned tree stays put without selecting an outside row");
  pin->click();
  require(!sidebar.folderPinned() && sidebar.folderRoot() == c, "unpin immediately follows the current file");
  sidebar.setCurrentDocument("Untitled", {}, true);
  require(sidebar.folderRoot() == c, "untitled tabs keep the last followed directory");
  sidebar.setFolderRoot(a);
  require(sidebar.folderPinned() && pin->isChecked(), "Open Folder pins even when a directory is already shown");
  sidebar.setFolderRoot({});
  sidebar.setCurrentDocument("note000.md", c + "/note000.md", false);
  require(sidebar.folderRoot().isEmpty() && !pin->isEnabled(), "explicitly closed tree stays closed");
  {
    SidebarWidget restored;
    restored.restoreFolderState(sidebar.folderState());
    restored.setCurrentDocument("note000.md", c + "/note000.md", false);
    require(restored.folderRoot().isEmpty(), "explicitly closed tree stays closed after restart");
    restored.restoreFolderState(emptyState);
    require(restored.folderRoot() == c && !restored.folderPinned(),
            "fresh empty session can still follow its first opened file after restart");
  }
  auto* search = sidebar.findChild<QToolButton*>("SearchFilesButton");
  require(sidebar.width() == SidebarWidget::kMinimumWidth && pin->geometry().left() >= 0 &&
              search->geometry().right() < search->parentWidget()->width(),
          "pin fits without widening the minimum sidebar");
}

void testClosingWorkspaceAndToolbarCreationTargets() {
  FileTreeFixture f;
  auto* newFile = f.sidebar.findChild<QToolButton*>("NewFileButton");
  auto* newFolder = f.sidebar.findChild<QToolButton*>("NewFolderButton");
  QString fileTarget, folderTarget;
  QObject::connect(&f.sidebar, &SidebarWidget::newFileRequested, &f.sidebar,
                   [&](const QString& path) { fileTarget = path; });
  QObject::connect(&f.sidebar, &SidebarWidget::newFolderRequested, &f.sidebar,
                   [&](const QString& path) { folderTarget = path; });
  f.tree->setCurrentIndex(f.folder);
  newFile->click();
  newFolder->click();
  require(fileTarget == f.dir.filePath("Folder") && folderTarget == fileTarget,
          "creation toolbar uses the selected folder");
  f.tree->setCurrentIndex(f.model->index(f.dir.filePath("document.md")));
  newFile->click();
  require(fileTarget == f.dir.path(), "selecting a file creates beside that file");
  f.sidebar.restoreExpandedFolders({"Folder", "Folder/Nested"});
  f.sidebar.setFolderRoot({});
  QApplication::processEvents();
  auto* empty = f.sidebar.findChild<QLabel*>("FilesEmptyLabel");
  require(f.sidebar.folderRoot().isEmpty() && !f.tree->isVisible() && empty && empty->isVisible() &&
              !newFile->isEnabled() && !newFolder->isEnabled(),
          "closed workspace shows an empty tree with disabled creation controls");
  f.sidebar.setCurrentDocument("document.md", f.dir.filePath("document.md"), false);
  require(f.sidebar.folderRoot().isEmpty() && f.sidebar.expandedFolders().isEmpty(),
          "pending loads and document notifications cannot reopen a closed workspace");
}

QStringList fileNames(FileTreeFixture& f) {
  QStringList result;
  for (int row = 0; row < f.model->rowCount(f.tree->rootIndex()); ++row) {
    const auto index = f.model->index(row, 0, f.tree->rootIndex());
    if (!f.model->isDir(index))
      result.append(f.model->fileName(index));
  }
  return result;
}

void testSortMenuAndPersistence() {
  // Finish fixture data and metadata before the asynchronous model scans it.
  FileTreeFixture f([](const QTemporaryDir& dir) {
    const auto writeFile = [&](const QString& name, int size, qint64 modified) {
      QFile file(dir.filePath(name));
      require(file.open(QIODevice::WriteOnly), "create sortable fixture file");
      require(file.write(QByteArray(size, 'x')) == size && file.flush(),
              "flush sortable fixture before setting its time");
      require(file.setFileTime(QDateTime::fromSecsSinceEpoch(modified), QFileDevice::FileModificationTime),
              "set fixture modification time");
      file.close();
      require(QFileInfo(file.fileName()).lastModified().toSecsSinceEpoch() == modified,
              "fixture modification time survives closing the file");
    };
    writeFile("note10.md", 10, 1000000000);
    writeFile("note2.md", 2, 1200000000);
    writeFile("a.txt", 30, 1100000000);
  });
  waitUntil([&] { return f.model->rowCount(f.tree->rootIndex()) == 5; }, "new files join the sorted tree");
  auto* menu = f.sidebar.findChild<QMenu*>("FileSortMenu");
  require(menu && f.sidebar.findChild<QToolButton*>("SortFilesButton")->menu() == menu,
          "sort icon opens a dropdown menu");
  const auto choose = [&](const char* name) {
    auto* action = menu->findChild<QAction*>(name);
    require(action && !action->text().isEmpty(), "sort option has a translated label");
    action->trigger();
    QApplication::processEvents();
    require(f.model->isDir(f.model->index(0, 0, f.tree->rootIndex())), "folders stay first in either sort direction");
  };
  f.tree->setCurrentIndex(f.model->index(f.dir.filePath("document.md")));
  QPersistentModelIndex selected(f.tree->currentIndex());
  choose("SortByName");
  choose("SortAscending");
  require(fileNames(f) == QStringList{"a.txt", "document.md", "note2.md", "note10.md"},
          "filename sort uses natural numeric order");
  choose("SortDescending");
  require(fileNames(f) == QStringList{"note10.md", "note2.md", "document.md", "a.txt"},
          "descending reverses file order without moving folders below files");
  choose("SortBySize");
  require(fileNames(f) == QStringList{"a.txt", "document.md", "note10.md", "note2.md"}, "size sorting uses file sizes");
  choose("SortAscending");
  choose("SortByType");
  require(fileNames(f).last() == "a.txt", "type sorting groups filename extensions");
  choose("SortByModified");
  require(fileNames(f) == QStringList{"note10.md", "a.txt", "note2.md", "document.md"},
          "date sorting uses modification timestamps");
  choose("SortByCreated");
  const auto names = fileNames(f);
  QDateTime previous;
  for (const auto& name : names) {
    const QFileInfo info(f.dir.filePath(name));
    const auto created = info.birthTime().isValid() ? info.birthTime() : info.lastModified();
    require(!previous.isValid() || previous <= created,
            "creation dates are ordered with a fallback on unsupported filesystems");
    previous = created;
  }
  require(selected.isValid() && f.tree->currentIndex() == selected && f.model->fileName(selected) == "document.md",
          "sorting preserves the selected file");
  std::unique_ptr<QMimeData> mime(f.model->mimeData({selected}));
  require(mime && mime->hasFormat(kMuffinFileTreeDragMime) && mime->hasUrls(),
          "sorting preserves the in-app file drag marker and URL");
  SidebarWidget restored;
  auto* restoredTree = restored.findChild<QTreeView*>("FileTree");
  auto* restoredModel = qobject_cast<FileTreeModel*>(restoredTree->model());
  require(restoredModel->sortBy() == FileTreeModel::SortBy::Created && restoredModel->sortOrder() == Qt::AscendingOrder,
          "a new explorer remembers the selected field and direction");
  QSettings().remove("files/sortBy");
  QSettings().remove("files/sortDescending");
}

void testRefreshRescansWithoutLosingState() {
  FileTreeFixture f;
  const auto inside = f.dir.filePath("Folder/Nested/inside.md");
  {
    QFile file(inside);
    require(file.open(QIODevice::WriteOnly), "create selected nested file");
    file.write("old");
  }
  require(QDir(f.dir.path()).mkpath("Unloaded/Deep/Leaf"), "create unloaded subtree");
  f.sidebar.restoreExpandedFolders({"Folder", "Folder/Nested"});
  waitUntil(
      [&] {
        return f.tree->isExpanded(f.model->index(f.dir.filePath("Folder/Nested"))) && f.model->index(inside).isValid();
      },
      "nested selection is loaded before refresh");
  f.tree->setCurrentIndex(f.model->index(inside));
  const auto saved = f.sidebar.expandedFolders();
  auto* oldSource = qobject_cast<QFileSystemModel*>(f.model->sourceModel());
  require(oldSource != nullptr, "refresh fixture exposes the asynchronous filesystem source");
  // Simulate missed watcher notifications: refresh must discard stale caches,
  // rather than merely repainting the tree or reusing the same root index.
  oldSource->setOption(QFileSystemModel::DontWatchForChanges, true);
  require(QFile::remove(f.dir.filePath("document.md")), "remove a file while notifications are disabled");
  const auto added = f.dir.filePath("Folder/Nested/added.md");
  {
    QFile file(added);
    require(file.open(QIODevice::WriteOnly), "add file while notifications are disabled");
    file.write("new");
  }
  auto* refresh = f.sidebar.findChild<QToolButton*>("RefreshFoldersButton");
  require(refresh && !refresh->toolTip().isEmpty(), "refresh button has a hint");
  refresh->click();
  waitUntil(
      [&] {
        return f.model->sourceModel() != oldSource && f.model->index(added).isValid() &&
               !f.model->index(f.dir.filePath("document.md")).isValid() &&
               f.tree->isExpanded(f.model->index(f.dir.filePath("Folder/Nested"))) &&
               f.model->filePath(f.tree->currentIndex()) == inside;
      },
      "manual refresh reloads missed changes and restores nested expansion and selection");
  require(f.sidebar.expandedFolders() == saved && f.openedFiles == 0,
          "refresh preserves saved folders without opening a document");
  const auto unloaded = f.model->index(f.dir.filePath("Unloaded"));
  require(!f.tree->isExpanded(unloaded) && f.model->canFetchMore(unloaded),
          "refresh leaves unrelated subtrees unloaded");
  f.sidebar.setFolderRoot({});
  require(!refresh->isEnabled() && !f.sidebar.findChild<QToolButton*>("SortFilesButton")->isEnabled(),
          "closed workspace disables refresh and sort");
}

} // namespace

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  installMuffinTestCrashHandler();
  QTemporaryDir settings;
  QCoreApplication::setOrganizationName("MuffinTests");
  QCoreApplication::setApplicationName("SidebarInteraction");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  runTest("testToolbarIconsRetainTheirShapeAtHighDpi", testToolbarIconsRetainTheirShapeAtHighDpi);
  runTest("testFolderPinAndFollowState", testFolderPinAndFollowState);
  runTest("testSortMenuAndPersistence", testSortMenuAndPersistence);
  runTest(
      "testSortMenuAndPersistenceInCLocale", +[] {
        const QLocale previousLocale;
        QLocale::setDefault(QLocale::c());
        testSortMenuAndPersistence();
        QLocale::setDefault(previousLocale);
      });
  runTest("testRefreshRescansWithoutLosingState", testRefreshRescansWithoutLosingState);
  runTest("testToolbarCollapsesFoldersAndRestoresState", testToolbarCollapsesFoldersAndRestoresState);
  runTest("testClosingWorkspaceAndToolbarCreationTargets", testClosingWorkspaceAndToolbarCreationTargets);
  runTest("testFolderNameAndRowToggleOnSingleClick", testFolderNameAndRowToggleOnSingleClick);
  runTest("testFolderDoubleClickTogglesOnlyOnce", testFolderDoubleClickTogglesOnlyOnce);
  runTest("testArrowAndNestedFolderClicks", testArrowAndNestedFolderClicks);
  runTest("testFileDoubleClickStillOpensDocument", testFileDoubleClickStillOpensDocument);
  runTest("testRightClickAndInlineRenameDoNotToggleFolder", testRightClickAndInlineRenameDoNotToggleFolder);
  return 0;
}
