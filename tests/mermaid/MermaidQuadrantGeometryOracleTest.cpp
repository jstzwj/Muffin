// quadrantChart geometry + structure oracle: compares Muffin's native
// quadrant::QuadrantScene against real mermaid 11.16.0 captured in
// tests/fixtures/mermaid/quadrant-geometry.json. The layout is pure formula
// (font-independent): quadrant rects, point cx/cy/r, 6 borders, axis-label
// transforms, title — all asserted within 0.001. Point insertion is REVERSE
// source order; point fill is the upstream-invalid hsl(NaN) string.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/quadrant/QuadrantScene.h"
#include "mermaid/scene/ParityDiff.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& m) { std::fprintf(stderr, "%s\n", qPrintable(m)); std::fflush(stderr); std::exit(1); }
void require(bool c, const QString& m) { if (!c) fail(m); }

// Extract the (x, y) translate from a "translate(X, Y) rotate(R)" transform.
QPointF translateOf(const QString& transform) {
  QPointF p;
  QRegularExpressionMatch m = QRegularExpression(QStringLiteral("translate\\(([-0-9.]+),\\s*([-0-9.]+)\\)")).match(transform);
  if (m.hasMatch()) { p.setX(m.captured(1).toDouble()); p.setY(m.captured(2).toDouble()); }
  return p;
}
// The oracle stores SVG attributes as strings ("263"); the native side emits
// numbers. Read either as a double.
double toD(const QJsonValue& v) {
  if (v.isDouble()) return v.toDouble();
  bool ok = false;
  const double n = v.toString().toDouble(&ok);
  return ok ? n : 0.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected quadrant geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Quadrant geometry: mermaidVersion drifted"));
  const parity::Tier num{0.001};

  for (const QJsonValue& cv : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject c = cv.toObject();
    const QString id = c.value(QStringLiteral("id")).toString();
    const QString source = c.value(QStringLiteral("source")).toString();
    const QJsonObject expected = c.value(QStringLiteral("expected")).toObject();

    editor::MermaidRenderCache cache;
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* scene = dynamic_cast<const quadrant::QuadrantScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && scene != nullptr,
            id + QStringLiteral(": native quadrant render failed: ") + entry.errorMessage);
    const QJsonObject actual = scene->toJsonObject();

    QStringList errors;
    // Quadrant rects + label text + center.
    const QJsonArray eq = expected.value(QStringLiteral("quadrants")).toArray();
    const QJsonArray aq = actual.value(QStringLiteral("quadrants")).toArray();
    if (aq.size() != eq.size()) errors << id + ": quadrant count mismatch";
    else for (int i = 0; i < eq.size(); ++i) {
      const QJsonObject e = eq[i].toObject().value(QStringLiteral("rect")).toObject();
      const QJsonObject er = eq[i].toObject();
      const QJsonObject a = aq[i].toObject().value(QStringLiteral("rect")).toObject();
      const QString pfx = id + QStringLiteral("/q%1").arg(i);
      if (a.value(QStringLiteral("fill")).toString() != e.value(QStringLiteral("fill")).toString()) errors << pfx + "/fill";
      if (aq[i].toObject().value(QStringLiteral("text")).toString() != er.value(QStringLiteral("text")).toString()) errors << pfx + "/text";
      const QPointF ec = translateOf(er.value(QStringLiteral("transform")).toString());
      errors += parity::compareNumber(a.value(QStringLiteral("x")).toDouble(), toD(e.value(QStringLiteral("x"))), num, pfx + "/x");
      errors += parity::compareNumber(a.value(QStringLiteral("y")).toDouble(), toD(e.value(QStringLiteral("y"))), num, pfx + "/y");
      errors += parity::compareNumber(a.value(QStringLiteral("width")).toDouble(), toD(e.value(QStringLiteral("width"))), num, pfx + "/w");
      errors += parity::compareNumber(a.value(QStringLiteral("height")).toDouble(), toD(e.value(QStringLiteral("height"))), num, pfx + "/h");
      errors += parity::compareNumber(translateOf(aq[i].toObject().value(QStringLiteral("transform")).toString()).x(), ec.x(), num, pfx + "/textX");
      errors += parity::compareNumber(translateOf(aq[i].toObject().value(QStringLiteral("transform")).toString()).y(), ec.y(), num, pfx + "/textY");
    }
    // Points (cx/cy/r/fill/text), reverse order.
    const QJsonArray ep = expected.value(QStringLiteral("points")).toArray();
    const QJsonArray ap = actual.value(QStringLiteral("points")).toArray();
    if (ap.size() != ep.size()) errors << id + ": point count mismatch";
    else for (int i = 0; i < ep.size(); ++i) {
      const QJsonObject e = ep[i].toObject(), a = ap[i].toObject();
      const QString pfx = id + QStringLiteral("/p%1").arg(i);
      if (a.value(QStringLiteral("text")).toString() != e.value(QStringLiteral("text")).toString()) errors << pfx + "/text";
      if (a.value(QStringLiteral("fill")).toString() != e.value(QStringLiteral("fill")).toString()) errors << pfx + "/fill";
      if (a.value(QStringLiteral("stroke")).toString() != e.value(QStringLiteral("stroke")).toString()) errors << pfx + "/stroke";
      errors += parity::compareNumber(a.value(QStringLiteral("cx")).toDouble(), e.value(QStringLiteral("cx")).toDouble(), num, pfx + "/cx");
      errors += parity::compareNumber(a.value(QStringLiteral("cy")).toDouble(), e.value(QStringLiteral("cy")).toDouble(), num, pfx + "/cy");
      errors += parity::compareNumber(a.value(QStringLiteral("r")).toDouble(), e.value(QStringLiteral("r")).toDouble(), num, pfx + "/r");
      errors += parity::compareNumber(a.value(QStringLiteral("strokeWidth")).toDouble(), e.value(QStringLiteral("strokeWidth")).toDouble(), num, pfx + "/strokeWidth");
    }
    // Borders.
    const QJsonArray eb = expected.value(QStringLiteral("borders")).toArray();
    const QJsonArray ab = actual.value(QStringLiteral("borders")).toArray();
    if (ab.size() != eb.size()) errors << id + ": border count mismatch";
    else for (int i = 0; i < eb.size(); ++i) {
      const QJsonObject e = eb[i].toObject(), a = ab[i].toObject();
      const QString pfx = id + QStringLiteral("/b%1").arg(i);
      errors += parity::compareNumber(a.value(QStringLiteral("x1")).toDouble(), e.value(QStringLiteral("x1")).toDouble(), num, pfx + "/x1");
      errors += parity::compareNumber(a.value(QStringLiteral("y1")).toDouble(), e.value(QStringLiteral("y1")).toDouble(), num, pfx + "/y1");
      errors += parity::compareNumber(a.value(QStringLiteral("x2")).toDouble(), e.value(QStringLiteral("x2")).toDouble(), num, pfx + "/x2");
      errors += parity::compareNumber(a.value(QStringLiteral("y2")).toDouble(), e.value(QStringLiteral("y2")).toDouble(), num, pfx + "/y2");
    }
    // Axis labels (text + position + rotation).
    const QJsonArray ea = expected.value(QStringLiteral("axisLabels")).toArray();
    const QJsonArray aa = actual.value(QStringLiteral("axisLabels")).toArray();
    if (aa.size() != ea.size()) errors << id + ": axisLabel count mismatch";
    else for (int i = 0; i < ea.size(); ++i) {
      const QJsonObject e = ea[i].toObject(), a = aa[i].toObject();
      const QString pfx = id + QStringLiteral("/a%1").arg(i);
      if (a.value(QStringLiteral("text")).toString() != e.value(QStringLiteral("text")).toString()) errors << pfx + "/text";
      const QPointF ep2 = translateOf(e.value(QStringLiteral("transform")).toString());
      const QPointF ap2 = translateOf(a.value(QStringLiteral("transform")).toString());
      errors += parity::compareNumber(ap2.x(), ep2.x(), num, pfx + "/x");
      errors += parity::compareNumber(ap2.y(), ep2.y(), num, pfx + "/y");
    }
    // Title.
    const QString et = expected.value(QStringLiteral("title")).isNull() ? QString() : expected.value(QStringLiteral("title")).toString();
    const QString at = actual.value(QStringLiteral("title")).isNull() ? QString() : actual.value(QStringLiteral("title")).toString();
    if (et != at) errors << id + ": title mismatch";

    if (!errors.isEmpty()) { for (const QString& e : errors) std::fprintf(stderr, "%s\n", qPrintable(e)); fail(id + ": quadrant geometry regression"); }
  }
  return 0;
}
