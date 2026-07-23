#pragma once

#include "document/MarkdownTypes.h"

#include <QString>
#include <QStringView>
#include <QVector>

#include <memory>
#include <variant>

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
  InlineNode(const InlineNode& other);
  InlineNode& operator=(const InlineNode& other);
  InlineNode(InlineNode&&) noexcept = default;
  InlineNode& operator=(InlineNode&&) noexcept = default;

  InlineType type() const;
  QString text() const;
  QStringView textView() const;
  void setText(QString text);
  bool bindSharedText(const std::shared_ptr<const QString>& source);
  void detachSharedText();
  bool usesSharedText() const;

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
  static InlineNode highlight(QString marker, QVector<InlineNode> children);
  static InlineNode subscript(QString marker, QVector<InlineNode> children);
  static InlineNode superscript(QString marker, QVector<InlineNode> children);
  static InlineNode code(QString value);
  static InlineNode link(QString href, QString title, QVector<InlineNode> label);
  static InlineNode image(QString src, QString alt, QString title);
  static InlineNode inlineMath(QString tex);
  // A footnote reference `[^label]`. `ordinal` is the resolved footnote number (e.g. "1",
  // computed by cmark at parse time) shown as a superscript link; `label` identifies the
  // target definition and is encoded into href as "#fn:<label>" for in-document navigation.
  static InlineNode footnoteReference(QString label, QString ordinal);

private:
  struct SharedTextSlice {
    std::shared_ptr<const QString> source;
    int start = 0;
    int length = 0;
  };

  // Most inline nodes are plain text, code, math, or breaks and never use any of these fields.
  // Keep the four implicitly-shared QString handles off those hot nodes; formatting/link/image
  // nodes allocate this payload on demand.
  struct ExtendedMetadata {
    QString marker;
    QString href;
    QString title;
    QString alt;
    bool autolink = false;

    bool isEmpty() const;
  };

  ExtendedMetadata& ensureExtendedMetadata();
  void pruneExtendedMetadata();

  InlineType type_ = InlineType::Text;
  std::variant<QString, SharedTextSlice> text_;
  QVector<InlineNode> children_;
  InlineSourceRanges sourceRanges_;
  std::unique_ptr<ExtendedMetadata> extendedMetadata_;
};

void shiftInlineSourcePositions(QVector<InlineNode>& inlines, qsizetype delta);

}  // namespace muffin
