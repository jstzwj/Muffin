// Verifies sequence.messageAlign / sequence.noteAlign (upstream start/middle/end):
//   1. The paint mechanic places a label's ink per mermaid drawText() anchor
//      semantics, with the textMargin inset: left -> rect.left + margin,
//      center -> centered, right -> rect.right - margin.
//   2. The config reaches SequenceSceneStyle (default + invalid -> Center).
//   3. Structural: the message aligns across the full span [min(startX,stopX),
//      max(startX,stopX)] (alignRect), which is much wider than the text-width
//      labelRect — for forward, reverse and self messages.
//   4. End-to-end pixel extraction: the rendered message text ink sits at
//      alignRect.left + wrapPadding / centered / alignRect.right - wrapPadding.
//   5. Math labels (Note/Message with $$...$$) stay centered regardless of align
//      (mermaid drawKatex ignores the anchor), and default == explicit center.
//
// Mermaid drawText: left -> x+textMargin, center -> x+width/2, right ->
// x+width-textMargin, rect = message span / note rect, textMargin =
// wrapPadding / noteMargin. drawKatex centers directly. Assertions are
// geometric (ink position), not SVG-DOM attributes.

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
#include <algorithm>
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

QRect alphaBounds(const QImage& image, int alphaThreshold = 32) {
  QRect result;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      if (image.pixelColor(x, y).alpha() < alphaThreshold) continue;
      result = result.isNull() ? QRect(x, y, 1, 1) : result.united(QRect(x, y, 1, 1));
    }
  return result;
}

// Isolated label render (transparent background) with a given align + margin.
QRect alignedLabelInk(flowchart::FlowLabelAlign align, qreal margin) {
  QImage image(440, 40, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::TextAntialiasing);
  const sequence::SequenceLabelDocument document = sequence::parseSequenceLabel(
      QStringLiteral("hello message"), sequence::SequenceLabelKind::Message);
  const QRectF rect(50.0, 5.0, 320.0, 30.0);
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

// Message text ink x-range in SCENE coords, scanned across the message's text
// vertical band ([alignRect.top, alignRect.bottom], which is above the message
// line) and excluding a small zone at the span edges so the dashed actor
// lifelines do not pollute the left/right edges. Returns false if no text ink.
bool messageTextRange(const QImage& img, const sequence::SequenceScene& scene,
                      int msgIdx, qreal dpr, qreal& outLeft, qreal& outRight) {
  const auto& msg = scene.messages.at(msgIdx);
  const QRectF vp = scene.viewportRect;
  const qreal sceneEdgeExclude = 5.0;  // drop lifeline columns at span edges
  const int y0 = qMax(0, qRound((msg.alignRect.top() - vp.top()) * dpr));
  const int y1 = qMin(img.height() - 1, qRound((msg.alignRect.bottom() - vp.top()) * dpr));
  const int xMin = qRound((msg.alignRect.left() - vp.left() + sceneEdgeExclude) * dpr);
  const int xMax = qRound((msg.alignRect.right() - vp.left() - sceneEdgeExclude) * dpr);
  int xmin = img.width(), xmax = -1;
  for (int y = y0; y <= y1; ++y)
    for (int x = xMin; x <= xMax; ++x)
      if (img.pixelColor(x, y).alpha() >= 32) {
        if (x < xmin) xmin = x;
        if (x > xmax) xmax = x;
      }
  if (xmax < 0) return false;
  outLeft = xmin / dpr + vp.left();
  outRight = xmax / dpr + vp.left();
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  // --- 1. Isolated geometric placement with the textMargin inset ---
  {
    const qreal margin = 10.0;
    const QRectF rect(50.0, 5.0, 320.0, 30.0);
    const QRect left = alignedLabelInk(flowchart::FlowLabelAlign::Left, margin);
    const QRect center = alignedLabelInk(flowchart::FlowLabelAlign::Center, margin);
    const QRect right = alignedLabelInk(flowchart::FlowLabelAlign::Right, margin);
    require(left.width() > 0 && center.width() > 0 && right.width() > 0,
            QStringLiteral("Aligned label rendered no ink"));
    require(qAbs(left.width() - center.width()) <= 1 &&
                qAbs(right.width() - center.width()) <= 1,
            QStringLiteral("Align changed the label ink width"));
    require(left.left() >= rect.left() + margin - 2 &&
                left.left() <= rect.left() + margin + 4,
            QStringLiteral("Left align ink not inset by margin: %1").arg(left.left()));
    require(right.right() <= rect.right() - margin + 2 &&
                right.right() >= rect.right() - margin - 4,
            QStringLiteral("Right align ink not inset by margin: %1").arg(right.right()));
    require(left.left() < center.left() && center.left() < right.left(),
            QStringLiteral("Align order wrong: L%1 C%2 R%3")
                .arg(left.left()).arg(center.left()).arg(right.left()));
    require(qAbs((center.left() + center.right()) -
                 (rect.left() + rect.right())) <= 3,
            QStringLiteral("Center align ink not centered"));
  }

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

  // --- 3. Structural: alignRect is the full span, wider than the text box ---
  // Forward (Alice left -> Bob right) and reverse (Bob -> Alice) both span the
  // full message extent; a self message has a zero-width span.
  {
    const auto check = [](const QString& src, const char* tag, bool self) {
      const auto* scene = sequenceScene(src);
      require(scene != nullptr && !scene->messages.isEmpty(),
              QStringLiteral("%1: no scene/messages").arg(QLatin1String(tag)));
      const auto& msg = scene->messages.at(0);
      const qreal spanLeft = std::min(msg.startX, msg.stopX) - 6.0;  // arrow offset slack
      const qreal spanRight = std::max(msg.startX, msg.stopX) + 6.0;
      require(qAbs(msg.alignRect.center().x() - (std::min(msg.startX, msg.stopX) +
                   std::max(msg.startX, msg.stopX)) / 2.0) < 1.0,
              QStringLiteral("%1: alignRect not centered on the span").arg(QLatin1String(tag)));
      if (self) {
        require(msg.alignRect.width() <= 1.0,
                QStringLiteral("%1: self-message alignRect not zero-width (%2)")
                    .arg(QLatin1String(tag)).arg(msg.alignRect.width()));
      } else {
        require(msg.alignRect.left() >= spanLeft && msg.alignRect.right() <= spanRight,
                QStringLiteral("%1: alignRect does not cover the span"));
        require(msg.alignRect.width() > msg.labelRect.width() + 40.0,
                QStringLiteral("%1: alignRect not wider than labelRect (%2 > %3)")
                    .arg(QLatin1String(tag)).arg(msg.alignRect.width()).arg(msg.labelRect.width()));
      }
    };
    check(QStringLiteral("sequenceDiagram\nAlice->>Bob: hello message"), "forward", false);
    check(QStringLiteral("sequenceDiagram\nAlice->>Bob: hi\nBob->>Alice: reply"), "reverse", false);
    check(QStringLiteral("sequenceDiagram\nAlice->>Alice: hello message"), "self", true);
  }

  // --- 4. End-to-end pixel extraction: message text ink at span edges + margin ---
  {
    const QString body = QStringLiteral(
        "sequenceDiagram\nAlice->>Bob: hello message\nNote over Alice,Bob: a note");
    const auto renderSeq = [&body](const QString& sequenceJson) {
      return QStringLiteral("%%{init: {\"sequence\": ") + sequenceJson +
             QStringLiteral("}}%%\n") + body;
    };
    const QString wide = QStringLiteral("{\"actorMargin\": 200}");
    const QString leftSrc = renderSeq(
        QStringLiteral("{\"actorMargin\": 200, \"messageAlign\": \"left\"}"));
    const QString centerSrc = renderSeq(
        QStringLiteral("{\"actorMargin\": 200, \"messageAlign\": \"center\"}"));
    const QString rightSrc = renderSeq(
        QStringLiteral("{\"actorMargin\": 200, \"messageAlign\": \"right\"}"));

    const QImage leftImg = renderPng(leftSrc);
    const QImage centerImg = renderPng(centerSrc);
    const QImage rightImg = renderPng(rightSrc);
    const QImage defaultImg = renderPng(renderSeq(wide));

    require(!alphaBounds(leftImg).isNull(), QStringLiteral("message render produced no ink"));

    const auto* scene = sequenceScene(leftSrc);
    require(scene != nullptr && !scene->messages.isEmpty(), QStringLiteral("no scene"));
    const auto& msg = scene->messages.at(0);
    const qreal margin = scene->style.wrapPadding;
    const qreal tol = 5.0;

    qreal lLeft = 0, lRight = 0, cLeft = 0, cRight = 0, rLeft = 0, rRight = 0;
    require(messageTextRange(leftImg, *scene, 0, 2.0, lLeft, lRight),
            QStringLiteral("could not extract left-aligned message text"));
    require(messageTextRange(centerImg, *sequenceScene(centerSrc), 0, 2.0, cLeft, cRight),
            QStringLiteral("could not extract center-aligned message text"));
    require(messageTextRange(rightImg, *sequenceScene(rightSrc), 0, 2.0, rLeft, rRight),
            QStringLiteral("could not extract right-aligned message text"));

    // Left align: text left edge ~= alignRect.left + margin.
    require(std::abs(lLeft - (msg.alignRect.left() + margin)) <= tol,
            QStringLiteral("left render: text left %1 != alignRect.left+margin %2")
                .arg(lLeft).arg(msg.alignRect.left() + margin));
    // Right align: text right edge ~= alignRect.right - margin.
    require(std::abs(rRight - (msg.alignRect.right() - margin)) <= tol,
            QStringLiteral("right render: text right %1 != alignRect.right-margin %2")
                .arg(rRight).arg(msg.alignRect.right() - margin));
    // Center: text midpoint ~= alignRect center, and left/right edges symmetric.
    require(std::abs((cLeft + cRight) / 2.0 - msg.alignRect.center().x()) <= tol,
            QStringLiteral("center render: text midpoint %1 != alignRect center %2")
                .arg((cLeft + cRight) / 2.0).arg(msg.alignRect.center().x()));
    // Distinct placements (not a sub-pixel rounding artifact).
    require(lLeft < cLeft - 5.0 && cLeft < rLeft - 5.0,
            QStringLiteral("message placements not distinct: L%1 C%2 R%3")
                .arg(lLeft).arg(cLeft).arg(rLeft));
    // Default (center) must be byte-identical to explicit center.
    require(rgbaDiffPixels(defaultImg, centerImg) == 0,
            QStringLiteral("Default render differs from explicit center"));
    // noteAlign still moves the note text (RGBA, opaque yellow bg).
    const QImage noteLeft = renderPng(renderSeq(
        QStringLiteral("{\"actorMargin\": 200, \"noteAlign\": \"left\"}")));
    require(rgbaDiffPixels(noteLeft, defaultImg) > 20,
            QStringLiteral("noteAlign:left did not move the note text"));
  }

  // --- 5. Math labels stay centered regardless of align (drawKatex) ---
  // Each align is exercised in isolation so a neighboring plain-text label
  // (which DOES honor align) cannot mask the math label's invariance.
  {
    const auto mathMsgUnchanged = [](const QString& msg) {
      const auto src = [&msg](const QString& align) {
        return QStringLiteral("%%{init: {\"sequence\": {\"messageAlign\": \"") + align +
               QStringLiteral("\"}}}%%\nsequenceDiagram\nAlice->>Bob: ") + msg;
      };
      const QImage left = renderPng(src("left"));
      const QImage center = renderPng(src("center"));
      const QImage right = renderPng(src("right"));
      require(rgbaDiffPixels(left, center) == 0 && rgbaDiffPixels(right, center) == 0,
              QStringLiteral("Math message changed with messageAlign: %1").arg(msg));
    };
    const auto mathNoteUnchanged = [](const QString& note) {
      const auto src = [&note](const QString& align) {
        return QStringLiteral("%%{init: {\"sequence\": {\"noteAlign\": \"") + align +
               QStringLiteral("\"}}}%%\nsequenceDiagram\nAlice->>Bob: hi\nNote over Alice,Bob: ") +
               note;
      };
      const QImage left = renderPng(src("left"));
      const QImage center = renderPng(src("center"));
      const QImage right = renderPng(src("right"));
      require(rgbaDiffPixels(left, center) == 0 && rgbaDiffPixels(right, center) == 0,
              QStringLiteral("Math note changed with noteAlign: %1").arg(note));
    };
    mathMsgUnchanged(QStringLiteral("$$x^2 + y^2 = r^2$$"));
    mathMsgUnchanged(QStringLiteral("value $$x^2$$ here"));
    mathNoteUnchanged(QStringLiteral("$$E=mc^2$$"));
    mathNoteUnchanged(QStringLiteral("energy $$E$$ now"));
  }

  qDebug().noquote()
      << "MermaidSequenceAlignTest: messageAlign/noteAlign reach the scene "
         "(default/invalid=Center); paint places ink at rect.left+margin / "
         "centered / rect.right-margin; the message aligns across the full span "
         "(alignRect >> labelRect), verified by pixel extraction for "
         "left/center/right; Math labels stay centered; default == explicit "
         "center byte-for-byte.";
  return 0;
}
