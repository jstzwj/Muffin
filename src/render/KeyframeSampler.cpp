#include "render/KeyframeSampler.h"

#include "theme/CssThemeMapper.h"

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtMath>

namespace muffin {
namespace KeyframeSampler {

namespace {

// Stop values are pre-resolved at map time, so no var table is needed here.
const QHash<QString, QString> kNoVars;

qreal lerp(qreal a, qreal b, qreal t) { return a + (b - a) * t; }

QColor lerpColor(const QColor& a, const QColor& b, qreal t) {
  return QColor(qBound(0, qRound(lerp(a.red(), b.red(), t)), 255),
                qBound(0, qRound(lerp(a.green(), b.green(), t)), 255),
                qBound(0, qRound(lerp(a.blue(), b.blue(), t)), 255),
                qBound(0, qRound(lerp(a.alpha(), b.alpha(), t)), 255));
}

qreal opacityOf(const QString& raw) {
  bool ok = false;
  const qreal v = raw.trimmed().toDouble(&ok);
  return ok ? qBound(qreal(0.0), v, qreal(1.0)) : 1.0;
}

// Parse the scale factor out of `transform: scale(N)` / `scale(Nx, Ny)`.
qreal scaleOf(const QString& raw) {
  static const QRegularExpression re(QStringLiteral("scale\\(\\s*([0-9.]+)"), QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(raw.trimmed());
  if (!m.hasMatch()) { return 1.0; }
  bool ok = false;
  const qreal v = m.captured(1).toDouble(&ok);
  return ok ? v : 1.0;
}

// box-shadow blur: the 3rd length token (offsetX offsetY blur). 0 if none.
qreal shadowBlurOf(const QString& raw) {
  const QStringList parts = raw.split(QRegularExpression(QStringLiteral("\\s+")));
  QVector<qreal> nums;
  for (const QString& p : parts) {
    const QString t = p.trimmed();
    if (t.isEmpty()) { continue; }
    if (!(t.at(0).isDigit() || t.at(0) == QLatin1Char('+') || t.at(0) == QLatin1Char('-') || t.at(0) == QLatin1Char('.'))) {
      continue;
    }
    nums.append(CssThemeMapper::resolveLengthPx(t, kNoVars));
  }
  if (nums.size() >= 3) { return nums.at(2); }
  if (nums.size() >= 2) { return 8.0; }
  return 0.0;
}

}  // namespace

AnimatedSample sampleAtPhase(const KeyframesDef& kf, qreal phase) {
  AnimatedSample s;
  if (kf.stops.empty()) { return s; }
  phase = qBound(qreal(0.0), phase, qreal(1.0));
  // Surrounding stops: lo = last stop at/before phase, hi = first stop at/after.
  const KeyframeStop* lo = &kf.stops.front();
  const KeyframeStop* hi = &kf.stops.back();
  for (const auto& stop : kf.stops) {
    if (stop.position <= phase) { lo = &stop; }
  }
  for (const auto& stop : kf.stops) {
    if (stop.position >= phase) { hi = &stop; break; }
  }
  const qreal span = hi->position - lo->position;
  const qreal t = span > 0.0 ? (phase - lo->position) / span : 0.0;

  const bool loOpacity = lo->declarations.contains(QStringLiteral("opacity"));
  const bool hiOpacity = hi->declarations.contains(QStringLiteral("opacity"));
  if (loOpacity || hiOpacity) {
    const qreal a = loOpacity ? opacityOf(lo->declarations.value(QStringLiteral("opacity"))) : 1.0;
    const qreal b = hiOpacity ? opacityOf(hi->declarations.value(QStringLiteral("opacity"))) : 1.0;
    s.hasOpacity = true;
    s.opacity = lerp(a, b, t);
  }

  const QString kShadow = QStringLiteral("box-shadow");
  const bool loShadow = lo->declarations.contains(kShadow);
  const bool hiShadow = hi->declarations.contains(kShadow);
  if (loShadow || hiShadow) {
    QColor ca = loShadow ? CssThemeMapper::resolveColor(lo->declarations.value(kShadow), kNoVars) : QColor();
    QColor cb = hiShadow ? CssThemeMapper::resolveColor(hi->declarations.value(kShadow), kNoVars) : QColor();
    if (!ca.isValid()) { ca = cb; }
    if (!cb.isValid()) { cb = ca; }
    if (ca.isValid()) {
      const qreal ba = loShadow ? shadowBlurOf(lo->declarations.value(kShadow)) : 0.0;
      const qreal bb = hiShadow ? shadowBlurOf(hi->declarations.value(kShadow)) : 0.0;
      s.hasGlow = true;
      s.glowColor = lerpColor(ca, cb, t);
      s.glowBlur = lerp(ba, bb, t);
    }
  }

  const QString kTransform = QStringLiteral("transform");
  const bool loScale = lo->declarations.contains(kTransform);
  const bool hiScale = hi->declarations.contains(kTransform);
  if (loScale || hiScale) {
    const qreal a = loScale ? scaleOf(lo->declarations.value(kTransform)) : 1.0;
    const qreal b = hiScale ? scaleOf(hi->declarations.value(kTransform)) : 1.0;
    if (qAbs(a - 1.0) > 0.001 || qAbs(b - 1.0) > 0.001) {
      s.hasScale = true;
      s.scale = lerp(a, b, t);
    }
  }

  return s;
}

}  // namespace KeyframeSampler
}  // namespace muffin
