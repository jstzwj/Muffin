#include "app/FileOpenDispatcher.h"

#include "app/MainWindow.h"
#include "app/WindowsUpdater.h"

#include <QApplication>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QSettings>
#include <algorithm>

bool muffin::file_open_preferences::openInTabs() {
  QSettings settings;
  settings.sync();
  return settings.value(QLatin1String(kOpenInTabs), true).toBool();
}

muffin::FileOpenDispatcher::FileOpenDispatcher(QObject* parent) : QObject(parent) {
  qApp->installEventFilter(this);
  retry_.setInterval(100);
  retry_.setSingleShot(true);
  connect(&retry_, &QTimer::timeout, this, &FileOpenDispatcher::dispatch);
  connect(&WindowsUpdater::instance(), &WindowsUpdater::preparingRestart, this, [this] {
    if (!pending_.isEmpty())
      WindowsUpdater::instance().interruptRestart();
  });
}

bool muffin::FileOpenDispatcher::setInitialWindow(MainWindow* window) {
  initialWindow_ = window;
  ready_ = true;
  const bool hadRequests = !pending_.isEmpty();
  dispatch();
  return hadRequests;
}

void muffin::FileOpenDispatcher::openFiles(const QStringList& paths) {
  if (paths.isEmpty())
    return;
  // An acknowledged OS open must survive an update attempt. Let it cancel the
  // pending restart; dispatch() will retry after prepared windows are released.
  WindowsUpdater::instance().interruptRestart();
  pending_.enqueue({paths, !file_open_preferences::openInTabs()});
  dispatch();
}

bool muffin::FileOpenDispatcher::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::WindowActivate) {
    if (auto* window = qobject_cast<MainWindow*>(watched))
      lastActiveWindow_ = window;
  } else if (event->type() == QEvent::FileOpen) {
    const auto* fileEvent = static_cast<QFileOpenEvent*>(event);
    const auto path = fileEvent->file().isEmpty() ? fileEvent->url().toLocalFile() : fileEvent->file();
    if (!path.isEmpty())
      openFiles({QFileInfo(path).absoluteFilePath()});
    return true;
  }
  return QObject::eventFilter(watched, event);
}

muffin::MainWindow* muffin::FileOpenDispatcher::targetWindow() const {
  if (lastActiveWindow_ && lastActiveWindow_->isVisible())
    return lastActiveWindow_;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (auto* window = qobject_cast<MainWindow*>(widget); window && window->isVisible())
      return window;
  }
  return initialWindow_;
}

void muffin::FileOpenDispatcher::dispatch() {
  if (!ready_ || pending_.isEmpty())
    return;
  // Never interrupt a save/conflict dialog, export, or workspace replacement.
  if (QApplication::activeModalWidget() || WindowsUpdater::instance().restartPending()) {
    retry_.start();
    return;
  }
  while (!pending_.isEmpty()) {
    const auto request = pending_.dequeue();
    MainWindow* target = targetWindow();
    const bool openingFolder = std::any_of(request.paths.cbegin(), request.paths.cend(),
                                           [](const QString& path) { return QFileInfo(path).isDir(); });
    if (!target || ((request.newWindow || openingFolder) && target->isVisible())) {
      target = new MainWindow();
      target->setAttribute(Qt::WA_DeleteOnClose);
    }
    MainWindow* lastOpened = target;
    for (const QString& path : request.paths) {
      if (QFileInfo(path).isDir()) {
        target->openFolderAtPath(path);
        continue;
      }
      MainWindow* owner = target;
      if (!request.newWindow) {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (auto* window = qobject_cast<MainWindow*>(widget);
              window && window->isVisible() && window->hasOpenFile(path)) {
            owner = window;
            break;
          }
        }
      }
      owner->openFile(path);
      owner->show();
      lastOpened = owner;
    }
    lastOpened->setWindowState(lastOpened->windowState() & ~Qt::WindowMinimized);
    lastOpened->show();
    lastOpened->raise();
    lastOpened->activateWindow();
  }
}
