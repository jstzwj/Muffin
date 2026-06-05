#include "math/MathSvgGeometry.h"

#include <QFile>
#include <QPainterPath>
#include <QRegularExpression>
#include <QtMath>

static void initKatexFontsResource() {
  Q_INIT_RESOURCE(katex_fonts);
}

namespace muffin::math {
namespace {

QHash<QString, QString>& paths() {
  static QHash<QString, QString> value;
  return value;
}

bool& didLoad() {
  static bool value = false;
  return value;
}

QVector<QString> pathTokens(const QString& path) {
  QVector<QString> tokens;
  static const QRegularExpression tokenRe(QStringLiteral("([A-Za-z])|([-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?)"));
  QRegularExpressionMatchIterator it = tokenRe.globalMatch(path);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    tokens.push_back(match.captured(0));
  }
  return tokens;
}

bool isCommandToken(const QString& token) {
  return token.size() == 1 && token.at(0).isLetter();
}

qreal nextNumber(const QVector<QString>& tokens, int& index) {
  if (index >= tokens.size()) {
    return 0.0;
  }
  return tokens.at(index++).toDouble();
}

QPainterPath parseSvgPath(const QString& source) {
  const QVector<QString> tokens = pathTokens(source);
  QPainterPath path;
  QChar command;
  QPointF current;
  QPointF subpathStart;
  QPointF lastCubicControl;
  QPointF lastQuadControl;
  bool hasLastCubic = false;
  bool hasLastQuad = false;
  int i = 0;
  while (i < tokens.size()) {
    if (isCommandToken(tokens.at(i))) {
      command = tokens.at(i++).at(0);
    }
    if (command.isNull()) {
      break;
    }

    const bool relative = command.isLower();
    const QChar c = command.toUpper();
    const auto point = [&](qreal x, qreal y) {
      QPointF p(x, y);
      return relative ? current + p : p;
    };
    const auto hasNumber = [&]() {
      return i < tokens.size() && !isCommandToken(tokens.at(i));
    };

    if (c == QLatin1Char('M')) {
      bool first = true;
      while (hasNumber() && i + 1 < tokens.size()) {
        const QPointF p = point(nextNumber(tokens, i), nextNumber(tokens, i));
        if (first) {
          path.moveTo(p);
          subpathStart = p;
          first = false;
        } else {
          path.lineTo(p);
        }
        current = p;
      }
    } else if (c == QLatin1Char('L')) {
      while (hasNumber() && i + 1 < tokens.size()) {
        current = point(nextNumber(tokens, i), nextNumber(tokens, i));
        path.lineTo(current);
      }
    } else if (c == QLatin1Char('H')) {
      while (hasNumber()) {
        const qreal x = nextNumber(tokens, i);
        current.setX(relative ? current.x() + x : x);
        path.lineTo(current);
      }
    } else if (c == QLatin1Char('V')) {
      while (hasNumber()) {
        const qreal y = nextNumber(tokens, i);
        current.setY(relative ? current.y() + y : y);
        path.lineTo(current);
      }
    } else if (c == QLatin1Char('C')) {
      while (hasNumber() && i + 5 < tokens.size()) {
        const QPointF c1 = point(nextNumber(tokens, i), nextNumber(tokens, i));
        const QPointF c2 = point(nextNumber(tokens, i), nextNumber(tokens, i));
        const QPointF p = point(nextNumber(tokens, i), nextNumber(tokens, i));
        path.cubicTo(c1, c2, p);
        current = p;
        lastCubicControl = c2;
        hasLastCubic = true;
        hasLastQuad = false;
      }
    } else if (c == QLatin1Char('S')) {
      while (hasNumber() && i + 3 < tokens.size()) {
        const QPointF c1 = hasLastCubic ? current * 2.0 - lastCubicControl : current;
        const QPointF c2 = point(nextNumber(tokens, i), nextNumber(tokens, i));
        const QPointF p = point(nextNumber(tokens, i), nextNumber(tokens, i));
        path.cubicTo(c1, c2, p);
        current = p;
        lastCubicControl = c2;
        hasLastCubic = true;
        hasLastQuad = false;
      }
    } else if (c == QLatin1Char('Q')) {
      while (hasNumber() && i + 3 < tokens.size()) {
        const QPointF q = point(nextNumber(tokens, i), nextNumber(tokens, i));
        const QPointF p = point(nextNumber(tokens, i), nextNumber(tokens, i));
        path.quadTo(q, p);
        current = p;
        lastQuadControl = q;
        hasLastQuad = true;
        hasLastCubic = false;
      }
    } else if (c == QLatin1Char('T')) {
      while (hasNumber() && i + 1 < tokens.size()) {
        const QPointF q = hasLastQuad ? current * 2.0 - lastQuadControl : current;
        const QPointF p = point(nextNumber(tokens, i), nextNumber(tokens, i));
        path.quadTo(q, p);
        current = p;
        lastQuadControl = q;
        hasLastQuad = true;
        hasLastCubic = false;
      }
    } else if (c == QLatin1Char('Z')) {
      path.closeSubpath();
      current = subpathStart;
      hasLastCubic = false;
      hasLastQuad = false;
    } else {
      break;
    }
    if (c != QLatin1Char('C') && c != QLatin1Char('S')) {
      hasLastCubic = false;
    }
    if (c != QLatin1Char('Q') && c != QLatin1Char('T')) {
      hasLastQuad = false;
    }
  }
  return path;
}

}  // namespace

void MathSvgGeometry::ensureLoaded() {
  if (didLoad()) {
    return;
  }
  initKatexFontsResource();
  didLoad() = true;

  QFile file(QStringLiteral(":/katex/src/svgGeometry.ts"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  const QString source = QString::fromUtf8(file.readAll());
  static const QRegularExpression pathRe(QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)\\s*:\\s*`([^`]*)`"));
  QRegularExpressionMatchIterator it = pathRe.globalMatch(source);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    paths().insert(match.captured(1), match.captured(2));
  }
}

bool MathSvgGeometry::hasPath(const QString& name) {
  ensureLoaded();
  return paths().contains(name);
}

QString MathSvgGeometry::path(const QString& name) {
  ensureLoaded();
  return paths().value(name);
}

QPainterPath MathSvgGeometry::painterPath(const QString& name, QRectF target) {
  ensureLoaded();
  return painterPathFromSvgPath(paths().value(name), {}, target);
}

QPainterPath MathSvgGeometry::painterPathFromSvgPath(const QString& svgPath, QRectF viewBox, QRectF target) {
  QPainterPath parsed = parseSvgPath(svgPath);
  const QRectF source = viewBox.isEmpty() ? parsed.boundingRect() : viewBox;
  if (parsed.isEmpty() || source.isEmpty() || target.isEmpty()) {
    return {};
  }
  QTransform transform;
  transform.translate(target.left(), target.top());
  transform.scale(target.width() / source.width(), target.height() / source.height());
  transform.translate(-source.left(), -source.top());
  return transform.map(parsed);
}

QString MathSvgGeometry::innerPath(const QString& name, int height) {
  const auto doubleBrushStroke = [](const QString& svgPath) {
    return svgPath + QLatin1Char(' ') + svgPath;
  };
  if (name == QStringLiteral("\u239c")) return doubleBrushStroke(QStringLiteral("M291 0 H417 V%1 H291z").arg(height));
  if (name == QStringLiteral("\u2223")) return doubleBrushStroke(QStringLiteral("M145 0 H188 V%1 H145z").arg(height));
  if (name == QStringLiteral("\u2225")) {
    return doubleBrushStroke(QStringLiteral("M145 0 H188 V%1 H145z").arg(height)) +
           doubleBrushStroke(QStringLiteral("M367 0 H410 V%1 H367z").arg(height));
  }
  if (name == QStringLiteral("\u239f")) return doubleBrushStroke(QStringLiteral("M457 0 H583 V%1 H457z").arg(height));
  if (name == QStringLiteral("\u23a2")) return doubleBrushStroke(QStringLiteral("M319 0 H403 V%1 H319z").arg(height));
  if (name == QStringLiteral("\u23a5")) return doubleBrushStroke(QStringLiteral("M263 0 H347 V%1 H263z").arg(height));
  if (name == QStringLiteral("\u23aa")) return doubleBrushStroke(QStringLiteral("M384 0 H504 V%1 H384z").arg(height));
  if (name == QStringLiteral("\u23d0")) return doubleBrushStroke(QStringLiteral("M312 0 H355 V%1 H312z").arg(height));
  if (name == QStringLiteral("\u2016")) {
    return doubleBrushStroke(QStringLiteral("M257 0 H300 V%1 H257z").arg(height)) +
           doubleBrushStroke(QStringLiteral("M478 0 H521 V%1 H478z").arg(height));
  }
  return {};
}

QString MathSvgGeometry::tallDelimiterPath(const QString& label, int midHeight) {
  if (label == QStringLiteral("lbrack")) {
    return QStringLiteral("M403 1759 V84 H666 V0 H319 V1759 v%1 v1759 v84 h347 v-84 H403z M403 1759 V0 H319 V1759 v%1 v1759 v84 h84z")
        .arg(midHeight);
  }
  if (label == QStringLiteral("rbrack")) {
    return QStringLiteral("M347 1759 V0 H0 V84 H263 V1759 v%1 v1759 H0 v84 H347z M347 1759 V0 H263 V1759 v%1 v1759 h84z")
        .arg(midHeight);
  }
  if (label == QStringLiteral("vert")) {
    return QStringLiteral("M145 15 v585 v%1 v585 c2.667,10,9.667,15,21,15 c10,0,16.667,-5,20,-15 v-585 v%2 v-585 c-2.667,-10,-9.667,-15,-21,-15 c-10,0,-16.667,5,-20,15z M188 15 H145 v585 v%1 v585 h43z")
        .arg(midHeight)
        .arg(-midHeight);
  }
  if (label == QStringLiteral("doublevert")) {
    return QStringLiteral("M145 15 v585 v%1 v585 c2.667,10,9.667,15,21,15 c10,0,16.667,-5,20,-15 v-585 v%2 v-585 c-2.667,-10,-9.667,-15,-21,-15 c-10,0,-16.667,5,-20,15z M188 15 H145 v585 v%1 v585 h43z M367 15 v585 v%1 v585 c2.667,10,9.667,15,21,15 c10,0,16.667,-5,20,-15 v-585 v%2 v-585 c-2.667,-10,-9.667,-15,-21,-15 c-10,0,-16.667,5,-20,15z M410 15 H367 v585 v%1 v585 h43z")
        .arg(midHeight)
        .arg(-midHeight);
  }
  if (label == QStringLiteral("lfloor")) {
    return QStringLiteral("M319 602 V0 H403 V602 v%1 v1715 h263 v84 H319z M319 602 V0 H403 V602 v%1 v1715 H319z").arg(midHeight);
  }
  if (label == QStringLiteral("rfloor")) {
    return QStringLiteral("M319 602 V0 H403 V602 v%1 v1799 H0 v-84 H319z M319 602 V0 H403 V602 v%1 v1715 H319z").arg(midHeight);
  }
  if (label == QStringLiteral("lceil")) {
    return QStringLiteral("M403 1759 V84 H666 V0 H319 V1759 v%1 v602 h84z M403 1759 V0 H319 V1759 v%1 v602 h84z").arg(midHeight);
  }
  if (label == QStringLiteral("rceil")) {
    return QStringLiteral("M347 1759 V0 H0 V84 H263 V1759 v%1 v602 h84z M347 1759 V0 h-84 V1759 v%1 v602 h84z").arg(midHeight);
  }
  if (label == QStringLiteral("lparen")) {
    return QStringLiteral("M863,9c0,-2,-2,-5,-6,-9c0,0,-17,0,-17,0c-12.7,0,-19.3,0.3,-20,1 c-5.3,5.3,-10.3,11,-15,17c-242.7,294.7,-395.3,682,-458,1162c-21.3,163.3,-33.3,349, -36,557 l0,%1c0.2,6,0,26,0,60c2,159.3,10,310.7,24,454c53.3,528,210, 949.7,470,1265c4.7,6,9.7,11.7,15,17c0.7,0.7,7,1,19,1c0,0,18,0,18,0c4,-4,6,-7,6,-9 c0,-2.7,-3.3,-8.7,-10,-18c-135.3,-192.7,-235.5,-414.3,-300.5,-665c-65,-250.7,-102.5, -544.7,-112.5,-882c-2,-104,-3,-167,-3,-189 l0,%2c0,-162.7,5.7,-314,17,-454c20.7,-272,63.7,-513,129,-723c65.3, -210,155.3,-396.3,270,-559c6.7,-9.3,10,-15.3,10,-18z")
        .arg(midHeight + 84)
        .arg(-(midHeight + 92));
  }
  if (label == QStringLiteral("rparen")) {
    return QStringLiteral("M76,0c-16.7,0,-25,3,-25,9c0,2,2,6.3,6,13c21.3,28.7,42.3,60.3, 63,95c96.7,156.7,172.8,332.5,228.5,527.5c55.7,195,92.8,416.5,111.5,664.5 c11.3,139.3,17,290.7,17,454c0,28,1.7,43,3.3,45l0,%1 c-3,4,-3.3,16.7,-3.3,38c0,162,-5.7,313.7,-17,455c-18.7,248,-55.8,469.3,-111.5,664 c-55.7,194.7,-131.8,370.3,-228.5,527c-20.7,34.7,-41.7,66.3,-63,95c-2,3.3,-4,7,-6,11 c0,7.3,5.7,11,17,11c0,0,11,0,11,0c9.3,0,14.3,-0.3,15,-1c5.3,-5.3,10.3,-11,15,-17 c242.7,-294.7,395.3,-681.7,458,-1161c21.3,-164.7,33.3,-350.7,36,-558 l0,%2c-2,-159.3,-10,-310.7,-24,-454c-53.3,-528,-210,-949.7, -470,-1265c-4.7,-6,-9.7,-11.7,-15,-17c-0.7,-0.7,-6.7,-1,-18,-1z")
        .arg(midHeight + 9)
        .arg(-(midHeight + 144));
  }
  return {};
}

QString MathSvgGeometry::sqrtPath(const QString& size, qreal extraVinculum, int viewBoxHeight) {
  const qreal ev = 1000.0 * extraVinculum;
  const int hLinePad = 80;
  if (size == QStringLiteral("sqrtMain")) {
    return QStringLiteral("M95,%1 c-2.7,0,-7.17,-2.7,-13.5,-8c-5.8,-5.3,-9.5,-10,-9.5,-14 c0,-2,0.3,-3.3,1,-4c1.3,-2.7,23.83,-20.7,67.5,-54 c44.2,-33.3,65.8,-50.3,66.5,-51c1.3,-1.3,3,-2,5,-2c4.7,0,8.7,3.3,12,10 s173,378,173,378c0.7,0,35.3,-71,104,-213c68.7,-142,137.5,-285,206.5,-429 c69,-144,104.5,-217.7,106.5,-221 l%2 -%3 c5.3,-9.3,12,-14,20,-14 H400000v%4H845.2724 s-225.272,467,-225.272,467s-235,486,-235,486c-2.7,4.7,-9,7,-19,7 c-6,0,-10,-1,-12,-3s-194,-422,-194,-422s-65,47,-65,47z M%5 %6h400000v%4h-400000z")
        .arg(622 + ev + hLinePad)
        .arg(ev / 2.075)
        .arg(ev)
        .arg(40 + ev)
        .arg(834 + ev)
        .arg(hLinePad);
  }
  if (size == QStringLiteral("sqrtSize1")) {
    return QStringLiteral("M263,%1c0.7,0,18,39.7,52,119 c34,79.3,68.167,158.7,102.5,238c34.3,79.3,51.8,119.3,52.5,120 c340,-704.7,510.7,-1060.3,512,-1067 l%2 -%3 c4.7,-7.3,11,-11,19,-11 H40000v%4H1012.3 s-271.3,567,-271.3,567c-38.7,80.7,-84,175,-136,283c-52,108,-89.167,185.3,-111.5,232 c-22.3,46.7,-33.8,70.3,-34.5,71c-4.7,4.7,-12.3,7,-23,7s-12,-1,-12,-1 s-109,-253,-109,-253c-72.7,-168,-109.3,-252,-110,-252c-10.7,8,-22,16.7,-34,26 c-22,17.3,-33.3,26,-34,26s-26,-26,-26,-26s76,-59,76,-59s76,-60,76,-60z M%5 %6h400000v%4h-400000z")
        .arg(601 + ev + hLinePad)
        .arg(ev / 2.084)
        .arg(ev)
        .arg(40 + ev)
        .arg(1001 + ev)
        .arg(hLinePad);
  }
  if (size == QStringLiteral("sqrtSize2")) {
    return QStringLiteral("M983 %1 l%2 -%3 c4,-6.7,10,-10,18,-10 H400000v%4 H1013.1s-83.4,268,-264.1,840c-180.7,572,-277,876.3,-289,913c-4.7,4.7,-12.7,7,-24,7 s-12,0,-12,0c-1.3,-3.3,-3.7,-11.7,-7,-25c-35.3,-125.3,-106.7,-373.3,-214,-744 c-10,12,-21,25,-33,39s-32,39,-32,39c-6,-5.3,-15,-14,-27,-26s25,-30,25,-30 c26.7,-32.7,52,-63,76,-91s52,-60,52,-60s208,722,208,722 c56,-175.3,126.3,-397.3,211,-666c84.7,-268.7,153.8,-488.2,207.5,-658.5 c53.7,-170.3,84.5,-266.8,92.5,-289.5z M%5 %6h400000v%4h-400000z")
        .arg(10 + ev + hLinePad)
        .arg(ev / 3.13)
        .arg(ev)
        .arg(40 + ev)
        .arg(1001 + ev)
        .arg(hLinePad);
  }
  if (size == QStringLiteral("sqrtSize3")) {
    return QStringLiteral("M424,%1 c-1.3,-0.7,-38.5,-172,-111.5,-514c-73,-342,-109.8,-513.3,-110.5,-514 c0,-2,-10.7,14.3,-32,49c-4.7,7.3,-9.8,15.7,-15.5,25c-5.7,9.3,-9.8,16,-12.5,20 s-5,7,-5,7c-4,-3.3,-8.3,-7.7,-13,-13s-13,-13,-13,-13s76,-122,76,-122s77,-121,77,-121 s209,968,209,968c0,-2,84.7,-361.7,254,-1079c169.3,-717.3,254.7,-1077.7,256,-1081 l%2 -%3c4,-6.7,10,-10,18,-10 H400000 v%4H1014.6 s-87.3,378.7,-272.6,1166c-185.3,787.3,-279.3,1182.3,-282,1185 c-2,6,-10,9,-24,9 c-8,0,-12,-0.7,-12,-2z M%5 %6 h400000v%4h-400000z")
        .arg(2398 + ev + hLinePad)
        .arg(ev / 4.223)
        .arg(ev)
        .arg(40 + ev)
        .arg(1001 + ev)
        .arg(hLinePad);
  }
  if (size == QStringLiteral("sqrtSize4")) {
    return QStringLiteral("M473,%1 c339.3,-1799.3,509.3,-2700,510,-2702 l%2 -%3 c3.3,-7.3,9.3,-11,18,-11 H400000v%4H1017.7 s-90.5,478,-276.2,1466c-185.7,988,-279.5,1483,-281.5,1485c-2,6,-10,9,-24,9 c-8,0,-12,-0.7,-12,-2c0,-1.3,-5.3,-32,-16,-92c-50.7,-293.3,-119.7,-693.3,-207,-1200 c0,-1.3,-5.3,8.7,-16,30c-10.7,21.3,-21.3,42.7,-32,64s-16,33,-16,33s-26,-26,-26,-26 s76,-153,76,-153s77,-151,77,-151c0.7,0.7,35.7,202,105,604c67.3,400.7,102,602.7,104,606zM%5 %6h400000v%4H1017.7z")
        .arg(2713 + ev + hLinePad)
        .arg(ev / 5.298)
        .arg(ev)
        .arg(40 + ev)
        .arg(1001 + ev)
        .arg(hLinePad);
  }
  const int vertSegment = viewBoxHeight - 54 - hLinePad - static_cast<int>(ev);
  return QStringLiteral("M702 %1H400000%2 H742v%3l-4 4-4 4c-.667.7 -2 1.5-4 2.5s-4.167 1.833-6.5 2.5-5.5 1-9.5 1 h-12l-28-84c-16.667-52-96.667 -294.333-240-727l-212 -643 -85 170 c-4-3.333-8.333-7.667-13 -13l-13-13l77-155 77-156c66 199.333 139 419.667 219 661 l218 661zM702 %4H400000v%2H742z")
      .arg(ev + hLinePad)
      .arg(40 + ev)
      .arg(vertSegment)
      .arg(hLinePad);
}

bool MathSvgGeometry::loaded() {
  ensureLoaded();
  return !paths().isEmpty();
}

}  // namespace muffin::math
