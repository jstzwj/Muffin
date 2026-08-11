#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool value, const QString& message) { if (!value) fail(message); }
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Swimlane grammar fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("634c396f9b4f05b9f653ab42dfc2970e65b7ed4a2475c500ad8c15fe574b3cdc"),
          QStringLiteral("Swimlane grammar fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("7b0d07e8d9d7281841acd24fc3b51d095e113a22208a80177e0f914d50a8f9a8"),
          QStringLiteral("Swimlane grammar provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 17, QStringLiteral("Swimlane grammar case count"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool accepted = fixture.value(QStringLiteral("expected")).toObject()
                              .value(QStringLiteral("accepted")).toBool();
    editor::MermaidRenderCache cache;
    const editor::MermaidRenderEntry entry = cache.getSync(cache.makeKey(source), source);
    require((entry.status == editor::MermaidRenderStatus::Ready) == accepted,
            id + QStringLiteral("/source-entry status=%1 expected=%2 error=%3")
                     .arg(static_cast<int>(entry.status)).arg(accepted)
                     .arg(entry.errorMessage));
    if (accepted)
      require(detectDiagramType(source) == QLatin1String("swimlane"),
              id + QStringLiteral("/detector"));
  }
  std::puts("MermaidSwimlaneParserTest: 17/17 passed");
  return 0;
}
