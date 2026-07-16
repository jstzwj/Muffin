#pragma once

#include "mermaid/flowchart/FlowLabel.h"

#include <QColor>
#include <QRectF>

class QPainter;

namespace muffin::mermaid::sequence {

enum class SequenceLabelKind { Participant, Message, Note, Fragment, Box };

struct SequenceLabelDocument {
  flowchart::FlowLabelDocument richText;
  bool markdown = false;
  SequenceLabelKind kind = SequenceLabelKind::Message;
};

using SequenceLabelVisualRun = flowchart::FlowLabelVisualRun;
using SequenceLabelLineMetrics = flowchart::FlowLabelLineMetrics;
using SequenceLabelLayoutMetrics = flowchart::FlowLabelLayoutMetrics;

SequenceLabelDocument parseSequenceLabel(const QString& source,
                                         SequenceLabelKind kind = SequenceLabelKind::Message);
SequenceLabelDocument wrapSequenceLabel(SequenceLabelDocument label,
                                        const QString& fontFamily,
                                        qreal fontPixelSize,
                                        qreal maximumWidth);
SequenceLabelLayoutMetrics layoutSequenceLabel(const SequenceLabelDocument& label,
                                               const QString& fontFamily,
                                               qreal fontPixelSize = 16.0,
                                               qreal lineHeight = 22.0);
void paintSequenceLabel(QPainter& painter, const SequenceLabelDocument& label,
                        const QRectF& rect, const QString& fontFamily,
                        qreal fontPixelSize, qreal lineHeight,
                        const QColor& color, bool centerVertically = true);

}  // namespace muffin::mermaid::sequence
