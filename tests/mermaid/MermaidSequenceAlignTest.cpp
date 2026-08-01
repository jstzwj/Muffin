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
  // A persistent cache keeps the returned entry's shared_ptr<scene> alive after
  // this function returns; otherwise sequenceScene() would return a dangling
  // pointer into freed memory (use-after-free).
  static editor::MermaidRenderCache cache;
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
// vertical band ([alignRect.top, alignRect.bottom], above the message line).
// For a normal span the dashed actor lifelines at the span edges are excluded.
// For a zero-width self span, a window around the anchor is scanned with the
// lifeline column at the anchor excluded. Returns false if no text ink found.
bool messageTextRange(const QImage& img, const sequence::SequenceScene& scene,
                      int msgIdx, qreal dpr, qreal& outLeft, qreal& outRight) {
  const auto& msg = scene.messages.at(msgIdx);
  const QRectF vp = scene.viewportRect;
  const qreal excl = 5.0;
  const int y0 = qMax(0, qRound((msg.alignRect.top() - vp.top()) * dpr));
  const int y1 = qMin(img.height() - 1, qRound((msg.alignRect.bottom() - vp.top()) * dpr));
  qreal scanL, scanR;
  bool excludeCenter = false;
  qreal centerScene = 0.0;
  if (msg.alignRect.width() > 2.0 * excl) {
    scanL = msg.alignRect.left() + excl;
    scanR = msg.alignRect.right() - excl;
  } else {
    centerScene = msg.alignRect.center().x();
    scanL = centerScene - 220.0;
    scanR = centerScene + 220.0;
    excludeCenter = true;
  }
  const int xMin = qMax(0, qRound((scanL - vp.left()) * dpr));
  const int xMax = qMin(img.width() - 1, qRound((scanR - vp.left()) * dpr));
  const int exclImg = qRound(excl * dpr);
  const int centerImg = qRound((centerScene - vp.left()) * dpr);
  int xmin = img.width(), xmax = -1;
  for (int y = y0; y <= y1; ++y)
    for (int x = xMin; x <= xMax; ++x) {
      if (excludeCenter && std::abs(x - centerImg) <= exclImg) continue;
      if (img.pixelColor(x, y).alpha() >= 32) {
        if (x < xmin) xmin = x;
        if (x > xmax) xmax = x;
      }
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
  // Forward (Alice->>Bob) and reverse (Bob->>Alice, actors ordered so Bob is
  // right) both span the full message extent; a self message has zero width.
  // The reverse case uses a single reverse message so index 0 IS the reverse.
  {
    const auto check = [](const QString& src, int idx, const char* tag, bool self) {
      const auto* scene = sequenceScene(src);
      require(scene != nullptr && scene->messages.size() > idx,
              QStringLiteral("%1: no scene/message at idx %2").arg(QLatin1String(tag)).arg(idx));
      const auto& msg = scene->messages.at(idx);
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
    check(QStringLiteral("sequenceDiagram\nAlice->>Bob: hello message"), 0, "forward", false);
    check(QStringLiteral("sequenceDiagram\nparticipant Alice\nparticipant Bob\nBob->>Alice: reply"),
          0, "reverse", false);
    check(QStringLiteral("sequenceDiagram\nAlice->>Alice: hello message"), 0, "self", true);
  }

  // --- 4. End-to-end pixel extraction across message kinds ---
  // Forward and reverse: text left edge ~= alignRect.left + wrapPadding (left),
  // centered (center), right edge ~= alignRect.right - wrapPadding (right).
  // Self (zero-width span): left -> anchor + margin, center -> anchor,
  // right -> anchor - margin.
  {
    const auto msgSrc = [](const QString& body, const QString& align) {
      return QStringLiteral(
          "%%{init: {\"sequence\": {\"actorMargin\": 200, \"messageAlign\": \"") +
             align + QStringLiteral("\"}}}%%\n") + body;
    };
    const qreal tol = 5.0;
    const auto checkSpanBbox = [&](const QString& body, int idx, const char* tag) {
      const QString leftSrc = msgSrc(body, "left");
      const QString centerSrc = msgSrc(body, "center");
      const QString rightSrc = msgSrc(body, "right");
      const QImage leftImg = renderPng(leftSrc);
      const QImage centerImg = renderPng(centerSrc);
      const QImage rightImg = renderPng(rightSrc);
      const auto* scene = sequenceScene(leftSrc);
      require(scene != nullptr && scene->messages.size() > idx,
              QStringLiteral("%1: no message").arg(QLatin1String(tag)));
      const auto& msg = scene->messages.at(idx);
      const qreal margin = scene->style.wrapPadding;
      qreal lL = 0, lR = 0, cL = 0, cR = 0, rL = 0, rR = 0;
      require(messageTextRange(leftImg, *scene, idx, 2.0, lL, lR),
              QStringLiteral("%1: could not extract left text").arg(QLatin1String(tag)));
      require(messageTextRange(centerImg, *sequenceScene(centerSrc), idx, 2.0, cL, cR),
              QStringLiteral("%1: could not extract center text").arg(QLatin1String(tag)));
      require(messageTextRange(rightImg, *sequenceScene(rightSrc), idx, 2.0, rL, rR),
              QStringLiteral("%1: could not extract right text").arg(QLatin1String(tag)));
      require(std::abs(lL - (msg.alignRect.left() + margin)) <= tol,
              QStringLiteral("%1: left text %2 != alignRect.left+margin %3")
                  .arg(QLatin1String(tag)).arg(lL).arg(msg.alignRect.left() + margin));
      require(std::abs(rR - (msg.alignRect.right() - margin)) <= tol,
              QStringLiteral("%1: right text %2 != alignRect.right-margin %3")
                  .arg(QLatin1String(tag)).arg(rR).arg(msg.alignRect.right() - margin));
      require(std::abs((cL + cR) / 2.0 - msg.alignRect.center().x()) <= tol,
              QStringLiteral("%1: center midpoint %2 != alignRect center %3")
                  .arg(QLatin1String(tag)).arg((cL + cR) / 2.0).arg(msg.alignRect.center().x()));
      require(lL < cL - 5.0 && cL < rL - 5.0,
              QStringLiteral("%1: placements not distinct L%2 C%3 R%4")
                  .arg(QLatin1String(tag)).arg(lL).arg(cL).arg(rL));
    };
    checkSpanBbox(QStringLiteral("sequenceDiagram\nAlice->>Bob: hello message"), 0, "forward");
    checkSpanBbox(QStringLiteral(
        "sequenceDiagram\nparticipant Alice\nparticipant Bob\nBob->>Alice: reply"), 0, "reverse");

    // Self message: zero-width span -> text at anchor +/- margin.
    {
      const QString selfBody = QStringLiteral("sequenceDiagram\nAlice->>Alice: hello message");
      const QString sl = msgSrc(selfBody, "left");
      const QString sc = msgSrc(selfBody, "center");
      const QString sr = msgSrc(selfBody, "right");
      const auto* scene = sequenceScene(sl);
      require(scene != nullptr && !scene->messages.isEmpty(),
              QStringLiteral("self: no message"));
      const auto& msg = scene->messages.at(0);
      require(msg.alignRect.width() <= 1.0,
              QStringLiteral("self: alignRect not zero-width (%1)").arg(msg.alignRect.width()));
      const qreal anchor = msg.alignRect.center().x();
      const qreal margin = scene->style.wrapPadding;
      qreal lL = 0, lR = 0, cL = 0, cR = 0, rL = 0, rR = 0;
      require(messageTextRange(renderPng(sl), *scene, 0, 2.0, lL, lR),
              QStringLiteral("self: could not extract left text"));
      require(messageTextRange(renderPng(sc), *sequenceScene(sc), 0, 2.0, cL, cR),
              QStringLiteral("self: could not extract center text"));
      require(messageTextRange(renderPng(sr), *sequenceScene(sr), 0, 2.0, rL, rR),
              QStringLiteral("self: could not extract right text"));
      require(lL > anchor + margin - 6.0,
              QStringLiteral("self left text %1 not right of anchor+margin %2")
                  .arg(lL).arg(anchor + margin));
      require(rR < anchor - margin + 6.0,
              QStringLiteral("self right text %1 not left of anchor-margin %2")
                  .arg(rR).arg(anchor - margin));
      require(std::abs((cL + cR) / 2.0 - anchor) <= 7.0,
              QStringLiteral("self center %1 not on anchor %2").arg((cL + cR) / 2.0).arg(anchor));
    }

    // Default (center) byte-identical to explicit center.
    const QString fwdBody = QStringLiteral("sequenceDiagram\nAlice->>Bob: hello message");
    const QImage defaultImg = renderPng(
        QStringLiteral("%%{init: {\"sequence\": {\"actorMargin\": 200}}}%%\n") + fwdBody);
    require(rgbaDiffPixels(defaultImg, renderPng(msgSrc(fwdBody, "center"))) == 0,
            QStringLiteral("Default render differs from explicit messageAlign:center"));

    // noteAlign left/right move the note text (RGBA over opaque yellow bg).
    const QString noteBody = QStringLiteral(
        "sequenceDiagram\nAlice->>Bob: hi\nNote over Alice,Bob: a note");
    const auto noteSrc = [&noteBody](const QString& align) {
      return QStringLiteral(
          "%%{init: {\"sequence\": {\"actorMargin\": 200, \"noteAlign\": \"") +
             align + QStringLiteral("\"}}}%%\n") + noteBody;
    };
    const QImage noteDefault = renderPng(
        QStringLiteral("%%{init: {\"sequence\": {\"actorMargin\": 200}}}%%\n") + noteBody);
    const QImage noteLeft = renderPng(noteSrc("left"));
    const QImage noteRight = renderPng(noteSrc("right"));
    require(rgbaDiffPixels(noteLeft, noteDefault) > 20,
            QStringLiteral("noteAlign:left did not move the note text"));
    require(rgbaDiffPixels(noteRight, noteDefault) > 20,
            QStringLiteral("noteAlign:right did not move the note text"));
    require(rgbaDiffPixels(noteLeft, noteRight) > 20,
            QStringLiteral("noteAlign:left and :right rendered identically"));
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
