#include "mermaid/c4/C4Scene.h"

#include "mermaid/c4/C4ScenePainter.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QFont>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace muffin::mermaid::c4 {
namespace {

struct TextBox {
  QString text;
  qreal width = 0.0;
  qreal height = 0.0;
  qreal y = 0.0;
  int lines = 0;
};

struct LayoutElement {
  C4Element source;
  TextBox label;
  TextBox description;
  TextBox technology;
  TextBox type;
  TextBox shapeType;
  QRectF rect;
  qreal margin = 0.0;
  qreal imageY = 0.0;
  bool hasImage = false;
  int parseIndex = 0;
};

struct Extent {
  qreal startX = 0.0;
  qreal stopX = 0.0;
  qreal startY = 0.0;
  qreal stopY = 0.0;
  qreal widthLimit = 0.0;
};

struct Bounds {
  Extent data;
  Extent next;
  int count = 0;

  void setData(qreal startX, qreal stopX, qreal startY, qreal stopY) {
    data.startX = next.startX = startX;
    data.stopX = next.stopX = stopX;
    data.startY = next.startY = startY;
    data.stopY = next.stopY = stopY;
    count = 0;
  }

  void insert(LayoutElement& item, const C4Config& config, int inRow) {
    ++count;
    qreal startX = next.startX == next.stopX ? next.stopX + item.margin
                                             : next.stopX + item.margin * 2.0;
    qreal stopX = startX + item.rect.width();
    qreal startY = next.startY + item.margin * 2.0;
    qreal stopY = startY + item.rect.height();
    if (startX >= data.widthLimit || stopX >= data.widthLimit || count > inRow) {
      startX = next.startX + item.margin + config.nextLinePaddingX;
      startY = next.stopY + item.margin * 2.0;
      next.stopX = stopX = startX + item.rect.width();
      next.startY = next.stopY;
      next.stopY = stopY = startY + item.rect.height();
      count = 1;
    }
    item.rect.moveTo(startX, startY);
    data.startX = std::min(data.startX, startX);
    data.startY = std::min(data.startY, startY);
    data.stopX = std::max(data.stopX, stopX);
    data.stopY = std::max(data.stopY, stopY);
    next.startX = std::min(next.startX, startX);
    next.startY = std::min(next.startY, startY);
    next.stopX = std::max(next.stopX, stopX);
    next.stopY = std::max(next.stopY, stopY);
  }
};

QFont::Weight cssWeight(const QString& value) {
  const QString lower = value.trimmed().toLower();
  if (lower == QLatin1String("bold") || lower == QLatin1String("bolder"))
    return QFont::Bold;
  bool ok = false;
  const int number = lower.toInt(&ok);
  if (ok && number >= 700) return QFont::Bold;
  if (ok && number >= 500) return QFont::DemiBold;
  return QFont::Normal;
}

flowchart::FlowLabelDocument documentFor(const QString& text,
                                         const C4Font& font) {
  flowchart::FlowLabelDocument document;
  document.text = text;
  document.baseWeight = cssWeight(font.weight);
  document.direction = Qt::LeftToRight;
  return document;
}

qreal textWidth(const QString& line, const C4Font& font) {
  if (line.isEmpty()) return 0.0;
  return std::round(flowchart::measureChromiumSvgTextBounds(
                        documentFor(line, font), font.family, font.size,
                        cssWeight(font.weight))
                        .width());
}

qreal lineHeight(const QString& line, const C4Font& font) {
  const QString visible = line.isEmpty() ? QString(QChar(0x200b)) : line;
  return std::round(flowchart::measureFlowSvgTextBounds(
                        documentFor(visible, font), font.family, font.size)
                        .height());
}

QStringList splitLines(QString text) {
  static const QRegularExpression breaks(
      QStringLiteral(R"(<br\s*/?>|\r?\n)"),
      QRegularExpression::CaseInsensitiveOption);
  text.replace(breaks, QStringLiteral("\n"));
  return text.split(QLatin1Char('\n'));
}

QString wrapText(const QString& source, qreal maximumWidth,
                 const C4Font& font) {
  if (source.isEmpty() || source.contains(QLatin1Char('\n')) ||
      source.contains(QRegularExpression(QStringLiteral(R"(<br\s*/?>)"),
                                          QRegularExpression::CaseInsensitiveOption)))
    return source;
  const QStringList words = source.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  QStringList complete;
  QString nextLine;
  for (qsizetype wordIndex = 0; wordIndex < words.size(); ++wordIndex) {
    const QString& word = words.at(wordIndex);
    const qreal wordLength = textWidth(word + QLatin1Char(' '), font);
    const qreal nextLength = textWidth(nextLine, font);
    if (wordLength > maximumWidth) {
      if (!nextLine.isEmpty()) complete.append(std::exchange(nextLine, {}));
      QString current;
      for (qsizetype index = 0; index < word.size(); ++index) {
        current += word.at(index);
        if (textWidth(current, font) < maximumWidth) continue;
        if (index + 1 < word.size()) current += QLatin1Char('-');
        complete.append(std::exchange(current, {}));
      }
      nextLine = std::move(current);
    } else if (nextLength + wordLength >= maximumWidth) {
      if (!nextLine.isEmpty()) complete.append(nextLine);
      nextLine = word;
    } else {
      if (!nextLine.isEmpty()) nextLine += QLatin1Char(' ');
      nextLine += word;
    }
    if (wordIndex + 1 == words.size() && !nextLine.isEmpty())
      complete.append(nextLine);
  }
  return complete.join(QStringLiteral("<br/>") );
}

TextBox measureText(QString text, const C4Font& font, bool wrap,
                    qreal widthLimit) {
  TextBox result;
  if (text.isEmpty()) return result;
  if (wrap) text = wrapText(text, widthLimit, font);
  result.text = std::move(text);
  const QStringList lines = splitLines(result.text);
  result.lines = static_cast<int>(lines.size());
  result.width = wrap ? widthLimit : 0.0;
  for (const QString& line : lines) {
    if (!wrap) result.width = std::max(result.width, textWidth(line, font));
    result.height += lineHeight(line, font);
  }
  return result;
}

C4Font fontFor(const C4Config& config, const QString& type) {
  return config.fonts.value(type, C4Font{});
}

C4Primitive textPrimitive(const QString& role, const QString& alias,
                          const TextBox& box, qreal x, qreal y, qreal width,
                          C4Font font, const QString& fill,
                          const C4ElementCss& css, bool bold = false,
                          bool italic = false, bool mathematical = true) {
  C4Primitive primitive;
  primitive.kind = C4PrimitiveKind::Text;
  primitive.role = role;
  primitive.alias = alias;
  primitive.text = box.text;
  primitive.position = QPointF(x + width / 2.0, y);
  primitive.fontFamily = font.family;
  primitive.fontSize = font.size;
  primitive.fontWeight = bold ? QStringLiteral("bold") : font.weight;
  primitive.italic = italic;
  primitive.middleAnchor = true;
  primitive.mathematicalBaseline = mathematical;
  primitive.fill = fill;
  primitive.css = css;
  return primitive;
}

// Neutral slots used when themeCSS is inactive (or when the adapter produced
// fewer slots than the data holds); the defaults keep primitive values.
const C4CssOverrides::Shape& noShapeCss() {
  static const C4CssOverrides::Shape value;
  return value;
}
const C4CssOverrides::Boundary& noBoundaryCss() {
  static const C4CssOverrides::Boundary value;
  return value;
}
const C4CssOverrides::Relation& noRelationCss() {
  static const C4CssOverrides::Relation value;
  return value;
}

class Builder {
public:
  Builder(const C4Data& data, C4Config config, C4SceneStyle style,
          const C4CssOverrides* css = nullptr)
      : data_(data), config_(std::move(config)), style_(std::move(style)),
        css_(css) {
    shapes_.reserve(data_.shapes.size());
    for (qsizetype i = 0; i < data_.shapes.size(); ++i) {
      shapes_.append({data_.shapes.at(i)});
      shapes_.last().parseIndex = static_cast<int>(i);
    }
    boundaries_.reserve(data_.boundaries.size());
    for (qsizetype i = 0; i < data_.boundaries.size(); ++i) {
      boundaries_.append({data_.boundaries.at(i)});
      boundaries_.last().parseIndex = static_cast<int>(i);
    }
  }

  C4Scene build() {
    Bounds screen;
    screen.setData(config_.diagramMarginX, config_.diagramMarginX,
                   config_.diagramMarginY, config_.diagramMarginY);
    screen.data.widthLimit = 800.0;
    globalMaxX_ = config_.diagramMarginX;
    globalMaxY_ = config_.diagramMarginY;
    drawInside(QString(), screen, childrenOf(QString()));
    drawRelations();
    screen.data.stopX = globalMaxX_;
    screen.data.stopY = globalMaxY_;
    const qreal width = screen.data.stopX - screen.data.startX +
                        2.0 * config_.diagramMarginX;
    const qreal height = screen.data.stopY - screen.data.startY +
                         2.0 * config_.diagramMarginY;
    if (!data_.title.isEmpty()) {
      TextBox title;
      title.text = data_.title;
      title.lines = 1;
      C4Primitive primitive;
      primitive.kind = C4PrimitiveKind::Text;
      primitive.role = QStringLiteral("title");
      primitive.text = data_.title;
      primitive.position = QPointF((screen.data.stopX - screen.data.startX) / 2.0 -
                                       4.0 * config_.diagramMarginX,
                                   screen.data.startY + config_.diagramMarginY);
      primitive.fontFamily = style_.rootFontFamily;
      primitive.fontSize = style_.rootFontSize;
      primitive.fontWeight = style_.rootFontWeight;
      primitive.fill = style_.rootTextColor;
      if (css_) primitive.css = css_->title;
      primitives_.append(std::move(primitive));
    }
    const qreal titleExtra = data_.title.isEmpty() ? 0.0 : 60.0;
    C4Scene scene;
    scene.bounds = QRectF(screen.data.startX - config_.diagramMarginX,
                          -(config_.diagramMarginY + titleExtra), width,
                          height + titleExtra);
    scene.contentBounds = QRectF(screen.data.startX, screen.data.startY,
                                screen.data.stopX - screen.data.startX,
                                screen.data.stopY - screen.data.startY);
    scene.viewBoxAttribute = number(scene.bounds.x()) + QLatin1Char(' ') +
                             number(scene.bounds.y()) + QLatin1Char(' ') +
                             number(scene.bounds.width()) + QLatin1Char(' ') +
                             number(scene.bounds.height());
    scene.useMaxWidth = config_.useMaxWidth;
    scene.config = config_;
    scene.style = style_;
    scene.primitives = std::move(primitives_);
    return scene;
  }

private:
  static QString number(qreal value) {
    return QString::number(value, 'g', 15);
  }

  QVector<int> childrenOf(const QString& parent) const {
    QVector<int> result;
    for (qsizetype i = 0; i < boundaries_.size(); ++i)
      if (boundaries_.at(i).source.parentBoundary == parent)
        result.append(static_cast<int>(i));
    return result;
  }

  QVector<int> shapesOf(const QString& parent) const {
    QVector<int> result;
    for (qsizetype i = 0; i < shapes_.size(); ++i)
      if (shapes_.at(i).source.parentBoundary == parent)
        result.append(static_cast<int>(i));
    return result;
  }

  void appendText(const QString& role, const QString& alias,
                  const TextBox& box, qreal x, qreal y, qreal width,
                  const C4Font& font, const QString& fill,
                  const C4ElementCss& css, bool bold = false,
                  bool italic = false) {
    const QStringList lines = splitLines(box.text);
    for (qsizetype i = 0; i < lines.size(); ++i) {
      TextBox line = box;
      line.text = lines.at(i);
      line.lines = 1;
      C4Primitive primitive = textPrimitive(role, alias, line, x, y, width,
                                            font, fill, css, bold, italic);
      primitive.textDy = i * font.size -
                         font.size * (lines.size() - 1) / 2.0;
      primitives_.append(std::move(primitive));
    }
  }

  void measureBoundary(LayoutElement& boundary, qreal widthLimit) {
    qreal y = 0.0;
    C4Font labelFont = fontFor(config_, QStringLiteral("boundary"));
    labelFont.size += 2.0;
    labelFont.weight = QStringLiteral("bold");
    boundary.label = measureText(boundary.source.label, labelFont,
                                 data_.wrap && config_.wrap,
                                 widthLimit);
    boundary.label.y = y + 8.0;
    y = boundary.label.y + boundary.label.height;
    if (!boundary.source.type.isEmpty()) {
      boundary.type.text = QLatin1Char('[') + boundary.source.type + QLatin1Char(']');
      boundary.type = measureText(boundary.type.text,
                                  fontFor(config_, QStringLiteral("boundary")),
                                  data_.wrap && config_.wrap, widthLimit);
      boundary.type.y = y + 5.0;
      y = boundary.type.y + boundary.type.height;
    }
    if (!boundary.source.description.isEmpty()) {
      C4Font descriptionFont = fontFor(config_, QStringLiteral("boundary"));
      descriptionFont.size -= 2.0;
      boundary.description = measureText(boundary.source.description,
                                         descriptionFont,
                                         data_.wrap && config_.wrap,
                                         widthLimit);
      boundary.description.y = y + 20.0;
    }
  }

  void prepareShape(LayoutElement& shape) {
    qreal y = 0.0;
    C4Font typeFont = fontFor(config_, shape.source.typeC4Shape);
    typeFont.size -= 2.0;
    shape.shapeType.text = shape.source.typeC4Shape;
    const auto typeDocument = documentFor(
        QChar(0x00ab) + shape.source.typeC4Shape + QChar(0x00bb), typeFont);
    if (typeFont.family.contains(QStringLiteral("CJK"),
                                 Qt::CaseInsensitive)) {
      const qreal cjkInk = flowchart::measureFlowSvgTextBounds(
                                typeDocument, typeFont.family, typeFont.size)
                                .width();
      shape.shapeType.width = std::round(cjkInk + typeFont.size / 8.0);
    } else {
      qreal typeAdvance =
          flowchart::measureOpenTypeDesignAdvance(typeDocument,
                                                  typeFont.family,
                                                  typeFont.size)
              .value_or(flowchart::measureFlowTextInkWidth(
                  typeDocument, typeFont.family, typeFont.size));
      if (cssWeight(typeFont.weight) > QFont::Normal)
        typeAdvance += typeFont.size / 6.0;
      shape.shapeType.width = std::round(typeAdvance);
    }
    shape.shapeType.height = typeFont.size + 2.0;
    shape.shapeType.y = config_.c4ShapePadding;
    y = shape.shapeType.y + shape.shapeType.height - 4.0;
    if (shape.source.typeC4Shape == QLatin1String("person") ||
        shape.source.typeC4Shape == QLatin1String("external_person")) {
      shape.hasImage = true;
      shape.imageY = y;
      y += 48.0;
    }
    const qreal widthLimit = config_.width - config_.c4ShapePadding * 2.0;
    C4Font labelFont = fontFor(config_, shape.source.typeC4Shape);
    labelFont.size += 2.0;
    labelFont.weight = QStringLiteral("bold");
    shape.label = measureText(shape.source.label, labelFont,
                              data_.wrap && config_.wrap,
                              widthLimit);
    shape.label.y = y + 8.0;
    y = shape.label.y + shape.label.height;
    if (!shape.source.type.isEmpty()) {
      shape.type = measureText(QLatin1Char('[') + shape.source.type + QLatin1Char(']'),
                               fontFor(config_, shape.source.typeC4Shape),
                               data_.wrap && config_.wrap, widthLimit);
      shape.type.y = y + 5.0;
      y = shape.type.y + shape.type.height;
    } else if (!shape.source.technology.isEmpty()) {
      // Upstream accidentally asks for a font named after the bracketed
      // technology. The missing config keys make calculateTextDimensions use
      // its own Arial/12px defaults, even though drawing later uses the shape
      // font.
      C4Font measurementFont;
      measurementFont.family = QStringLiteral("Arial");
      measurementFont.size = 12.0;
      measurementFont.weight = QStringLiteral("normal");
      shape.technology = measureText(
          QLatin1Char('[') + shape.source.technology + QLatin1Char(']'),
          measurementFont,
          data_.wrap && config_.wrap, widthLimit);
      // Chromium's Windows Arial 12px SVG bbox is 14px high. Keep this
      // deterministic when Qt's offscreen font database cannot expose the
      // platform Arial face and otherwise falls back to the bundled Noto.
      shape.technology.height = shape.technology.lines * 14.0;
      shape.technology.y = y + 5.0;
      y = shape.technology.y + shape.technology.height;
    }
    qreal rectangleHeight = y;
    qreal rectangleWidth = shape.label.width;
    if (!shape.source.description.isEmpty()) {
      shape.description = measureText(shape.source.description,
                                      fontFor(config_, shape.source.typeC4Shape),
                                      data_.wrap && config_.wrap, widthLimit);
      shape.description.y = y + 20.0;
      y = shape.description.y + shape.description.height;
      rectangleWidth = std::max(shape.label.width, shape.description.width);
      rectangleHeight = y - shape.description.lines * 5.0;
    }
    rectangleWidth += config_.c4ShapePadding;
    shape.rect.setSize(QSizeF(std::max(config_.width, rectangleWidth),
                              std::max(config_.height, rectangleHeight)));
    shape.margin = config_.c4ShapeMargin;
  }

  void drawShape(LayoutElement& shape) {
    const QString type = shape.source.typeC4Shape;
    const C4CssOverrides::Shape& shapeCss =
        css_ && shape.parseIndex < css_->shapes.size()
            ? css_->shapes.at(shape.parseIndex)
            : noShapeCss();
    const QString fill = shape.source.backgroundColor.value_or(
        config_.backgroundColors.value(type, QStringLiteral("#1168BD")));
    const QString stroke = shape.source.borderColor.value_or(
        config_.borderColors.value(type, QStringLiteral("#3C7FC0")));
    const QString fontColor =
        shape.source.fontColor.value_or(QStringLiteral("#FFFFFF"));
    const bool database = type.endsWith(QLatin1String("_db"));
    const bool queue = type.endsWith(QLatin1String("_queue"));
    if (!database && !queue) {
      C4Primitive rect;
      rect.kind = C4PrimitiveKind::Rect;
      rect.role = QStringLiteral("shape");
      rect.alias = shape.source.alias;
      rect.rect = shape.rect;
      rect.fill = fill;
      rect.stroke = stroke;
      rect.strokeWidth = 0.5;
      rect.rx = 2.5;
      rect.css = shapeCss.body;
      primitives_.append(std::move(rect));
    } else if (database) {
      const qreal x = shape.rect.x(), y = shape.rect.y();
      const qreal half = shape.rect.width() / 2.0;
      const qreal h = shape.rect.height();
      C4Primitive body;
      body.kind = C4PrimitiveKind::Path;
      body.role = QStringLiteral("shape"); body.alias = shape.source.alias;
      body.fill = fill; body.stroke = stroke; body.strokeWidth = 0.5;
      body.path.moveTo(x, y); body.path.cubicTo(x, y - 10, x + half, y - 10, x + half, y - 10);
      body.path.cubicTo(x + half, y - 10, x + 2*half, y - 10, x + 2*half, y);
      body.path.lineTo(x + 2*half, y+h);
      body.path.cubicTo(x + 2*half, y+h+10, x+half, y+h+10, x+half, y+h+10);
      body.path.cubicTo(x+half,y+h+10,x,y+h+10,x,y+h); body.path.lineTo(x,y);
      body.pathData = QStringLiteral("M%1,%2c0,-10 %3,-10 %3,-10c0,0 %3,0 %3,10l0,%4c0,10 -%3,10 -%3,10c0,0 -%3,0 -%3,-10l0,-%4")
                          .arg(number(x), number(y), number(half), number(h));
      body.css = shapeCss.body;
      primitives_.append(body);
      C4Primitive lip;
      lip.kind=C4PrimitiveKind::Path;lip.role=QStringLiteral("shape-detail");lip.alias=shape.source.alias;
      lip.fill=QStringLiteral("none");lip.stroke=stroke;lip.strokeWidth=.5;
      lip.path.moveTo(x,y);lip.path.cubicTo(x,y+10,x+half,y+10,x+half,y+10);lip.path.cubicTo(x+half,y+10,x+2*half,y+10,x+2*half,y);
      lip.pathData=QStringLiteral("M%1,%2c0,10 %3,10 %3,10c0,0 %3,0 %3,-10").arg(number(x),number(y),number(half));
      lip.css = shapeCss.detail;
      primitives_.append(std::move(lip));
    } else {
      const qreal x=shape.rect.x(),y=shape.rect.y(),w=shape.rect.width(),half=shape.rect.height()/2.0;
      C4Primitive body;body.kind=C4PrimitiveKind::Path;body.role=QStringLiteral("shape");body.alias=shape.source.alias;body.fill=fill;body.stroke=stroke;body.strokeWidth=.5;
      body.path.moveTo(x,y);body.path.lineTo(x+w,y);body.path.cubicTo(x+w+5,y,x+w+5,y+half,x+w+5,y+half);body.path.cubicTo(x+w+5,y+half,x+w+5,y+2*half,x+w,y+2*half);body.path.lineTo(x,y+2*half);body.path.cubicTo(x-5,y+2*half,x-5,y+half,x-5,y+half);body.path.cubicTo(x-5,y+half,x-5,y,x,y);
      body.pathData=QStringLiteral("M%1,%2l%3,0c5,0 5,%4 5,%4c0,0 0,%4 -5,%4l-%3,0c-5,0 -5,-%4 -5,-%4c0,0 0,-%4 5,-%4").arg(number(x),number(y),number(w),number(half));
      body.css = shapeCss.body;
      primitives_.append(body);
      C4Primitive lip;lip.kind=C4PrimitiveKind::Path;lip.role=QStringLiteral("shape-detail");lip.alias=shape.source.alias;lip.fill=QStringLiteral("none");lip.stroke=stroke;lip.strokeWidth=.5;
      lip.path.moveTo(x+w,y);lip.path.cubicTo(x+w-5,y,x+w-5,y+half,x+w-5,y+half);lip.path.cubicTo(x+w-5,y+half,x+w,y+2*half,x+w,y+2*half);
      lip.pathData=QStringLiteral("M%1,%2c-5,0 -5,%3 -5,%3c0,%3 5,%3 5,%3").arg(number(x+w),number(y),number(half));
      lip.css = shapeCss.detail;
      primitives_.append(std::move(lip));
    }

    C4Font typeFont=fontFor(config_,type);typeFont.size-=2.0;
    TextBox displayType=shape.shapeType;displayType.text=QStringLiteral("<<")+type+QStringLiteral(">>");
    C4Primitive typeText=textPrimitive(QStringLiteral("shape-type"),shape.source.alias,displayType,
        shape.rect.x()-shape.rect.width()/2.0+shape.shapeType.width/2.0,
        shape.rect.y()+shape.shapeType.y,shape.rect.width(),typeFont,fontColor,shapeCss.stereotype,false,true,false);
    typeText.position.setX(shape.rect.center().x()-shape.shapeType.width/2.0);
    typeText.middleAnchor=false;typeText.forcedTextWidth=shape.shapeType.width;
    primitives_.append(std::move(typeText));
    if(shape.hasImage){C4Primitive image;image.kind=C4PrimitiveKind::Image;image.role=QStringLiteral("person-image");image.alias=shape.source.alias;image.rect=QRectF(shape.rect.center().x()-24,shape.rect.y()+shape.imageY,48,48);image.imageKind=type;image.css=shapeCss.image;primitives_.append(std::move(image));}
    C4Font labelFont=fontFor(config_,type);labelFont.size+=2;labelFont.weight=QStringLiteral("bold");
    appendText(QStringLiteral("shape-label"),shape.source.alias,shape.label,shape.rect.x(),shape.rect.y()+shape.label.y,shape.rect.width(),labelFont,fontColor,shapeCss.label,true);
    if(!shape.technology.text.isEmpty())appendText(QStringLiteral("shape-technology"),shape.source.alias,shape.technology,shape.rect.x(),shape.rect.y()+shape.technology.y,shape.rect.width(),fontFor(config_,type),fontColor,shapeCss.technology,false,true);
    else if(!shape.type.text.isEmpty())appendText(QStringLiteral("shape-technology"),shape.source.alias,shape.type,shape.rect.x(),shape.rect.y()+shape.type.y,shape.rect.width(),fontFor(config_,type),fontColor,shapeCss.technology,false,true);
    if(!shape.description.text.isEmpty())appendText(QStringLiteral("shape-description"),shape.source.alias,shape.description,shape.rect.x(),shape.rect.y()+shape.description.y,shape.rect.width(),fontFor(config_,QStringLiteral("person")),fontColor,shapeCss.description);
  }

  void drawShapeArray(Bounds& bounds, const QVector<int>& indexes) {
    for(int index:indexes){LayoutElement& shape=shapes_[index];prepareShape(shape);bounds.insert(shape,config_,data_.shapeInRow);drawShape(shape);}
    bounds.data.stopX+=config_.c4ShapeMargin;bounds.data.stopY+=config_.c4ShapeMargin;
  }

  void drawBoundaryPrimitive(LayoutElement& boundary, const Bounds& bounds) {
    const C4CssOverrides::Boundary& boundaryCss =
        css_ && boundary.parseIndex < css_->boundaries.size()
            ? css_->boundaries.at(boundary.parseIndex)
            : noBoundaryCss();
    boundary.rect=QRectF(bounds.data.startX,bounds.data.startY,bounds.data.stopX-bounds.data.startX,bounds.data.stopY-bounds.data.startY);
    // Upstream assigns c4ShapeMargin - 35 to the lowercase `label.y`, but
    // c4Shapes reads the original uppercase `label.Y` produced during text
    // measurement. Preserve that observable case-mismatch instead of
    // applying the otherwise dead assignment.
    C4Primitive rect;rect.kind=C4PrimitiveKind::Rect;rect.role=QStringLiteral("boundary");rect.alias=boundary.source.alias;rect.rect=boundary.rect;rect.fill=boundary.source.backgroundColor.value_or(QStringLiteral("none"));rect.stroke=boundary.source.borderColor.value_or(QStringLiteral("#444444"));rect.strokeWidth=1;rect.rx=2.5;if(boundary.source.nodeType.isEmpty())rect.dash={7,7};rect.css=boundaryCss.body;primitives_.append(rect);
    C4Font labelFont=fontFor(config_,QStringLiteral("boundary"));labelFont.size+=2;labelFont.weight=QStringLiteral("bold");appendText(QStringLiteral("boundary-label"),boundary.source.alias,boundary.label,boundary.rect.x(),boundary.rect.y()+boundary.label.y,boundary.rect.width(),labelFont,QStringLiteral("#444444"),boundaryCss.label,true);
    if(!boundary.type.text.isEmpty())appendText(QStringLiteral("boundary-type"),boundary.source.alias,boundary.type,boundary.rect.x(),boundary.rect.y()+boundary.type.y,boundary.rect.width(),fontFor(config_,QStringLiteral("boundary")),QStringLiteral("#444444"),boundaryCss.type);
    if(!boundary.description.text.isEmpty()){C4Font font=fontFor(config_,QStringLiteral("boundary"));font.size-=2;appendText(QStringLiteral("boundary-description"),boundary.source.alias,boundary.description,boundary.rect.x(),boundary.rect.y()+boundary.description.y,boundary.rect.width(),font,QStringLiteral("#444444"),boundaryCss.description);}
  }

  void drawInside(const QString&, Bounds& parent, const QVector<int>& children) {
    if(children.isEmpty())return;
    Bounds current;current.data.widthLimit=parent.data.widthLimit/std::min(data_.boundaryInRow,static_cast<int>(children.size()));
    for(qsizetype order=0;order<children.size();++order){LayoutElement& boundary=boundaries_[children.at(order)];measureBoundary(boundary,current.data.widthLimit);qreal y=0;if(!boundary.label.text.isEmpty())y=boundary.label.y+boundary.label.height;if(!boundary.type.text.isEmpty())y=boundary.type.y+boundary.type.height;if(!boundary.description.text.isEmpty())y=boundary.description.y+boundary.description.height;
      if(order==0||order%data_.boundaryInRow==0){const qreal x=parent.data.startX+config_.diagramMarginX;const qreal yy=parent.data.stopY+config_.diagramMarginY+y;current.setData(x,x,yy,yy);}else{const qreal x=current.data.stopX!=current.data.startX?current.data.stopX+config_.diagramMarginX:current.data.startX;const qreal yy=current.data.startY;current.setData(x,x,yy,yy);}current.data.widthLimit=parent.data.widthLimit/std::min(data_.boundaryInRow,static_cast<int>(children.size()));
      const QVector<int> shapeIndexes=shapesOf(boundary.source.alias);if(!shapeIndexes.isEmpty())drawShapeArray(current,shapeIndexes);const QVector<int> nested=childrenOf(boundary.source.alias);if(!nested.isEmpty())drawInside(boundary.source.alias,current,nested);if(boundary.source.alias!=QLatin1String("global"))drawBoundaryPrimitive(boundary,current);
      parent.data.stopY=std::max(current.data.stopY+config_.c4ShapeMargin,parent.data.stopY);parent.data.stopX=std::max(current.data.stopX+config_.c4ShapeMargin,parent.data.stopX);globalMaxX_=std::max(globalMaxX_,parent.data.stopX);globalMaxY_=std::max(globalMaxY_,parent.data.stopY);
    }
  }

  static QPointF intersection(const QRectF& rect,const QPointF& outside){const qreal x1=rect.x(),y1=rect.y(),x2=outside.x(),y2=outside.y();const qreal cx=rect.center().x(),cy=rect.center().y();const qreal dx=std::abs(x1-x2),dy=std::abs(y1-y2);const qreal tan=dy/dx,ratio=rect.height()/rect.width();QPointF p;
    if(y1==y2&&x1<x2)p={rect.right(),cy};else if(y1==y2&&x1>x2)p={x1,cy};else if(x1==x2&&y1<y2)p={cx,rect.bottom()};else if(x1==x2&&y1>y2)p={cx,y1};
    if(x1>x2&&y1<y2)p=ratio>=tan?QPointF(x1,cy+tan*rect.width()/2):QPointF(cx-(dx/dy)*rect.height()/2,rect.bottom());
    else if(x1<x2&&y1<y2)p=ratio>=tan?QPointF(rect.right(),cy+tan*rect.width()/2):QPointF(cx+(dx/dy)*rect.height()/2,rect.bottom());
    else if(x1<x2&&y1>y2)p=ratio>=tan?QPointF(rect.right(),cy-tan*rect.width()/2):QPointF(cx+(rect.height()/2)*dx/dy,y1);
    else if(x1>x2&&y1>y2)p=ratio>=tan?QPointF(x1,cy-rect.width()/2*tan):QPointF(cx-(rect.height()/2)*dx/dy,y1);return p;}

  void drawRelations(){int sequence=0;bool first=true;for(const C4Relation& relation:data_.relations){++sequence;auto fromIt=std::find_if(shapes_.begin(),shapes_.end(),[&](const LayoutElement& x){return x.source.alias==relation.from;});auto toIt=std::find_if(shapes_.begin(),shapes_.end(),[&](const LayoutElement& x){return x.source.alias==relation.to;});if(fromIt==shapes_.end()||toIt==shapes_.end())continue;const C4CssOverrides::Relation& relationCss=css_&&sequence-1<css_->relations.size()?css_->relations.at(sequence-1):noRelationCss();const QPointF start=intersection(fromIt->rect,toIt->rect.center()),end=intersection(toIt->rect,fromIt->rect.center());const QString color=relation.lineColor.value_or(QStringLiteral("#444444"));C4Primitive edge;edge.role=QStringLiteral("relation");edge.alias=relation.from+QLatin1Char('-')+relation.to;edge.stroke=color;edge.strokeWidth=1;edge.markerEnd=relation.type!=QLatin1String("rel_b");edge.markerStart=relation.type==QLatin1String("birel")||relation.type==QLatin1String("rel_b");edge.css=relationCss.body;edge.markerFill=css_&&!css_->markers.fill.trimmed().isEmpty()?css_->markers.fill:style_.rootTextColor;
      if(first){edge.kind=C4PrimitiveKind::Line;edge.line=QLineF(start,end);first=false;}else{edge.kind=C4PrimitiveKind::Path;const QPointF control(start.x()+(end.x()-start.x())/4.0,start.y()+(end.y()-start.y())/2.0);edge.path.moveTo(start);edge.path.quadTo(control,end);edge.pathData=QStringLiteral("M%1,%2 Q%3,%4 %5,%6").arg(number(start.x()),number(start.y()),number(control.x()),number(control.y()),number(end.x()),number(end.y()));}primitives_.append(edge);
      C4Font message=fontFor(config_,QStringLiteral("message"));QString label=relation.label;if(data_.c4Type==QLatin1String("C4Dynamic"))label=QString::number(sequence)+QStringLiteral(": ")+label;const bool wrap=data_.wrap&&config_.wrap;TextBox labelBox=measureText(label,message,wrap,textWidth(label,message));const qreal ox=relation.offsetX.value_or(0),oy=relation.offsetY.value_or(0);const qreal midX=std::min(start.x(),end.x())+std::abs(end.x()-start.x())/2+ox,midY=std::min(start.y(),end.y())+std::abs(end.y()-start.y())/2+oy;appendText(QStringLiteral("relation-label"),edge.alias,labelBox,midX,midY,labelBox.width,message,relation.textColor.value_or(QStringLiteral("#444444")),relationCss.label);if(!relation.technology.isEmpty()){TextBox tech=measureText(relation.technology,message,wrap,textWidth(relation.technology,message));const qreal width=std::max(labelBox.width,tech.width);TextBox displayTech{QLatin1Char('[')+tech.text+QLatin1Char(']'),tech.width,tech.height,0,tech.lines};appendText(QStringLiteral("relation-technology"),edge.alias,displayTech,midX,midY+message.size+5,width,message,relation.textColor.value_or(QStringLiteral("#444444")),relationCss.technology,false,true);}}
  }

  const C4Data& data_;C4Config config_;C4SceneStyle style_;const C4CssOverrides* css_=nullptr;QVector<LayoutElement> shapes_;QVector<LayoutElement> boundaries_;QVector<C4Primitive> primitives_;qreal globalMaxX_=0,globalMaxY_=0;
};

QJsonObject rectangleJson(const QRectF& rect){return{{QStringLiteral("x"),rect.x()},{QStringLiteral("y"),rect.y()},{QStringLiteral("width"),rect.width()},{QStringLiteral("height"),rect.height()}};}
}

C4Scene buildC4Scene(const C4Data& data,C4Config config,C4SceneStyle style,const C4CssOverrides* css){return Builder(data,std::move(config),std::move(style),css).build();}

void C4Scene::paint(QPainter& painter,const MermaidPaintOptions& options)const{paintC4Scene(*this,painter,options);}

QJsonObject C4Scene::toJsonObject()const{QJsonArray values;for(const C4Primitive& p:primitives){QJsonObject item{{QStringLiteral("kind"),static_cast<int>(p.kind)},{QStringLiteral("role"),p.role},{QStringLiteral("alias"),p.alias},{QStringLiteral("rect"),rectangleJson(p.rect)},{QStringLiteral("text"),p.text},{QStringLiteral("x"),p.position.x()},{QStringLiteral("y"),p.position.y()},{QStringLiteral("path"),p.pathData},{QStringLiteral("fill"),p.fill},{QStringLiteral("stroke"),p.stroke},{QStringLiteral("strokeWidth"),p.strokeWidth}};if(p.kind==C4PrimitiveKind::Line)item.insert(QStringLiteral("line"),QJsonArray{p.line.x1(),p.line.y1(),p.line.x2(),p.line.y2()});values.append(item);}return{{QStringLiteral("bounds"),rectangleJson(bounds)},{QStringLiteral("viewBox"),viewBoxAttribute},{QStringLiteral("useMaxWidth"),useMaxWidth},{QStringLiteral("primitives"),values}};}

}  // namespace muffin::mermaid::c4
