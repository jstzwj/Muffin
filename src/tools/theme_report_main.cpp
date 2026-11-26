#include "io/AtomicWrite.h"
#include "theme/CssCompatibilityReport.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>

#include <cmath>

// Developer diagnostic: stable English CLI and JSON identifiers, independent
// of the editor's language/settings and without a GUI or theme font loading.
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  app.setApplicationName("muffin-theme-report");
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Audit native CSS theme capabilities offline; JSON output does not imply browser pixel parity.");
  parser.addHelpOption();
  parser.addPositionalArgument("css", "CSS stylesheet to audit.");
  const QCommandLineOption output("output", "Write JSON atomically to a file (default: stdout).", "file");
  const QCommandLineOption dark("dark", "Evaluate conditions in dark mode.");
  const QCommandLineOption print("print", "Evaluate print media conditions.");
  const QCommandLineOption width("width", "Viewport width in CSS pixels.", "pixels", "1024");
  const QCommandLineOption height("height", "Viewport height in CSS pixels.", "pixels", "768");
  parser.addOptions({output, dark, print, width, height});
  parser.process(app);
  if (parser.positionalArguments().size() != 1)
    parser.showHelp(2);
  muffin::CssEnvironment environment;
  bool widthOk = false, heightOk = false;
  environment.viewportWidth = parser.value(width).toDouble(&widthOk);
  environment.viewportHeight = parser.value(height).toDouble(&heightOk);
  if (!widthOk || !heightOk || !std::isfinite(environment.viewportWidth) ||
      !std::isfinite(environment.viewportHeight) || environment.viewportWidth <= 0 || environment.viewportHeight <= 0) {
    QTextStream(stderr) << "Viewport dimensions must be finite, positive numbers.\n";
    return 2;
  }
  environment.dark = parser.isSet(dark);
  environment.print = parser.isSet(print);
  const QFileInfo source(parser.positionalArguments().first());
  QFile input(source.absoluteFilePath());
  if (!input.open(QIODevice::ReadOnly)) {
    QTextStream(stderr) << "Cannot read CSS: " << input.errorString() << '\n';
    return 2;
  }
  const auto sheet = muffin::CssThemeParser::parse(QString::fromUtf8(input.readAll()), source.absolutePath(),
                                                   source.absoluteFilePath());
  const auto report = muffin::CssCompatibilityReport::analyze(sheet, source.absolutePath(), environment);
  const auto json = QJsonDocument(report.toJson()).toJson(QJsonDocument::Indented);
  if (parser.isSet(output)) {
    QString error;
    if (muffin::AtomicWrite::write(parser.value(output), json, &error) != muffin::AtomicWrite::Result::Written) {
      QTextStream(stderr) << "Cannot write report: " << error << '\n';
      return 2;
    }
  } else {
    QFile stream;
    if (!stream.open(stdout, QIODevice::WriteOnly) || stream.write(json) != json.size())
      return 2;
  }
  return 0; // Capability findings are data; I/O/usage errors alone fail the CLI.
}
