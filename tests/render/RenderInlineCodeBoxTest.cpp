#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "render/InlineLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QRgb>

#include <functional>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

QImage renderParagraphImage(const RenderTheme& theme, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(p != nullptr, QStringLiteral("fixture should contain a paragraph"));
  const BlockLayout* block = layout.block(p->id());
  require(block != nullptr, QStringLiteral("paragraph block should be promoted"));
  const QRectF rect = block->rect();
  require(rect.width() > 100.0 && rect.height() > 4.0, QStringLiteral("paragraph block should be non-trivial"));

  QImage image(int(rect.width()), int(rect.height() + 4), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr);
  painter.end();
  return image;
}

QRect colorBBox(const QImage& image, const std::function<bool(QRgb)>& pred) {
  int left = image.width(), top = image.height(), right = -1, bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (pred(image.pixel(x, y))) {
        left = qMin(left, x); top = qMin(top, y);
        right = qMax(right, x); bottom = qMax(bottom, y);
      }
    }
  }
  return right < left ? QRect() : QRect(left, top, right - left + 1, bottom - top + 1);
}

const std::function<bool(QRgb)> isRed = [](QRgb p) {
  return qRed(p) > 150 && qGreen(p) < 90 && qBlue(p) < 90;
};

// CSS padding reserves inline advance and grows the painted box. A theme with larger padding produces a wider
// red background bbox than the default — relative (same code text / font), so
// robust to offscreen font-metric differences.
void testInlineCodeChipGrowsWithPadding() {
  const QString md = QStringLiteral("a `ab` c\n");
  const QString base = QStringLiteral("#write { color:#000000; } code { background-color:#d00000; }");
  const QString padded = QStringLiteral(
      "#write { color:#000000; } code { background-color:#d00000; padding:10px 14px; border-radius:8px; }");

  const QImage baseImg = renderParagraphImage(
      RenderTheme::fromDefinition(CssThemeMapper::fromCss(base, QStringLiteral("b"), QString())), md);
  const QImage paddedImg = renderParagraphImage(
      RenderTheme::fromDefinition(CssThemeMapper::fromCss(padded, QStringLiteral("p"), QString())), md);

  const QRect baseRed = colorBBox(baseImg, isRed);
  const QRect paddedRed = colorBBox(paddedImg, isRed);
  require(baseRed.isValid() && paddedRed.isValid(),
          QStringLiteral("both chips should paint a red background (base=%1x%2 padded=%3x%4)")
              .arg(baseRed.width()).arg(baseRed.height()).arg(paddedRed.width()).arg(paddedRed.height()));
  // Extra 14px horizontal padding each side → ≥20px wider bbox (the default is 3).
  require(paddedRed.width() > baseRed.width() + 20.0,
          QStringLiteral("padding should widen the chip (base=%1 padded=%2)").arg(baseRed.width()).arg(paddedRed.width()));
}

void testInlineBoxPaddingReservesFlowAndPreservesText() {
  const auto endpoint = [](const QString& css, const QString& markdown) {
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("flow"), QString()));
    DocumentSession session;
    session.setMarkdownText(markdown, false);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 800);
    const auto* node = findFirstBlock(session.document().root(), BlockType::Paragraph);
    const auto* block = layout.block(node->id());
    const auto* text = block->inlineLayout();
    const QString visible = text->visibleText();
    require(visible == QStringLiteral("a ab c"), QStringLiteral("generated spacing must not enter visible/copy text"));
    const auto cursor = text->cursorRect(visible.size());
    for (qsizetype offset = 0; offset <= visible.size(); ++offset) {
      const auto caret = text->cursorRect(offset);
      require(text->hitTestTextOffset(QPointF(caret.left(), caret.center().y())) == offset,
              QStringLiteral("clicking the caret must round-trip through inline box padding at offset %1").arg(offset));
    }
    return cursor.x();
  };
  const qreal codeBase = endpoint(QStringLiteral("#write{color:black}code{padding:0;border:none}"), QStringLiteral("a `ab` c\n"));
  const qreal codePad = endpoint(QStringLiteral("#write{color:black}code{padding:0 14px;border:none}"), QStringLiteral("a `ab` c\n"));
  require(qAbs(codePad - codeBase - 28) < .1, QStringLiteral("code padding must reserve exactly 28px in text flow: base=%1 padded=%2 delta=%3")
                                                   .arg(codeBase).arg(codePad).arg(codePad-codeBase));
  const qreal kbdBase =
      endpoint(QStringLiteral("#write{color:black}kbd{padding:0;border:0 solid black}"), QStringLiteral("a <kbd>ab</kbd> c\n"));
  const qreal kbdPad =
      endpoint(QStringLiteral("#write{color:black}kbd{padding:0 12px;border:0 solid black}"), QStringLiteral("a <kbd>ab</kbd> c\n"));
  require(qAbs(kbdPad - kbdBase - 24) < .1, QStringLiteral("kbd padding must reserve exactly 24px in text flow"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInlineCodeChipGrowsWithPadding);
  RUN_TEST(testInlineBoxPaddingReservesFlowAndPreservesText);
#undef RUN_TEST
  return 0;
}
