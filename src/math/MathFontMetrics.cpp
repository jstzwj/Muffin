#include "math/MathFontMetrics.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

static void initKatexFontsResource() {
  Q_INIT_RESOURCE(katex_fonts);
}

namespace muffin::math {
namespace {

using FontMetricMap = QHash<int, CharacterMetrics>;

QHash<QString, FontMetricMap>& metricMap() {
  static QHash<QString, FontMetricMap> map;
  return map;
}

bool& metricsLoaded() {
  static bool loaded = false;
  return loaded;
}

QString fontMetricsResource() {
  QFile file(QStringLiteral(":/katex/src/fontMetricsData.js"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

void parseFontBlock(const QString& fontName, const QString& block) {
  FontMetricMap metrics;
  static const QRegularExpression entryRe(QStringLiteral("\"(\\d+)\"\\s*:\\s*\\[([^\\]]+)\\]"));
  QRegularExpressionMatchIterator it = entryRe.globalMatch(block);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    const int codepoint = match.captured(1).toInt();
    const QStringList values = match.captured(2).split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (values.size() < 5) {
      continue;
    }
    CharacterMetrics metric;
    metric.depth = values.at(0).trimmed().toDouble();
    metric.height = values.at(1).trimmed().toDouble();
    metric.italic = values.at(2).trimmed().toDouble();
    metric.skew = values.at(3).trimmed().toDouble();
    metric.width = values.at(4).trimmed().toDouble();
    metrics.insert(codepoint, metric);
  }
  metricMap().insert(fontName, std::move(metrics));
}

QString blockForFont(const QString& source, qsizetype nameStart) {
  qsizetype brace = source.indexOf(QLatin1Char('{'), nameStart);
  if (brace < 0) {
    return {};
  }
  int depth = 0;
  for (qsizetype i = brace; i < source.size(); ++i) {
    if (source.at(i) == QLatin1Char('{')) {
      ++depth;
    } else if (source.at(i) == QLatin1Char('}')) {
      --depth;
      if (depth == 0) {
        return source.mid(brace + 1, i - brace - 1);
      }
    }
  }
  return {};
}

}  // namespace

void MathFontMetrics::ensureLoaded() {
  if (metricsLoaded()) {
    return;
  }
  initKatexFontsResource();
  metricsLoaded() = true;

  const QString source = fontMetricsResource();
  if (source.isEmpty()) {
    return;
  }

  static const QRegularExpression fontRe(QStringLiteral("\"([^\"]+)\"\\s*:\\s*\\{"));
  QRegularExpressionMatchIterator it = fontRe.globalMatch(source);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    const QString fontName = match.captured(1);
    const QString block = blockForFont(source, match.capturedStart(1));
    if (!block.isEmpty()) {
      parseFontBlock(fontName, block);
    }
  }
}

std::optional<CharacterMetrics> MathFontMetrics::characterMetrics(const QString& fontName, const QString& character) {
  ensureLoaded();
  if (character.isEmpty() || !metricMap().contains(fontName)) {
    return std::nullopt;
  }
  const int codepoint = character.at(0).unicode();
  const FontMetricMap& metrics = metricMap().value(fontName);
  if (metrics.contains(codepoint)) {
    return metrics.value(codepoint);
  }
  return std::nullopt;
}

GlobalFontMetrics MathFontMetrics::globalMetrics(int styleSize) {
  const int index = styleSize <= 1 ? 0 : (styleSize == 2 ? 1 : 2);
  const auto pick = [index](qreal text, qreal script, qreal scriptscript) {
    if (index == 0) return text;
    if (index == 1) return script;
    return scriptscript;
  };

  GlobalFontMetrics metrics;
  metrics.cssEmPerMu = pick(1.0, 1.171, 1.472) / 18.0;
  metrics.xHeight = 0.431;
  metrics.quad = pick(1.0, 1.171, 1.472);
  metrics.defaultRuleThickness = pick(0.04, 0.049, 0.049);
  metrics.bigOpSpacing1 = pick(0.111, 0.111, 0.111);
  metrics.bigOpSpacing2 = pick(0.166, 0.166, 0.166);
  metrics.bigOpSpacing3 = pick(0.200, 0.200, 0.200);
  metrics.bigOpSpacing4 = pick(0.600, 0.611, 0.611);
  metrics.bigOpSpacing5 = pick(0.100, 0.143, 0.143);
  metrics.sqrtRuleThickness = 0.04;
  metrics.ptPerEm = 10.0;
  metrics.arrayRuleWidth = 0.04;
  metrics.axisHeight = 0.25;
  metrics.num1 = pick(0.677, 0.732, 0.925);
  metrics.num2 = pick(0.394, 0.384, 0.387);
  metrics.num3 = pick(0.444, 0.471, 0.504);
  metrics.denom1 = pick(0.686, 0.752, 1.025);
  metrics.denom2 = pick(0.345, 0.344, 0.532);
  metrics.sup1 = pick(0.413, 0.503, 0.504);
  metrics.sup2 = pick(0.363, 0.431, 0.404);
  metrics.sup3 = pick(0.289, 0.286, 0.294);
  metrics.sub1 = pick(0.150, 0.143, 0.200);
  metrics.sub2 = pick(0.247, 0.286, 0.400);
  metrics.supDrop = pick(0.386, 0.353, 0.494);
  metrics.subDrop = pick(0.050, 0.071, 0.100);
  metrics.delim1 = pick(2.390, 1.700, 1.980);
  metrics.delim2 = pick(1.010, 1.157, 1.420);
  metrics.fboxsep = 0.3;
  metrics.fboxrule = 0.04;
  return metrics;
}

bool MathFontMetrics::loaded() {
  ensureLoaded();
  return !metricMap().isEmpty();
}

}  // namespace muffin::math
