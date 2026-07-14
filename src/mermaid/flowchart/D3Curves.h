#pragma once

// 1:1 C++ port of the d3-shape curve state machines that mermaid's v3 flow
// renderer selects via `flowchart.curve` (see insertEdge in
// node_modules/mermaid/dist/chunks/mermaid.esm/chunk-4F4KDU6L.mjs, the
// `switch (edgeCurveType)` that maps the config string to a d3 factory).
//
// Each curve is ported verbatim from node_modules/d3-shape/src/curve/*.js:
// the same `_point` state machine, the same shift registers, the same
// coincident-point guard, the same `_line` closePath condition. The sink is a
// d3.path()-compatible PathContext that emits M/L/C/Z commands. mermaid
// produces its edge `d` attribute via `d3.line().x(x).y(y).curve(factory)
// (points)`, so the driver here is `lineStart(); point(x,y)*; lineEnd();` —
// exactly what d3.line invokes (no areaStart/areaEnd, so `_line` stays
// undefined/NaN and closePath is never emitted for multi-point edges).
//
// Number formatting matches mermaid's rendered SVG after the fixture
// generator's 3-decimal rounding; the geometry tests compare numbers by value
// (0.002 tolerance), so the textual format is not load-bearing.

#include <QPointF>
#include <QString>
#include <QVector>

#include <cmath>
#include <limits>

namespace muffin::mermaid::flowchart::d3curve {

// d3.path()-compatible accumulator. Emits `Mx,y`, `Lx,y`, `Cx1,y1,x2,y2,x,y`,
// `Z` — the same command/coordinate layout d3.path produces, which the geometry
// tests' structure check expects.
struct PathContext {
  QString out;

  void moveTo(qreal x, qreal y) { out += QLatin1Char('M'); writeNumber(x); out += QLatin1Char(','); writeNumber(y); }
  void lineTo(qreal x, qreal y) { out += QLatin1Char('L'); writeNumber(x); out += QLatin1Char(','); writeNumber(y); }
  void bezierCurveTo(qreal c1x, qreal c1y, qreal c2x, qreal c2y, qreal x, qreal y) {
    out += QLatin1Char('C'); writeNumber(c1x); out += QLatin1Char(','); writeNumber(c1y);
    out += QLatin1Char(','); writeNumber(c2x); out += QLatin1Char(','); writeNumber(c2y);
    out += QLatin1Char(','); writeNumber(x); out += QLatin1Char(','); writeNumber(y);
  }
  void closePath() { out += QLatin1Char('Z'); }

private:
  void writeNumber(qreal v) {
    if (std::abs(v) < 0.0005) v = 0.0;
    QString s = QString::number(v, 'f', 3);
    int i = s.size() - 1;
    while (i >= 0 && s[i] == QLatin1Char('0')) --i;
    if (i >= 0 && s[i] == QLatin1Char('.')) --i;
    out += s.mid(0, i + 1);
  }
};

namespace detail {

// d3 leaves curve._line undefined across a single line() call (the constructor
// and lineStart never assign it, and d3.line never calls areaStart/areaEnd).
// undefined is falsy and `!== 0`, so the closePath condition reduces to
// `_point === 1`. We model undefined as NaN: NaN is falsy, `NaN != 0` is true,
// and `NaN >= 0` is false — matching JS exactly for every branch these curves
// use. `1 - NaN` stays NaN, so the flag never mutates for our single-call use.
inline constexpr qreal kLineUndefined() {
  return std::numeric_limits<qreal>::quiet_NaN();
}
inline bool lineTruthy(qreal l) { return l != 0.0 && !std::isnan(l); }  // JS `if (this._line)`
inline bool lineNotEq0(qreal l) { return l != 0.0; }                    // JS `this._line !== 0`

// JS `h0 || (h1 < 0 && -0)`: divisor is h0 when h0 is truthy (nonzero),
// otherwise -0 when the sibling span is negative, else +0. -0 vs +0 fixes the
// sign of the resulting Infinity, matching d3's slope math exactly.
inline qreal slopeDiv(qreal h, qreal sibling) {
  return (h != 0.0) ? h : ((sibling < 0.0) ? -0.0 : 0.0);
}

// --- curveLinear (linear.js) ---
struct Linear {
  PathContext& c;
  int _point = 0;
  qreal _line = kLineUndefined();
  explicit Linear(PathContext& ctx) : c(ctx) {}
  void lineStart() { _point = 0; }
  void lineEnd() {
    if (lineTruthy(_line) || (lineNotEq0(_line) && _point == 1)) c.closePath();
    _line = 1 - _line;
  }
  void point(qreal x, qreal y) {
    switch (_point) {
      case 0: _point = 1; if (lineTruthy(_line)) c.lineTo(x, y); else c.moveTo(x, y); break;
      case 1: _point = 2; [[fallthrough]];
      default: c.lineTo(x, y); break;
    }
  }
};

// --- curveBasis (basis.js) ---
struct Basis {
  PathContext& c;
  qreal _x0 = kLineUndefined(), _x1 = kLineUndefined();
  qreal _y0 = kLineUndefined(), _y1 = kLineUndefined();
  int _point = 0;
  qreal _line = kLineUndefined();
  explicit Basis(PathContext& ctx) : c(ctx) {}
  void bezier(qreal x, qreal y) {
    c.bezierCurveTo(
      (2 * _x0 + _x1) / 3, (2 * _y0 + _y1) / 3,
      (_x0 + 2 * _x1) / 3, (_y0 + 2 * _y1) / 3,
      (_x0 + 4 * _x1 + x) / 6, (_y0 + 4 * _y1 + y) / 6);
  }
  void lineStart() { _x0 = _x1 = _y0 = _y1 = kLineUndefined(); _point = 0; }
  void lineEnd() {
    switch (_point) {
      case 3: bezier(_x1, _y1); [[fallthrough]];
      case 2: c.lineTo(_x1, _y1); break;
    }
    if (lineTruthy(_line) || (lineNotEq0(_line) && _point == 1)) c.closePath();
    _line = 1 - _line;
  }
  void point(qreal x, qreal y) {
    switch (_point) {
      case 0: _point = 1; if (lineTruthy(_line)) c.lineTo(x, y); else c.moveTo(x, y); break;
      case 1: _point = 2; break;
      case 2: _point = 3; c.lineTo((5 * _x0 + _x1) / 6, (5 * _y0 + _y1) / 6); [[fallthrough]];
      default: bezier(x, y); break;
    }
    _x0 = _x1; _x1 = x;
    _y0 = _y1; _y1 = y;
  }
};

// --- curveStep / stepBefore / stepAfter (step.js) ---
struct Step {
  PathContext& c;
  qreal _t;
  qreal _x = kLineUndefined(), _y = kLineUndefined();
  int _point = 0;
  qreal _line = kLineUndefined();
  Step(PathContext& ctx, qreal t) : c(ctx), _t(t) {}
  void lineStart() { _x = _y = kLineUndefined(); _point = 0; }
  void lineEnd() {
    if (_t > 0 && _t < 1 && _point == 2) c.lineTo(_x, _y);
    if (lineTruthy(_line) || (lineNotEq0(_line) && _point == 1)) c.closePath();
    if (_line >= 0) { _t = 1 - _t; _line = 1 - _line; }
  }
  void point(qreal x, qreal y) {
    switch (_point) {
      case 0: _point = 1; if (lineTruthy(_line)) c.lineTo(x, y); else c.moveTo(x, y); break;
      case 1: _point = 2; [[fallthrough]];
      default: {
        if (_t <= 0) {
          c.lineTo(_x, y);
          c.lineTo(x, y);
        } else {
          qreal x1 = _x * (1 - _t) + x * _t;
          c.lineTo(x1, _y);
          c.lineTo(x1, y);
        }
        break;
      }
    }
    _x = x; _y = y;
  }
};

// --- curveCardinal (cardinal.js), default tension 0 -> _k = 1/6 ---
struct Cardinal {
  PathContext& c;
  qreal _k;
  qreal _x0 = kLineUndefined(), _x1 = kLineUndefined(), _x2 = kLineUndefined();
  qreal _y0 = kLineUndefined(), _y1 = kLineUndefined(), _y2 = kLineUndefined();
  int _point = 0;
  qreal _line = kLineUndefined();
  Cardinal(PathContext& ctx, qreal tension) : c(ctx), _k((1 - tension) / 6) {}
  void bezier(qreal x, qreal y) {
    c.bezierCurveTo(
      _x1 + _k * (_x2 - _x0), _y1 + _k * (_y2 - _y0),
      _x2 + _k * (_x1 - x), _y2 + _k * (_y1 - y),
      _x2, _y2);
  }
  void lineStart() {
    _x0 = _x1 = _x2 = _y0 = _y1 = _y2 = kLineUndefined(); _point = 0;
  }
  void lineEnd() {
    switch (_point) {
      case 2: c.lineTo(_x2, _y2); break;
      case 3: bezier(_x1, _y1); break;
    }
    if (lineTruthy(_line) || (lineNotEq0(_line) && _point == 1)) c.closePath();
    _line = 1 - _line;
  }
  void point(qreal x, qreal y) {
    switch (_point) {
      case 0: _point = 1; if (lineTruthy(_line)) c.lineTo(x, y); else c.moveTo(x, y); break;
      case 1: _point = 2; _x1 = x; _y1 = y; break;
      case 2: _point = 3; [[fallthrough]];
      default: bezier(x, y); break;
    }
    _x0 = _x1; _x1 = _x2; _x2 = x;
    _y0 = _y1; _y1 = _y2; _y2 = y;
  }
};

// --- curveMonotoneX / curveMonotoneY (monotone.js) ---
// Steffen (1990) monotonic interpolation expressed as cubic Beziers. Templated
// on the context type so monotoneY can drive it through a ReflectContext that
// swaps x/y (d3's MonotoneY = MonotoneX with a ReflectContext, calling
// point(y, x)).
template <typename Ctx>
struct MonotoneX {
  Ctx& c;
  qreal _x0 = kLineUndefined(), _x1 = kLineUndefined();
  qreal _y0 = kLineUndefined(), _y1 = kLineUndefined();
  qreal _t0 = kLineUndefined();
  int _point = 0;
  qreal _line = kLineUndefined();
  explicit MonotoneX(Ctx& ctx) : c(ctx) {}

  static qreal sign(qreal x) { return x < 0 ? -1.0 : 1.0; }

  qreal slope3(qreal x2, qreal y2) const {
    qreal h0 = _x1 - _x0;
    qreal h1 = x2 - _x1;
    qreal s0 = (_y1 - _y0) / slopeDiv(h0, h1);
    qreal s1 = (y2 - _y1) / slopeDiv(h1, h0);
    qreal p = (s0 * h1 + s1 * h0) / (h0 + h1);
    qreal v = (sign(s0) + sign(s1)) * std::min(std::abs(s0), std::min(std::abs(s1), 0.5 * std::abs(p)));
    return (v != 0.0 && !std::isnan(v)) ? v : 0.0;  // `|| 0`
  }
  qreal slope2(qreal t) const {
    qreal h = _x1 - _x0;
    return h != 0.0 ? (3 * (_y1 - _y0) / h - t) / 2 : t;  // `h ? ... : t`
  }
  void bezier(qreal t0, qreal t1) {
    qreal x0 = _x0, y0 = _y0, x1 = _x1, y1 = _y1;
    qreal dx = (x1 - x0) / 3;
    c.bezierCurveTo(x0 + dx, y0 + dx * t0, x1 - dx, y1 - dx * t1, x1, y1);
  }
  void lineStart() { _x0 = _x1 = _y0 = _y1 = _t0 = kLineUndefined(); _point = 0; }
  void lineEnd() {
    switch (_point) {
      case 2: c.lineTo(_x1, _y1); break;
      case 3: bezier(_t0, slope2(_t0)); break;
    }
    if (lineTruthy(_line) || (lineNotEq0(_line) && _point == 1)) c.closePath();
    _line = 1 - _line;
  }
  void point(qreal x, qreal y) {
    qreal t1 = kLineUndefined();
    if (x == _x1 && y == _y1) return;  // ignore coincident points
    switch (_point) {
      case 0: _point = 1; if (lineTruthy(_line)) c.lineTo(x, y); else c.moveTo(x, y); break;
      case 1: _point = 2; break;
      case 2: _point = 3; t1 = slope3(x, y); bezier(slope2(t1), t1); break;
      default: t1 = slope3(x, y); bezier(_t0, t1); break;
    }
    _x0 = _x1; _x1 = x;
    _y0 = _y1; _y1 = y;
    _t0 = t1;
  }
};

// ReflectContext: swaps x/y so MonotoneX driven with (y,x) yields monotoneY.
struct ReflectContext {
  PathContext& c;
  explicit ReflectContext(PathContext& ctx) : c(ctx) {}
  void moveTo(qreal x, qreal y) { c.moveTo(y, x); }
  void lineTo(qreal x, qreal y) { c.lineTo(y, x); }
  void bezierCurveTo(qreal x1, qreal y1, qreal x2, qreal y2, qreal x, qreal y) {
    c.bezierCurveTo(y1, x1, y2, x2, y, x);
  }
  void closePath() { c.closePath(); }
};

struct MonotoneY {
  ReflectContext rc;
  MonotoneX<ReflectContext> mx;
  explicit MonotoneY(PathContext& ctx) : rc(ctx), mx(rc) {}
  void lineStart() { mx.lineStart(); }
  void lineEnd() { mx.lineEnd(); }
  void point(qreal x, qreal y) { mx.point(y, x); }  // d3: MonotoneX.point.call(this, y, x)
};

// --- curveBumpX / curveBumpY (bump.js) ---
struct Bump {
  PathContext& c;
  bool _x;  // true = bumpX
  qreal _x0 = kLineUndefined(), _y0 = kLineUndefined();
  int _point = 0;
  qreal _line = kLineUndefined();
  Bump(PathContext& ctx, bool x) : c(ctx), _x(x) {}
  void lineStart() { _point = 0; }
  void lineEnd() {
    if (lineTruthy(_line) || (lineNotEq0(_line) && _point == 1)) c.closePath();
    _line = 1 - _line;
  }
  void point(qreal x, qreal y) {
    switch (_point) {
      case 0: _point = 1; if (lineTruthy(_line)) c.lineTo(x, y); else c.moveTo(x, y); break;
      case 1: _point = 2; [[fallthrough]];
      default: {
        if (_x) {
          qreal mid = (_x0 + x) / 2;  // d3 assigns this._x0 mid-expression; both
          c.bezierCurveTo(mid, _y0, mid, y, x, y);  // control x use the midpoint,
        } else {                                    // then _x0 is overwritten below.
          qreal mid = (_y0 + y) / 2;
          c.bezierCurveTo(_x0, mid, x, mid, x, y);
        }
        break;
      }
    }
    _x0 = x; _y0 = y;
  }
};

// --- curveNatural (natural.js) ---
// Buffers all points, then solves a tridiagonal system for control points.
struct Natural {
  PathContext& c;
  QVector<qreal> _x, _y;
  qreal _line = kLineUndefined();
  explicit Natural(PathContext& ctx) : c(ctx) {}
  static void controlPoints(const QVector<qreal>& x, QVector<qreal>& a, QVector<qreal>& b) {
    int n = x.size() - 1;
    a.resize(n); b.resize(n);
    if (n <= 0) return;
    QVector<qreal> r(n);
    a[0] = 0; b[0] = 2; r[0] = x[0] + 2 * x[1];
    for (int i = 1; i < n - 1; ++i) { a[i] = 1; b[i] = 4; r[i] = 4 * x[i] + 2 * x[i + 1]; }
    a[n - 1] = 2; b[n - 1] = 7; r[n - 1] = 8 * x[n - 1] + x[n];
    for (int i = 1; i < n; ++i) { qreal m = a[i] / b[i - 1]; b[i] -= m; r[i] -= m * r[i - 1]; }
    a[n - 1] = r[n - 1] / b[n - 1];
    for (int i = n - 2; i >= 0; --i) a[i] = (r[i] - a[i + 1]) / b[i];
    b[n - 1] = (x[n] + a[n - 1]) / 2;
    for (int i = 0; i < n - 1; ++i) b[i] = 2 * x[i + 1] - a[i + 1];
  }
  void lineStart() { _x.clear(); _y.clear(); }
  void lineEnd() {
    const int n = _x.size();
    if (n) {
      if (lineTruthy(_line)) c.lineTo(_x[0], _y[0]); else c.moveTo(_x[0], _y[0]);
      if (n == 2) {
        c.lineTo(_x[1], _y[1]);
      } else if (n > 2) {
        QVector<qreal> px0, px1, py0, py1;
        controlPoints(_x, px0, px1);
        controlPoints(_y, py0, py1);
        for (int i0 = 0, i1 = 1; i1 < n; ++i0, ++i1) {
          c.bezierCurveTo(px0[i0], py0[i0], px1[i0], py1[i0], _x[i1], _y[i1]);
        }
      }
    }
    if (lineTruthy(_line) || (lineNotEq0(_line) && n == 1)) c.closePath();
    _line = 1 - _line;
    _x.clear(); _y.clear();
  }
  void point(qreal x, qreal y) { _x.push_back(x); _y.push_back(y); }
};

// --- curveCatmullRom (catmullRom.js), default alpha 0.5 ---
// alpha == 0 would delegate to Cardinal(0); the config string "catmullRom"
// resolves to catmullRom_default = custom(0.5), so alpha is 0.5.
struct CatmullRom {
  PathContext& c;
  qreal _alpha;
  qreal _x0 = kLineUndefined(), _x1 = kLineUndefined(), _x2 = kLineUndefined();
  qreal _y0 = kLineUndefined(), _y1 = kLineUndefined(), _y2 = kLineUndefined();
  qreal _l01_a = 0, _l12_a = 0, _l23_a = 0;
  qreal _l01_2a = 0, _l12_2a = 0, _l23_2a = 0;
  int _point = 0;
  qreal _line = kLineUndefined();
  static constexpr qreal kEpsilon = 1e-6;
  CatmullRom(PathContext& ctx, qreal alpha) : c(ctx), _alpha(alpha) {}
  void bezier(qreal x, qreal y) {
    qreal x1 = _x1, y1 = _y1, x2 = _x2, y2 = _y2;
    if (_l01_a > kEpsilon) {
      qreal a = 2 * _l01_2a + 3 * _l01_a * _l12_a + _l12_2a;
      qreal n = 3 * _l01_a * (_l01_a + _l12_a);
      x1 = (x1 * a - _x0 * _l12_2a + _x2 * _l01_2a) / n;
      y1 = (y1 * a - _y0 * _l12_2a + _y2 * _l01_2a) / n;
    }
    if (_l23_a > kEpsilon) {
      qreal b = 2 * _l23_2a + 3 * _l23_a * _l12_a + _l12_2a;
      qreal m = 3 * _l23_a * (_l23_a + _l12_a);
      x2 = (x2 * b + _x1 * _l23_2a - x * _l12_2a) / m;
      y2 = (y2 * b + _y1 * _l23_2a - y * _l12_2a) / m;
    }
    c.bezierCurveTo(x1, y1, x2, y2, _x2, _y2);
  }
  void lineStart() {
    _x0 = _x1 = _x2 = _y0 = _y1 = _y2 = kLineUndefined();
    _l01_a = _l12_a = _l23_a = _l01_2a = _l12_2a = _l23_2a = 0; _point = 0;
  }
  void lineEnd() {
    switch (_point) {
      case 2: c.lineTo(_x2, _y2); break;
        // d3 calls this.point(this._x2, this._y2) — the METHOD, not the helper.
        // Re-entering point() zeroes _l23_a (x23 = _x2 - _x2 = 0) before the
        // helper runs, so the x2/y2 control is NOT adjusted on the final segment.
      case 3: point(_x2, _y2); break;
    }
    if (lineTruthy(_line) || (lineNotEq0(_line) && _point == 1)) c.closePath();
    _line = 1 - _line;
  }
  void point(qreal x, qreal y) {
    if (_point) {
      qreal x23 = _x2 - x, y23 = _y2 - y;
      _l23_a = std::sqrt(_l23_2a = std::pow(x23 * x23 + y23 * y23, _alpha));
    }
    switch (_point) {
      case 0: _point = 1; if (lineTruthy(_line)) c.lineTo(x, y); else c.moveTo(x, y); break;
      case 1: _point = 2; break;
      case 2: _point = 3; [[fallthrough]];
      default: bezier(x, y); break;
    }
    _l01_a = _l12_a; _l12_a = _l23_a;
    _l01_2a = _l12_2a; _l12_2a = _l23_2a;
    _x0 = _x1; _x1 = _x2; _x2 = x;
    _y0 = _y1; _y1 = _y2; _y2 = y;
  }
};

}  // namespace detail

// Drive a curve the way d3.line does: lineStart, point per data point, lineEnd.
template <typename Curve>
inline void drive(Curve& curve, const QVector<QPointF>& points) {
  curve.lineStart();
  for (const QPointF& p : points) curve.point(p.x(), p.y());
  curve.lineEnd();
}

// mermaid's insertEdge switch (edgeCurveType), verbatim mapping. "rounded" uses
// a custom generator (generateRoundedPath) and is NOT a d3 curve — it falls
// through to the default (basis) here; porting it is deferred. Unknown values
// also default to basis, matching mermaid's `default:` branch.
inline QString pathForCurve(const QVector<QPointF>& points, const QString& name) {
  PathContext c;
  if (name == QLatin1String("linear")) { detail::Linear cur(c); drive(cur, points); }
  else if (name == QLatin1String("basis")) { detail::Basis cur(c); drive(cur, points); }
  else if (name == QLatin1String("step")) { detail::Step cur(c, 0.5); drive(cur, points); }
  else if (name == QLatin1String("stepBefore")) { detail::Step cur(c, 0.0); drive(cur, points); }
  else if (name == QLatin1String("stepAfter")) { detail::Step cur(c, 1.0); drive(cur, points); }
  else if (name == QLatin1String("cardinal")) { detail::Cardinal cur(c, 0.0); drive(cur, points); }
  else if (name == QLatin1String("monotoneX")) { detail::MonotoneX<PathContext> cur(c); drive(cur, points); }
  else if (name == QLatin1String("monotoneY")) { detail::MonotoneY cur(c); drive(cur, points); }
  else if (name == QLatin1String("bumpX")) { detail::Bump cur(c, true); drive(cur, points); }
  else if (name == QLatin1String("bumpY")) { detail::Bump cur(c, false); drive(cur, points); }
  else if (name == QLatin1String("catmullRom")) { detail::CatmullRom cur(c, 0.5); drive(cur, points); }
  else if (name == QLatin1String("natural")) { detail::Natural cur(c); drive(cur, points); }
  else { detail::Basis cur(c); drive(cur, points); }  // default / rounded / unknown -> basis
  return c.out;
}

}  // namespace muffin::mermaid::flowchart::d3curve
