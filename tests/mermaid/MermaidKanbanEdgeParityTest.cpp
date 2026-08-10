#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/kanban/KanbanScene.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); std::exit(1);
}
void require(bool value, const QString& message) { if (!value) fail(message); }

std::shared_ptr<const kanban::KanbanScene> render(const QString& source,
                                                  editor::MermaidRenderEntry* out = nullptr) {
  editor::MermaidRenderCache cache;
  const auto entry = cache.getSync(cache.makeKey(source), source);
  if (out) *out = entry;
  if (entry.status != editor::MermaidRenderStatus::Ready || !entry.scene) return {};
  return std::dynamic_pointer_cast<const kanban::KanbanScene>(entry.scene);
}

const QJsonObject* findCase(const QVector<QJsonObject>& cases, const QString& id) {
  for (const QJsonObject& value : cases)
    if (value.value(QStringLiteral("id")).toString() == id) return &value;
  return nullptr;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Kanban config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("c8d570267ea8a421e9844584b4886493a0061434e7e21a0a4c75b8a150708c20"),
          QStringLiteral("Kanban config fixture provenance drifted"));
  QVector<QJsonObject> cases;
  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray())
    cases.append(value.toObject());

  for (const QJsonObject& fixture : cases) {
    editor::MermaidRenderEntry entry;
    const auto scene = render(fixture.value(QStringLiteral("source")).toString(), &entry);
    const bool expectError = fixture.value(QStringLiteral("status")).toString() ==
                             QLatin1String("error");
    require(expectError ? !scene : bool(scene),
            fixture.value(QStringLiteral("id")).toString() +
                QStringLiteral(": status parity"));
  }

  const auto baseline = render(findCase(cases, QStringLiteral("baseline"))->value(QStringLiteral("source")).toString());
  const auto zero = render(findCase(cases, QStringLiteral("section-width-zero"))->value(QStringLiteral("source")).toString());
  const auto width80 = render(findCase(cases, QStringLiteral("section-width-string"))->value(QStringLiteral("source")).toString());
  const auto widthRadix = render(findCase(cases, QStringLiteral("section-width-radix"))->value(QStringLiteral("source")).toString());
  const auto paddingRadix = render(findCase(cases, QStringLiteral("mindmap-padding-radix"))->value(QStringLiteral("source")).toString());
  require(baseline && zero && width80 && widthRadix && paddingRadix,
          QStringLiteral("Kanban width/padding cases"));
  require(zero->bounds == baseline->bounds, QStringLiteral("sectionWidth 0 uses || 200"));
  require(width80->sections.first().shapeBounds.width() == 80.0,
          QStringLiteral("string sectionWidth coerces numerically"));
  require(widthRadix->sections.first().shapeBounds.width() == 0.0 &&
              widthRadix->items.first().localBounds.width() == 65.0 &&
              widthRadix->sections.first().shapeBounds.x() == 40.0,
          QStringLiteral("radix sectionWidth separates JS layout from SVG used width"));
  require(paddingRadix->bounds ==
              baseline->contentBounds.adjusted(-20.0, -20.0, 20.0, 20.0),
          QStringLiteral("radix mindmap padding uses JS Number semantics"));

  const auto tcl0 = render(findCase(cases, QStringLiteral("tcl-zero"))->value(QStringLiteral("source")).toString());
  const auto tcl2 = render(findCase(cases, QStringLiteral("tcl-two"))->value(QStringLiteral("source")).toString());
  const auto tclFraction = render(findCase(cases, QStringLiteral("tcl-fraction"))->value(QStringLiteral("source")).toString());
  require(tcl0 && tcl2 && tclFraction, QStringLiteral("Kanban TCL cases"));
  require(tcl0->items.first().stroke == QLatin1String("none"),
          QStringLiteral("TCL zero removes generic node rule"));
  require(tcl2->sections.first().stroke == QLatin1String("none"),
          QStringLiteral("TCL two lacks section-1 rule"));
  require(tcl2->sections.first().fill == tcl2->style.textColor &&
              tcl2->sections.first().label.fill == tcl2->style.textColor,
          QStringLiteral("unmatched section rule inherits the SVG root fill"));
  require(tclFraction->sections.first().stroke != QLatin1String("none"),
          QStringLiteral("TCL 2.5 emits the third rule"));

  const auto darkFalse = render(findCase(cases, QStringLiteral("dark-mode-false"))->value(QStringLiteral("source")).toString());
  const auto darkTrue = render(findCase(cases, QStringLiteral("dark-mode-true"))->value(QStringLiteral("source")).toString());
  const auto darkString = render(findCase(cases, QStringLiteral("dark-mode-string-false"))->value(QStringLiteral("source")).toString());
  require(darkFalse && darkTrue && darkString, QStringLiteral("Kanban darkMode cases"));
  require(darkFalse->sections.first().fill != darkTrue->sections.first().fill &&
              darkString->sections.first().fill == darkTrue->sections.first().fill,
          QStringLiteral("darkMode uses JavaScript truthiness"));

  const auto hand = render(findCase(cases, QStringLiteral("look-hand-drawn"))->value(QStringLiteral("source")).toString());
  const auto neo = render(findCase(cases, QStringLiteral("look-neo"))->value(QStringLiteral("source")).toString());
  const auto neoShadowNone = render(findCase(cases, QStringLiteral("look-neo-drop-shadow-none"))->value(QStringLiteral("source")).toString());
  const auto neoShadowCustom = render(findCase(cases, QStringLiteral("look-neo-drop-shadow-custom"))->value(QStringLiteral("source")).toString());
  const auto neoShadowInvalid = render(findCase(cases, QStringLiteral("look-neo-drop-shadow-invalid"))->value(QStringLiteral("source")).toString());
  require(hand && hand->sections.first().handDrawn &&
              hand->sections.first().shapeBounds.height() > 550.0,
          QStringLiteral("handDrawn section retains 3W height"));
  require(neo && neo->sections.first().dropShadow && !neo->items.first().priorityVisible,
          QStringLiteral("neo applies to sections, not item metadata"));
  require(neoShadowNone && neoShadowCustom && neoShadowInvalid &&
              !neoShadowNone->sections.first().dropShadow &&
              !neoShadowCustom->sections.first().dropShadow &&
              !neoShadowInvalid->sections.first().dropShadow,
          QStringLiteral("explicit source dropShadow overrides have used value none"));

  const auto zeroFont = render(findCase(cases, QStringLiteral("theme-font-size-zero"))->value(QStringLiteral("source")).toString());
  require(zeroFont && zeroFont->style.fontSize == 0.0 &&
              zeroFont->sections.first().label.bounds.isEmpty() &&
              zeroFont->items.first().title.bounds.isEmpty() &&
              zeroFont->items.first().localBounds.height() == 20.0,
          QStringLiteral("font-size zero collapses labels without constructing a font"));

  const auto repeatedTicketPlaceholder = render(QStringLiteral(
      "%%{init: {\"kanban\": {\"ticketBaseUrl\": "
      "\"https://example.test/#TICKET#/#TICKET#\"}}}%%\n"
      "kanban\n"
      "  todo[Todo]\n"
      "    task1[Write docs]@{ ticket: KAN-7 }"));
  require(repeatedTicketPlaceholder &&
              repeatedTicketPlaceholder->items.first().href ==
                  QLatin1String("https://example.test/KAN-7/#TICKET#") &&
              repeatedTicketPlaceholder->items.first().ticket.document.underline,
          QStringLiteral("ticketBaseUrl replaces only the first placeholder"));
  return 0;
}
