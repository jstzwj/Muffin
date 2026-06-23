#pragma once

#include "html/HtmlBox.h"
#include "html/HtmlTextMeasurer.h"

#include <QPainter>
#include <QPointF>
#include <QHash>
#include <QImage>
#include <QSizeF>

#include <memory>
#include <vector>

namespace muffin::html {

class HtmlLayoutResult {
public:
  struct HitResult {
    QString linkHref;
    QString imageSrc;
  };
  HtmlLayoutResult();
  ~HtmlLayoutResult();

  HtmlLayoutResult(HtmlLayoutResult&&) noexcept;
  HtmlLayoutResult& operator=(HtmlLayoutResult&&) noexcept;
  HtmlLayoutResult(const HtmlLayoutResult&) = delete;
  HtmlLayoutResult& operator=(const HtmlLayoutResult&) = delete;

  bool valid() const;
  QSizeF size() const;
  QString error() const;
  QString baseDirectory() const;
  const HtmlBox* root() const;

  // True iff the laid-out box tree actually paints something a user would read as content —
  // text glyphs, an image, a rule, a list marker, or a box with a background/border. Pure
  // scaffolding (<div>, <br>, whitespace-only text, display:none subtrees) yields false, so a
  // host can decide to fall back to showing the raw HTML source instead of empty space. The
  // predicate mirrors paintBox()'s decisions, so "visible" here == "would draw".
  bool hasVisibleContent() const;

  void setBaseDirectory(QString directory);
  void setRoot(std::unique_ptr<HtmlBox> root);
  void setTextLayouts(std::vector<std::unique_ptr<HtmlTextLayout>> layouts);
  void setSize(QSizeF size);
  void setError(QString error);
  void setPalette(HtmlColorPalette palette);

  void paint(QPainter& painter, QPointF origin) const;
  HitResult hitTest(QPointF localPos) const;

private:
  HitResult hitTestBox(const HtmlBox& box, QPointF localPos, QPointF origin) const;
  bool boxHasVisibleContent(const HtmlBox& box) const;
  QString linkHrefAtTextLayout(const HtmlBox& box, QPointF localPos) const;
  void paintBox(QPainter& painter, const HtmlBox& box, QPointF origin) const;
  void paintInlineContent(QPainter& painter, const HtmlBox& box, QPointF origin) const;
  void paintTextRun(QPainter& painter, const HtmlBox& box, QPointF origin) const;
  void paintListMarker(QPainter& painter, const HtmlBox& box, const QRectF& contentRect) const;
  void paintHr(QPainter& painter, const HtmlBox& box, const QRectF& contentRect) const;
  void paintImage(QPainter& painter, const HtmlBox& box, QPointF origin) const;
  const QImage& cachedImage(const QString& src) const;

  std::unique_ptr<HtmlBox> root_;
  std::vector<std::unique_ptr<HtmlTextLayout>> textLayouts_;
  QSizeF size_;
  QString error_;
  QString baseDirectory_;
  HtmlColorPalette palette_ = HtmlColorPalette::defaultLight();
  mutable QHash<QString, QImage> imageCache_;
};

}  // namespace muffin::html
