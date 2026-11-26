// Where the document's text may be changed.
//
// DocumentSession owns the text and has the functions that change it, but only the editor knows
// about carets, the undo stack and the layout. Code that changed the text through the session
// directly therefore lost those: image path rewrites and spelling replacements once skipped the
// undo stack and were not repainted. The window and the command controllers go through the editor
// instead (applyEdit, RenderCommandFacade::replaceSourceRange).
//
// This test reads the sources and fails on any caller of the session's edit functions that is not
// one of the editor's own entry points.

#include "../TestUtils.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace {

// A file that may change the text, and the session functions it may call. The comment says why.
struct Caller {
  const char* path;
  const char* functions;
};

const Caller kCallers[] = {
    {"src/document/DocumentSession.cpp", "*"},
    {"src/document/DocumentSession.h", "*"},
    // The editor's edit pipeline.
    {"src/editor/TextEditApplier.cpp", "applyTextEdit"},
    // History replay, a source-range replace with undo, and a whole-text replace with undo.
    {"src/editor/EditorControllerUndo.cpp", "applyTextEdit|applyTableSnapshot|applyNodeSnapshot|applyInsertedNode"},
    {"src/editor/EditorController.cpp", "applyTextEdit|applyMarkdownText"},
    // Source mode.
    {"src/editor/VirtualSourceEdit.cpp", "applyTextEdit"},
    {"src/app/RenderEditorBackend.cpp", "applyMarkdownText"},
    // Block controllers change one block in place and keep undo records of their own.
    {"src/blocks/table/TableController.cpp", "applyTextEdit|applyTableSnapshot|applyNodeSnapshot|applyInsertedNode"},
    {"src/blocks/code/CodeFenceController.cpp", "applyNodeSnapshot"},
    {"src/blocks/literal/LiteralBlockController.cpp", "applyNodeSnapshot"},
    {"src/blocks/literal/LiteralBlockUtil.cpp", "applyInsertedNode"},
};

QString withoutLineComment(QString line) {
  const qsizetype start = line.indexOf(QStringLiteral("//"));
  return start < 0 ? line : line.left(start);
}

} // namespace

void testOnlyTheEditorChangesTheText() {
  const QString sourceDir = QStringLiteral(MUFFIN_SOURCE_DIR "/");
  const QRegularExpression call(
      QStringLiteral("\\bapply(TextEdit|MarkdownText|TableSnapshot|NodeSnapshot|InsertedNode)\\s*\\("));
  QHash<QString, QString> allowed;
  for (const Caller& caller : kCallers) {
    allowed.insert(QString::fromUtf8(caller.path), QString::fromUtf8(caller.functions));
  }

  QStringList offenders;
  int seen = 0;
  QDirIterator it(sourceDir + QStringLiteral("src"), QStringList() << QStringLiteral("*.cpp") << QStringLiteral("*.h"),
                  QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    const QString relative = QDir(sourceDir).relativeFilePath(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      require(false, QStringLiteral("cannot read %1").arg(relative));
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (qsizetype index = 0; index < lines.size(); ++index) {
      const QString line = withoutLineComment(lines.at(index));
      auto matches = call.globalMatch(line);
      while (matches.hasNext()) {
        ++seen;
        const auto match = matches.next();
        const QString function = QStringLiteral("apply") + match.captured(1);
        const QString permitted = allowed.value(relative);
        if (permitted == QStringLiteral("*") || permitted.split(QLatin1Char('|')).contains(function)) {
          continue;
        }
        offenders << QStringLiteral("%1:%2 calls %3").arg(relative).arg(index + 1).arg(function);
      }
    }
  }
  // The scan must see the entry points, or a wrong path would let anything through.
  require(seen >= 20, QStringLiteral("the scan found only %1 calls; is the source directory right?").arg(seen));
  for (const QString& offender : offenders) {
    qCritical().noquote() << offender;
  }
  require(offenders.isEmpty(), QStringLiteral("%1 caller(s) change the text outside the editor; route them through "
                                              "applyEdit or RenderCommandFacade::replaceSourceRange")
                                   .arg(offenders.size()));
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testOnlyTheEditorChangesTheText);
#undef RUN_TEST
  return 0;
}
