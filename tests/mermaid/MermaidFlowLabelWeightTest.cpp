// Foundational test for FlowLabelDocument::baseWeight (commit 1 of the sequence
// per-label font-weight feature). baseWeight defaults to Normal (so default
// rendering is byte-identical), and when raised it must reach the WHOLE
// measurement chain — measure, wrap and paint — via the single makeFlowLabelFont
// helper, so wrap/layout/paint agree with the drawn font. This is family-agnostic
// (FlowLabel only); the sequence per-kind wiring is in MermaidSequenceWeightTest.
//
// Font reality: the bundled Noto Sans has no Bold face, so Qt (like Chromium)
// synthesizes bold from the Regular face and PRESERVES horizontal advances —
// bold changes ink/stroke but not advance, layout or advance-based wrap. So for
// the production font we prove ink-measure + paint change and advances stay
// equal (the Chromium-matching invariant). Wrap is then proven separately with a
// font that has a real Bold face (Segoe UI), whose advances differ with weight.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>

#include <cstdlib>
#include <iostream>

using namespace muffin::mermaid;

namespace {
void log(const QString& message) {
  std::cout << message.toStdString() << std::endl;
}
[[noreturn]] void fail(const QString& message) {
  std::cout << "FAIL: " << message.toStdString() << std::endl;
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

flowchart::FlowLabelDocument plainDoc(const QString& text) {
  flowchart::FlowLabelDocument doc;
  doc.text = text;
  doc.direction = Qt::LeftToRight;
  return doc;
}

qint64 inkPixelCount(const flowchart::FlowLabelDocument& doc, const QString& family,
                     qreal size, qreal lineHeight) {
  QImage image(420, 90, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  paintFlowLabel(painter, doc, QRectF(10, 10, 400, 70), family, size, lineHeight,
                 QColor(Qt::black), true);
  painter.end();
  qint64 count = 0;
  for (int y = 0; y < image.height(); ++y) {
    const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x)
      if (qAlpha(line[x]) > 0) ++count;
  }
  return count;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const qreal size = 16.0;
  const qreal lineHeight = 22.0;
  const QString text = QStringLiteral("testing wrapping here");

  // Default baseWeight is Normal — the guarantee that commit 1 changes nothing.
  flowchart::FlowLabelDocument normalDoc = plainDoc(text);
  require(normalDoc.baseWeight == QFont::Normal,
          QStringLiteral("default baseWeight must be Normal, got %1")
              .arg(normalDoc.baseWeight));

  flowchart::FlowLabelDocument boldDoc = plainDoc(text);
  boldDoc.baseWeight = QFont::Bold;

  // --- Production font (bundled Noto Sans) ---------------------------------
  const QString prodFamily = MermaidFontRegistry::cssFamilyStack();
  log(QStringLiteral("prod family=%1").arg(prodFamily));

  // 1. Ink measurement: bold strictly exceeds normal (synthetic bold widens the
  //    stroke). This is the metric the sequence ink-based wrap consumes.
  const qreal prodNormalInk =
      flowchart::measureFlowTextInkWidth(normalDoc, prodFamily, size);
  const qreal prodBoldInk =
      flowchart::measureFlowTextInkWidth(boldDoc, prodFamily, size);
  log(QStringLiteral("prod ink normal=%1 bold=%2").arg(prodNormalInk).arg(prodBoldInk));
  require(prodBoldInk > prodNormalInk,
          QStringLiteral("prod bold ink %1 must exceed normal %2")
              .arg(prodBoldInk).arg(prodNormalInk));

  // 2. Advance invariance: synthetic bold (like Chromium) keeps the Regular
  //    advance table, so advances are equal — bold changes ink, not layout.
  const qreal prodNormalAdv = flowchart::measureFlowTextAdvanceWidth(
      normalDoc, 0, normalDoc.text.size(), prodFamily, size);
  const qreal prodBoldAdv = flowchart::measureFlowTextAdvanceWidth(
      boldDoc, 0, boldDoc.text.size(), prodFamily, size);
  log(QStringLiteral("prod advance normal=%1 bold=%2").arg(prodNormalAdv).arg(prodBoldAdv));
  require(qFuzzyCompare(prodNormalAdv, prodBoldAdv),
          QStringLiteral("prod advances must be equal (synthetic bold), got %1 vs %2")
              .arg(prodNormalAdv).arg(prodBoldAdv));

  // 3. Paint: bold lays down strictly more ink than normal.
  const qint64 prodNormalPx = inkPixelCount(normalDoc, prodFamily, size, lineHeight);
  const qint64 prodBoldPx = inkPixelCount(boldDoc, prodFamily, size, lineHeight);
  log(QStringLiteral("prod inkPixels normal=%1 bold=%2").arg(prodNormalPx).arg(prodBoldPx));
  require(prodBoldPx > prodNormalPx,
          QStringLiteral("prod bold ink pixels %1 must exceed normal %2")
              .arg(prodBoldPx).arg(prodNormalPx));

  // --- Real-bold font (Segoe UI) ------------------------------------------
  // Proves wrapFlowLabel consumes baseWeight: with a real Bold face, bold
  // advances exceed normal, so at a width between them normal stays one line
  // while bold wraps.
  const QString realFamily = QStringLiteral("Segoe UI");
  const qreal realNormalAdv = flowchart::measureFlowTextAdvanceWidth(
      normalDoc, 0, normalDoc.text.size(), realFamily, size);
  const qreal realBoldAdv = flowchart::measureFlowTextAdvanceWidth(
      boldDoc, 0, boldDoc.text.size(), realFamily, size);
  log(QStringLiteral("real advance normal=%1 bold=%2").arg(realNormalAdv).arg(realBoldAdv));
  require(realBoldAdv > realNormalAdv,
          QStringLiteral("real-bold advance %1 must exceed normal %2 (need a font with a Bold face)")
              .arg(realBoldAdv).arg(realNormalAdv));
  const qreal wrapWidth = (realNormalAdv + realBoldAdv) / 2.0;
  const flowchart::FlowLabelDocument realNormalWrapped =
      flowchart::wrapFlowLabel(normalDoc, realFamily, size, wrapWidth);
  const flowchart::FlowLabelDocument realBoldWrapped =
      flowchart::wrapFlowLabel(boldDoc, realFamily, size, wrapWidth);
  log(QStringLiteral("wrap normalLines=%1 boldLines=%2")
          .arg(realNormalWrapped.visualLines.size()).arg(realBoldWrapped.visualLines.size()));
  require(realNormalWrapped.visualLines.isEmpty(),
          QStringLiteral("real-normal at the midpoint width must stay one line"));
  require(!realBoldWrapped.visualLines.isEmpty(),
          QStringLiteral("real-bold at the midpoint width must wrap (wrapFlowLabel consumes baseWeight)"));

  log("ALL PASS");
  return 0;
}
