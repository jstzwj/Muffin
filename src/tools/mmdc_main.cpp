// muffin-mmdc — a headless, native drop-in for the common `mmdc` workflow.
//
// Renders Mermaid source to SVG or PNG through the same pure C++/Qt engine the
// editor uses (mermaid 11.16 parity, no browser or JS runtime). The flag
// surface mirrors mermaid-cli's frequently used subset:
//
//   muffin-mmdc -i diagram.mmd [-o out.svg|out.png|-] [-t theme] [-s scale]
//                [-C config.json] [-b background] [--version]
//
// - input '-' (or an omitted -i) reads stdin; output '-' writes to stdout.
// - output format follows the file extension (.svg default; .png rasterizes).
// - -t and -C are injected as trailing %%{init}%% directives, so explicit
//   flags override both the config file and the source's own directive
//   (mmdc's precedence) — multiple inits merge with later values winning.
// - invalid sources still write the lightbulb error diagram (like a browser
//   page), print the structured diagnostic to stderr, and exit 1.

#include "mermaid/editor/MermaidRenderCache.h"

#include <QBuffer>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <cstdio>

#ifndef MUFFIN_VERSION
#define MUFFIN_VERSION "0.0.0"
#endif

namespace {

void printError(const QString& message) {
  std::fprintf(stderr, "muffin-mmdc: %s\n", qPrintable(message));
  std::fflush(stderr);
}

QString readStdin() {
  QByteArray bytes;
  char buffer[65536];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
    bytes.append(buffer, static_cast<qsizetype>(read));
  }
  return QString::fromUtf8(bytes);
}

QString readFile(const QString& path, bool* ok) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *ok = false;
    return file.errorString();
  }
  *ok = true;
  return QString::fromUtf8(file.readAll());
}

bool writeBytes(const QString& path, const QByteArray& bytes, QString* error) {
  if (path == QLatin1String("-")) {
    std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
    std::fflush(stdout);
    return true;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    *error = file.errorString();
    return false;
  }
  if (file.write(bytes) != bytes.size()) {
    *error = file.errorString();
    return false;
  }
  return true;
}

// Appends `config` as a trailing init directive: later inits win in the
// preprocessor's assignWithDepth merge, giving the CLI's explicit values the
// same precedence mmdc gives its flags over the source and config file.
QString withInitDirective(const QString& source, const QJsonObject& config) {
  if (config.isEmpty()) return source;
  return source + QStringLiteral("\n%%{init: %1}%%")
                     .arg(QString::fromUtf8(
                         QJsonDocument(config).toJson(QJsonDocument::Compact)));
}

void reportDiagnostic(const QJsonObject& diagnostic) {
  printError(QStringLiteral(
      "warning: source has errors (exported the error diagram)"));
  printError(QString::fromUtf8(
      QJsonDocument(diagnostic).toJson(QJsonDocument::Compact)));
}

}  // namespace

int main(int argc, char** argv) {
  // The renderer needs QGuiApplication (fonts, raster surfaces) but never a
  // window; offscreen keeps the binary usable over ssh/CI unless the caller
  // has explicitly chosen a platform.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("muffin-mmdc"));
  QCoreApplication::setApplicationVersion(QStringLiteral(MUFFIN_VERSION));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral(
      "Render Mermaid diagrams with the native Muffin engine (mermaid 11.16 parity)."));
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption inputOption(
      {QStringLiteral("i"), QStringLiteral("input")},
      QStringLiteral("Mermaid source file ('-' or omitted reads stdin)."),
      QStringLiteral("file"), QStringLiteral("-"));
  QCommandLineOption outputOption(
      {QStringLiteral("o"), QStringLiteral("output")},
      QStringLiteral(
          "Output file ('-' writes stdout). Extension picks the format: .svg (default) or .png."),
      QStringLiteral("file"));
  QCommandLineOption themeOption(
      {QStringLiteral("t"), QStringLiteral("theme")},
      QStringLiteral("Theme: default, neutral, dark, forest, base (overrides source/config)."),
      QStringLiteral("name"));
  QCommandLineOption scaleOption(
      {QStringLiteral("s"), QStringLiteral("scale")},
      QStringLiteral("PNG scale factor (default 1; 2 renders a crisp 2x image)."),
      QStringLiteral("factor"), QStringLiteral("1"));
  QCommandLineOption configOption(
      {QStringLiteral("C"), QStringLiteral("config")},
      QStringLiteral(
          "JSON config file applied like %%{init}%% (a {\"config\":{...}} wrapper is unwrapped)."),
      QStringLiteral("file"));
  QCommandLineOption backgroundOption(
      {QStringLiteral("b"), QStringLiteral("background")},
      QStringLiteral("PNG background color (name or #rrggbb; default transparent)."),
      QStringLiteral("color"));
  parser.addOptions({inputOption, outputOption, themeOption, scaleOption,
                     configOption, backgroundOption});
  parser.process(app);

  const QString inputPath = parser.value(inputOption);
  QString outputPath = parser.value(outputOption);

  QString source;
  if (inputPath == QLatin1String("-")) {
    source = readStdin();
  } else {
    bool ok = true;
    source = readFile(inputPath, &ok);
    if (!ok) {
      printError(QStringLiteral("cannot read %1: %2").arg(inputPath, source));
      return 1;
    }
  }

  // Default output: the input's basename with .svg (mermaid-cli's rule).
  if (outputPath.isEmpty()) {
    if (inputPath == QLatin1String("-")) {
      printError(QStringLiteral("-o is required when reading stdin"));
      return 1;
    }
    outputPath = QFileInfo(inputPath).absolutePath() + QLatin1Char('/') +
                 QFileInfo(inputPath).completeBaseName() + QStringLiteral(".svg");
  }

  // Precedence (later init wins): source's own directives < -C config < -t.
  if (parser.isSet(configOption)) {
    bool ok = true;
    const QString configText = readFile(parser.value(configOption), &ok);
    if (!ok) {
      printError(QStringLiteral("cannot read config %1: %2")
                     .arg(parser.value(configOption), configText));
      return 1;
    }
    const QJsonDocument document = QJsonDocument::fromJson(configText.toUtf8());
    if (!document.isObject()) {
      printError(QStringLiteral("config file must contain a JSON object"));
      return 1;
    }
    QJsonObject config = document.object();
    if (config.contains(QStringLiteral("config")) &&
        config.value(QStringLiteral("config")).isObject()) {
      config = config.value(QStringLiteral("config")).toObject();
    }
    source = withInitDirective(source, config);
  }
  if (parser.isSet(themeOption)) {
    source = withInitDirective(
        source,
        QJsonObject{{QStringLiteral("theme"), parser.value(themeOption)}});
  }

  const bool wantPng =
      outputPath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive);

  if (!wantPng) {
    const auto rendered =
        muffin::mermaid::editor::MermaidRenderCache::renderMermaidSourceToSvg(source);
    if (rendered.svg.isEmpty()) {
      printError(QStringLiteral("rendering produced no SVG"));
      return 1;
    }
    QString writeError;
    if (!writeBytes(outputPath, rendered.svg, &writeError)) {
      printError(QStringLiteral("cannot write %1: %2").arg(outputPath, writeError));
      return 1;
    }
    if (rendered.error) {
      reportDiagnostic(rendered.errorDiagnostic);
      return 1;
    }
    return 0;
  }

  const qreal scale = parser.value(scaleOption).toDouble();
  const auto rendered =
      muffin::mermaid::editor::MermaidRenderCache::renderMermaidSourceToPng(
          source, scale > 0.0 ? scale : 1.0);
  static const QLatin1String kPngPrefix("data:image/png;base64,");
  if (!rendered.dataUrl.startsWith(kPngPrefix)) {
    printError(QStringLiteral("rendering produced no PNG"));
    return 1;
  }
  QByteArray png = QByteArray::fromBase64(
      rendered.dataUrl.mid(kPngPrefix.size()).toLatin1());
  if (parser.isSet(backgroundOption)) {
    const QColor background(parser.value(backgroundOption));
    if (!background.isValid()) {
      printError(
          QStringLiteral("invalid background color '%1'").arg(parser.value(backgroundOption)));
      return 1;
    }
    QImage image;
    if (!image.loadFromData(png, "PNG")) {
      printError(QStringLiteral("failed to decode the rendered PNG for compositing"));
      return 1;
    }
    QImage flattened(image.size(), QImage::Format_ARGB32_Premultiplied);
    flattened.fill(background);
    QPainter painter(&flattened);
    painter.drawImage(0, 0, image);
    painter.end();
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    flattened.save(&buffer, "PNG");
  }
  QString writeError;
  if (!writeBytes(outputPath, png, &writeError)) {
    printError(QStringLiteral("cannot write %1: %2").arg(outputPath, writeError));
    return 1;
  }
  if (rendered.error) {
    reportDiagnostic(rendered.errorDiagnostic);
    return 1;
  }
  return 0;
}
