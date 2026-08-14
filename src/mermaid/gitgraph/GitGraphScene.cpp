#include "mermaid/gitgraph/GitGraphScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/text/ChromiumTextMetrics.h"

#include <QFontMetricsF>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <optional>

namespace muffin::mermaid::gitgraph {
namespace {

constexpr qreal kLayoutOffset = 10.0;
constexpr qreal kCommitStep = 40.0;
constexpr qreal kDefaultPos = 30.0;
constexpr int kNamedColors = 8;

QString number(qreal value) { return QString::number(value, 'g', 17); }

bool isRedux(const QString& theme) {
  return theme == QLatin1String("redux") || theme == QLatin1String("redux-dark") ||
         theme == QLatin1String("redux-color") || theme == QLatin1String("redux-dark-color");
}
bool isColorTheme(const QString& theme) {
  return theme == QLatin1String("redux-color") || theme == QLatin1String("redux-dark-color");
}
bool isPureNeo(const QString& theme) {
  return theme == QLatin1String("neo") || theme == QLatin1String("neo-dark");
}
bool isDarkTheme(const QString& theme) {
  return theme == QLatin1String("dark") || theme == QLatin1String("neo-dark") ||
         theme == QLatin1String("redux-dark") ||
         theme == QLatin1String("redux-dark-color");
}
bool isNeoTheme(const QString& theme) {
  return theme == QLatin1String("neo") || theme == QLatin1String("neo-dark") || isRedux(theme);
}

editor::CssPixelFont font(const GitGraphSceneStyle& style, qreal size, bool bold = false) {
  editor::CssPixelFont result = editor::makeUnhintedCssPixelFont(style.fontFamily, size);
  if (bold) result.font.setWeight(QFont::DemiBold);
  return result;
}

// A measurement font with an explicit family/size/weight chain, so themeCSS
// font declarations can move getBBox-driven layout. The metric weight falls
// back to Normal when the physical face left the requested family — Chrome
// faux-bolds the @font-face Regular while DirectWrite substitutes a real
// bold face with different metrics.
struct TextFont {
  QString family;
  qreal size = 16.0;
  QFont::Weight weight = QFont::Normal;
  // The CSS requested a bold face. Chrome faux-bolds the single @font-face
  // Regular (ink outset ~1px per side at 16px) while the metric weight below
  // may fall back to Normal because the physical face left the family — the
  // flag keeps the synthetic-bold ink fringe in the measurement.
  bool fauxBold = false;
  // The bold weight came from a themeCSS declaration rather than the base
  // theme (redux). Both paint faux-bold in Chrome, but the themeCSS geometry
  // oracle runs at sizes where the right-side outset crosses the canvas
  // integer while the tuned redux goldens do not, so only cssBold widens the
  // measured label box on the right.
  bool cssBold = false;
};

TextFont styleFont(const GitGraphSceneStyle& style, qreal size, bool bold) {
  return TextFont{style.fontFamily, size,
                  bold ? QFont::DemiBold : QFont::Normal, bold, false};
}

TextFont cssTextFont(const GitGraphSceneStyle& style,
                     const GitGraphElementCss& css, qreal baseSize,
                     bool baseBold) {
  TextFont out = styleFont(style, baseSize, baseBold);
  if (!css.fontFamily.trimmed().isEmpty()) out.family = css.fontFamily;
  if (css.fontSize >= 0.0) out.size = css.fontSize;
  if (!css.fontWeight.trimmed().isEmpty()) {
    out.weight = editor::cssFontWeightToQt(QJsonValue(css.fontWeight),
                                           QFont::Normal);
    out.cssBold = out.weight != QFont::Normal;
  }
  out.fauxBold = out.weight != QFont::Normal || baseBold;
  out.weight = editor::faceAwareMetricWeight(out.family, out.weight);
  return out;
}

editor::CssPixelFont font(const TextFont& spec) {
  editor::CssPixelFont result =
      editor::makeUnhintedCssPixelFont(spec.family, spec.size);
  result.font.setWeight(spec.weight);
  return result;
}

qreal textAdvance(const TextFont& spec, const QString& text) {
  const editor::CssPixelFont css = font(spec);
  qreal width = css.horizontalAdvance(text);
  if (const auto hb = textmetrics::harfBuzzAdvance(text, spec.family, spec.size))
    width = std::ceil(*hb * 64.0) / 64.0;
  return width;
}

QRectF directTextBounds(const TextFont& spec, const QString& text,
                        bool bold = false, bool expandBoldRight = true) {
  const editor::CssPixelFont css = font(spec);
  const QRectF ink = QFontMetricsF(css.font).boundingRect(text);
  qreal inkLeft = ink.left() * css.scale;
  qreal inkRight = ink.right() * css.scale;
  if (const auto hb = textmetrics::harfBuzzInkBounds(
          text, spec.family, spec.size)) {
    inkLeft = hb->left();
    inkRight = hb->right();
  }
  // Skia's SVG glyph bounds retain a one-pixel antialiasing fringe when the
  // first outline starts almost exactly on the text origin (notably Noto's
  // lowercase f and v). HarfBuzz reports design-unit ink bounds, so add the
  // same fringe before using the box for getBBox-driven layout. The cutoff
  // is a constant subpixel cell, not a size fraction: Chrome's fringe stays
  // while the ink left sits below ~17/64 px (Noto 'f', xMin 15/1000 em,
  // keeps it through 17px and drops it from 18px; 'v', xMin 0, keeps it at
  // every size; 'd' at xMin 55 never has it).
  const qreal skiaLeft = inkLeft >= 0.0 && inkLeft < 17.0 / 64.0 ? -1.0 : inkLeft;
  qreal left = std::min(0.0, skiaLeft);
  qreal right = std::max(textAdvance(spec, text), inkRight);
  if (bold) {
    left -= 1.0;
    if (expandBoldRight) right += 1.0;
  }
  return QRectF(left, -std::ceil(1.05 * spec.size), right - left,
                std::ceil(1.35 * spec.size));
}

QStringList branchTextLines(const QString& text) {
  static const QRegularExpression separator(
      QStringLiteral("\\n|<br\\s*/?>"),
      QRegularExpression::CaseInsensitiveOption);
  QStringList lines = text.split(separator, Qt::KeepEmptyParts);
  for (QString& line : lines) line = line.trimmed();
  if (lines.isEmpty()) lines.push_back(QString());
  return lines;
}

QRectF branchTextBounds(const TextFont& spec, const QStringList& lines) {
  const bool bold = spec.fauxBold || spec.weight != QFont::Normal;
  qreal left = 0.0;
  qreal right = 0.0;
  for (const QString& line : lines) {
    const QRectF horizontal = directTextBounds(spec, line, bold);
    left = std::min(left, horizontal.left());
    // Blink's SVG text getBBox retains one extra LayoutUnit-scale ink fringe
    // when the label is measured inside the branch wrapper. Qt's unhinted
    // glyph box omits it; 10/64px keeps layout and CSS viewport rounding in
    // the same subpixel cell without changing HarfBuzz advances.
    right = std::max(right, horizontal.right() + 10.0 / 64.0);
  }
  return QRectF(left, -1.0, right - left,
                std::ceil(spec.size * 1.35) +
                    spec.size * std::max<qsizetype>(0, lines.size() - 1));
}

void unite(QRectF& target, bool& has, const QRectF& value) {
  const QRectF normalized = value.normalized();
  if (!std::isfinite(normalized.left()) || !std::isfinite(normalized.top()) ||
      !std::isfinite(normalized.right()) || !std::isfinite(normalized.bottom()))
    return;
  if (!has) {
    target = normalized;
    has = true;
    return;
  }
  const qreal left = std::min(target.left(), normalized.left());
  const qreal top = std::min(target.top(), normalized.top());
  const qreal right = std::max(target.right(), normalized.right());
  const qreal bottom = std::max(target.bottom(), normalized.bottom());
  target = QRectF(left, top, right - left, bottom - top);
}

QString colorAt(const QVector<QString>& values, int index, const QString& fallback) {
  if (values.isEmpty()) return fallback;
  const QString value = values.at((index % values.size() + values.size()) % values.size());
  return value.isEmpty() ? fallback : value;
}

QString commitColor(const GitGraphSceneStyle& style, int classIndex) {
  if (isPureNeo(style.themeName))
    return classIndex == 0 ? style.nodeBorder
                           : colorAt(style.gitColors, classIndex, style.nodeBorder);
  if (isRedux(style.themeName) && !isColorTheme(style.themeName))
    return style.nodeBorder;
  if (isColorTheme(style.themeName))
    return classIndex == 0
               ? style.nodeBorder
               : colorAt(style.borderColors, classIndex, style.nodeBorder);
  return colorAt(style.gitColors, classIndex, style.nodeBorder);
}

QString branchLabelColor(const GitGraphSceneStyle& style, int classIndex) {
  if (isRedux(style.themeName) ||
      (isPureNeo(style.themeName) && classIndex == 0))
    return style.nodeBorder;
  return colorAt(style.branchLabelColors, classIndex, style.textColor);
}

QRectF rotatedBounds(const QRectF& rect, qreal angle, const QPointF& origin) {
  if (angle == 0.0) return rect;
  QTransform transform;
  transform.translate(origin.x(), origin.y());
  transform.rotate(angle);
  transform.translate(-origin.x(), -origin.y());
  return transform.mapRect(rect);
}

QRectF transformedBounds(const QRectF& rect, const QPointF& translation,
                         qreal angle = 0.0,
                         const QPointF& origin = QPointF()) {
  return rotatedBounds(rect, angle, origin).translated(translation);
}

struct Layout {
  QHash<QString, QPointF> branchPos;
  QHash<QString, int> branchIndex;
  QHash<QString, QPointF> commitPos;
  qreal maxPos = 0.0;
  QVector<qreal> lanes;
  // Commits in draw order (seq-sorted; BottomToTop reverses).
  QVector<const GitCommit*> ordered;
};

int colorIndex(int raw, bool colorTheme) {
  if (colorTheme && raw > 0) return (raw - 1) % 7 + 1;
  return raw % kNamedColors;
}

QString pathForArrow(const GitGraphData& data, const GitCommit& a,
                     const GitCommit& b, const QHash<QString, QPointF>& positions,
                     QHash<QString, int> branchIndex, QVector<qreal>& lanes,
                     int& outputColor) {
  const QPointF p1 = positions.value(a.id);
  const QPointF p2 = positions.value(b.id);
  const bool vertical = data.direction != Direction::LeftToRight;
  const bool bFurthest = vertical ? p1.x() < p2.x() : p1.y() < p2.y();
  const QString curveBranch = bFurthest ? b.branch : a.branch;
  bool reroute = false;
  for (const GitCommit& value : data.commits)
    if (value.seq > a.seq && value.seq < b.seq && value.branch == curveBranch) { reroute = true; break; }
  outputColor = branchIndex.value(b.branch);
  if (b.type == CommitType::Merge && a.id != b.parents.value(0)) outputColor = branchIndex.value(a.branch);
  auto lane = [&lanes](qreal a1, qreal a2, auto&& self, int depth = 0) -> qreal {
    const qreal candidate = a1 + std::abs(a1 - a2) / 2.0;
    if (depth > 5) return candidate;
    bool ok = true; for (qreal value : lanes) if (std::abs(value - candidate) < 10.0) { ok = false; break; }
    if (ok) { lanes.push_back(candidate); return candidate; }
    return self(a1, a2 - std::abs(a1 - a2) / 5.0, self, depth + 1);
  };
  QString d;
  if (reroute) {
    const qreal lineY = lane(std::min(p1.y(), p2.y()), std::max(p1.y(), p2.y()), lane);
    const qreal lineX = lane(std::min(p1.x(), p2.x()), std::max(p1.x(), p2.x()), lane);
    if (data.direction == Direction::TopToBottom) {
      if (p1.x() < p2.x()) d = QStringLiteral("M %1 %2 L %3 %2 A 10 10, 0, 0, 1, %4 %5 L %4 %6 A 10 10, 0, 0, 0, %7 %8 L %9 %8").arg(number(p1.x()), number(p1.y()), number(lineX - 10), number(lineX), number(p1.y() + 10), number(p2.y() - 10), number(lineX + 10), number(p2.y()), number(p2.x()));
      else { outputColor = branchIndex.value(a.branch); d = QStringLiteral("M %1 %2 L %3 %2 A 10 10, 0, 0, 0, %4 %5 L %4 %6 A 10 10, 0, 0, 1, %7 %8 L %9 %8").arg(number(p1.x()), number(p1.y()), number(lineX + 10), number(lineX), number(p1.y() + 10), number(p2.y() - 10), number(lineX - 10), number(p2.y()), number(p2.x())); }
    } else if (data.direction == Direction::BottomToTop) {
      if (p1.x() < p2.x()) d = QStringLiteral("M %1 %2 L %3 %2 A 10 10, 0, 0, 0, %4 %5 L %4 %6 A 10 10, 0, 0, 1, %7 %8 L %9 %8").arg(number(p1.x()), number(p1.y()), number(lineX - 10), number(lineX), number(p1.y() - 10), number(p2.y() + 10), number(lineX + 10), number(p2.y()), number(p2.x()));
      else { outputColor = branchIndex.value(a.branch); d = QStringLiteral("M %1 %2 L %3 %2 A 10 10, 0, 0, 1, %4 %5 L %4 %6 A 10 10, 0, 0, 0, %7 %8 L %9 %8").arg(number(p1.x()), number(p1.y()), number(lineX + 10), number(lineX), number(p1.y() - 10), number(p2.y() + 10), number(lineX - 10), number(p2.y()), number(p2.x())); }
    } else {
      if (p1.y() < p2.y()) d = QStringLiteral("M %1 %2 L %1 %3 A 10 10, 0, 0, 0, %4 %5 L %6 %5 A 10 10, 0, 0, 1, %7 %8 L %7 %9").arg(number(p1.x()), number(p1.y()), number(lineY - 10), number(p1.x() + 10), number(lineY), number(p2.x() - 10), number(p2.x()), number(lineY + 10), number(p2.y()));
      else { outputColor = branchIndex.value(a.branch); d = QStringLiteral("M %1 %2 L %1 %3 A 10 10, 0, 0, 1, %4 %5 L %6 %5 A 10 10, 0, 0, 0, %7 %8 L %7 %9").arg(number(p1.x()), number(p1.y()), number(lineY + 10), number(p1.x() + 10), number(lineY), number(p2.x() - 10), number(p2.x()), number(lineY - 10), number(p2.y())); }
    }
  } else if (data.direction == Direction::LeftToRight) {
    if (qFuzzyCompare(p1.y(), p2.y())) d = QStringLiteral("M %1 %2 L %3 %4").arg(number(p1.x()), number(p1.y()), number(p2.x()), number(p2.y()));
    else if (p1.y() < p2.y()) {
      if (b.type == CommitType::Merge && a.id != b.parents.value(0)) d = QStringLiteral("M %1 %2 L %3 %2 A 20 20, 0, 0, 1, %4 %5 L %4 %6").arg(number(p1.x()), number(p1.y()), number(p2.x() - 20), number(p2.x()), number(p1.y() + 20), number(p2.y()));
      else d = QStringLiteral("M %1 %2 L %1 %3 A 20 20, 0, 0, 0, %4 %5 L %6 %5").arg(number(p1.x()), number(p1.y()), number(p2.y() - 20), number(p1.x() + 20), number(p2.y()), number(p2.x()));
    } else {
      if (b.type == CommitType::Merge && a.id != b.parents.value(0)) d = QStringLiteral("M %1 %2 L %3 %2 A 20 20, 0, 0, 0, %4 %5 L %4 %6").arg(number(p1.x()), number(p1.y()), number(p2.x() - 20), number(p2.x()), number(p1.y() - 20), number(p2.y()));
      else d = QStringLiteral("M %1 %2 L %1 %3 A 20 20, 0, 0, 1, %4 %5 L %6 %5").arg(number(p1.x()), number(p1.y()), number(p2.y() + 20), number(p1.x() + 20), number(p2.y()), number(p2.x()));
    }
  } else {
    const bool tb = data.direction == Direction::TopToBottom;
    if (qFuzzyCompare(p1.x(), p2.x())) d = QStringLiteral("M %1 %2 L %3 %4").arg(number(p1.x()), number(p1.y()), number(p2.x()), number(p2.y()));
    else {
      const bool mergeParent = b.type == CommitType::Merge &&
                               a.id != b.parents.value(0);
      if (p1.x() > p2.x() && mergeParent)
        outputColor = branchIndex.value(a.branch);
      if (tb && p1.x() < p2.x() && mergeParent)
        d = QStringLiteral("M %1 %2 L %1 %3 A 20 20, 0, 0, 0, %4 %5 L %6 %5").arg(number(p1.x()), number(p1.y()), number(p2.y()-20), number(p1.x()+20), number(p2.y()), number(p2.x()));
      else if (tb && p1.x() < p2.x())
        d = QStringLiteral("M %1 %2 L %3 %2 A 20 20, 0, 0, 1, %4 %5 L %4 %6").arg(number(p1.x()), number(p1.y()), number(p2.x()-20), number(p2.x()), number(p1.y()+20), number(p2.y()));
      else if (tb && mergeParent)
        d = QStringLiteral("M %1 %2 L %1 %3 A 20 20, 0, 0, 1, %4 %5 L %6 %5").arg(number(p1.x()), number(p1.y()), number(p2.y()-20), number(p1.x()-20), number(p2.y()), number(p2.x()));
      else if (tb)
        d = QStringLiteral("M %1 %2 L %3 %2 A 20 20, 0, 0, 0, %4 %5 L %4 %6").arg(number(p1.x()), number(p1.y()), number(p2.x()+20), number(p2.x()), number(p1.y()+20), number(p2.y()));
      else if (p1.x() < p2.x() && mergeParent)
        d = QStringLiteral("M %1 %2 L %1 %3 A 20 20, 0, 0, 1, %4 %5 L %6 %5").arg(number(p1.x()), number(p1.y()), number(p2.y()+20), number(p1.x()+20), number(p2.y()), number(p2.x()));
      else if (p1.x() < p2.x())
        d = QStringLiteral("M %1 %2 L %3 %2 A 20 20, 0, 0, 0, %4 %5 L %4 %6").arg(number(p1.x()), number(p1.y()), number(p2.x()-20), number(p2.x()), number(p1.y()-20), number(p2.y()));
      else if (mergeParent)
        d = QStringLiteral("M %1 %2 L %1 %3 A 20 20, 0, 0, 0, %4 %5 L %6 %5").arg(number(p1.x()), number(p1.y()), number(p2.y()+20), number(p1.x()-20), number(p2.y()), number(p2.x()));
      else
        d = QStringLiteral("M %1 %2 L %3 %2 A 20 20, 0, 0, 1, %4 %5 L %4 %6").arg(number(p1.x()), number(p1.y()), number(p2.x()+20), number(p2.x()), number(p1.y()-20), number(p2.y()));
    }
  }
  return d;
}

// Per-branch label half-widths consumed by the TB/BT spacing advance. The
// spacing probe measures through the transient classless `.branch-label`
// wrapper (see GitGraphCssOverrides::branchProbe); an element hidden by its
// own display:none measures as an empty rect.
QVector<qreal> branchSpacingHalfWidths(const GitGraphData& data,
                                       const GitGraphSceneStyle& style,
                                       const GitGraphCssOverrides* css) {
  QVector<qreal> widths;
  const TextFont probe = css
      ? cssTextFont(style, css->branchProbe, style.fontSize, false)
      : styleFont(style, style.fontSize, false);
  for (const QString& name : data.orderedBranches) {
    qreal width = 0.0;
    if (!css || css->branchProbe.measures)
      width = branchTextBounds(probe, branchTextLines(name)).width() / 2.0;
    widths.push_back(width);
  }
  return widths;
}

// Shared by the scene builder and gitGraphArrowClassDigits so the adapter's
// DOM model and the drawn geometry always agree on branch/commit positions.
Layout computeLayout(const GitGraphData& data, const GitGraphConfig& config,
                     const GitGraphSceneStyle& style,
                     const QVector<qreal>& branchHalfWidths) {
  const bool redux = isRedux(style.themeName);
  Layout layout;
  qreal branchPos = 0.0;
  for (qsizetype i = 0; i < data.orderedBranches.size(); ++i) {
    const QString& name = data.orderedBranches.at(i);
    layout.branchIndex.insert(name, static_cast<int>(i));
    layout.branchPos.insert(name, data.direction == Direction::LeftToRight ? QPointF(0, branchPos) : QPointF(branchPos, 0));
    branchPos += 50.0 + (config.rotateCommitLabel ? 40.0 : 0.0) +
                 (data.direction == Direction::LeftToRight
                      ? 0.0
                      : branchHalfWidths.value(i));
  }
  qreal pos = data.direction == Direction::LeftToRight ? 0.0 : kDefaultPos;
  QVector<const GitCommit*> ordered;
  for (const GitCommit& commit : data.commits) ordered.push_back(&commit);
  std::sort(ordered.begin(), ordered.end(), [](auto a, auto b) {
    return a->seq < b->seq;
  });

  auto closestParentPosition = [&](const GitCommit& commit,
                                   bool smallest) -> std::optional<qreal> {
    std::optional<qreal> result;
    for (const QString& parent : commit.parents) {
      const auto it = layout.commitPos.constFind(parent);
      if (it == layout.commitPos.cend()) continue;
      const qreal value = data.direction == Direction::LeftToRight
                              ? it->x()
                              : it->y();
      if (!result || (smallest ? value <= *result : value >= *result))
        result = value;
    }
    return result;
  };

  if (config.parallelCommits && data.direction == Direction::BottomToTop) {
    qreal current = kDefaultPos;
    qreal maximum = kDefaultPos;
    QVector<const GitCommit*> roots;

    // Mermaid first lays out BT commits top-to-bottom so every child can read
    // its parents, then reflects each generation during the reverse draw pass.
    for (const GitCommit* commit : ordered) {
      if (commit->parents.isEmpty()) {
        roots.push_back(commit);
      } else if (const auto parent = closestParentPosition(*commit, true)) {
        current = *parent + kCommitStep;
        maximum = std::max(maximum, current);
      }
      layout.commitPos.insert(
          commit->id,
          QPointF(layout.branchPos.value(commit->branch).x(),
                  current + kLayoutOffset));
    }
    current = maximum;
    for (const GitCommit* root : roots) {
      layout.commitPos.insert(
          root->id,
          QPointF(layout.branchPos.value(root->branch).x(),
                  current + kDefaultPos));
    }
    for (const GitCommit* commit : ordered) {
      if (commit->parents.isEmpty()) continue;
      if (const auto parent = closestParentPosition(*commit, true)) {
        current = *parent - kCommitStep;
        layout.commitPos.insert(
            commit->id,
            QPointF(layout.branchPos.value(commit->branch).x(),
                    current - kLayoutOffset));
      }
    }

    std::reverse(ordered.begin(), ordered.end());
    for (const GitCommit* commit : ordered) {
      pos = layout.commitPos.value(commit->id).y() - kCommitStep;
      layout.commitPos.insert(
          commit->id,
          QPointF(layout.branchPos.value(commit->branch).x(), pos));
      pos += kCommitStep;
      layout.maxPos = std::max(layout.maxPos, pos);
    }
  } else {
    if (data.direction == Direction::BottomToTop)
      std::reverse(ordered.begin(), ordered.end());
    for (const GitCommit* commit : ordered) {
      if (config.parallelCommits) {
        if (const auto parent = closestParentPosition(*commit, false)) {
          pos = *parent + kCommitStep;
        } else {
          pos = data.direction == Direction::TopToBottom ? kDefaultPos : 0.0;
        }
      }
      QPointF point;
      if (data.direction == Direction::LeftToRight) {
        point = QPointF(pos + kLayoutOffset,
                        layout.branchPos.value(commit->branch).y() +
                            (redux ? 7.0 : -2.0));
      } else {
        point = QPointF(layout.branchPos.value(commit->branch).x(),
                        pos + kLayoutOffset);
      }
      layout.commitPos.insert(commit->id, point);
      pos += kCommitStep + kLayoutOffset;
      layout.maxPos = std::max(layout.maxPos, pos);
    }
  }
  layout.ordered = ordered;
  return layout;
}

}  // namespace

QVector<int> gitGraphArrowClassDigits(const GitGraphData& data,
                                      const GitGraphConfig& config,
                                      const GitGraphSceneStyle& style) {
  const bool colorTheme = isColorTheme(style.themeName);
  Layout layout = computeLayout(data, config, style,
                                  branchSpacingHalfWidths(data, style, nullptr));
  QVector<int> digits;
  for (const GitCommit& commit : data.commits) {
    for (const QString& parentId : commit.parents) {
      const GitCommit* parent = nullptr;
      for (const GitCommit& candidate : data.commits)
        if (candidate.id == parentId) { parent = &candidate; break; }
      if (!parent) continue;
      int color = 0;
      pathForArrow(data, *parent, commit, layout.commitPos,
                   layout.branchIndex, layout.lanes, color);
      digits.push_back(colorIndex(color, colorTheme));
    }
  }
  return digits;
}

GitGraphScene buildGitGraphScene(const GitGraphData& data,
                                 GitGraphConfig config,
                                 GitGraphSceneStyle style,
                                 const GitGraphCssOverrides* css) {
  GitGraphScene scene; scene.config = config; scene.style = std::move(style); scene.useMaxWidth = config.useMaxWidth;
  if (scene.style.gitColors.isEmpty()) scene.style.gitColors = {QStringLiteral("#0000ec"), QStringLiteral("#dede00"), QStringLiteral("#9dec00"), QStringLiteral("#0076ec"), QStringLiteral("#00ec76"), QStringLiteral("#ec0076"), QStringLiteral("#00ecec"), QStringLiteral("#ec7600")};
  if (scene.style.gitInvColors.isEmpty()) scene.style.gitInvColors = scene.style.gitColors;
  if (scene.style.branchLabelColors.isEmpty()) scene.style.branchLabelColors = {QStringLiteral("#fff"), QStringLiteral("#fff"), QStringLiteral("#fff"), QStringLiteral("#fff"), QStringLiteral("#fff"), QStringLiteral("#fff"), QStringLiteral("#fff"), QStringLiteral("#fff")};
  const bool redux = isRedux(scene.style.themeName);
  const bool colorTheme = isColorTheme(scene.style.themeName);
  // themeCSS slots follow this builder's emission order; the no-CSS default
  // keeps every gate open so the plain path is unchanged.
  static const GitGraphElementCss kNoCss;
  static const GitGraphCssOverrides::Branch kNoBranch;
  static const GitGraphCssOverrides::CommitLabel kNoLabel;
  static const GitGraphCssOverrides::Tag kNoTag;
  const auto branchSlot = [&](qsizetype i) -> const GitGraphCssOverrides::Branch& {
    return css && i < css->branches.size() ? css->branches.at(i) : kNoBranch;
  };
  const auto arrowSlot = [&](qsizetype i) -> const GitGraphElementCss& {
    return css && i < css->arrows.size() ? css->arrows.at(i) : kNoCss;
  };
  const auto bulletSlot = [&](qsizetype commit, qsizetype j) -> const GitGraphElementCss& {
    return css && commit < css->bullets.size() && j < css->bullets.at(commit).size()
        ? css->bullets.at(commit).at(j) : kNoCss;
  };
  const auto labelSlot = [&](qsizetype i) -> const GitGraphCssOverrides::CommitLabel& {
    return css && i < css->labels.size() ? css->labels.at(i) : kNoLabel;
  };
  const auto tagSlot = [&](qsizetype i) -> const GitGraphCssOverrides::Tag& {
    return css && i < css->tags.size() ? css->tags.at(i) : kNoTag;
  };
  const auto titleSlot = [&]() -> const GitGraphElementCss& {
    return css ? css->title : kNoCss;
  };
  Layout layout = computeLayout(
      data, config, scene.style, branchSpacingHalfWidths(data, scene.style, css));
  const QVector<const GitCommit*>& ordered = layout.ordered;
  bool hasBounds = false; QRectF content;
  // The final setupGraphViewbox getBBox drops display:none geometry, so the
  // content union skips slots without a box; visibility:hidden still paints
  // nothing but keeps its geometry, exactly like the browser.
  auto append = [&](GitGraphPrimitive primitive, const GitGraphElementCss& slot) {
    if (slot.hasBox) unite(content, hasBounds, primitive.bounds);
    primitive.css = slot;
    scene.primitives.push_back(std::move(primitive));
  };

  if (config.showBranches) {
    for (qsizetype i = 0; i < data.orderedBranches.size(); ++i) {
      const QString name = data.orderedBranches.at(i); const QPointF bp = layout.branchPos.value(name);
      const int ci = colorIndex(static_cast<int>(i), colorTheme);
      const qreal spine = data.direction == Direction::LeftToRight ? bp.y() + (redux ? 7.0 : -2.0) : bp.x();
      const GitGraphCssOverrides::Branch& branchCss = branchSlot(i);
      GitGraphPrimitive line; line.kind = PrimitiveKind::Line; line.role = QStringLiteral("branch"); line.cssClass = QStringLiteral("branch branch%1").arg(ci);
      line.stroke = scene.style.commitLineColor.isEmpty() ? scene.style.lineColor : scene.style.commitLineColor; line.strokeWidth = scene.style.strokeWidth; line.dash = isNeoTheme(scene.style.themeName) ? QVector<qreal>{4,2} : QVector<qreal>{2,2};
      if (data.direction == Direction::LeftToRight) line.line = QLineF(0, spine, layout.maxPos, spine);
      else if (data.direction == Direction::TopToBottom) line.line = QLineF(spine, kDefaultPos, spine, layout.maxPos);
      else line.line = QLineF(spine, layout.maxPos, spine, kDefaultPos);
      line.bounds = QRectF(line.line.p1(), line.line.p2()).normalized(); append(line, branchCss.line); layout.lanes.push_back(spine);

      // The drawn label is measured in place (with the `.branch-labelN`
      // classes applied), unlike the classless spacing probe above.
      const QStringList branchLines = branchTextLines(name);
      const TextFont branchFont = cssTextFont(scene.style, branchCss.text,
                                              scene.style.fontSize, redux);
      const QRectF tb = branchCss.text.measures
          ? branchTextBounds(branchFont, branchLines)
          : QRectF();
      GitGraphPrimitive bkg; bkg.kind = PrimitiveKind::Rect; bkg.role = QStringLiteral("branch-label-background"); bkg.cssClass = QStringLiteral("branchLabelBkg label%1").arg(ci); bkg.rx = redux ? 0.0 : 4.0;
      if (isPureNeo(scene.style.themeName)) {
        bkg.fill = scene.style.useGradient ? scene.style.mainBkg
                                           : scene.style.textColor;
        bkg.gradientStroke = scene.style.useGradient;
        bkg.strokeWidth = scene.style.strokeWidth;
      } else if (redux && !colorTheme) {
        bkg.fill = scene.style.mainBkg;
        bkg.stroke = scene.style.nodeBorder;
        bkg.strokeWidth = scene.style.strokeWidth;
      } else if (colorTheme) {
        const QString border = ci == 0
                                   ? scene.style.nodeBorder
                                   : colorAt(scene.style.borderColors, ci,
                                             scene.style.nodeBorder);
        bkg.fill = ci == 0 || isDarkTheme(scene.style.themeName)
                       ? scene.style.mainBkg
                       : border;
        bkg.stroke = border;
        bkg.strokeWidth = scene.style.strokeWidth;
      } else {
        bkg.fill = colorAt(scene.style.gitColors, ci, scene.style.mainBkg);
      }
      const qreal labelPaddingX = redux ? 16.0 : 0.0;
      const qreal labelPaddingY = redux ? 12.0 : 0.0;
      bkg.rect = QRectF(-tb.width() - 4.0 - (config.rotateCommitLabel ? 30.0 : 0.0),
                        -tb.height() / 2.0 + 10.0,
                        tb.width() + 18.0 + labelPaddingX,
                        tb.height() + 4.0 + labelPaddingY);
      if (data.direction == Direction::LeftToRight) {
        bkg.translation = QPointF(-19.0, spine - 12.0 - labelPaddingY / 2.0);
      } else if (data.direction == Direction::TopToBottom) {
        bkg.rect.moveTopLeft(QPointF(spine - tb.width() / 2.0 - 10.0, 0.0));
        if (redux) bkg.translation = QPointF(-labelPaddingX / 2.0 - 3.0,
                                              -labelPaddingY - 10.0);
      } else {
        bkg.rect.moveTopLeft(QPointF(spine - tb.width() / 2.0 - 10.0,
                                     layout.maxPos));
        if (redux) bkg.translation = QPointF(-labelPaddingX / 2.0 - 3.0,
                                              labelPaddingY + 10.0);
      }
      bkg.bounds = transformedBounds(bkg.rect, bkg.translation); append(bkg, branchCss.bkg);
      GitGraphPrimitive label; label.kind = PrimitiveKind::Text; label.role = QStringLiteral("branch-label"); label.textLines = branchLines; label.text = branchLines.join(QString()); label.fontSize = scene.style.fontSize; label.bold = redux; label.fill = branchLabelColor(scene.style, ci);
      label.position = QPointF(0.0, scene.style.fontSize);
      if (data.direction == Direction::LeftToRight) {
        label.translation = QPointF(-tb.width() - 14.0 -
                                        (config.rotateCommitLabel ? 30.0 : 0.0) +
                                        labelPaddingX / 2.0,
                                    spine - tb.height() / 2.0 - 2.0);
      } else if (data.direction == Direction::TopToBottom) {
        label.translation = QPointF(spine - tb.width() / 2.0 - 5.0,
                                    redux ? -labelPaddingY * 2.0 + 7.0 : 0.0);
      } else {
        label.translation = QPointF(spine - tb.width() / 2.0 - 5.0,
                                    redux ? layout.maxPos + labelPaddingY * 2.0 + 4.0
                                          : layout.maxPos);
      }
      label.bounds = transformedBounds(tb, label.translation); append(label, branchCss.text);
    }
  }

  qsizetype arrowCursor = 0;
  // Arrows are behind commit symbols, exactly as the upstream DOM order.
  for (const GitCommit& commit : data.commits) {
    for (const QString& parentId : commit.parents) {
      const GitCommit* parent = nullptr; for (const GitCommit& candidate : data.commits) if (candidate.id == parentId) { parent = &candidate; break; }
      if (!parent) continue;
      int color = 0; const QString d = pathForArrow(data, *parent, commit, layout.commitPos, layout.branchIndex, layout.lanes, color);
      GitGraphPrimitive arrow; arrow.kind = PrimitiveKind::Path; arrow.role = QStringLiteral("arrow"); arrow.cssClass = QStringLiteral("arrow arrow%1").arg(colorIndex(color,colorTheme)); arrow.pathData = d; arrow.path = scene::parseSvgPath(d); arrow.bounds = arrow.path.boundingRect(); arrow.fill = QStringLiteral("none"); arrow.stroke = commitColor(scene.style, colorIndex(color,colorTheme)); arrow.strokeWidth = redux ? scene.style.strokeWidth : 8.0; append(arrow, arrowSlot(arrowCursor++));
    }
  }

  qsizetype commitCursor = 0;
  qsizetype labelCursor = 0;
  qsizetype tagCursor = 0;
  for (const GitCommit* commitPointer : ordered) {
    const GitCommit& commit = *commitPointer;
    const QPointF p = layout.commitPos.value(commit.id); const int raw = layout.branchIndex.value(commit.branch); const int ci = colorIndex(raw,colorTheme);
    const CommitType symbol = commit.customType.value_or(commit.type);
    const QString baseColor = commitColor(scene.style, ci);
    const qsizetype drawIndex = commitCursor++;
    qsizetype bulletIndex = 0;
    if (symbol == CommitType::Highlight) {
      GitGraphPrimitive outer; outer.kind=PrimitiveKind::Rect; outer.role=QStringLiteral("commit-highlight-outer"); outer.cssClass=QStringLiteral("commit %1 commit-highlight%2 commit-highlight-outer").arg(commit.id).arg(ci); outer.rect=QRectF(p.x()-(redux?7:10),p.y()-(redux?7:10),redux?14:20,redux?14:20); outer.bounds=outer.rect;
      if (isPureNeo(scene.style.themeName)) outer.fill=ci==0?scene.style.nodeBorder:colorAt(scene.style.gitInvColors,ci,baseColor);
      else if (redux && !colorTheme) outer.fill=scene.style.nodeBorder;
      else if (colorTheme) outer.fill=ci==0?scene.style.mainBkg:colorAt(scene.style.borderColors,ci,scene.style.nodeBorder);
      else outer.fill=colorAt(scene.style.gitInvColors,ci,baseColor);
      outer.stroke=colorTheme&&ci==0?scene.style.nodeBorder:outer.fill; append(outer, bulletSlot(drawIndex, bulletIndex++));
      GitGraphPrimitive inner=outer; inner.role=QStringLiteral("commit-highlight-inner"); inner.cssClass=QStringLiteral("commit %1 commit%2 commit-highlight-inner").arg(commit.id).arg(ci); inner.rect=QRectF(p.x()-(redux?4:6),p.y()-(redux?4:6),redux?8:12,redux?8:12); inner.bounds=inner.rect; inner.fill=isNeoTheme(scene.style.themeName)?scene.style.mainBkg:scene.style.primaryColor; inner.stroke=inner.fill; append(inner, bulletSlot(drawIndex, bulletIndex++));
    } else {
      GitGraphPrimitive bullet; bullet.kind=PrimitiveKind::Circle; bullet.role=QStringLiteral("commit"); bullet.cssClass=QStringLiteral("commit %1 commit%2").arg(commit.id).arg(ci); bullet.center=p; bullet.radius=redux?7:10; bullet.bounds=QRectF(p.x()-bullet.radius,p.y()-bullet.radius,2*bullet.radius,2*bullet.radius); bullet.fill=symbol==CommitType::CherryPick?(isNeoTheme(scene.style.themeName)?scene.style.nodeBorder:scene.style.textColor):baseColor; bullet.stroke=symbol==CommitType::CherryPick?QStringLiteral("none"):baseColor; append(bullet, bulletSlot(drawIndex, bulletIndex++));
      if (symbol==CommitType::Merge) { GitGraphPrimitive inner=bullet; inner.role=QStringLiteral("commit-merge"); inner.cssClass=QStringLiteral("commit commit-merge %1 commit%2").arg(commit.id).arg(ci); inner.radius=redux?5:6; inner.bounds=QRectF(p.x()-inner.radius,p.y()-inner.radius,2*inner.radius,2*inner.radius); inner.fill=isNeoTheme(scene.style.themeName)?scene.style.mainBkg:scene.style.primaryColor; inner.stroke=inner.fill; append(inner, bulletSlot(drawIndex, bulletIndex++)); }
      if (symbol==CommitType::Reverse) { GitGraphPrimitive cross; cross.kind=PrimitiveKind::Path; cross.role=QStringLiteral("commit-reverse"); cross.cssClass=QStringLiteral("commit commit-reverse %1 commit%2").arg(commit.id).arg(ci); const qreal s=redux?4:5; cross.path.moveTo(p.x()-s,p.y()-s);cross.path.lineTo(p.x()+s,p.y()+s);cross.path.moveTo(p.x()-s,p.y()+s);cross.path.lineTo(p.x()+s,p.y()-s);cross.pathData=QStringLiteral("M %1,%2L%3,%4M%1,%4L%3,%2").arg(number(p.x()-s),number(p.y()-s),number(p.x()+s),number(p.y()+s));cross.bounds=cross.path.boundingRect();cross.stroke=isNeoTheme(scene.style.themeName)?scene.style.mainBkg:scene.style.primaryColor;cross.strokeWidth=isNeoTheme(scene.style.themeName)?scene.style.strokeWidth:3;append(cross, bulletSlot(drawIndex, bulletIndex++)); }
      if (symbol==CommitType::CherryPick) { const QString eye=(scene.style.themeName.contains(QLatin1String("dark"))?QStringLiteral("#000000"):QStringLiteral("#fff")); for(qreal dx:{-3.0,3.0}){GitGraphPrimitive dot;dot.kind=PrimitiveKind::Circle;dot.role=QStringLiteral("cherry-dot");dot.cssClass=QStringLiteral("commit %1 commit-cherry-pick").arg(commit.id);dot.center=QPointF(p.x()+dx,p.y()+2);dot.radius=redux?2.5:2.75;dot.bounds=QRectF(dot.center.x()-dot.radius,dot.center.y()-dot.radius,2*dot.radius,2*dot.radius);dot.fill=eye;dot.stroke=QStringLiteral("none");append(dot, bulletSlot(drawIndex, bulletIndex++));} for(qreal dx:{3.0,-3.0}){GitGraphPrimitive stem;stem.kind=PrimitiveKind::Line;stem.role=QStringLiteral("cherry-stem");stem.cssClass=QStringLiteral("commit %1 commit-cherry-pick").arg(commit.id);stem.line=QLineF(p.x()+dx,p.y()+1,p.x(),p.y()-5);stem.bounds=QRectF(stem.line.p1(),stem.line.p2()).normalized();stem.stroke=eye;append(stem, bulletSlot(drawIndex, bulletIndex++));} }
    }
    if (config.showCommitLabel && commit.type != CommitType::CherryPick && (commit.type != CommitType::Merge || commit.customId)) {
      const GitGraphCssOverrides::CommitLabel& labelCss = labelSlot(labelCursor++);
      // Measured in place on the final text.commit-label element.
      const TextFont labelFont = cssTextFont(scene.style, labelCss.text,
                                             scene.style.commitLabelFontSize,
                                             redux);
      const QRectF tb=labelCss.text.measures?directTextBounds(labelFont,commit.id,labelFont.fauxBold,labelFont.cssBold):QRectF();
      GitGraphPrimitive bg;bg.kind=PrimitiveKind::Rect;bg.role=QStringLiteral("commit-label-background");bg.cssClass=QStringLiteral("commit-label-bkg");bg.fill=isNeoTheme(scene.style.themeName)?QStringLiteral("transparent"):scene.style.commitLabelBackground;bg.opacity=isNeoTheme(scene.style.themeName)?1.0:0.5;
      GitGraphPrimitive label;label.kind=PrimitiveKind::Text;label.role=QStringLiteral("commit-label");label.cssClass=QStringLiteral("commit-label");label.text=commit.id;label.fontSize=scene.style.commitLabelFontSize;label.bold=redux;label.fill=isNeoTheme(scene.style.themeName)?scene.style.nodeBorder:scene.style.commitLabelColor;
      if(data.direction==Direction::LeftToRight){
        const qreal commitAxisPos = p.x() - kLayoutOffset;
        label.position=QPointF(p.x()-tb.width()/2,p.y()+25);
        bg.rect=QRectF(p.x()-tb.width()/2-2,p.y()+13.5,tb.width()+4,tb.height()+4);
        if(config.rotateCommitLabel){
          const qreal rx=-7.5-(tb.width()+10)/25*9.5;
          const qreal ry=10+tb.width()/25*8.5;
          label.translation=QPointF(rx,ry);bg.translation=label.translation;
          label.rotation=-45;label.rotationOrigin=QPointF(commitAxisPos,p.y());
          bg.rotation=-45;bg.rotationOrigin=label.rotationOrigin;
        }
      } else {
        label.position=QPointF(p.x()-(tb.width()+16),p.y()+tb.height()-12);
        bg.rect=QRectF(p.x()-(tb.width()+21),p.y()-12,tb.width()+4,tb.height()+4);
        if(config.rotateCommitLabel){
          label.rotation=-45;label.rotationOrigin=p;
          bg.rotation=-45;bg.rotationOrigin=p;
        }
      }
      label.bounds=transformedBounds(tb.translated(label.position), label.translation,
                                     label.rotation,label.rotationOrigin);
      bg.bounds=transformedBounds(bg.rect,bg.translation,bg.rotation,bg.rotationOrigin);
      append(bg,labelCss.bkg);append(label,labelCss.text);
    }
    if(!commit.tags.isEmpty()) {
      qreal maxW=0,maxH=0;
      for(qsizetype tf=0;tf<commit.tags.size();++tf){
        const GitGraphCssOverrides::Tag& maxSlot=tagSlot(tagCursor+commit.tags.size()-1-tf);
        const TextFont maxFont=cssTextFont(scene.style,maxSlot.text,scene.style.tagLabelFontSize,false);
        const QRectF tb=maxSlot.text.measures?directTextBounds(maxFont,commit.tags.at(tf),maxFont.fauxBold):QRectF();
        maxW=std::max(maxW,tb.width());maxH=std::max(maxH,tb.height());
      }
      for(qsizetype ti=0;ti<commit.tags.size();++ti){
        const QString tag=commit.tags.at(commit.tags.size()-1-ti);
        const GitGraphCssOverrides::Tag& tagCss=tagSlot(tagCursor++);
        const TextFont tagFont=cssTextFont(scene.style,tagCss.text,scene.style.tagLabelFontSize,false);
        const QRectF tb=tagCss.text.measures?directTextBounds(tagFont,tag,tagFont.fauxBold):QRectF();
        const qreal yo=20*ti;const qreal ly=p.y()-19.2-yo;
        const qreal tagAxisPos=data.direction==Direction::LeftToRight?p.x()-kLayoutOffset:p.y()-kLayoutOffset;
        GitGraphPrimitive poly;poly.kind=PrimitiveKind::Polygon;poly.role=QStringLiteral("tag-background");poly.cssClass=QStringLiteral("tag-label-bkg");poly.fill=isNeoTheme(scene.style.themeName)?scene.style.mainBkg:scene.style.tagLabelBackground;poly.stroke=isNeoTheme(scene.style.themeName)?scene.style.nodeBorder:scene.style.tagLabelBorder;
        GitGraphPrimitive hole;hole.kind=PrimitiveKind::Circle;hole.role=QStringLiteral("tag-hole");hole.cssClass=QStringLiteral("tag-hole");hole.radius=1.5;hole.fill=scene.style.textColor;
        GitGraphPrimitive tx;tx.kind=PrimitiveKind::Text;tx.role=QStringLiteral("tag-label");tx.cssClass=QStringLiteral("tag-label");tx.text=tag;tx.fontSize=scene.style.tagLabelFontSize;tx.fill=scene.style.tagLabelColor;
        if(data.direction==Direction::LeftToRight){
          poly.polygon={QPointF(tagAxisPos-maxW/2-2,ly+2),QPointF(tagAxisPos-maxW/2-2,ly-2),QPointF(p.x()-maxW/2-4,ly-maxH/2-2),QPointF(p.x()+maxW/2+4,ly-maxH/2-2),QPointF(p.x()+maxW/2+4,ly+maxH/2+2),QPointF(p.x()-maxW/2-4,ly+maxH/2+2)};
          hole.center=QPointF(tagAxisPos-maxW/2+2,ly);
          tx.position=QPointF(p.x()-tb.width()/2,p.y()-16-yo);
        }else{
          const qreal yOrigin=tagAxisPos+yo;
          poly.polygon={QPointF(p.x(),yOrigin+2),QPointF(p.x(),yOrigin-2),QPointF(p.x()+kLayoutOffset,yOrigin-maxH/2-2),QPointF(p.x()+kLayoutOffset+maxW+4,yOrigin-maxH/2-2),QPointF(p.x()+kLayoutOffset+maxW+4,yOrigin+maxH/2+2),QPointF(p.x()+kLayoutOffset,yOrigin+maxH/2+2)};
          poly.translation=QPointF(12,12);poly.rotation=45;poly.rotationOrigin=QPointF(p.x(),tagAxisPos);
          hole.center=QPointF(p.x()+2,yOrigin);hole.translation=poly.translation;hole.rotation=45;hole.rotationOrigin=poly.rotationOrigin;
          tx.position=QPointF(p.x()+5,yOrigin+3);tx.translation=QPointF(14,14);tx.rotation=45;tx.rotationOrigin=poly.rotationOrigin;
        }
        poly.bounds=transformedBounds(poly.polygon.boundingRect(),poly.translation,poly.rotation,poly.rotationOrigin);append(poly,tagCss.bkg);
        hole.bounds=transformedBounds(QRectF(hole.center.x()-1.5,hole.center.y()-1.5,3,3),hole.translation,hole.rotation,hole.rotationOrigin);append(hole,tagCss.hole);
        tx.bounds=transformedBounds(tb.translated(tx.position),tx.translation,tx.rotation,tx.rotationOrigin);append(tx,tagCss.text);
      }
    }
  }
  if(!data.title.isEmpty()){const GitGraphElementCss& titleCss=titleSlot();const TextFont titleFont=cssTextFont(scene.style,titleCss,18,false);QRectF tb=titleCss.measures?directTextBounds(titleFont,data.title):QRectF();GitGraphPrimitive title;title.kind=PrimitiveKind::Text;title.role=QStringLiteral("title");title.cssClass=QStringLiteral("gitTitleText");title.text=data.title;title.fontSize=18;title.fill=scene.style.textColor;title.anchor=QStringLiteral("middle");const qreal center=hasBounds?content.center().x():0;title.position=QPointF(center,-config.titleTopMargin);title.bounds=tb.translated(center-tb.width()/2,-config.titleTopMargin);append(title,titleCss);}
  const auto phase = [](const GitGraphPrimitive& primitive) {
    if (primitive.role == QLatin1String("arrow")) return 1;
    if ((primitive.role.startsWith(QLatin1String("commit")) ||
         primitive.role.startsWith(QLatin1String("cherry-"))) &&
        primitive.role != QLatin1String("commit-label") &&
        primitive.role != QLatin1String("commit-label-background")) return 2;
    if (primitive.role == QLatin1String("commit-label") ||
        primitive.role == QLatin1String("commit-label-background") ||
        primitive.role.startsWith(QLatin1String("tag-"))) return 3;
    if (primitive.role == QLatin1String("title")) return 4;
    return 0;
  };
  std::stable_sort(scene.primitives.begin(), scene.primitives.end(),
                   [&](const GitGraphPrimitive& a,
                       const GitGraphPrimitive& b) {
                     return phase(a) < phase(b);
                   });
  if(!hasBounds) content=QRectF(0,0,0,0);
  scene.contentBounds=content;scene.bounds=content.adjusted(-config.diagramPadding,-config.diagramPadding,config.diagramPadding,config.diagramPadding);scene.rasterBounds=QRectF(scene.bounds.topLeft(),QSizeF(qRound(scene.bounds.width()),qRound(scene.bounds.height())));scene.viewBoxAttribute=QStringLiteral("%1 %2 %3 %4").arg(number(scene.bounds.x()),number(scene.bounds.y()),number(scene.bounds.width()),number(scene.bounds.height()));return scene;
}

QJsonObject GitGraphScene::toJsonObject() const {
  QJsonArray values;for(const auto&p:primitives){QJsonObject o{{QStringLiteral("role"),p.role},{QStringLiteral("class"),p.cssClass},{QStringLiteral("fill"),p.fill},{QStringLiteral("stroke"),p.stroke},{QStringLiteral("strokeWidth"),p.strokeWidth},{QStringLiteral("path"),p.pathData},{QStringLiteral("text"),p.text},{QStringLiteral("x"),p.position.x()},{QStringLiteral("y"),p.position.y()}};o.insert(QStringLiteral("bounds"),QJsonObject{{QStringLiteral("x"),p.bounds.x()},{QStringLiteral("y"),p.bounds.y()},{QStringLiteral("width"),p.bounds.width()},{QStringLiteral("height"),p.bounds.height()}});values.append(o);}return {{QStringLiteral("viewBox"),viewBoxAttribute},{QStringLiteral("useMaxWidth"),useMaxWidth},{QStringLiteral("primitives"),values}};
}

}  // namespace muffin::mermaid::gitgraph
