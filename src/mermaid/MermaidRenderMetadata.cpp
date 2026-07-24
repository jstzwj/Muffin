#include "mermaid/MermaidRenderMetadata.h"

#include "mermaid/theme/MermaidColor.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>

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
  return QFontMetricsF(titleFont(metadata)).horizontalAdvance(
      singleLineTitle(metadata.title));
}

void paintMermaidTitle(const MermaidRenderMetadata& metadata,
                       QPainter& painter, const QRectF& titleRect) {
  if (!metadata.hasVisibleTitle() || titleRect.isEmpty()) return;
  painter.save();
  painter.setFont(titleFont(metadata));
  painter.setPen(color::toQColor(metadata.titleColor));
  painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter |
                                  Qt::TextSingleLine,
                   singleLineTitle(metadata.title));
  painter.restore();
}

}  // namespace muffin::mermaid
