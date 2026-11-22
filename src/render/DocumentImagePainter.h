#pragma once

class QImage;
class QPainter;
class QRectF;

namespace muffin {

void paintDocumentImage(QPainter& painter, const QRectF& target, const QImage& original);

} // namespace muffin
