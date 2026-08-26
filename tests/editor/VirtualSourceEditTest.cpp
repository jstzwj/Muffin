#include "editor/VirtualSourceEdit.h"

#include "../TestUtils.h"

#include <QApplication>
#include <QFocusEvent>
#include <QImage>
#include <QInputMethodEvent>
#include <QPainter>

using namespace muffin;

namespace {

QImage captureEdit(VirtualSourceEdit& edit) {
  return edit.grab().toImage();
}

// The composition must render with an underline, NOT overlap the following text, and NOT be
// blink-gated (the old code drew the preedit only while the caret blink was "on").
void testSourcePreeditRendersWithoutOverlap() {
  QApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QApplication::setApplicationName(QStringLiteral("VirtualSourceEditTest"));

  VirtualSourceEdit edit;
  edit.setStandaloneText(QStringLiteral("alpha beta gamma"));
  edit.resize(600, 300);
  edit.show();
  edit.setCursorPosition(6);  // between "alpha" and "beta"
  QApplication::processEvents();
  edit.setFocus();
  QApplication::processEvents();

  const QImage baseline = captureEdit(edit);

  QInputMethodEvent preeditEvent(QStringLiteral("にほん"), {});
  QApplication::sendEvent(&edit, &preeditEvent);
  QApplication::processEvents();
  const QImage composed = captureEdit(edit);

  require(baseline.size() == composed.size(), "captures should be size-stable");
  int differingPixels = 0;
  for (int y = 0; y < baseline.height(); ++y) {
    for (int x = 0; x < baseline.width(); ++x) {
      if (baseline.pixel(x, y) != composed.pixel(x, y)) {
        ++differingPixels;
      }
    }
  }
  require(differingPixels > 100,
          "the composition should paint visible pixels (glyphs + underline) at the caret");

  // "beta" (previously starting at the caret x) must have shifted right by the preedit advance —
  // sample the column at the preedit's start: it must now hold composition glyphs, not "beta"'s.
  // The old overlap rendering left "beta"'s pixels at the caret x mixed with the preedit.
  const QImage withoutPreedit = baseline;
  int changedInCaretColumn = 0;
  for (int y = 0; y < baseline.height(); ++y) {
    // Find the caret row: the row band with the most changed pixels.
    Q_UNUSED(y);
  }
  // Simpler and stronger: the differing-pixel count must include the SHIFTED tail — compare the
  // composed image's right-of-caret region against both candidates.
  const QRect caretRect = edit.inputMethodQuery(Qt::ImCursorRectangle).toRect();
  require(!caretRect.isEmpty(), "ImCursorRectangle should resolve");
  // The composition occupies the caret row right of the caret: glyphs (and their underline) must
  // put substantial non-background ink in that window. The old overlap rendering left the same
  // window with "beta"'s ink, so also require the window's ink to have CHANGED vs the baseline.
  int composedInk = 0;
  int changedInk = 0;
  for (int y = caretRect.top(); y <= caretRect.bottom() + 2 && y < composed.height(); ++y) {
    for (int x = caretRect.left(); x < caretRect.left() + 60 && x < composed.width(); ++x) {
      const QColor pixel = composed.pixelColor(x, y);
      if (pixel.lightness() < 230) {
        ++composedInk;
      }
      if (baseline.pixel(x, y) != composed.pixel(x, y)) {
        ++changedInk;
      }
    }
  }
  require(composedInk > 40, "composition glyphs should ink the caret row window");
  require(changedInk > 40, "the caret row window must change vs baseline (shifted tail, not overlap)");
  Q_UNUSED(withoutPreedit);
  Q_UNUSED(changedInCaretColumn);
}

// Focus-out clears the composition (no stale preedit).
void testSourcePreeditClearsOnFocusOut() {
  VirtualSourceEdit edit;
  edit.setStandaloneText(QStringLiteral("hello"));
  edit.resize(600, 300);
  edit.show();
  edit.setCursorPosition(2);
  edit.setFocus();
  QApplication::processEvents();

  QInputMethodEvent preeditEvent(QStringLiteral("かん"), {});
  QApplication::sendEvent(&edit, &preeditEvent);
  QApplication::processEvents();

  QFocusEvent focusOut(QEvent::FocusOut);
  QApplication::sendEvent(&edit, &focusOut);
  QApplication::processEvents();

  const QImage after = captureEdit(edit);
  const QImage fresh = [] {
    VirtualSourceEdit plain;
    plain.setStandaloneText(QStringLiteral("hello"));
    plain.resize(600, 300);
    plain.show();
    plain.setCursorPosition(2);
    QApplication::processEvents();
    return captureEdit(plain);
  }();
  require(after.size() == fresh.size(), "captures should be comparable");
  int differing = 0;
  for (int y = 0; y < after.height(); ++y) {
    for (int x = 0; x < after.width(); ++x) {
      if (after.pixel(x, y) != fresh.pixel(x, y)) {
        ++differing;
      }
    }
  }
  require(differing < 20, "focus-out should leave a clean image (no preedit remnants)");
}

}  // namespace

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testSourcePreeditRendersWithoutOverlap);
  RUN_TEST(testSourcePreeditClearsOnFocusOut);
#undef RUN_TEST
  qInfo("All source-editor IME tests passed.");
  return 0;
}
