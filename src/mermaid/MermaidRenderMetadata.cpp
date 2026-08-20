#include "mermaid/MermaidRenderMetadata.h"

#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>

#include <cmath>

namespace muffin::mermaid {
namespace {

QString singleLineTitle(QString title) {
  title.replace(QLatin1Char('\r'), QLatin1Char(' '));
  title.replace(QLatin1Char('\n'), QLatin1Char(' '));
  return title.trimmed();
}

QFont titleFont(const MermaidRenderMetadata& metadata) {
  QFont font(metadata.fontFamily);
  font.setPixelSize(qMax(1, qRound(metadata.titleFontSize)));
  font.setWeight(QFont::Normal);
  return font;
}

}  // namespace

QString MermaidRenderMetadata::accessibleName() const {
  const QString explicitName = accessibleTitle.trimmed();
  if (!explicitName.isEmpty()) return explicitName;
  const QString visibleName = singleLineTitle(title);
  return visibleName.isEmpty() ? QStringLiteral("Mermaid diagram") : visibleName;
}

qreal measureMermaidTitleWidth(const MermaidRenderMetadata& metadata) {
  if (!metadata.hasVisibleTitle()) return 0.0;
  const QString text = singleLineTitle(metadata.title);
  // getComputedTextLength parity: the browser's title bbox width is the
  // LayoutUnit-quantized design advance (ceil to 1/64), not QFontMetricsF's
  // hinted advance (which drifts ~1.6px for an 18px title).
  flowchart::FlowLabelDocument document;
  document.text = text;
  const qreal advance = flowchart::measureOpenTypeDesignAdvance(
                            document, metadata.fontFamily,
                            metadata.titleFontSize)
                            .value_or(QFontMetricsF(titleFont(metadata))
                                          .horizontalAdvance(text));
  return std::ceil(advance * 64.0 - 1e-9) / 64.0;
}

void paintMermaidTitle(const MermaidRenderMetadata& metadata,
                       QPainter& painter, const QRectF& titleRect) {
  if (!metadata.hasVisibleTitle() || titleRect.isEmpty()) return;
  painter.save();
  const QFont font = titleFont(metadata);
  painter.setFont(font);
  painter.setPen(color::toQColor(metadata.titleColor));
  // Upstream insertTitle places the title BASELINE titleTopMargin above the
  // content bbox (text y = -titleTopMargin, text-anchor:middle on the
  // content bbox center) — not vertically centered in the reserved strip.
  const QString text = singleLineTitle(metadata.title);
  const qreal baseline = titleRect.bottom() - metadata.titleTopMargin;
  const qreal advance = measureMermaidTitleWidth(metadata);
  painter.drawText(QPointF(titleRect.center().x() - advance / 2.0, baseline),
                   text);
  painter.restore();
}

}  // namespace muffin::mermaid
