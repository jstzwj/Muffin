#include "render/InlineTextFormats.h"
#include "render/InlineFormatting.h"
#include "render/TextLayout.h"

#include <QRegularExpression>

namespace muffin {
namespace {

using DisplayOffsetRange = InlineDisplayMap::DisplayOffsetRange;

constexpr QChar kTabIndentSourceChar(0x200b);
constexpr QChar kTabIndentLayoutChar(0x00a0);

void applyLetterSpacing(QTextCharFormat& format, const QFont& font) {
  if (font.letterSpacing() != 0.0) {
    format.setFontLetterSpacingType(font.letterSpacingType());
    format.setFontLetterSpacing(font.letterSpacing());
  }
}

class InlineFormatBuilder {
public:
  explicit InlineFormatBuilder(const InlineFormatInput& input) : input_(input) {
    baseFormat_.setFont(input.font);
    applyLetterSpacing(baseFormat_, input.font);
    baseFormat_.setForeground(input.foreground.isValid() ? input.foreground : input.theme.textColor());
  }

  QVector<QTextLayout::FormatRange> build() {
    if (input_.displayMap.text().isEmpty())
      return {};
    formats_.push_back({0, static_cast<int>(input_.displayMap.text().size()), baseFormat_});
    appendProjection();
    appendTabs();
    appendObjects();
    appendHtml();
    appendSpelling();
    appendSpacers();
    appendGenerated();
    return std::move(formats_);
  }

private:
  const InlineFormatInput& input_;
  QTextCharFormat baseFormat_;
  QVector<QTextLayout::FormatRange> formats_;

  QTextCharFormat projectionFormat(const InlineProjectionSpan& span) const {
    QTextCharFormat format = baseFormat_;
    if (span.kind == InlineSpanKind::OpenMarker || span.kind == InlineSpanKind::CloseMarker ||
        span.kind == InlineSpanKind::HiddenSyntax || span.kind == InlineSpanKind::EmptyContentSlot) {
      format.setForeground(input_.theme.mutedTextColor());
    }
    if (span.bold) {
      format.setFontWeight(QFont::Bold);
    }
    if (span.italic) {
      format.setFontItalic(true);
    }
    if (span.strike) {
      format.setFontStrikeOut(true);
      // Phase 5: CSS `del { color }` mutes deleted text (phycat → #999). The strike
      // line itself can't be recoloured (Qt strikeOut uses the text colour), but the
      // text colour is honoured when the theme declares one.
      if (input_.theme.delColor().isValid()) {
        format.setForeground(input_.theme.delColor());
      }
    }
    if (span.underline) {
      format.setFontUnderline(true);
    }
    if (span.highlight) {
      format.setBackground(input_.theme.highlightBackgroundColor());
    }
    if (span.subscript) {
      format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    } else if (span.superscript) {
      format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    }
    switch (span.type) {
    case InlineType::Code:
      format.setFont(input_.codeStyle.font);
      applyLetterSpacing(format, input_.codeStyle.font);
      if (input_.codeStyle.style.paint.color.isValid()) {
        format.setForeground(input_.codeStyle.style.paint.color);
      }
      break;
    case InlineType::InlineMath:
      format.setFont(input_.theme.mathFont());
      break;
    default:
      break;
    }
    // Link formatting is an orthogonal wrapping attribute (span.link), decoupled from span.type, so a
    // link composes with any inner node: an image-link renders as a clickable image, `[`code`](url)`
    // keeps its code background, etc. Atom is excluded so an image-link's placeholder isn't underlined.
    if (span.link && span.kind != InlineSpanKind::OpenMarker && span.kind != InlineSpanKind::CloseMarker &&
        span.kind != InlineSpanKind::HiddenSyntax && span.kind != InlineSpanKind::EmptyContentSlot &&
        span.kind != InlineSpanKind::Atom) {
      format.setForeground(input_.theme.linkColor());
      // CSS `a { text-decoration }`: underline + style + colour, and optional overline.
      if (input_.theme.linkUnderlined()) {
        format.setFontUnderline(true);
        const int style = input_.theme.linkUnderlineStyle();
        format.setUnderlineStyle(style >= 0 ? static_cast<QTextCharFormat::UnderlineStyle>(style)
                                            : QTextCharFormat::SingleUnderline);
        if (const QColor uc = input_.theme.linkUnderlineColor(); uc.isValid()) {
          format.setUnderlineColor(uc);
        }
      }
      if (input_.theme.linkOverline()) {
        format.setFontOverline(true);
      }
    }
    const auto computedRun = input_.spanStyles.constFind(span.displayStart);
    if (computedRun != input_.spanStyles.cend() && span.kind == InlineSpanKind::Text &&
        span.type != InlineType::InlineMath) {
      const auto& run = *computedRun.value();
      if (span.highlight)
        format.clearBackground(); // painted by the semantic mark box
      format.setFont(run.font);
      applyLetterSpacing(format, run.font);
      const auto& textStyle = run.style.text;
      if (textStyle.strokeWidth > 0)
        format.setTextOutline(QPen(textStyle.strokeColor, textStyle.strokeWidth * input_.theme.zoomPercent() / 100.));
      format.setFontUnderline(textStyle.decorationLines & 1);
      format.setFontOverline(textStyle.decorationLines & 2);
      format.setFontStrikeOut(textStyle.decorationLines & 4);
      if (textStyle.decorationLines & 1) {
        format.setUnderlineStyle(static_cast<QTextCharFormat::UnderlineStyle>(textStyle.underlineStyle));
        if (textStyle.decorationColor.isValid())
          format.setUnderlineColor(textStyle.decorationColor);
      }
      if (run.style.paint.color.isValid())
        format.setForeground(run.style.paint.color);
    }
    if (span.subscript || span.superscript)
      applyScriptBaseline(
          format, span.superscript ? QTextCharFormat::AlignSuperScript : QTextCharFormat::AlignSubScript, input_.font);
    return format;
  }

  void appendProjection() {
    for (const InlineProjectionSpan& span : input_.projection.spans()) {
      if (span.displayEnd <= span.displayStart) {
        continue;
      }
      const auto format = projectionFormat(span);
      const DisplayOffsetRange layoutRange =
          input_.displayMap.layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
      if (!layoutRange.valid || layoutRange.end > input_.displayMap.text().size()) {
        continue;
      }
      // The base range already covers ordinary prose.  Do not add an identical
      // range for every projection span: splitting a shaping run at an
      // otherwise invisible boundary changes kerning/advance (notably around
      // generated counters).  A computed inline style still forces a range so
      // an explicit normal value can reset inherited Markdown emphasis.
      const auto computedStyleIt = input_.spanStyles.constFind(span.displayStart);
      const bool hasComputedInlineStyle =
          computedStyleIt != input_.spanStyles.cend() && span.kind == InlineSpanKind::Text &&
          span.type != InlineType::InlineMath &&
          (computedStyleIt.value()->style.text.fontSizeSet || computedStyleIt.value()->style.text.fontWeightSet ||
           computedStyleIt.value()->style.text.italicSet || computedStyleIt.value()->style.text.decorationLines != 0 ||
           computedStyleIt.value()->style.text.letterSpacing != 0 ||
           computedStyleIt.value()->style.text.wordSpacing != 0 ||
           computedStyleIt.value()->style.text.textTransform != 0);
      const bool authoredInlineElement = hasComputedInlineStyle &&
                                         computedStyleIt.value()->style.key != QStringLiteral("p") &&
                                         !computedStyleIt.value()->style.key.startsWith(QStringLiteral("h"));
      const bool hasSpanOverride = hasComputedInlineStyle || span.bold || span.italic || span.strike ||
                                   span.underline || span.highlight || span.subscript || span.superscript ||
                                   span.link || span.kind != InlineSpanKind::Text || span.type != InlineType::Text;
      if (!hasSpanOverride)
        continue;
      QTextLayout::FormatRange range;
      range.start = static_cast<int>(layoutRange.start);
      range.length = static_cast<int>(layoutRange.end - layoutRange.start);
      range.format = format;
      if (!authoredInlineElement && span.kind == InlineSpanKind::Text && span.type == InlineType::Text &&
          format.font() == baseFormat_.font() &&
          (!format.hasProperty(QTextFormat::ForegroundBrush) || format.foreground() == baseFormat_.foreground()) &&
          (!format.hasProperty(QTextFormat::BackgroundBrush) || format.background() == baseFormat_.background()) &&
          !format.fontUnderline() && !format.fontOverline() && !format.fontStrikeOut() &&
          format.verticalAlignment() == QTextCharFormat::AlignNormal)
        continue;
      formats_.push_back(range);
    }
  }

  void appendTabs() {
    const TextFontMetrics tabMetrics(input_.font);
    const qreal tabIndentTargetWidth = qMax<qreal>(1.0, tabMetrics.horizontalAdvance(QStringLiteral("汉汉")));
    const qreal tabIndentPlaceholderWidth =
        qMax<qreal>(0.0, tabMetrics.horizontalAdvance(QString(kTabIndentLayoutChar)));
    for (qsizetype i = 0; i < input_.displayMap.text().size(); ++i) {
      if (input_.displayMap.text().at(i) != kTabIndentSourceChar || input_.shapingText.at(i) != kTabIndentLayoutChar) {
        continue;
      }
      QFont tabFont = input_.font;
      tabFont.setLetterSpacing(QFont::AbsoluteSpacing, tabIndentTargetWidth - tabIndentPlaceholderWidth);
      QTextCharFormat format = baseFormat_;
      format.setFont(tabFont);
      format.setForeground(QColor(Qt::transparent));
      QTextLayout::FormatRange range;
      range.start = static_cast<int>(i);
      range.length = 1;
      range.format = format;
      formats_.push_back(range);
    }
  }

  void appendHtml() {
    // Apply HTML inline format spans (from <b>, <i>, <span style="...">, etc.)
    for (const InlineHtmlFormatSpan& hs : input_.htmlSpans) {
      if (hs.layoutEnd <= hs.layoutStart) {
        continue;
      }
      QTextCharFormat format = baseFormat_;
      if (hs.fontSet)
        format.setFont(hs.font);
      if (hs.color.isValid()) {
        format.setForeground(hs.color);
      }
      if (hs.fontSize > 0 && input_.font.pointSizeF() > 0) {
        format.setFontPointSize(hs.fontSize);
      }
      if (hs.verticalAlignment != QTextCharFormat::AlignNormal) {
        applyScriptBaseline(format, hs.verticalAlignment, input_.font);
      }
      if (hasDecoration(hs.decoration, html::HtmlTextDecoration::Underline)) {
        format.setFontUnderline(true);
      }
      if (hasDecoration(hs.decoration, html::HtmlTextDecoration::LineThrough)) {
        format.setFontStrikeOut(true);
      }
      if (!hs.href.isEmpty()) {
        format.setForeground(input_.theme.linkColor());
        // HTML <a> shares the Markdown link's text-decoration (underline + style +
        // colour + overline). phycat sets `a { text-decoration: none }`; this keeps
        // HTML links in step with Markdown links under the same CSS rule.
        if (input_.theme.linkUnderlined()) {
          format.setFontUnderline(true);
          const int style = input_.theme.linkUnderlineStyle();
          format.setUnderlineStyle(style >= 0 ? static_cast<QTextCharFormat::UnderlineStyle>(style)
                                              : QTextCharFormat::SingleUnderline);
          if (const QColor uc = input_.theme.linkUnderlineColor(); uc.isValid()) {
            format.setUnderlineColor(uc);
          }
        }
        if (input_.theme.linkOverline()) {
          format.setFontOverline(true);
        }
      }
      QTextLayout::FormatRange range;
      range.start = hs.layoutStart;
      range.length = hs.layoutEnd - hs.layoutStart;
      range.format = format;
      formats_.push_back(range);
    }
  }

  void appendSpelling() {
    // Spell-check overlay (rendered mode): append SpellCheckUnderline ranges for misspelled
    // prose words. Appended last so the underline paints over any bold/italic/link styling
    // on the same glyphs (Qt composes FormatRanges per-property, later wins). Only Text spans
    // are scanned — code spans, inline math and atoms are skipped. The misspelled predicate
    // is supplied by the builder; when it is unset, spell checking is off and this is skipped.
    if (input_.isMisspelled) {
      static const QRegularExpression wordRe(QStringLiteral("[\\p{L}][\\p{L}'\\x{2019}]*"),
                                             QRegularExpression::UseUnicodePropertiesOption);
      const QColor errorColor = input_.theme.spellCheckColor();
      for (const InlineProjectionSpan& span : input_.projection.spans()) {
        if (span.kind != InlineSpanKind::Text || span.type == InlineType::Code || span.type == InlineType::InlineMath) {
          continue;
        }
        const DisplayOffsetRange spanRange =
            input_.displayMap.layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
        if (!spanRange.valid || spanRange.end > input_.displayMap.text().size()) {
          continue;
        }
        const QString spanText = input_.displayMap.text().mid(spanRange.start, spanRange.end - spanRange.start);
        QRegularExpressionMatchIterator it = wordRe.globalMatch(spanText);
        while (it.hasNext()) {
          const QRegularExpressionMatch m = it.next();
          const int localStart = static_cast<int>(m.capturedStart());
          const int localLen = static_cast<int>(m.capturedLength());
          if (localLen <= 1) {
            continue;
          }
          const QStringView word(spanText.constData() + localStart, localLen);
          if (!input_.isMisspelled(word)) {
            continue;
          }
          QTextCharFormat format;
          format.setUnderlineColor(errorColor);
          format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
          QTextLayout::FormatRange spellRange;
          spellRange.start = static_cast<int>(spanRange.start) + localStart;
          spellRange.length = localLen;
          spellRange.format = format;
          formats_.push_back(spellRange);
        }
      }
    }
  }

  void appendSpacers() {
    for (const InlineSpacer& spacer : input_.spacers) {
      QFont font = input_.font;
      font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
      font.setLetterSpacing(QFont::AbsoluteSpacing,
                            spacer.width - TextFontMetrics(font).horizontalAdvance(kInlineBoxSpacer));
      QTextCharFormat format;
      format.setFont(font);
      format.setForeground(Qt::transparent);
      formats_.push_back({static_cast<int>(spacer.start), 1, format});
    }
  }

  void appendGenerated() {
    // Generated text inherits the surrounding font when its computed font is
    // unchanged. Keeping a redundant format range here splits HarfBuzz runs at
    // every pseudo-element boundary and loses kerning across `::before`/text/
    // `::after`; the browser treats adjacent inline text with the same style as
    // one shaping run. Preserve only ranges that actually override the base
    // font or paint properties.
    for (const auto& generated : input_.generated) {
      const auto generatedFont = generated.format.font();
      const bool sameFont = generatedFont == baseFormat_.font();
      const bool hasForeground = generated.format.hasProperty(QTextFormat::ForegroundBrush);
      const bool hasBackground = generated.format.hasProperty(QTextFormat::BackgroundBrush);
      if (sameFont && !hasForeground && !hasBackground)
        continue;
      formats_.push_back(generated);
    }
  }

  void appendObjects() {
    for (const auto& object : input_.objects) {
      QFont font = input_.font;
      if (object.resetLetterSpacing)
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
      const qreal advance = TextFontMetrics(font).horizontalAdvance(object.placeholder);
      font.setLetterSpacing(QFont::AbsoluteSpacing, object.width - advance);
      QTextCharFormat format = object.inheritBaseFormat ? baseFormat_ : QTextCharFormat{};
      format.setFont(font);
      format.setForeground(Qt::transparent);
      formats_.push_back({static_cast<int>(object.start), static_cast<int>(object.length), format});
    }
  }
};

} // namespace

QVector<QTextLayout::FormatRange> buildInlineTextFormats(const InlineFormatInput& input) {
  return InlineFormatBuilder(input).build();
}

} // namespace muffin
