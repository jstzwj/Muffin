#pragma once

#include "document/MarkdownTypes.h"

#include <QString>
#include <QStringView>
#include <QVector>

namespace muffin {

struct InlineRange {
  qsizetype start = -1;
  qsizetype end = -1;

  bool isValid() const;
  qsizetype length() const;
};

inline qsizetype countLeading(QStringView text, qsizetype start, qsizetype end, QChar ch) {
  qsizetype count = 0;
  while (start + count < end && text.at(start + count) == ch) {
    ++count;
  }
  return count;
}

inline qsizetype countTrailing(QStringView text, qsizetype start, qsizetype end, QChar ch) {
  qsizetype count = 0;
  while (end - count > start && text.at(end - count - 1) == ch) {
    ++count;
  }
  return count;
}

struct InlineSourceRanges {
  InlineRange source;
  InlineRange content;
  InlineRange openMarker;
  InlineRange closeMarker;
};

class InlineNode {
public:
  explicit InlineNode(InlineType type = InlineType::Text);

  InlineType type() const;
  QString text() const;
  void setText(QString text);

  QString marker() const;
  void setMarker(QString marker);

  QString href() const;
  void setHref(QString href);

  QString title() const;
  void setTitle(QString title);

  QString alt() const;
  void setAlt(QString alt);

  qsizetype sourceStart() const;
  void setSourceStart(qsizetype start);
  qsizetype sourceEnd() const;
  void setSourceEnd(qsizetype end);
  InlineRange sourceRange() const;
  void setSourceRange(InlineRange range);
  InlineRange contentRange() const;
  void setContentRange(InlineRange range);
  InlineRange openMarkerRange() const;
  void setOpenMarkerRange(InlineRange range);
  InlineRange closeMarkerRange() const;
  void setCloseMarkerRange(InlineRange range);
  InlineSourceRanges sourceRanges() const;
  void setSourceRanges(InlineSourceRanges ranges);

  bool isAutolink() const;
  void setAutolink(bool autolink);

  QVector<InlineNode>& children();
  const QVector<InlineNode>& children() const;

  static InlineNode text(QString value);
  static InlineNode softBreak();
  static InlineNode lineBreak();
  static InlineNode strong(QString marker, QVector<InlineNode> children);
  static InlineNode emphasis(QString marker, QVector<InlineNode> children);
  static InlineNode strikethrough(QString marker, QVector<InlineNode> children);
  static InlineNode code(QString value);
  static InlineNode link(QString href, QString title, QVector<InlineNode> label);
  static InlineNode image(QString src, QString alt, QString title);
  static InlineNode inlineMath(QString tex);

private:
  InlineType type_ = InlineType::Text;
  QString text_;
  QString marker_;
  QString href_;
  QString title_;
  QString alt_;
  QVector<InlineNode> children_;
  InlineSourceRanges sourceRanges_;
  bool autolink_ = false;
};

void shiftInlineSourcePositions(QVector<InlineNode>& inlines, qsizetype delta);

}  // namespace muffin
