#include "app/MainWindow.h"

#include "app/PreferencesDialog.h"
#include "app/SidebarWidget.h"
#include "editor/EditorView.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QUrl>

namespace {

int countWords(const QString& text) {
  int count = 0;
  bool inWord = false;
  for (const QChar ch : text) {
    const bool wordChar = ch.isLetterOrNumber() || ch == QLatin1Char('_');
    if (wordChar && !inWord) {
      ++count;
    }
    inWord = wordChar;
  }
  return count;
}

QString elidedPath(const QString& path) {
  const QString nativePath = QDir::toNativeSeparators(path);
  constexpr qsizetype maxLength = 72;
  if (nativePath.size() <= maxLength) {
    return nativePath;
  }
  return QStringLiteral("...") + nativePath.right(maxLength - 3);
}

}  // namespace

void muffin::MainWindow::rebuildRecentFilesMenu() {
  if (!recentFilesMenu_) {
    return;
  }

  recentFilesMenu_->clear();
  const QStringList paths = recentFiles();
  recentFilesMenu_->setEnabled(!paths.isEmpty());

  for (const QString& path : paths) {
    QAction* action = recentFilesMenu_->addAction(elidedPath(path));
    action->setToolTip(QDir::toNativeSeparators(path));
    connect(action, &QAction::triggered, this, [this, path] {
      openFile(path);
    });
  }

  if (!paths.isEmpty()) {
    recentFilesMenu_->addSeparator();
    QAction* clearAction = recentFilesMenu_->addAction(tr("Clear Recent Files"));
    connect(clearAction, &QAction::triggered, this, [this] {
      setRecentFiles({});
      rebuildRecentFilesMenu();
    });
  }
}

void muffin::MainWindow::addRecentFile(QString path) {
  if (path.isEmpty()) {
    return;
  }
  path = QFileInfo(path).absoluteFilePath();

  QStringList paths = recentFiles();
  paths.removeAll(path);
  paths.prepend(path);
  while (paths.size() > 10) {
    paths.removeLast();
  }
  setRecentFiles(paths);
  rebuildRecentFilesMenu();
}

QStringList muffin::MainWindow::recentFiles() const {
  QSettings settings;
  return settings.value(QStringLiteral("recentFiles")).toStringList();
}

void muffin::MainWindow::setRecentFiles(const QStringList& paths) const {
  QSettings settings;
  settings.setValue(QStringLiteral("recentFiles"), paths);
}

void muffin::MainWindow::showDocumentProperties() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  const QFileInfo info(session_.filePath());
  const QString message = tr(
      "Name: %1\n"
      "Location: %2\n"
      "Size: %3 bytes\n"
      "Modified: %4\n"
      "Words: %5\n"
      "Parse time: %6 ms")
                              .arg(
                                  info.fileName(),
                                  QDir::toNativeSeparators(info.absolutePath()),
                                  QString::number(info.size()),
                                  QLocale().toString(info.lastModified(), QLocale::ShortFormat),
                                  QString::number(countWords(session_.markdownText())),
                                  QString::number(session_.lastParseElapsedMs()));
  QMessageBox::information(this, tr("Properties"), message);
}

void muffin::MainWindow::showPreferences() {
  PreferencesDialog dialog(this);
  dialog.setAvailableThemes(themeManager_.availableThemes());
  dialog.setCurrentThemeName(themeManager_.currentThemeName());
  dialog.setStatusBarVisible(statusBar()->isVisible());
  dialog.setZoomPercent(zoomPercent());
  dialog.setFontSizePx(fontSizePx());

  connect(&dialog, &PreferencesDialog::themeRequested, this, [this](const QString& name) {
    if (themeManager_.setTheme(name)) {
      saveAppearanceTheme(themeManager_.currentThemeName());
    }
  });
  connect(&dialog, &PreferencesDialog::statusBarVisibleRequested, this, [this](bool visible) {
    setStatusBarVisible(visible);
    saveAppearanceStatusBarVisible(visible);
  });
  connect(&dialog, &PreferencesDialog::zoomPercentRequested, this, [this](int percent) {
    setZoomPercent(percent);
    saveAppearanceZoomPercent(zoomPercent_);
  });
  connect(&dialog, &PreferencesDialog::fontSizePxRequested, this, [this](int px) {
    setFontSizePx(px);
    saveAppearanceFontSizePx(fontSizePx_);
  });
  connect(&dialog, &PreferencesDialog::clearRecentFilesRequested, this, [this] {
    setRecentFiles({});
    rebuildRecentFilesMenu();
  });
  connect(&dialog, &PreferencesDialog::disableTypewriterFocusRequested, this, [this] {
    setTypewriterMode(false);
    setFocusMode(false);
    saveAppearanceTypewriterMode(false);
    saveAppearanceFocusMode(false);
  });

  dialog.exec();
}

void muffin::MainWindow::revealCurrentFile() {
  if (session_.filePath().isEmpty()) {
    return;
  }
  const QFileInfo info(session_.filePath());
  QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

bool muffin::MainWindow::saveCurrentDocument() {
  if (fileController_.save(session_, this)) {
    addRecentFile(session_.filePath());
    return true;
  }
  return false;
}

bool muffin::MainWindow::isDocumentModified() const {
  return session_.document().isModified();
}

void muffin::MainWindow::buildReopenEncodingMenu() {
  if (!reopenEncodingMenu_) {
    return;
  }

  reopenEncodingMenu_->clear();

  // name = ICU canonical name, label = display text
  struct Entry {
    const char* name;
    const char* label;
  };

  struct Group {
    const char* title;
    std::vector<Entry> entries;
  };

  static const Group groups[] = {
      {QT_TRANSLATE_NOOP("MainWindow", "Unicode"),
       {
           {"UTF-8", QT_TRANSLATE_NOOP("MainWindow", "UTF-8")},
           {"UTF-16LE", QT_TRANSLATE_NOOP("MainWindow", "UTF-16 LE")},
           {"UTF-16BE", QT_TRANSLATE_NOOP("MainWindow", "UTF-16 BE")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Western"),
       {
           {"windows-1252", QT_TRANSLATE_NOOP("MainWindow", "Western (Windows-1252)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Cyrillic"),
       {
           {"windows-1251", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (Windows-1251)")},
           {"ISO-8859-5", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (ISO-8859-5)")},
           {"IBM866", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (IBM866)")},
           {"IBM855", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (IBM855)")},
           {"KOI8-R", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (KOI8-R)")},
           {"x-mac-cyrillic", QT_TRANSLATE_NOOP("MainWindow", "Cyrillic (Mac)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Central European"),
       {
           {"windows-1250", QT_TRANSLATE_NOOP("MainWindow", "Central European (Windows-1250)")},
           {"ISO-8859-2", QT_TRANSLATE_NOOP("MainWindow", "Central European (ISO-8859-2)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Greek"),
       {
           {"windows-1253", QT_TRANSLATE_NOOP("MainWindow", "Greek (Windows-1253)")},
           {"ISO-8859-7", QT_TRANSLATE_NOOP("MainWindow", "Greek (ISO-8859-7)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Hebrew"),
       {
           {"windows-1255", QT_TRANSLATE_NOOP("MainWindow", "Hebrew (Windows-1255)")},
           {"ISO-8859-8", QT_TRANSLATE_NOOP("MainWindow", "Hebrew (ISO-8859-8)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Chinese Simplified"),
       {
           {"GB2312", QT_TRANSLATE_NOOP("MainWindow", "Chinese Simplified (GB2312)")},
           {"GB18030", QT_TRANSLATE_NOOP("MainWindow", "Chinese Simplified (GB18030)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Chinese Traditional"),
       {
           {"Big5", QT_TRANSLATE_NOOP("MainWindow", "Chinese Traditional (Big5)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Japanese"),
       {
           {"Shift_JIS", QT_TRANSLATE_NOOP("MainWindow", "Japanese (Shift_JIS)")},
           {"EUC-JP", QT_TRANSLATE_NOOP("MainWindow", "Japanese (EUC-JP)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Korean"),
       {
           {"EUC-KR", QT_TRANSLATE_NOOP("MainWindow", "Korean (EUC-KR)")},
       }},
      {QT_TRANSLATE_NOOP("MainWindow", "Thai"),
       {
           {"TIS-620", QT_TRANSLATE_NOOP("MainWindow", "Thai (TIS-620)")},
       }},
  };

  for (const auto& group : groups) {
    for (const auto& entry : group.entries) {
      QAction* action = reopenEncodingMenu_->addAction(tr(entry.label));
      const QString encodingName = QString::fromLatin1(entry.name);
      connect(action, &QAction::triggered, this, [this, encodingName] {
        reopenWithEncoding(encodingName);
      });
    }
    reopenEncodingMenu_->addSeparator();
  }
}

void muffin::MainWindow::reopenWithEncoding(const QString& encodingName) {
  if (session_.filePath().isEmpty()) {
    return;
  }

  if (fileController_.reopenWithEncoding(session_, this, encodingName)) {
    editorController_.clearHistoryAndSelection();
  }
}

void muffin::MainWindow::moveToFile() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  const QString oldPath = session_.filePath();
  if (!fileController_.moveTo(session_, this)) {
    return;
  }

  QStringList recent = recentFiles();
  recent.removeAll(QFileInfo(oldPath).absoluteFilePath());
  setRecentFiles(recent);
  addRecentFile(session_.filePath());
  editorController_.clearHistoryAndSelection();
}

void muffin::MainWindow::saveAllOpenFiles() {
  int savedCount = 0;
  int failedCount = 0;

  for (QWidget* widget : QApplication::topLevelWidgets()) {
    auto* window = qobject_cast<MainWindow*>(widget);
    if (!window) {
      continue;
    }
    if (window->isDocumentModified()) {
      if (window->saveCurrentDocument()) {
        ++savedCount;
      } else {
        ++failedCount;
      }
    }
  }

  if (failedCount > 0) {
    QMessageBox::warning(this, tr("Save All"),
                         tr("Saved %1 file(s). %2 file(s) could not be saved.")
                             .arg(savedCount)
                             .arg(failedCount));
  }
}

void muffin::MainWindow::showInSidebar() {
  if (session_.filePath().isEmpty()) {
    return;
  }
  setSidebarPanel(SidebarWidget::Panel::Files);
  sidebar_->setCurrentDocument(
      session_.displayName(), session_.filePath(),
      session_.document().isModified());
}

void muffin::MainWindow::deleteFile() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  const QString filePath = session_.filePath();
  const QString fileName = QFileInfo(filePath).fileName();

  const QMessageBox::StandardButton confirm = QMessageBox::warning(
      this, tr("Delete File"),
      tr("Are you sure you want to move \"%1\" to the trash?\n\n"
         "This action cannot be undone.")
          .arg(fileName),
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No);

  if (confirm != QMessageBox::Yes) {
    return;
  }

  bool trashed = QFile::moveToTrash(filePath);

  if (!trashed) {
    const QMessageBox::StandardButton confirm2 = QMessageBox::critical(
        this, tr("Delete File"),
        tr("Could not move to trash. Permanently delete \"%1\"?\n\n"
           "This action cannot be undone.")
            .arg(fileName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (confirm2 != QMessageBox::Yes) {
      return;
    }

    if (!QFile::remove(filePath)) {
      QMessageBox::critical(this, tr("Delete Failed"),
                            tr("Could not delete file:\n%1").arg(filePath));
      return;
    }
  }

  fileController_.newFile(session_, this);
  editorController_.clearHistoryAndSelection();

  QStringList recent = recentFiles();
  recent.removeAll(QFileInfo(filePath).absoluteFilePath());
  setRecentFiles(recent);
  rebuildRecentFilesMenu();
}

bool muffin::MainWindow::maybeSaveChanges() {
  if (!session_.document().isModified()) {
    return true;
  }

  const QMessageBox::StandardButton choice = QMessageBox::warning(
      this,
      tr("Muffin"),
      tr("The current document has unsaved changes."),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  if (choice == QMessageBox::Cancel) {
    return false;
  }
  if (choice == QMessageBox::Save) {
    return fileController_.save(session_, this);
  }
  return true;
}
