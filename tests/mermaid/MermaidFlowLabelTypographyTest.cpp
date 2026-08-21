// Foundational test for the Commit 2 neutral typography plumbing on
// FlowLabelDocument (baseStyle / letterSpacingPx / wordSpacingPx / underline /
// strikeOut / overline). Like baseWeight before it, every field defaults to a
// neutral value so default rendering is byte-identical; when raised, each must
// reach the WHOLE measurement chain — measure, wrap, layout, paint — via the
// single makeFlowLabelFont helper. This is family-agnostic (FlowLabel only);
// the Requirement text-CSS wiring that SETS these fields is a later commit.
//
// Font reality: the bundled Noto Sans ships only a Regular face, so Qt (like
// Chromium) synthesizes bold/italic and advances are less portable. Tests that
// need a real italic/bold face (italic reach, Markdown stacking) therefore use
// KaTeX_Main, which ships Regular/Italic/Bold/BoldItalic and is bundled —
// deterministic across the Windows/macOS/Linux/ASan CI matrix.
#include "math/MathFontRegistry.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QChar>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <utility>
#include <vector>

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

// Paint a label onto a fixed transparent canvas. Used for both pixel-count
// (decoration adds ink) and byte-for-byte image comparison (neutral == default,
// spacing/decoration change the painted output).
QImage renderLabelImage(const flowchart::FlowLabelDocument& doc,
                        const QString& family, qreal size, qreal lineHeight) {
  QImage image(480, 120, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  paintFlowLabel(painter, doc, QRectF(20, 20, 440, 80), family, size, lineHeight,
                 QColor(Qt::black), true);
  painter.end();
  return image;
}

qint64 inkPixelCount(const QImage& image) {
  qint64 count = 0;
  for (int y = 0; y < image.height(); ++y) {
    const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x)
      if (qAlpha(line[x]) > 0) ++count;
  }
  return count;
}

qint64 inkPixelCount(const flowchart::FlowLabelDocument& doc, const QString& family,
                     qreal size, qreal lineHeight) {
  return inkPixelCount(renderLabelImage(doc, family, size, lineHeight));
}

bool imagesIdentical(const QImage& a, const QImage& b) {
  if (a.size() != b.size() || a.format() != b.format()) return false;
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y),
                    a.bytesPerLine()) != 0)
      return false;
  }
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  muffin::math::MathFontRegistry::ensureLoaded();
  const qreal size = 16.0;
  const qreal lineHeight = 22.0;
  const QString text = QStringLiteral("testing wrapping here");

  const QString prodFamily = MermaidFontRegistry::cssFamilyStack();
  const QString realFamily = QStringLiteral("KaTeX_Main");
  log(QStringLiteral("prod family=%1 real family=%2").arg(prodFamily, realFamily));

  // 0. Neutral defaults — the guarantee that Commit 2 changes nothing.
  flowchart::FlowLabelDocument d;
  require(d.baseStyle == QFont::StyleNormal,
          QStringLiteral("default baseStyle must be StyleNormal"));
  require(d.letterSpacingPx == 0.0 && d.wordSpacingPx == 0.0,
          QStringLiteral("default letter/word spacing must be 0"));
  require(!d.underline && !d.strikeOut && !d.overline,
          QStringLiteral("default decoration flags must be false"));

  // 1. Explicit-neutral == default: layout metrics equal AND RGBA identical.
  flowchart::FlowLabelDocument explicitNeutral = plainDoc(text);
  explicitNeutral.baseStyle = QFont::StyleNormal;
  explicitNeutral.letterSpacingPx = 0.0;
  explicitNeutral.wordSpacingPx = 0.0;
  explicitNeutral.underline = explicitNeutral.strikeOut =
      explicitNeutral.overline = false;
  const QSizeF defaultSize =
      flowchart::measureFlowLabel(plainDoc(text), prodFamily, size, lineHeight);
  const QSizeF neutralSize =
      flowchart::measureFlowLabel(explicitNeutral, prodFamily, size, lineHeight);
  require(qFuzzyCompare(defaultSize.width(), neutralSize.width()) &&
              qFuzzyCompare(defaultSize.height(), neutralSize.height()),
          QStringLiteral("explicit-neutral layout must equal default: %1x%2 vs %3x%4")
              .arg(defaultSize.width()).arg(defaultSize.height())
              .arg(neutralSize.width()).arg(neutralSize.height()));
  require(imagesIdentical(renderLabelImage(plainDoc(text), prodFamily, size, lineHeight),
                          renderLabelImage(explicitNeutral, prodFamily, size, lineHeight)),
          QStringLiteral("explicit-neutral paint must be RGBA-identical to default"));

  // 2. makeFlowLabelFont carries weight/style faithfully, and the 2-arg
  //    (Requirement ex/ch) call still works with neutral metrics.
  const QFont neutralFont = flowchart::makeFlowLabelFont(prodFamily, size);
  require(neutralFont.weight() == QFont::Normal && neutralFont.style() == QFont::StyleNormal,
          QStringLiteral("2-arg makeFlowLabelFont must be Normal/Normal"));
  require(QFontMetricsF(neutralFont).xHeight() > 0.0 &&
              QFontMetricsF(neutralFont).horizontalAdvance(QChar('0')) > 0.0,
          QStringLiteral("2-arg makeFlowLabelFont ex/ch metrics must be valid"));
  const QFont italicFont = flowchart::makeFlowLabelFont(realFamily, size, QFont::Normal, QFont::StyleItalic);
  const QFont obliqueFont = flowchart::makeFlowLabelFont(realFamily, size, QFont::Normal, QFont::StyleOblique);
  require(italicFont.style() == QFont::StyleItalic,
          QStringLiteral("makeFlowLabelFont must carry StyleItalic"));
  require(obliqueFont.style() == QFont::StyleOblique,
          QStringLiteral("makeFlowLabelFont must carry StyleOblique"));
  require(italicFont.style() != obliqueFont.style(),
          QStringLiteral("StyleItalic vs StyleOblique distinction must be preserved"));

  // 3. Italic reaches measure + paint (KaTeX_Main real Italic face -> deterministic).
  {
    flowchart::FlowLabelDocument italicDoc = plainDoc(text);
    italicDoc.baseStyle = QFont::StyleItalic;
    const qreal normalAdv = flowchart::measureFlowTextAdvanceWidth(
        plainDoc(text), 0, text.size(), realFamily, size);
    const qreal italicAdv = flowchart::measureFlowTextAdvanceWidth(
        italicDoc, 0, text.size(), realFamily, size);
    log(QStringLiteral("italic advance normal=%1 italic=%2").arg(normalAdv).arg(italicAdv));
    require(!qFuzzyCompare(normalAdv, italicAdv),
            QStringLiteral("italic must change advance (real face); got %1 vs %2")
                .arg(normalAdv).arg(italicAdv));
    const qint64 normalPx = inkPixelCount(plainDoc(text), realFamily, size, lineHeight);
    const qint64 italicPx = inkPixelCount(italicDoc, realFamily, size, lineHeight);
    require(normalPx != italicPx,
            QStringLiteral("italic must change paint; got %1 vs %2").arg(normalPx).arg(italicPx));
    const flowchart::FlowLabelLayoutMetrics layout =
        flowchart::layoutFlowLabel(italicDoc, realFamily, size, lineHeight);
    require(std::any_of(layout.lines.cbegin(), layout.lines.cend(),
                        [](const flowchart::FlowLabelLineMetrics& line) {
                          return std::any_of(line.runs.cbegin(), line.runs.cend(),
                              [](const flowchart::FlowLabelVisualRun& run) {
                                return !run.math && run.fontItalic;
                              });
                        }),
            QStringLiteral("italic must reach the shaped runs (fontItalic)"));
  }

  // 4. letterSpacing reaches measure + wrap + paint simultaneously.
  {
    flowchart::FlowLabelDocument spacedDoc = plainDoc(text);
    spacedDoc.letterSpacingPx = 2.0;
    const qreal normalAdv = flowchart::measureFlowTextAdvanceWidth(
        plainDoc(text), 0, text.size(), prodFamily, size);
    const qreal spacedAdv = flowchart::measureFlowTextAdvanceWidth(
        spacedDoc, 0, text.size(), prodFamily, size);
    log(QStringLiteral("letterSpacing advance normal=%1 spaced=%2").arg(normalAdv).arg(spacedAdv));
    require(spacedAdv > normalAdv,
            QStringLiteral("letterSpacing must widen advance; got %1 vs %2").arg(spacedAdv).arg(normalAdv));
    const qreal wrapWidth = (normalAdv + spacedAdv) / 2.0;
    require(flowchart::wrapFlowLabel(plainDoc(text), prodFamily, size, wrapWidth).visualLines.isEmpty(),
            QStringLiteral("normal at the midpoint width must stay one line"));
    require(!flowchart::wrapFlowLabel(spacedDoc, prodFamily, size, wrapWidth).visualLines.isEmpty(),
            QStringLiteral("spaced at the midpoint width must wrap (letterSpacing reaches wrap)"));
    require(!imagesIdentical(renderLabelImage(plainDoc(text), prodFamily, size, lineHeight),
                             renderLabelImage(spacedDoc, prodFamily, size, lineHeight)),
            QStringLiteral("letterSpacing must change the painted output"));
  }

  // 5. wordSpacing widens text WITH spaces by ~px-per-space, and leaves
  //    space-less text untouched.
  {
    const QString withSpaces = QStringLiteral("a b c");  // 2 spaces
    const QString noSpaces = QStringLiteral("abc");
    auto advance = [&](const QString& t, qreal wordSpacing) {
      flowchart::FlowLabelDocument doc = plainDoc(t);
      doc.wordSpacingPx = wordSpacing;
      return flowchart::measureFlowTextAdvanceWidth(doc, 0, t.size(), prodFamily, size);
    };
    const qreal withBase = advance(withSpaces, 0.0);
    const qreal withWide = advance(withSpaces, 4.0);
    require(qAbs((withWide - withBase) - 8.0) < 1.0,
            QStringLiteral("wordSpacing must add ~4px per space (2 spaces); got delta %1")
                .arg(withWide - withBase));
    require(qFuzzyCompare(advance(noSpaces, 0.0), advance(noSpaces, 4.0)),
            QStringLiteral("wordSpacing must NOT affect text without spaces"));
  }

  // 6. underline/strikeout/overline add pixels but never change measure size.
  {
    const QSizeF plainSize =
        flowchart::measureFlowLabel(plainDoc(text), prodFamily, size, lineHeight);
    const qint64 plainPx = inkPixelCount(plainDoc(text), prodFamily, size, lineHeight);
    const std::vector<std::pair<QString, std::function<void(flowchart::FlowLabelDocument&)>>> flags = {
        {QStringLiteral("underline"), [](flowchart::FlowLabelDocument& d) { d.underline = true; }},
        {QStringLiteral("strikeOut"), [](flowchart::FlowLabelDocument& d) { d.strikeOut = true; }},
        {QStringLiteral("overline"),  [](flowchart::FlowLabelDocument& d) { d.overline = true; }},
    };
    for (const auto& [name, apply] : flags) {
      flowchart::FlowLabelDocument decoDoc = plainDoc(text);
      apply(decoDoc);
      const QSizeF decoSize =
          flowchart::measureFlowLabel(decoDoc, prodFamily, size, lineHeight);
      require(qFuzzyCompare(decoSize.width(), plainSize.width()) &&
                  qFuzzyCompare(decoSize.height(), plainSize.height()),
              QStringLiteral("%1 must not change measure size: %2x%3 vs %4x%5")
                  .arg(name).arg(decoSize.width()).arg(decoSize.height())
                  .arg(plainSize.width()).arg(plainSize.height()));
      const qint64 decoPx = inkPixelCount(decoDoc, prodFamily, size, lineHeight);
      require(decoPx > plainPx,
              QStringLiteral("%1 must add painted pixels; got %2 vs %3").arg(name).arg(decoPx).arg(plainPx));
    }
  }

  // 7. Markdown bold/italic ranges STACK on the base style (not replace it).
  //    Proven by contrast: a bold range on a normal base is not italic, but on
  //    an italic base it becomes bold+italic (and symmetric for italic range).
  {
    auto runStyle = [&](const flowchart::FlowLabelDocument& doc,
                        const QString& family) -> std::pair<int, bool> {
      const flowchart::FlowLabelLayoutMetrics layout =
          flowchart::layoutFlowLabel(doc, family, size, lineHeight);
      for (const auto& line : layout.lines)
        for (const auto& run : line.runs)
          if (!run.math) return {run.fontWeight, run.fontItalic};
      return {QFont::Normal, false};
    };
    // base italic + markdown bold -> bold AND italic.
    flowchart::FlowLabelDocument boldNormalBase =
        flowchart::parseFlowLabel(QStringLiteral("**bold**"), QStringLiteral("markdown"));
    flowchart::FlowLabelDocument boldItalicBase = boldNormalBase;
    boldItalicBase.baseStyle = QFont::StyleItalic;
    const auto [bnW, bnI] = runStyle(boldNormalBase, realFamily);
    const auto [biW, biI] = runStyle(boldItalicBase, realFamily);
    log(QStringLiteral("markdown bold on normal base: w=%1 i=%2; on italic base: w=%3 i=%4")
            .arg(bnW).arg(bnI).arg(biW).arg(biI));
    require(bnW >= QFont::Bold && !bnI,
            QStringLiteral("markdown bold on normal base must be bold, not italic"));
    require(biW >= QFont::Bold && biI,
            QStringLiteral("base italic must stack onto markdown bold (bold+italic)"));
    // base bold + markdown italic -> bold AND italic.
    flowchart::FlowLabelDocument italicNormalBase =
        flowchart::parseFlowLabel(QStringLiteral("*em*"), QStringLiteral("markdown"));
    flowchart::FlowLabelDocument italicBoldBase = italicNormalBase;
    italicBoldBase.baseWeight = QFont::Bold;
    const auto [inW, inI] = runStyle(italicNormalBase, realFamily);
    const auto [ibW, ibI] = runStyle(italicBoldBase, realFamily);
    log(QStringLiteral("markdown italic on normal base: w=%1 i=%2; on bold base: w=%3 i=%4")
            .arg(inW).arg(inI).arg(ibW).arg(ibI));
    require(inI && inW < QFont::Bold,
            QStringLiteral("markdown italic on normal base must be italic, not bold"));
    require(ibI && ibW >= QFont::Bold,
            QStringLiteral("base bold must stack onto markdown italic (bold+italic)"));
  }

  log("ALL PASS");
  return 0;
}
