#pragma once

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace muffin::test {

// A message box no step of a test answers blocks that step for good: the app
// shows it and waits in a nested event loop, nothing in the test ever closes
// it, and ctest waits with it. (Windows can refuse a file for a moment while
// another process looks at it, and what the app does about a failed save or
// rename is to say so in a box.) After `grace`, longer than any wait a test
// makes, the run fails with what the box says, the way `require` fails it,
// instead of hanging.
inline void failOnUnansweredDialog(std::chrono::milliseconds grace = std::chrono::seconds(60)) {
  struct Seen {
    QPointer<QWidget> dialog;
    QElapsedTimer since;
  };
  auto seen = std::make_shared<Seen>();
  auto* timer = new QTimer(QCoreApplication::instance());
  timer->setInterval(250);
  QObject::connect(timer, &QTimer::timeout, timer, [seen, grace] {
    auto* modal = QApplication::activeModalWidget();
    if (!modal) {
      seen->dialog = nullptr;
      return;
    }
    if (seen->dialog.data() != modal) {
      seen->dialog = modal;
      seen->since.start();
      return;
    }
    if (std::chrono::milliseconds(seen->since.elapsed()) < grace)
      return;
    const auto* box = qobject_cast<const QMessageBox*>(modal);
    const QString message = QStringLiteral("A dialog no test step answered blocks the run after %1 s: %2 \"%3\": %4")
                                .arg(seen->since.elapsed() / 1000)
                                .arg(QString::fromLatin1(modal->metaObject()->className()), modal->windowTitle(),
                                     box ? box->text() : QStringLiteral("(not a message box)"));
    fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    std::exit(1);
  });
  timer->start();
}

} // namespace muffin::test
