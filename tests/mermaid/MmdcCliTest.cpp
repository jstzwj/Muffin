#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

// Integration test for the muffin-mmdc CLI: runs the real binary (path baked
// in by CMake) as a subprocess and checks the mmdc-compatible contract —
// SVG/PNG emission, extension-driven format, -t theme override, stdin input,
// and the invalid-source behavior (error-diagram output + exit 1 + stderr
// diagnostic).
//
// QProcess needs an event loop only for async modes; the blocking
// QProcess::execute family used here works without one. QT_QPA_PLATFORM is
// inherited from ctest (offscreen) or left for the CLI to default.

using namespace std::chrono_literals;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  qFatal("%s", qPrintable(message));
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

struct RunResult {
  int exitCode = -1;
  QByteArray stdoutBytes;
  QByteArray stderrBytes;
};

RunResult run(const QStringList& arguments, const QByteArray& stdinBytes = QByteArray()) {
  QProcess process;
  process.start(QStringLiteral(MUFFIN_MMDC_PATH), arguments);
  if (!process.waitForStarted(10000)) fail("could not start muffin-mmdc");
  if (!stdinBytes.isEmpty()) {
    process.write(stdinBytes);
    process.closeWriteChannel();
  }
  if (!process.waitForFinished(60000)) {
    process.kill();
    fail(QStringLiteral("muffin-mmdc timed out: %1").arg(arguments.join(QLatin1Char(' '))));
  }
  return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTemporaryDir scratch;
  require(scratch.isValid(), "scratch dir");

  // 1. File → SVG (default output next to the input, mmdc's rule).
  {
    const QString input = scratch.filePath(QStringLiteral("in.mmd"));
    QFile file(input);
    require(file.open(QIODevice::WriteOnly), "write input");
    file.write("flowchart LR\n  A[Christmas] -->|Get money| B(Go shopping)\n");
    file.close();
    const RunResult result = run({QStringLiteral("-i"), input});
    require(result.exitCode == 0,
            QStringLiteral("svg render exit=%1 stderr=%2")
                .arg(result.exitCode)
                .arg(QString::fromUtf8(result.stderrBytes)));
    QFile out(scratch.filePath(QStringLiteral("in.svg")));
    require(out.exists(), "default .svg output next to input");
    require(out.open(QIODevice::ReadOnly), "open svg");
    const QByteArray svg = out.readAll();
    require(svg.startsWith("<?xml") || svg.contains("<svg"), "output looks like SVG");
  }

  // 2. Explicit PNG output with scale + background composite.
  {
    const QString input = scratch.filePath(QStringLiteral("in.mmd"));
    const QString output = scratch.filePath(QStringLiteral("chart.png"));
    const RunResult result = run({QStringLiteral("-i"), input,
                                  QStringLiteral("-o"), output,
                                  QStringLiteral("-s"), QStringLiteral("2"),
                                  QStringLiteral("-b"), QStringLiteral("white")});
    require(result.exitCode == 0,
            QStringLiteral("png render exit=%1 stderr=%2")
                .arg(result.exitCode)
                .arg(QString::fromUtf8(result.stderrBytes)));
    QFile out(output);
    require(out.open(QIODevice::ReadOnly), "open png");
    const QByteArray png = out.readAll();
    require(png.startsWith("\x89PNG"), "output looks like PNG");
  }

  // 3. stdin → stdout SVG.
  {
    const RunResult result = run({QStringLiteral("-o"), QStringLiteral("-")},
                                  QByteArray("flowchart TD\n  A --> B\n"));
    require(result.exitCode == 0, QStringLiteral("stdin/stdout exit=%1").arg(result.exitCode));
    require(result.stdoutBytes.contains("<svg"), "stdout carries SVG");
  }

  // 4. Theme override reaches the render (dark theme CSS class / fill in SVG).
  {
    const QString output = scratch.filePath(QStringLiteral("dark.svg"));
    const RunResult result = run({QStringLiteral("-i"), scratch.filePath(QStringLiteral("in.mmd")),
                                  QStringLiteral("-o"), output,
                                  QStringLiteral("-t"), QStringLiteral("dark")});
    require(result.exitCode == 0, "dark render exit");
    QFile out(output);
    require(out.open(QIODevice::ReadOnly), "open dark svg");
    // The dark theme swaps the node fill toward the dark palette: the default
    // theme's #ececff fill (flowchart node0) must not survive.
    const QByteArray svg = out.readAll();
    require(!svg.contains("ececff"), "dark theme overrode the default node fill");
  }

  // 5. Invalid source: error diagram still exported, exit 1, stderr carries a
  //    structured diagnostic (mmdc signals failure the same way).
  {
    const QString input = scratch.filePath(QStringLiteral("bad.mmd"));
    QFile file(input);
    require(file.open(QIODevice::WriteOnly), "write bad input");
    file.write("flowchart LR\n  A --> B[unclosed\n");
    file.close();
    const QString output = scratch.filePath(QStringLiteral("bad.svg"));
    const RunResult result = run({QStringLiteral("-i"), input, QStringLiteral("-o"), output});
    require(result.exitCode == 1, QStringLiteral("invalid source exit=%1").arg(result.exitCode));
    require(result.stderrBytes.contains("warning"), "stderr warns about the error export");
    QFile out(output);
    require(out.open(QIODevice::ReadOnly), "open error svg");
    require(out.readAll().contains("<svg"), "error diagram exported");
  }

  return 0;
}
