#pragma once

#include "document/InlineNode.h"

#include <QString>
#include <QVector>

namespace muffin {

class InlineSourceMap {
public:
  InlineSourceMap() = default;
  InlineSourceMap(const QVector<InlineNode>& inlines, QString sourceText);

  bool isValid() const;
  QString visibleText() const;

  bool sourceOffsetForVisibleOffset(qsizetype visibleOffset, qsizetype& sourceOffset) const;
  bool visibleOffsetForSourceOffset(qsizetype sourceOffset, qsizetype& visibleOffset) const;

  static QString plainTextForInlines(const QVector<InlineNode>& inlines);
  static bool isPlainInlineSource(const QVector<InlineNode>& inlines, const QString& sourceText);

private:
  struct Segment {
    qsizetype sourceStart = 0;
    qsizetype sourceEnd = 0;
    qsizetype visibleStart = 0;
    qsizetype visibleEnd = 0;
    qsizetype contentSourceStart = 0;
    qsizetype contentSourceEnd = 0;
  };

  static QString markdownForInline(const InlineNode& node);
  static QString markdownForInlines(const QVector<InlineNode>& inlines);
  static QString plainTextForInline(const InlineNode& node);
  static Segment makeSegment(const InlineNode& node, const QString& markdown, qsizetype sourceStart, qsizetype visibleStart);

  QVector<Segment> segments_;
  QString sourceText_;
  QString visibleText_;
  bool valid_ = false;
};

}  // namespace muffin
