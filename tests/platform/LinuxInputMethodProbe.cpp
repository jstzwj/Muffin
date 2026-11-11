// Run only from a relocated dist by check_linux_input_methods.py. Native X11
// events go through Qt's input context and a real Fcitx service, not sendEvent.
#include "app/BundledQtPlugins.h"
#include "document/DocumentSession.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/VirtualSourceEdit.h"
#include "unicode/IcuRuntime.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QTimer>

#include <cstdio>

namespace {
class InputObserver : public QObject {
public:
  bool preedit = false;
  bool committed = false;
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::InputMethod) {
      const auto* input = static_cast<QInputMethodEvent*>(event);
      preedit |= !input->preeditString().isEmpty();
      committed |= input->commitString().contains(QString::fromUtf8("你好"));
    }
    return false;
  }
};
} // namespace

int main(int argc, char** argv) {
  muffin::initializeBundledQtPlugins();
  QApplication app(argc, argv);
  const auto directory = QCoreApplication::applicationDirPath();
  const auto paths = QCoreApplication::libraryPaths();
  bool bundleOnly = !paths.isEmpty();
  // Qt replays pre-construction path changes when it discovers qt.conf;
  // the same bundle path can consequently occur more than once.
  for (const auto& path : paths)
    bundleOnly &= QDir(path).canonicalPath() == directory;
  if (!bundleOnly || QString::fromLatin1(qVersion()) != QT_VERSION_STR) {
    std::fprintf(stderr, "Unexpected Qt runtime or plugin paths: %s, %s\n", qVersion(), qPrintable(paths.join(';')));
    return 2;
  }
  const auto arguments = app.arguments();
  if (arguments.size() == 2 && arguments[1] == QStringLiteral("--paths-only"))
    return 0;
  const QDir plugins(directory + QStringLiteral("/platforminputcontexts"));
  // Resolve every symbol immediately: this catches the private QObject ABI
  // failure even when a lazy loader would defer it until the first keypress.
  for (const auto& name : {QStringLiteral("libfcitx5platforminputcontextplugin.so"),
                           QStringLiteral("libibusplatforminputcontextplugin.so")}) {
    QPluginLoader plugin(plugins.filePath(name));
    plugin.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
    if (!plugin.load()) {
      std::fprintf(stderr, "%s\n", qPrintable(plugin.errorString()));
      return 3;
    }
  }
  if (arguments.size() == 2 && arguments[1] == QStringLiteral("--load-only"))
    return 0;
  if (arguments.size() != 3)
    return 4;
  QString error;
  if (!muffin::initializeIcuData(&error)) {
    std::fprintf(stderr, "%s\n", qPrintable(error));
    return 5;
  }
  const bool sourceMode = arguments[1] == QStringLiteral("source");
  muffin::DocumentSession session;
  muffin::EditorView rendered;
  muffin::EditorController controller;
  controller.attach(&session, &rendered);
  session.setMarkdownText(QStringLiteral("prefix"), false);
  rendered.setDocument(session.document());
  controller.setCursorForSourceOffset(6);
  muffin::VirtualSourceEdit source;
  source.bindSession(&session);
  source.setCursorPosition(6);
  QWidget* editor = sourceMode ? static_cast<QWidget*>(&source) : &rendered;
  editor->resize(800, 400);
  editor->setWindowTitle(QStringLiteral("Muffin IME probe"));
  editor->show();
  editor->activateWindow();
  editor->setFocus();
  InputObserver observer;
  app.installEventFilter(&observer);
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &app, [&] { app.exit(6); });
  deadline.start(20000);
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &app, [&] {
    if (!observer.committed)
      return;
    const auto text = session.markdownText().toString();
    const auto cursor = sourceMode ? source.inputMethodQuery(Qt::ImCursorRectangle).toRectF()
                                   : rendered.inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    const bool passed = observer.preedit && text == QString::fromUtf8("prefix你好") && cursor.isValid() &&
                        session.document().isModified();
    QFile report(arguments[2]);
    if (!report.open(QIODevice::WriteOnly)) {
      app.exit(7);
      return;
    }
    report.write(QJsonDocument(QJsonObject{{"passed", passed},
                                           {"preedit", observer.preedit},
                                           {"committed", observer.committed},
                                           {"text", text},
                                           {"qt", QString::fromLatin1(qVersion())},
                                           {"platform", QGuiApplication::platformName()}})
                     .toJson());
    app.exit(passed ? 0 : 8);
  });
  poll.start(50);
  return app.exec();
}
