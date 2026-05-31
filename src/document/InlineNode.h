#pragma once

#include "document/MarkdownTypes.h"

#include <QString>
#include <QVector>

namespace muffin {

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
};

}  // namespace muffin
