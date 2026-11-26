// Hit testing in a line of mixed direction.
//
// InlineLayout::textLayoutHitForPoint() used to scan the line left to right and take the first
// caret to the right of the click. That is the nearest caret only when carets are ordered like
// the text: in a right-to-left run the carets move the other way, and for a paragraph whose
// base direction is right-to-left the first caret sits at the far right, so every click landed
// on the start of the line.

#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QVector>

#include <algorithm>
#include <cmath>

#include "../TestUtils.h"

using namespace muffin;

namespace {

struct Caret {
  qsizetype offset = 0;
  qreal x = 0.0;
  qreal y = 0.0;
};

void buildSingleLine(InlineLayout& layout, const QString& text, const RenderTheme& theme) {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(text));
  // Wide enough that nothing wraps: every caret below belongs to the same visual line.
  layout.build(inlines, theme, 4000.0, theme.paragraphFont());
}

// The carets Qt paints for the line, sorted by their x position.
QVector<Caret> visualCarets(const InlineLayout& layout, const QString& label) {
  QVector<Caret> carets;
  const qsizetype length = layout.displayText().size();
  for (qsizetype offset = 0; offset <= length; ++offset) {
    const QRectF rect = layout.cursorRect(offset);
    require(!rect.isEmpty(), QStringLiteral("%1: caret %2 should exist").arg(label).arg(offset));
    carets.append({offset, rect.left(), rect.center().y()});
  }
  std::sort(carets.begin(), carets.end(), [](const Caret& a, const Caret& b) { return a.x < b.x; });
  return carets;
}

qsizetype hitAt(const InlineLayout& layout, qreal x, qreal y) {
  return layout.hitTestTextOffset(QPointF(x, y));
}

// offset@x, in visual order: what a failure message needs to say where every caret is.
QString caretTable(const QVector<Caret>& carets) {
  QStringList entries;
  for (const Caret& caret : carets)
    entries.append(QStringLiteral("%1@%2").arg(caret.offset).arg(caret.x, 0, 'f', 2));
  return entries.join(QLatin1Char(' '));
}

// Clicking exactly where a caret is drawn places the caret there. Two logical carets can share
// one x position (the boundary between runs of opposite direction); either is then correct.
void requireCaretsRoundTrip(const InlineLayout& layout, const QString& label) {
  const QVector<Caret> carets = visualCarets(layout, label);
  for (const Caret& caret : carets) {
    const qsizetype hit = hitAt(layout, caret.x, caret.y);
    const bool sharesPosition = std::any_of(carets.cbegin(), carets.cend(), [&](const Caret& other) {
      return other.offset == hit && qAbs(other.x - caret.x) < 0.5;
    });
    require(sharesPosition, QStringLiteral("%1: click at caret %2 (x=%3) hit offset %4")
                                .arg(label)
                                .arg(caret.offset)
                                .arg(caret.x)
                                .arg(hit));
  }
}

// Between two neighbouring carets, the click resolves to the nearer one. Two carets can share
// an x position (the boundary between runs of opposite direction, or a character cluster such
// as a surrogate pair); either of them is then the nearest.
void requireNearestCaretWins(const InlineLayout& layout, const QString& label) {
  const QVector<Caret> carets = visualCarets(layout, label);
  const auto landsOn = [&](qsizetype hit, const Caret& caret) {
    return std::any_of(carets.cbegin(), carets.cend(),
                       [&](const Caret& other) { return other.offset == hit && qAbs(other.x - caret.x) < 0.5; });
  };
  for (int i = 1; i < carets.size(); ++i) {
    const Caret& left = carets.at(i - 1);
    const Caret& right = carets.at(i);
    if (right.x - left.x < 1.0)
      continue;
    const qreal width = right.x - left.x;
    // At the quarters of the cell and near both of its ends: wherever the click falls, the
    // nearer caret must be found.
    for (const qreal fraction : {0.1, 0.25, 0.4, 0.6, 0.75, 0.9}) {
      const Caret& expected = fraction < 0.5 ? left : right;
      const qreal click = left.x + width * fraction;
      const qsizetype hit = hitAt(layout, click, left.y);
      require(landsOn(hit, expected),
              QStringLiteral("%1: a click %2 of the way from caret %3 to %4 (x=%5) should land on %6, hit %7; "
                             "carets %8")
                  .arg(label)
                  .arg(fraction)
                  .arg(left.offset)
                  .arg(right.offset)
                  .arg(click, 0, 'f', 2)
                  .arg(expected.offset)
                  .arg(hit)
                  .arg(caretTable(carets)));
    }
    const qsizetype middle = hitAt(layout, left.x + width * 0.5, left.y);
    require(landsOn(middle, left) || landsOn(middle, right),
            QStringLiteral("%1: the midpoint between carets %2 and %3 hit %4")
                .arg(label)
                .arg(left.offset)
                .arg(right.offset)
                .arg(middle));
  }
}

// The caret rectangle reported for a hit is the rect of the caret that was hit.
void requireHitCursorRectMatches(const InlineLayout& layout, const QString& label) {
  const QVector<Caret> carets = visualCarets(layout, label);
  for (const Caret& caret : carets) {
    const qsizetype hit = hitAt(layout, caret.x, caret.y);
    const QRectF reported = layout.hitTestCursorRect(QPointF(caret.x, caret.y));
    const QRectF painted = layout.cursorRect(hit);
    require(qAbs(reported.left() - painted.left()) < 0.5,
            QStringLiteral("%1: the hit cursor rect (x=%2) should be the painted caret %3 (x=%4)")
                .arg(label)
                .arg(reported.left())
                .arg(hit)
                .arg(painted.left()));
  }
}

void checkLine(const QString& text, const QString& label) {
  const RenderTheme theme = RenderTheme::github();
  InlineLayout layout;
  buildSingleLine(layout, text, theme);
  require(layout.visualLineCount() == 1, QStringLiteral("%1: the sample should stay on one line").arg(label));
  requireCaretsRoundTrip(layout, label);
  requireNearestCaretWins(layout, label);
  requireHitCursorRectMatches(layout, label);
}

// Left-to-right text keeps the established behaviour: the nearer half of a character wins.
void testLeftToRightUnchanged() {
  checkLine(QStringLiteral("alpha beta"), QStringLiteral("latin"));
  checkLine(QStringLiteral("中文与 English 混排"), QStringLiteral("cjk"));
  checkLine(QStringLiteral("a😀b élan"), QStringLiteral("clusters"));
}

// A right-to-left run inside a left-to-right paragraph has carets that run right to left.
void testRightToLeftRunInsideLeftToRightParagraph() {
  checkLine(QStringLiteral("abc אבג def"), QStringLiteral("hebrew run"));
  checkLine(QStringLiteral("قبل المرحلة بعد"), QStringLiteral("arabic run"));
}

// The base direction follows the first strong character: here the first caret is at the right
// edge, which is where every click used to land.
void testRightToLeftParagraph() {
  checkLine(QStringLiteral("אבג דהו זחט"), QStringLiteral("hebrew paragraph"));
  checkLine(QStringLiteral("مرحبا بالعالم"), QStringLiteral("arabic paragraph"));
  checkLine(QStringLiteral("אבג 123 def"), QStringLiteral("rtl with latin run"));
}

} // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testLeftToRightUnchanged);
  RUN_TEST(testRightToLeftRunInsideLeftToRightParagraph);
  RUN_TEST(testRightToLeftParagraph);
#undef RUN_TEST
  return 0;
}
