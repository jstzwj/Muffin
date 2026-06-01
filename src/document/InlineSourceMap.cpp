#include "document/InlineSourceMap.h"

namespace muffin {

InlineSourceMap::InlineSourceMap(const QVector<InlineNode>& inlines, QString sourceText)
    : sourceText_(std::move(sourceText)), visibleText_(plainTextForInlines(inlines)) {
  qsizetype searchFrom = 0;
  qsizetype visibleStart = 0;
  for (const InlineNode& node : inlines) {
    const QString markdown = markdownForInline(node);
    const qsizetype sourceStart = sourceText_.indexOf(markdown, searchFrom);
    if (sourceStart < 0) {
      valid_ = false;
      segments_.clear();
      return;
    }

    const Segment segment = makeSegment(node, markdown, sourceStart, visibleStart);
    segments_.push_back(segment);
    searchFrom = segment.sourceEnd;
    visibleStart = segment.visibleEnd;
  }
  valid_ = searchFrom <= sourceText_.size() && visibleStart == visibleText_.size();
}

bool InlineSourceMap::isValid() const {
  return valid_;
}

QString InlineSourceMap::visibleText() const {
  return visibleText_;
}

bool InlineSourceMap::sourceOffsetForVisibleOffset(qsizetype visibleOffset, qsizetype& sourceOffset) const {
  if (!valid_) {
    return false;
  }
  if (visibleOffset <= 0) {
    sourceOffset = 0;
    return true;
  }

  for (const Segment& segment : segments_) {
    if (visibleOffset <= segment.visibleEnd) {
      const qsizetype visibleDelta = qBound<qsizetype>(0, visibleOffset - segment.visibleStart, segment.visibleEnd - segment.visibleStart);
      if (visibleDelta <= 0) {
        sourceOffset = segment.sourceStart;
      } else if (visibleDelta >= segment.visibleEnd - segment.visibleStart) {
        sourceOffset = segment.sourceEnd;
      } else {
        sourceOffset = qBound<qsizetype>(
            segment.contentSourceStart,
            segment.contentSourceStart + visibleDelta,
            segment.contentSourceEnd);
      }
      return true;
    }
  }

  if (visibleOffset == visibleText_.size()) {
    sourceOffset = sourceText_.size();
    return true;
  }
  return false;
}

bool InlineSourceMap::visibleOffsetForSourceOffset(qsizetype sourceOffset, qsizetype& visibleOffset) const {
  if (!valid_) {
    return false;
  }
  if (sourceOffset <= 0) {
    visibleOffset = 0;
    return true;
  }

  for (const Segment& segment : segments_) {
    if (sourceOffset <= segment.sourceEnd) {
      if (sourceOffset <= segment.contentSourceStart) {
        visibleOffset = segment.visibleStart;
      } else if (sourceOffset >= segment.contentSourceEnd) {
        visibleOffset = segment.visibleEnd;
      } else {
        visibleOffset = qBound<qsizetype>(
            segment.visibleStart,
            segment.visibleStart + sourceOffset - segment.contentSourceStart,
            segment.visibleEnd);
      }
      return true;
    }
  }

  if (sourceOffset == sourceText_.size()) {
    visibleOffset = visibleText_.size();
    return true;
  }
  return false;
}

QString InlineSourceMap::plainTextForInlines(const QVector<InlineNode>& inlines) {
  QString text;
  for (const InlineNode& node : inlines) {
    text += plainTextForInline(node);
  }
  return text;
}

bool InlineSourceMap::isPlainInlineSource(const QVector<InlineNode>& inlines, const QString& sourceText) {
  QString plain;
  for (const InlineNode& node : inlines) {
    switch (node.type()) {
      case InlineType::Text:
        plain += node.text();
        break;
      case InlineType::SoftBreak:
        plain += QLatin1Char('\n');
        break;
      case InlineType::LineBreak:
        plain += QStringLiteral("  \n");
        break;
      default:
        return false;
    }
  }
  return plain == sourceText;
}

QString InlineSourceMap::markdownForInline(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Text:
      return node.text();
    case InlineType::SoftBreak:
      return QStringLiteral("\n");
    case InlineType::LineBreak:
      return QStringLiteral("  \n");
    case InlineType::Code:
      return QStringLiteral("`%1`").arg(node.text());
    case InlineType::InlineMath:
      return QStringLiteral("$%1$").arg(node.text());
    case InlineType::HtmlInline:
      return node.text();
    case InlineType::Emphasis:
      return QStringLiteral("%1%2%1").arg(
          node.marker().isEmpty() ? QStringLiteral("*") : node.marker(),
          markdownForInlines(node.children()));
    case InlineType::Strong:
      return QStringLiteral("%1%2%1").arg(
          node.marker().isEmpty() ? QStringLiteral("**") : node.marker(),
          markdownForInlines(node.children()));
    case InlineType::Strikethrough:
      return QStringLiteral("~~%1~~").arg(markdownForInlines(node.children()));
    case InlineType::Link:
      return QStringLiteral("[%1](%2%3)").arg(
          markdownForInlines(node.children()),
          node.href(),
          node.title().isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(node.title()));
    case InlineType::Image:
      return QStringLiteral("![%1](%2%3)").arg(
          node.alt(),
          node.href(),
          node.title().isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(node.title()));
    default:
      return node.text();
  }
}

QString InlineSourceMap::markdownForInlines(const QVector<InlineNode>& inlines) {
  QString markdown;
  for (const InlineNode& node : inlines) {
    markdown += markdownForInline(node);
  }
  return markdown;
}

QString InlineSourceMap::plainTextForInline(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Text:
    case InlineType::Code:
    case InlineType::InlineMath:
    case InlineType::HtmlInline:
      return node.text();
    case InlineType::SoftBreak:
      return QStringLiteral(" ");
    case InlineType::LineBreak:
      return QStringLiteral("\n");
    case InlineType::Image:
      return node.alt();
    default:
      return plainTextForInlines(node.children());
  }
}

InlineSourceMap::Segment InlineSourceMap::makeSegment(
    const InlineNode& node,
    const QString& markdown,
    qsizetype sourceStart,
    qsizetype visibleStart) {
  Segment segment;
  segment.sourceStart = sourceStart;
  segment.sourceEnd = sourceStart + markdown.size();
  segment.visibleStart = visibleStart;
  segment.visibleEnd = visibleStart + plainTextForInline(node).size();
  segment.contentSourceStart = segment.sourceStart;
  segment.contentSourceEnd = segment.sourceEnd;

  switch (node.type()) {
    case InlineType::Code:
    case InlineType::InlineMath:
      segment.contentSourceStart = qMin(segment.sourceEnd, segment.sourceStart + 1);
      segment.contentSourceEnd = qMax(segment.contentSourceStart, segment.sourceEnd - 1);
      break;
    case InlineType::Emphasis:
    case InlineType::Strong: {
      const qsizetype markerSize =
          node.marker().isEmpty() ? (node.type() == InlineType::Strong ? 2 : 1) : node.marker().size();
      segment.contentSourceStart = qMin(segment.sourceEnd, segment.sourceStart + markerSize);
      segment.contentSourceEnd = qMax(segment.contentSourceStart, segment.sourceEnd - markerSize);
      break;
    }
    case InlineType::Strikethrough:
      segment.contentSourceStart = qMin(segment.sourceEnd, segment.sourceStart + 2);
      segment.contentSourceEnd = qMax(segment.contentSourceStart, segment.sourceEnd - 2);
      break;
    case InlineType::Link: {
      const qsizetype labelEnd = markdown.indexOf(QLatin1Char(']'));
      segment.contentSourceStart = qMin(segment.sourceEnd, segment.sourceStart + 1);
      segment.contentSourceEnd = labelEnd >= 0 ? segment.sourceStart + labelEnd : segment.contentSourceStart;
      break;
    }
    case InlineType::Image: {
      const qsizetype labelEnd = markdown.indexOf(QLatin1Char(']'));
      segment.contentSourceStart = qMin(segment.sourceEnd, segment.sourceStart + 2);
      segment.contentSourceEnd = labelEnd >= 0 ? segment.sourceStart + labelEnd : segment.contentSourceStart;
      break;
    }
    default:
      break;
  }

  return segment;
}

}  // namespace muffin
