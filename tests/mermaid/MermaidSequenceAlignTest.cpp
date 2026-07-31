// Verifies sequence.messageAlign / sequence.noteAlign (upstream start/middle/end):
//   1. The paint mechanic places a label's ink per mermaid drawText() anchor
//      semantics, with the textMargin inset: left -> rect.left + margin,
//      center -> centered, right -> rect.right - margin.
//   2. The config reaches SequenceSceneStyle (default + invalid -> Center).
//   3. End-to-end, align changes the rendered output and default == explicit
//      center (byte-determinism), for both messages and notes.
//
// Mermaid drawText (sequenceDiagram-DXCB7GA4.mjs): for anchor left/center/right
// it sets x = round(x + textMargin) / round(x + width/2) / round(x + width -
// textMargin), where the rect is the message span [min(startx,stopx),
// max(startx,stopx)] for messages (textMargin = wrapPadding) and the note rect
// for notes (textMargin = noteMargin). We assert the painted ink position
// against that, not SVG-DOM attributes (Muffin's native SVG DOM != mermaid's).

#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceScene.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>

#include <QtGlobal>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  fprintf(stderr, "ALIGN TEST FAILED: %s\n", qPrintable(message));
  fflush(stderr);
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool value, const QString& message) {
  if (!value) fail(message);
}

// Painted-ink bounding box of a premultiplied ARGB image (alpha >= threshold).
QRect alphaBounds(const QImage& image, int alphaThreshold = 32) {
  QRect result;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      if (image.pixelColor(x, y).alpha() < alphaThreshold) continue;
      result = result.isNull() ? QRect(x, y, 1, 1) : result.united(QRect(x, y, 1, 1));
    }
  return result;
}

// Renders a short label into a wide rect with the given align + margin and
// returns the ink bbox. Rect, text, and margin are identical across calls.
QRect alignedLabelInk(flowchart::FlowLabelAlign align, qreal margin) {
  QImage image(420, 40, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::TextAntialiasing);
  const sequence::SequenceLabelDocument document = sequence::parseSequenceLabel(
      QStringLiteral("hi"), sequence::SequenceLabelKind::Message);
  const QRectF rect(50.0, 5.0, 300.0, 30.0);
  sequence::paintSequenceLabel(painter, document, rect, QStringLiteral("Noto Sans"),
                               16.0, 22.0, QColor(Qt::black), true, align, margin);
  return alphaBounds(image);
}

editor::MermaidRenderEntry renderEntry(const QString& source) {
  editor::MermaidRenderCache cache;
  return cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
}

const sequence::SequenceScene* sequenceScene(const QString& source) {
  const auto entry = renderEntry(source);
  return dynamic_cast<const sequence::SequenceScene*>(entry.scene.get());
}

QImage renderPng(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPngDataUrl(source, 2.0);
  const int comma = dataUrl.indexOf(QLatin1Char(','));
  require(comma > 0, QStringLiteral("Mermaid PNG data URL is malformed"));
  return QImage::fromData(
      QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
}

// Counts pixels whose premultiplied RGBA differs beyond a small AA tolerance.
// Unlike an alpha-only mask, this sees a text move over an opaque (e.g. yellow
// note) background, where only RGB changes.
int rgbaDiffPixels(const QImage& a, const QImage& b) {
  if (a.size() != b.size()) return -1;
  int diff = 0;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x) {
      const QColor ca = a.pixelColor(x, y);
      const QColor cb = b.pixelColor(x, y);
      const bool aa = ca.alpha() >= 16;
      const bool ab = cb.alpha() >= 16;
      if (aa != ab) { ++diff; continue; }
      if (!aa) continue;
      if (std::abs(ca.red() - cb.red()) + std::abs(ca.green() - cb.green()) +
              std::abs(ca.blue() - cb.blue()) >
          24)
        ++diff;
    }
  return diff;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  // --- 1. Geometric placement with the textMargin inset ---
  const qreal margin = 10.0;
  const QRectF alignRect(50.0, 5.0, 300.0, 30.0);
  const QRect left = alignedLabelInk(flowchart::FlowLabelAlign::Left, margin);
  const QRect center = alignedLabelInk(flowchart::FlowLabelAlign::Center, margin);
  const QRect right = alignedLabelInk(flowchart::FlowLabelAlign::Right, margin);
  require(left.width() > 0 && center.width() > 0 && right.width() > 0,
          QStringLiteral("Aligned label rendered no ink"));
  // Align is paint-only: the same text keeps the same ink width.
  require(qAbs(left.width() - center.width()) <= 1 &&
              qAbs(right.width() - center.width()) <= 1,
          QStringLiteral("Align changed the label ink width"));
  // Left -> ink begins at alignRect.left + margin; Right -> ink ends at
  // alignRect.right - margin (small glyph sidebearing slack).
  require(left.left() >= alignRect.left() + margin - 2 &&
              left.left() <= alignRect.left() + margin + 8,
          QStringLiteral("Left align ink not inset by margin: %1").arg(left.left()));
  require(right.right() <= alignRect.right() - margin + 2 &&
              right.right() >= alignRect.right() - margin - 8,
          QStringLiteral("Right align ink not inset by margin: %1").arg(right.right()));
  require(left.left() < center.left() && center.left() < right.left(),
          QStringLiteral("Align order wrong: L%1 C%2 R%3")
              .arg(left.left()).arg(center.left()).arg(right.left()));
  require(center.left() - left.left() > 40 && right.left() - center.left() > 40,
          QStringLiteral("Align placements not well separated"));
  require(qAbs((center.left() + center.right()) -
               (alignRect.left() + alignRect.right())) <= 3,
          QStringLiteral("Center align ink not centered"));

  // --- 2. Config reaches the scene; default + invalid -> Center ---
  {
    const auto* scene = sequenceScene(
        QStringLiteral("sequenceDiagram\nAlice->>Bob: hi\nNote over Bob: n"));
    require(scene != nullptr, QStringLiteral("Default sequence yielded no SequenceScene"));
    require(scene->style.messageAlign == flowchart::FlowLabelAlign::Center,
            QStringLiteral("Default messageAlign is not Center"));
    require(scene->style.noteAlign == flowchart::FlowLabelAlign::Center,
            QStringLiteral("Default noteAlign is not Center"));
  }
  {
    const auto* scene = sequenceScene(QStringLiteral(
        "%%{init: {\"sequence\": {\"messageAlign\": \"left\", "
        "\"noteAlign\": \"right\"}}}%%\n"
        "sequenceDiagram\nAlice->>Bob: hi\nNote over Bob: n"));
    require(scene != nullptr, QStringLiteral("Configured sequence yielded no SequenceScene"));
    require(scene->style.messageAlign == flowchart::FlowLabelAlign::Left,
            QStringLiteral("messageAlign:left did not reach the scene"));
    require(scene->style.noteAlign == flowchart::FlowLabelAlign::Right,
            QStringLiteral("noteAlign:right did not reach the scene"));
  }
  {
    const auto* scene = sequenceScene(QStringLiteral(
        "%%{init: {\"sequence\": {\"messageAlign\": \"diagonal\"}}}%%\n"
        "sequenceDiagram\nAlice->>Bob: hi"));
    require(scene != nullptr && scene->style.messageAlign == flowchart::FlowLabelAlign::Center,
            QStringLiteral("Invalid messageAlign did not fall back to Center"));
  }

  // --- 3. End-to-end: align changes the render; default == explicit center ---
  // Every render uses a wide actor span so left/center/right have room.
  const QString body = QStringLiteral(
      "sequenceDiagram\nAlice->>Bob: hello message\nNote over Alice,Bob: a note");
  const auto renderSeq = [&body](const QString& sequenceJson) {
    return QStringLiteral("%%{init: {\"sequence\": ") + sequenceJson +
           QStringLiteral("}}%%\n") + body;
  };
  const QString wide = QStringLiteral("{\"actorMargin\": 160}");
  const QImage defaultImg = renderPng(renderSeq(wide));
  const QImage msgLeft = renderPng(renderSeq(
      QStringLiteral("{\"actorMargin\": 160, \"messageAlign\": \"left\"}")));
  const QImage msgCenter = renderPng(renderSeq(
      QStringLiteral("{\"actorMargin\": 160, \"messageAlign\": \"center\"}")));
  const QImage msgRight = renderPng(renderSeq(
      QStringLiteral("{\"actorMargin\": 160, \"messageAlign\": \"right\"}")));
  const QImage noteLeft = renderPng(renderSeq(
      QStringLiteral("{\"actorMargin\": 160, \"noteAlign\": \"left\"}")));
  const QImage noteRight = renderPng(renderSeq(
      QStringLiteral("{\"actorMargin\": 160, \"noteAlign\": \"right\"}")));
  // Default (center) must be byte-identical to explicit center.
  require(rgbaDiffPixels(defaultImg, msgCenter) == 0,
          QStringLiteral("Default render differs from explicit messageAlign:center "
                         "(%1 px)").arg(rgbaDiffPixels(defaultImg, msgCenter)));
  // messageAlign moves the message text across the span.
  require(rgbaDiffPixels(msgLeft, msgCenter) > 20,
          QStringLiteral("messageAlign:left did not move the message text"));
  require(rgbaDiffPixels(msgRight, msgCenter) > 20,
          QStringLiteral("messageAlign:right did not move the message text"));
  require(rgbaDiffPixels(msgLeft, msgRight) > 20,
          QStringLiteral("messageAlign:left and :right rendered identically"));
  // noteAlign moves the note text within the (opaque yellow) note box.
  require(rgbaDiffPixels(noteLeft, defaultImg) > 20,
          QStringLiteral("noteAlign:left did not move the note text"));
  require(rgbaDiffPixels(noteRight, defaultImg) > 20,
          QStringLiteral("noteAlign:right did not move the note text"));
  require(rgbaDiffPixels(noteLeft, noteRight) > 20,
          QStringLiteral("noteAlign:left and :right rendered identically"));

  qDebug().noquote()
      << "MermaidSequenceAlignTest: messageAlign/noteAlign reach the scene "
         "(default=Center, invalid=Center); paint places ink at rect.left+margin "
         "/ centered / rect.right-margin; and end-to-end align moves the text "
         "while default stays byte-identical to explicit center.";
  return 0;
}
