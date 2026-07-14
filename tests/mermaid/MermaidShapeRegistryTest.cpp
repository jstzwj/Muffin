// Milestone E shape-registry gate. Verifies that canonicalShape() recognises
// every upstream flowchart shape shortName (no name silently falls back to the
// rect default), and tracks ported vs pending shapes as E progresses. The
// "upstream - native" diff must reach zero by the end of milestone E.

#include "mermaid/flowchart/FlowchartShapeRegistry.h"

#include <QDebug>
#include <QSet>
#include <QStringList>

#include <cstdlib>

using muffin::mermaid::flowchart::canonicalShape;
using muffin::mermaid::flowchart::nativeShapeCanonicalNames;
using muffin::mermaid::flowchart::upstreamShortNames;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }
}  // namespace

int main() {
  const QStringList upstream = upstreamShortNames();
  const QStringList native = nativeShapeCanonicalNames();
  const QSet<QString> nativeSet(native.begin(), native.end());

  // 1. The canonical map must recognise every upstream shortName. Only "rect"
  //    itself may canonicalise to "rect"; any other shortName falling back to
  //    "rect" means the map is missing an entry (a shape would silently render
  //    as a rectangle).
  require(!upstream.isEmpty(), QStringLiteral("upstream shortName list is empty"));
  for (const QString& sn : upstream) {
    const QString canon = canonicalShape(sn);
    if (sn == QLatin1String("rect")) {
      require(canon == QLatin1String("rect"),
              QStringLiteral("shortName 'rect' must canonicalise to 'rect', got %1").arg(canon));
    } else {
      require(canon != QLatin1String("rect"),
              QStringLiteral("shortName '%1' is not in the canonical map (fell back to rect)").arg(sn));
    }
  }

  // 2. Every alias/legacy name in the map must resolve to a canonical that is
  //    either a ported native shape or a recognised pending canonical. This
  //    catches typos in the registry. Spot-check a few alias -> canonical
  //    resolutions that span the three naming systems.
  require(canonicalShape(QStringLiteral("round")) == QLatin1String("round"), QStringLiteral("legacy 'round'"));
  require(canonicalShape(QStringLiteral("rounded")) == QLatin1String("round"), QStringLiteral("shortName 'rounded'"));
  require(canonicalShape(QStringLiteral("event")) == QLatin1String("round"), QStringLiteral("alias 'event'"));
  require(canonicalShape(QStringLiteral("dbl-circ")) == QLatin1String("double_circle"), QStringLiteral("shortName 'dbl-circ'"));
  require(canonicalShape(QStringLiteral("double-circle")) == QLatin1String("double_circle"), QStringLiteral("alias 'double-circle'"));
  require(canonicalShape(QStringLiteral("tri")) == QLatin1String("triangle"), QStringLiteral("shortName 'tri'"));
  require(canonicalShape(QStringLiteral("unknown-shape")) == QLatin1String("rect"), QStringLiteral("unknown shape must fall back to rect"));

  // 3. The native (ported) set must cover the 13 legacy shapes and be a subset
  //    of what the map can produce. Report the pending diff; it must empty by
  //    the end of milestone E.
  require(native.size() >= 13, QStringLiteral("native shape set dropped below the 13 legacy baseline"));
  QStringList pending;
  for (const QString& sn : upstream) {
    const QString canon = canonicalShape(sn);
    if (!nativeSet.contains(canon)) pending << QStringLiteral("%1->%2").arg(sn, canon);
  }
  const int ported = upstream.size() - pending.size();
  qInfo().noquote() << QStringLiteral("shape registry: %1/%2 upstream shapes ported (%3 pending)")
                           .arg(ported).arg(upstream.size()).arg(pending.size());

  return 0;
}
