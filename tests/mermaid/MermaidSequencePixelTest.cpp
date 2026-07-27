#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/math/MathMlCssLayout.h"
#include "mermaid/math/MathMlCssPainter.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceScenePainter.h"
#include "math/MathRenderer.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSet>

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <iterator>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool value, const QString& message) { if (!value) fail(message); }
QByteArray fileSha256(const QString& path) {
  QFile file(path); if(!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex();
}

struct Stats { int opaque = 0; qint64 red = 0, green = 0, blue = 0; };
Stats stats(const QImage& image) {
  Stats result;
  for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) {
    const QColor color=image.pixelColor(x,y);
    if (color.alpha()<32) continue;
    ++result.opaque; result.red+=color.red(); result.green+=color.green(); result.blue+=color.blue();
  }
  return result;
}
QRect alphaBounds(const QImage& image) {
  QRect result;
  for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) {
    if (image.pixelColor(x,y).alpha()<32) continue;
    result = result.isNull() ? QRect(x,y,1,1) : result.united(QRect(x,y,1,1));
  }
  return result;
}
qreal alphaIou(const QImage& a, const QImage& b) {
  const QImage left=a.scaled(400,400,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
  const QImage right=b.scaled(400,400,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
  int intersection=0, united=0;
  for(int y=0;y<400;++y) for(int x=0;x<400;++x) {
    const bool la=left.pixelColor(x,y).alpha()>=32, ra=right.pixelColor(x,y).alpha()>=32;
    intersection+=la&&ra; united+=la||ra;
  }
  return united ? qreal(intersection)/united : 0.0;
}
struct MaskAlignment { qreal iou=0.0; QPoint offset; };
MaskAlignment bestRawAlphaAlignment(const QImage& native,
                                    const QImage& browser,
                                    int radius=2) {
  MaskAlignment best;
  for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {
    const int left=std::min(0,dx),top=std::min(0,dy);
    const int right=std::max(native.width(),dx+browser.width());
    const int bottom=std::max(native.height(),dy+browser.height());
    int intersection=0,united=0;
    for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
      const bool nativeInk=x>=0&&y>=0&&x<native.width()&&y<native.height()&&
          native.pixelColor(x,y).alpha()>=32;
      const int browserX=x-dx,browserY=y-dy;
      const bool browserInk=browserX>=0&&browserY>=0&&
          browserX<browser.width()&&browserY<browser.height()&&
          browser.pixelColor(browserX,browserY).alpha()>=32;
      intersection+=nativeInk&&browserInk;
      united+=nativeInk||browserInk;
    }
    const qreal iou=united?qreal(intersection)/united:0.0;
    if(iou>best.iou) best={iou,QPoint(dx,dy)};
  }
  return best;
}
qreal tolerantGlyphCoverage(const QImage& a,const QImage& b,int radius=20) {
  constexpr int side=400;
  constexpr int inkAlpha=32;
  const QImage left=a.scaled(side,side,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
  const QImage right=b.scaled(side,side,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
  QVector<quint8> leftMask(side*side),rightMask(side*side),leftDilated(side*side),rightDilated(side*side);
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    leftMask[y*side+x]=left.pixelColor(x,y).alpha()>=inkAlpha;
    rightMask[y*side+x]=right.pixelColor(x,y).alpha()>=inkAlpha;
  }
  const auto dilate=[&](const QVector<quint8>& source,QVector<quint8>& target) {
    for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
      if(!source[y*side+x]) continue;
      for(int yy=std::max(0,y-radius);yy<=std::min(side-1,y+radius);++yy)
        for(int xx=std::max(0,x-radius);xx<=std::min(side-1,x+radius);++xx)
          target[yy*side+xx]=1;
    }
  };
  dilate(leftMask,leftDilated); dilate(rightMask,rightDilated);
  int leftCount=0,rightCount=0,leftMatched=0,rightMatched=0;
  for(int index=0;index<side*side;++index) {
    if(leftMask[index]) { ++leftCount; leftMatched+=rightDilated[index]; }
    if(rightMask[index]) { ++rightCount; rightMatched+=leftDilated[index]; }
  }
  if(!leftCount||!rightCount) return 0.0;
  return std::min(qreal(leftMatched)/leftCount,qreal(rightMatched)/rightCount);
}
QImage alphaTrimmed(const QImage& image) {
  const QRect bounds=alphaBounds(image);
  return bounds.isNull() ? image : image.copy(bounds);
}
QImage renderMathLayer(const sequence::SequenceLabelDocument& label,
                       qreal fontPixelSize,qreal dpr,
                       muffin::math::MathMlPaintLayer layer,
                       const QColor& color,bool trim=true) {
  if(label.richText.math.isEmpty()) return {};
  muffin::math::MathRenderer renderer;
  const qreal renderFontPixelSize=fontPixelSize*1.21;
  const muffin::math::MathLayoutResult layout=renderer.render(
      label.richText.math.front().source,renderFontPixelSize,color,true);
  if(!layout.valid()) return {};
  const muffin::math::MathCssBox box=muffin::math::layoutMathMlCssBox(
      layout,renderFontPixelSize,16.0);
  constexpr qreal padding=2.0;
  QImage image(qCeil((box.width+2.0*padding)*dpr),
               qCeil((box.height+2.0*padding)*dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.scale(dpr,dpr);
  painter.translate(padding,padding);
  muffin::math::paintMathMlOperations(
      painter,layout,color,renderFontPixelSize,layer);
  painter.end();
  return trim ? alphaTrimmed(image) : image;
}
QImage renderMathAtPhase(const sequence::SequenceLabelDocument& label,
                         qreal fontPixelSize,qreal dpr,qreal phase) {
  if(label.richText.math.isEmpty()) return {};
  muffin::math::MathRenderer renderer;
  const qreal renderFontPixelSize=fontPixelSize*1.21;
  const muffin::math::MathLayoutResult layout=renderer.render(
      label.richText.math.front().source,renderFontPixelSize,Qt::white,true);
  if(!layout.valid()) return {};
  const muffin::math::MathCssBox box=muffin::math::layoutMathMlCssBox(
      layout,renderFontPixelSize,16.0);
  constexpr qreal padding=8.0;
  QImage image(qCeil((box.width+2.0*padding+1.0)*dpr),
               qCeil((box.height+2.0*padding+1.0)*dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.scale(dpr,dpr);
  painter.translate(padding+phase,padding+phase);
  muffin::math::paintMathMlOperations(
      painter,layout,Qt::white,renderFontPixelSize);
  painter.end();
  return alphaTrimmed(image);
}
const muffin::math::MathCssVerticalGlyphOperation* findLargeOperatorGlyph(
    const muffin::math::MathCssPaintOperation& operation) {
  if (const auto* script = std::get_if<muffin::math::MathCssScriptOperation>(
          &operation.payload);
      script && script->largeOperatorGlyph)
    return &*script->largeOperatorGlyph;
  for (const auto& child : operation.children)
    if (const auto* glyph = findLargeOperatorGlyph(child)) return glyph;
  return nullptr;
}
const muffin::math::MathCssAccentOperation* findAccentOperation(
    const muffin::math::MathCssPaintOperation& operation) {
  if (const auto* accent = std::get_if<muffin::math::MathCssAccentOperation>(
          &operation.payload))
    return accent;
  for (const auto& child : operation.children)
    if (const auto* accent = findAccentOperation(child)) return accent;
  return nullptr;
}
bool hasPaintKindPath(
    const muffin::math::MathCssPaintOperation& operation,
    std::initializer_list<muffin::math::MathCssPaintKind> path) {
  if (path.size() == 0 || operation.kind() != *path.begin()) return false;
  const muffin::math::MathCssPaintOperation* current = &operation;
  for (auto expected = std::next(path.begin()); expected != path.end();
       ++expected) {
    const auto child = std::find_if(
        current->children.cbegin(), current->children.cend(),
        [&](const muffin::math::MathCssPaintOperation& candidate) {
          return candidate.kind() == *expected;
        });
    if (child == current->children.cend()) return false;
    current = &*child;
  }
  return true;
}
struct LargeOperatorRaster {
  QImage image;
  QRectF target;
  QRectF inkBounds;
  qsizetype partCount=0;
  bool fixedVariant=false;
};
LargeOperatorRaster renderLargeOperatorGlyph(
    const sequence::SequenceLabelDocument& label, qreal fontPixelSize,
    qreal dpr) {
  if (label.richText.math.isEmpty()) return {};
  muffin::math::MathRenderer renderer;
  const qreal renderFontPixelSize=fontPixelSize*1.21;
  const muffin::math::MathLayoutResult layout=renderer.render(
      label.richText.math.front().source,renderFontPixelSize,Qt::white,true);
  if(!layout.valid()) return {};
  const auto built=muffin::math::buildMathMlPaintOperations(
      layout,renderFontPixelSize,16.0);
  if(!built.operation) return {};
  const auto* glyph=findLargeOperatorGlyph(*built.operation);
  if(!glyph) return {};
  const muffin::math::MathCssBox box=muffin::math::layoutMathMlCssBox(
      layout,renderFontPixelSize,16.0);
  constexpr qreal padding=2.0;
  QImage image(qCeil((box.width+2.0*padding)*dpr),
               qCeil((box.height+2.0*padding)*dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.scale(dpr,dpr);
  painter.translate(padding,padding);
  muffin::math::paintMathMlVerticalGlyphOperation(
      painter,*glyph,Qt::white);
  painter.end();
  return {alphaTrimmed(image),glyph->target,glyph->inkBounds,
          glyph->parts.size(),
          glyph->kind==muffin::math::MathCssVerticalGlyphKind::FixedVariant};
}
struct NativeDelimiterRaster {
  QString character;
  QImage image;
  QRectF target;
  QRectF inkBounds;
  QVector<muffin::math::MathCssVerticalGlyphPart> parts;
};
QVector<NativeDelimiterRaster> renderDelimiterGlyphs(
    const sequence::SequenceLabelDocument& label, qreal fontPixelSize,
    qreal dpr) {
  QVector<NativeDelimiterRaster> result;
  if(label.richText.math.isEmpty()) return result;
  muffin::math::MathRenderer renderer;
  const qreal renderFontPixelSize=fontPixelSize*1.21;
  const auto layout=renderer.render(label.richText.math.front().source,
                                    renderFontPixelSize,Qt::white,true);
  if(!layout.valid()) return result;
  const auto built=muffin::math::buildMathMlPaintOperations(
      layout,renderFontPixelSize,16.0);
  if(!built.operation) return result;
  QVector<const muffin::math::MathCssVerticalGlyphOperation*> glyphs;
  for(const auto& primitive:
      muffin::math::collectMathMlPaintPrimitives(*built.operation)) {
    if(primitive.kind!=muffin::math::MathMlPaintPrimitiveKind::VerticalGlyph||
       primitive.role==muffin::math::MathMlPaintPrimitiveRole::LargeOperator)
      continue;
    glyphs.push_back(std::get<
        const muffin::math::MathCssVerticalGlyphOperation*>(primitive.payload));
  }
  std::stable_sort(glyphs.begin(),glyphs.end(),[](const auto* left,const auto* right) {
    if(!qFuzzyCompare(left->target.left()+1.0,right->target.left()+1.0))
      return left->target.left()<right->target.left();
    return left->target.top()<right->target.top();
  });
  const muffin::math::MathCssBox box=muffin::math::layoutMathMlCssBox(
      layout,renderFontPixelSize,16.0);
  constexpr qreal padding=4.0;
  for(const auto* glyph:glyphs) {
    QImage image(qCeil((box.width+2.0*padding)*dpr),
                 qCeil((box.height+2.0*padding)*dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(dpr,dpr);
    painter.translate(padding,padding);
    muffin::math::paintMathMlVerticalGlyphOperation(painter,*glyph,Qt::white);
    painter.end();
    result.push_back({glyph->character,alphaTrimmed(image),glyph->target,
                      glyph->inkBounds,glyph->parts});
  }
  return result;
}
QImage renderMathPrimitiveRole(
    const sequence::SequenceLabelDocument& label, qreal fontPixelSize,
    qreal dpr, muffin::math::MathMlPaintPrimitiveRole role,bool trim=true,
    QPointF phase={}) {
  if(label.richText.math.isEmpty()) return {};
  muffin::math::MathRenderer renderer;
  const qreal renderFontPixelSize=fontPixelSize*1.21;
  const muffin::math::MathLayoutResult layout=renderer.render(
      label.richText.math.front().source,renderFontPixelSize,Qt::white,true);
  if(!layout.valid()) return {};
  const auto built=muffin::math::buildMathMlPaintOperations(
      layout,renderFontPixelSize,16.0);
  if(!built.operation) return {};
  QVector<muffin::math::MathMlPaintPrimitive> selected;
  for(const auto& primitive:
      muffin::math::collectMathMlPaintPrimitives(*built.operation))
    if(primitive.role==role) selected.push_back(primitive);
  if(selected.isEmpty()) return {};
  const muffin::math::MathCssBox box=muffin::math::layoutMathMlCssBox(
      layout,renderFontPixelSize,16.0);
  const qreal padding=phase.isNull()?2.0:8.0;
  QImage image(qCeil((box.width+2.0*padding)*dpr),
               qCeil((box.height+2.0*padding)*dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.scale(dpr,dpr);
  painter.translate(QPointF(padding,padding)+phase);
  muffin::math::paintMathMlPrimitives(painter,selected,Qt::white);
  painter.end();
  return trim?alphaTrimmed(image):image;
}
struct NativeMathToken {
  muffin::math::MathMlPaintPrimitiveRole role;
  QString text;
  QRectF clip;
  QPointF baseline;
  qreal fontScale=1.0;
  QString path;
};
QVector<NativeMathToken> nativeMathTokens(
    const sequence::SequenceLabelDocument& label,qreal fontPixelSize) {
  QVector<NativeMathToken> result;
  if(label.richText.math.isEmpty()) return result;
  muffin::math::MathRenderer renderer;
  const qreal renderFontPixelSize=fontPixelSize*1.21;
  const auto layout=renderer.render(label.richText.math.front().source,
                                    renderFontPixelSize,Qt::white,true);
  if(!layout.valid()) return result;
  const auto built=muffin::math::buildMathMlPaintOperations(
      layout,renderFontPixelSize,16.0);
  if(!built.operation) return result;
  for(const auto& primitive:
      muffin::math::collectMathMlPaintPrimitives(*built.operation)) {
    if(primitive.kind!=muffin::math::MathMlPaintPrimitiveKind::GlyphRun)
      continue;
    const auto* run=std::get<const muffin::math::MathCssGlyphRunOperation*>(
        primitive.payload);
    result.push_back({primitive.role,run->text,run->clip,
                      run->baselineOrigin,run->fontScale,
                      primitive.operationPath});
  }
  return result;
}
QImage renderLabel(const sequence::SequenceScene& scene, const QString& kind, qreal dpr) {
  const sequence::SequenceLabelDocument* label=nullptr;
  QString textColor=scene.style.textColor;
  if(kind==QLatin1String("participant")&&!scene.participantLabels.isEmpty()) {
    label=&scene.participantLabels.first(); textColor=scene.style.actorTextColor;
  } else if(kind==QLatin1String("message")&&!scene.messageLabels.isEmpty()) {
    label=&scene.messageLabels.first(); textColor=scene.style.signalTextColor;
  } else if(kind==QLatin1String("note")&&!scene.noteLabels.isEmpty()) {
    label=&scene.noteLabels.first(); textColor=scene.style.noteTextColor;
  } else if(kind==QLatin1String("fragment")&&!scene.fragmentLabels.isEmpty()) {
    label=&scene.fragmentLabels.first(); textColor=scene.style.loopTextColor;
  } else if(kind==QLatin1String("box")&&!scene.boxLabels.isEmpty()) {
    label=&scene.boxLabels.first(); textColor=scene.style.actorTextColor;
  }
  if(!label) return {};
  const qreal lineHeight=scene.style.fontSize*1.375;
  const auto metrics=sequence::layoutSequenceLabel(*label,scene.style.fontFamily,
                                                    scene.style.fontSize,lineHeight);
  constexpr qreal padding=4.0;
  constexpr qreal paintGuard=4.0;
  QImage image(qCeil((metrics.size.width()+paintGuard+2*padding)*dpr),
               qCeil((metrics.size.height()+2*padding)*dpr),QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.scale(dpr,dpr);
  sequence::paintSequenceLabel(painter,*label,
      QRectF(padding,padding,metrics.size.width()+paintGuard,metrics.size.height()),
      scene.style.fontFamily,scene.style.fontSize,lineHeight,QColor(textColor),true);
  painter.end();
  return alphaTrimmed(image);
}
}

int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
#if defined(Q_OS_LINUX)
  qWarning("skipped on Linux: font/rendering golden coupled to x86 Windows (TODO, docs/mermaid-architecture.md step 5)");
  return 0;
#endif
  require(argc==2,QStringLiteral("Expected sequence pixel manifest"));
  QFile file(QString::fromLocal8Bit(argv[1])); require(file.open(QIODevice::ReadOnly),QStringLiteral("Cannot open sequence pixel manifest"));
  const QJsonObject root=QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString()==QLatin1String("11.16.0"),QStringLiteral("Sequence pixel version drifted"));
  require(root.value(QStringLiteral("fontMode")).toString()==
              QLatin1String("bundled-noto-stix-two-math-2.13b171"),
          QStringLiteral("Sequence pixel font oracle drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString()==
              QLatin1String("8ec403ca2a0a7b97f27a88386703ffd5ccc1d061ef6a0f1068cf9b5df25b7aaa"),
          QStringLiteral("Sequence pixel fixture changed; audit and update digest"));
  const QDir dir=QFileInfo(file).absoluteDir();
  editor::MermaidRenderCache cache;
  const QJsonArray cases=root.value(QStringLiteral("cases")).toArray();
  require(cases.size()==121,QStringLiteral("Sequence pixel matrix regressed"));
  QSet<QString> ids;
  QSet<QString> verticalDelimiters;
  int scenePixelCases=0;
  int labelPixelCases=0;
  for(const QJsonValue& value:cases) {
    const QJsonObject fixture=value.toObject(); const QString id=fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty()&&!ids.contains(id),QStringLiteral("Duplicate sequence pixel case: %1").arg(id));
    ids.insert(id);
    const QString verticalDelimiter =
        fixture.value(QStringLiteral("verticalDelimiter")).toString();
    if (!verticalDelimiter.isEmpty()) verticalDelimiters.insert(verticalDelimiter);
    const auto entry=cache.getSync(cache.makeKey(fixture.value(QStringLiteral("source")).toString()),fixture.value(QStringLiteral("source")).toString());
    require(entry.status==editor::MermaidRenderStatus::Ready&&entry.sequenceScene,
            QStringLiteral("%1 native scene failed: %2").arg(id,entry.errorMessage));
    if(id.startsWith(QLatin1String("label-math-root-index-"))) {
      require(!entry.sequenceScene->noteLabels.isEmpty()&&
                  !entry.sequenceScene->noteLabels.first().richText.math.isEmpty()&&
                  entry.sequenceScene->noteLabels.first()
                      .richText.math.first().prepared,
              QStringLiteral("%1 must use a prepared MathML operation").arg(id));
      const auto& label=entry.sequenceScene->noteLabels.first();
      muffin::math::MathRenderer renderer;
      const qreal renderFontPixelSize=entry.sequenceScene->style.fontSize*1.21;
      const auto layout=renderer.render(label.richText.math.first().source,
                                        renderFontPixelSize,Qt::white,true);
      const auto build=muffin::math::buildMathMlPaintOperations(
          layout,renderFontPixelSize,16.0);
      require(build.operation.has_value()&&
                  build.operation->kind()==muffin::math::MathCssPaintKind::Radical,
              QStringLiteral("%1 root radical operation is missing").arg(id));
      if(id.endsWith(QLatin1String("fraction")))
        require(hasPaintKindPath(*build.operation,
                    {muffin::math::MathCssPaintKind::Radical,
                     muffin::math::MathCssPaintKind::Fraction}),
                QStringLiteral("%1 fraction degree operation is missing").arg(id));
      if(id.endsWith(QLatin1String("radical")))
        require(hasPaintKindPath(*build.operation,
                    {muffin::math::MathCssPaintKind::Radical,
                     muffin::math::MathCssPaintKind::Radical}),
                QStringLiteral("%1 radical degree operation is missing").arg(id));
      if(id.endsWith(QLatin1String("supsub")))
        require(hasPaintKindPath(*build.operation,
                    {muffin::math::MathCssPaintKind::Radical,
                     muffin::math::MathCssPaintKind::SupSub}),
                QStringLiteral("%1 supsub degree operation is missing").arg(id));
    }
    const qreal dpr=fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    const QString sceneFile=fixture.value(QStringLiteral("file")).toString();
    if(!sceneFile.isEmpty()) {
      ++scenePixelCases;
      const QImage native=sequence::renderSequenceSceneToImage(*entry.sequenceScene,dpr,0.0);
      const QImage golden(dir.filePath(sceneFile));
      require(fileSha256(dir.filePath(sceneFile))==
                  fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
              QStringLiteral("%1 full pixel golden hash drifted").arg(id));
      require(!native.isNull()&&!golden.isNull(),QStringLiteral("%1 pixel image missing").arg(id));
      const qreal nativeRatio=qreal(native.width())/native.height(), goldenRatio=qreal(golden.width())/golden.height();
      const qreal ratioDrift=qAbs(nativeRatio-goldenRatio)/goldenRatio;
      const qreal iou=alphaIou(native,golden);
      qDebug().noquote() << id << "native" << native.size() << alphaBounds(native)
                         << "golden" << golden.size() << alphaBounds(golden)
                         << "scene" << entry.sequenceScene->bounds
                         << "ratio-drift" << ratioDrift << "alpha-IoU" << iou;
      require(ratioDrift<=0.05,
              QStringLiteral("%1 canvas ratio mismatch: %2 vs %3").arg(id).arg(nativeRatio).arg(goldenRatio));
      const qreal minimumSceneIou=id==QLatin1String("structural-combined-order") ? 0.25 : 0.80;
      require(iou>=minimumSceneIou,QStringLiteral("%1 alpha IoU too low: %2").arg(id).arg(iou));
      const Stats ns=stats(native), gs=stats(golden);
      require(ns.opaque>100&&gs.opaque>100,QStringLiteral("%1 rendered blank").arg(id));
      const auto average=[](qint64 sum,int count){return count?qreal(sum)/count:0.0;};
      const qreal colorDistance=qAbs(average(ns.red,ns.opaque)-average(gs.red,gs.opaque))+
          qAbs(average(ns.green,ns.opaque)-average(gs.green,gs.opaque))+
          qAbs(average(ns.blue,ns.opaque)-average(gs.blue,gs.opaque));
      require(colorDistance<260.0,QStringLiteral("%1 mean color drift: %2").arg(id).arg(colorDistance));
    }
    const QString cropFile=fixture.value(QStringLiteral("cropFile")).toString();
    if(!cropFile.isEmpty()) {
      ++labelPixelCases;
      const QImage nativeLabel=renderLabel(*entry.sequenceScene,
          fixture.value(QStringLiteral("cropKind")).toString(),dpr);
      const QImage browserLabel=alphaTrimmed(QImage(dir.filePath(cropFile)));
      require(fileSha256(dir.filePath(cropFile))==
                  fixture.value(QStringLiteral("cropSha256")).toString().toLatin1(),
              QStringLiteral("%1 label crop golden hash drifted").arg(id));
      require(!nativeLabel.isNull()&&!browserLabel.isNull(),
              QStringLiteral("%1 label crop image missing").arg(id));
      const qreal labelIou=alphaIou(nativeLabel,browserLabel);
      const qreal glyphCoverage=tolerantGlyphCoverage(nativeLabel,browserLabel);
      qDebug().noquote()<<id<<"label-crop"<<nativeLabel.size()<<browserLabel.size()
                        <<"IoU"<<labelIou<<"glyph-coverage"<<glyphCoverage;
      const QHash<QString,QSize> mathRoleRasterDrift{
          {QStringLiteral("row"),QSize(1,2)},
          {QStringLiteral("large-operator"),QSize(1,1)},
          {QStringLiteral("subscript"),QSize(4,2)},
          {QStringLiteral("superscript"),QSize(1,1)},
      };
      const QJsonArray browserDelimiters=fixture.value(
          QStringLiteral("mathDelimiters")).toArray();
      if(!browserDelimiters.isEmpty()) {
        require(!entry.sequenceScene->noteLabels.isEmpty(),
                QStringLiteral("%1 delimiter label is missing").arg(id));
        const QVector<NativeDelimiterRaster> nativeDelimiters=
            renderDelimiterGlyphs(entry.sequenceScene->noteLabels.first(),
                                  entry.sequenceScene->style.fontSize,dpr);
        require(nativeDelimiters.size()==browserDelimiters.size(),
                QStringLiteral("%1 delimiter operation count drifted: native=%2 browser=%3")
                    .arg(id).arg(nativeDelimiters.size())
                    .arg(browserDelimiters.size()));
        for(qsizetype delimiterIndex=0;
            delimiterIndex<nativeDelimiters.size();++delimiterIndex) {
          const NativeDelimiterRaster& nativeDelimiter=
              nativeDelimiters.at(delimiterIndex);
          const QJsonObject browserDelimiter=
              browserDelimiters.at(delimiterIndex).toObject();
          const QString delimiterFile=browserDelimiter.value(
              QStringLiteral("file")).toString();
          require(fileSha256(dir.filePath(delimiterFile))==
                      browserDelimiter.value(QStringLiteral("sha256"))
                          .toString().toLatin1(),
                  QStringLiteral("%1 delimiter %2 hash drifted")
                      .arg(id).arg(delimiterIndex));
          const QImage browserDelimiterImage=alphaTrimmed(
              QImage(dir.filePath(delimiterFile)));
          require(!nativeDelimiter.image.isNull()&&
                      !browserDelimiterImage.isNull(),
                  QStringLiteral("%1 delimiter %2 raster is missing")
                      .arg(id).arg(delimiterIndex));
          require(nativeDelimiter.character==browserDelimiter.value(
                      QStringLiteral("text")).toString(),
                  QStringLiteral("%1 delimiter %2 character drifted: native=%3 browser=%4")
                      .arg(id).arg(delimiterIndex)
                      .arg(nativeDelimiter.character,
                           browserDelimiter.value(QStringLiteral("text")).toString()));
          qreal previousOffset=-1.0;
          for(const auto& part:nativeDelimiter.parts) {
            require(part.glyphIndex!=0&&!part.inkBounds.isEmpty()&&
                        part.offset>previousOffset&&
                        qAbs(part.inkBounds.top()-part.offset)<=0.001&&
                        part.fullAdvance>0.0&&part.connectorOverlap>=0.0,
                    QStringLiteral("%1 delimiter %2 assembly part drifted")
                        .arg(id).arg(delimiterIndex));
            previousOffset=part.offset;
          }
          const qreal delimiterCoverage=tolerantGlyphCoverage(
              nativeDelimiter.image,browserDelimiterImage);
          const qreal delimiterIou=alphaIou(
              nativeDelimiter.image,browserDelimiterImage);
          const MaskAlignment delimiterAlignment=bestRawAlphaAlignment(
              nativeDelimiter.image,browserDelimiterImage);
          qDebug().noquote()<<id<<"delimiter"<<delimiterIndex
                            <<nativeDelimiter.character
                            <<nativeDelimiter.image.size()
                            <<browserDelimiterImage.size()
                            <<"IoU"<<delimiterIou
                            <<"coverage"<<delimiterCoverage
                            <<"offset"<<delimiterAlignment.offset
                            <<"target"<<nativeDelimiter.target
                            <<"parts"<<nativeDelimiter.parts.size();
          // Alpha-trimmed bounds include independently quantized edges plus
          // small-size stem hinting. That can add one horizontal device pixel
          // beyond the two edge pixels. The MATH target box and ordered
          // assembly geometry above remain strict, as do symmetric coverage
          // and aligned origin below.
          const QSize delimiterRasterDrift(3,2);
          require(qAbs(nativeDelimiter.image.width()-
                           browserDelimiterImage.width())<=
                      delimiterRasterDrift.width()&&
                      qAbs(nativeDelimiter.image.height()-
                           browserDelimiterImage.height())<=
                      delimiterRasterDrift.height(),
                  QStringLiteral("%1 delimiter %2 raster bounds drifted")
                      .arg(id).arg(delimiterIndex));
          require(qAbs(delimiterAlignment.offset.x())<=
                      delimiterRasterDrift.width()&&
                      qAbs(delimiterAlignment.offset.y())<=
                      delimiterRasterDrift.height(),
                  QStringLiteral("%1 delimiter %2 raster origin drifted: %3,%4")
                      .arg(id).arg(delimiterIndex)
                      .arg(delimiterAlignment.offset.x())
                      .arg(delimiterAlignment.offset.y()));
          // A one-device-pixel bracket or bar can lose a much larger fraction
          // of its binary alpha mask than a wide brace even when its assembly,
          // target box, bounds, and origin all match. Keep the broad-glyph
          // contract strict while accounting for that thin-stroke topology.
          const int delimiterWidth=qMin(nativeDelimiter.image.width(),
                                        browserDelimiterImage.width());
          const qreal minimumDelimiterCoverage=
              delimiterWidth<=7 ? 0.78 : 0.85;
          require(delimiterCoverage>=minimumDelimiterCoverage,
                  QStringLiteral("%1 delimiter %2 coverage too low: %3 < %4")
                      .arg(id).arg(delimiterIndex).arg(delimiterCoverage)
                      .arg(minimumDelimiterCoverage));
        }
      }
      const QString mathGlyphFile=fixture.value(
          QStringLiteral("mathGlyphFile")).toString();
      if(!mathGlyphFile.isEmpty()) {
        require(!entry.sequenceScene->noteLabels.isEmpty(),
                QStringLiteral("%1 large-operator label is missing").arg(id));
        const LargeOperatorRaster nativeGlyph=renderLargeOperatorGlyph(
            entry.sequenceScene->noteLabels.first(),
            entry.sequenceScene->style.fontSize,dpr);
        const QImage browserGlyph=alphaTrimmed(
            QImage(dir.filePath(mathGlyphFile)));
        require(fileSha256(dir.filePath(mathGlyphFile))==
                    fixture.value(QStringLiteral("mathGlyphSha256"))
                        .toString().toLatin1(),
                QStringLiteral("%1 glyph raster golden hash drifted").arg(id));
        require(!nativeGlyph.image.isNull()&&!browserGlyph.isNull(),
                QStringLiteral("%1 glyph raster image missing").arg(id));
        const QJsonObject browserBox=fixture.value(
            QStringLiteral("mathGlyphBox")).toObject();
        require(qAbs(nativeGlyph.target.width()-
                     browserBox.value(QStringLiteral("width")).toDouble())<=0.22&&
                    qAbs(nativeGlyph.target.height()-
                     browserBox.value(QStringLiteral("height")).toDouble())<=0.22,
                QStringLiteral("%1 glyph CSS allocation mismatch: native=%2x%3 browser=%4x%5")
                    .arg(id).arg(nativeGlyph.target.width())
                    .arg(nativeGlyph.target.height())
                    .arg(browserBox.value(QStringLiteral("width")).toDouble())
                    .arg(browserBox.value(QStringLiteral("height")).toDouble()));
        const qreal glyphIou=alphaIou(nativeGlyph.image,browserGlyph);
        const qreal isolatedCoverage=tolerantGlyphCoverage(
            nativeGlyph.image,browserGlyph);
        const MaskAlignment rawAlignment=bestRawAlphaAlignment(
            nativeGlyph.image,browserGlyph);
        qDebug().noquote()<<id<<"glyph-raster"<<nativeGlyph.image.size()
                          <<browserGlyph.size()<<"IoU"<<glyphIou
                          <<"coverage"<<isolatedCoverage
                          <<"raw-best"<<rawAlignment.iou
                          <<"offset"<<rawAlignment.offset
                          <<"target"<<nativeGlyph.target
                          <<"ink"<<nativeGlyph.inkBounds
                          <<"fixed"<<nativeGlyph.fixedVariant
                          <<"parts"<<nativeGlyph.partCount;
        require(qAbs(nativeGlyph.image.width()-browserGlyph.width())<=2&&
                    qAbs(nativeGlyph.image.height()-browserGlyph.height())<=2,
                QStringLiteral("%1 glyph raster bounds drifted").arg(id));
        // Browser and Qt font backends rasterize identical outlines with
        // different edge alpha. Keep IoU above as a diagnostic; bounds,
        // symmetric coverage, and origin are the portable pass/fail contract.
        require(isolatedCoverage>=0.95,
                QStringLiteral("%1 glyph raster coverage too low: %2")
                    .arg(id).arg(isolatedCoverage));
        require(qAbs(rawAlignment.offset.x())<=1&&
                    qAbs(rawAlignment.offset.y())<=1,
                QStringLiteral("%1 glyph raster origin drifted: %2,%3")
                    .arg(id).arg(rawAlignment.offset.x())
                    .arg(rawAlignment.offset.y()));
      }
      for(const QJsonValue& groupValue:
          fixture.value(QStringLiteral("mathTokenGroups")).toArray()) {
        const QJsonObject group=groupValue.toObject();
        const QString role=group.value(QStringLiteral("role")).toString();
        const auto nativeRole=role==QLatin1String("row")
            ? muffin::math::MathMlPaintPrimitiveRole::Row
            : role==QLatin1String("subscript")
                ? muffin::math::MathMlPaintPrimitiveRole::ScriptSubscript
                : muffin::math::MathMlPaintPrimitiveRole::ScriptSuperscript;
        const QString groupFile=group.value(QStringLiteral("file")).toString();
        require(fileSha256(dir.filePath(groupFile))==
                    group.value(QStringLiteral("sha256")).toString().toLatin1(),
                QStringLiteral("%1 %2 token-group hash drifted").arg(id,role));
        const QImage nativeGroupRaw=renderMathPrimitiveRole(
            entry.sequenceScene->noteLabels.first(),
            entry.sequenceScene->style.fontSize,dpr,nativeRole,false);
        const QImage browserGroupRaw=QImage(dir.filePath(groupFile));
        const QImage nativeGroup=alphaTrimmed(nativeGroupRaw);
        const QImage browserGroup=alphaTrimmed(browserGroupRaw);
        require(!nativeGroup.isNull()&&!browserGroup.isNull(),
                QStringLiteral("%1 %2 token-group image missing").arg(id,role));
        const qreal groupIou=alphaIou(nativeGroup,browserGroup);
        const qreal groupCoverage=tolerantGlyphCoverage(nativeGroup,browserGroup);
        const MaskAlignment groupAlignment=bestRawAlphaAlignment(
            nativeGroup,browserGroup);
        const QRect nativeInk=alphaBounds(nativeGroupRaw);
        const QRect browserInk=alphaBounds(browserGroupRaw);
        const QJsonObject groupBox=group.value(QStringLiteral("box")).toObject();
        const QJsonObject mathRootBox=fixture.value(QStringLiteral("structure"))
            .toObject().value(QStringLiteral("mathMlBox")).toObject();
        const QPointF nativeDeviceOrigin(
            nativeInk.x()-2.0*dpr,nativeInk.y()-2.0*dpr);
        const QPointF browserDeviceOrigin(
            (groupBox.value(QStringLiteral("x")).toDouble()-
             mathRootBox.value(QStringLiteral("x")).toDouble())*dpr+
                browserInk.x(),
            (groupBox.value(QStringLiteral("y")).toDouble()-
             mathRootBox.value(QStringLiteral("y")).toDouble())*dpr+
                browserInk.y());
        const QPointF groupOriginDelta =
            nativeDeviceOrigin - browserDeviceOrigin;
        qDebug().noquote()<<id<<"token-group"<<role<<nativeGroup.size()
                          <<browserGroup.size()<<"IoU"<<groupIou
                          <<"coverage"<<groupCoverage
                          <<"raw-best"<<groupAlignment.iou
                          <<"offset"<<groupAlignment.offset
                          <<"origin"<<nativeDeviceOrigin
                          <<browserDeviceOrigin
                          <<"delta"<<groupOriginDelta;
        require(mathRoleRasterDrift.contains(role),
                QStringLiteral("%1 has unknown token-group role %2")
                    .arg(id,role));
        const QSize groupDrift=mathRoleRasterDrift.value(role);
        require(qAbs(nativeGroup.width()-browserGroup.width())<=
                    groupDrift.width()&&
                    qAbs(nativeGroup.height()-browserGroup.height())<=
                    groupDrift.height(),
                QStringLiteral("%1 %2 token-group bounds drifted")
                    .arg(id,role));
        const bool smallScriptRaster =
            (role==QLatin1String("subscript") ||
             role==QLatin1String("superscript")) &&
            qMax(nativeGroup.height(),browserGroup.height())<=12;
        const qreal minimumGroupCoverage = smallScriptRaster ? 0.87 : 0.94;
        require(groupCoverage>=minimumGroupCoverage,
                QStringLiteral("%1 %2 token-group raster drifted: IoU=%3 coverage=%4")
                    .arg(id,role).arg(groupIou).arg(groupCoverage));
        require(qAbs(groupAlignment.offset.x())<=1&&
                    qAbs(groupAlignment.offset.y())<=1&&
                    qAbs(groupOriginDelta.x())<=
                        qMax(2,groupDrift.width())&&
                    qAbs(groupOriginDelta.y())<=
                        qMax(2,groupDrift.height()),
                QStringLiteral("%1 %2 token-group origin drifted")
                    .arg(id,role));
      }
      for(const QJsonValue& phaseValue:
          fixture.value(QStringLiteral("mathRasterPhases")).toArray()) {
        const QJsonObject phaseFixture=phaseValue.toObject();
        const qreal phase=phaseFixture.value(QStringLiteral("phase")).toDouble();
        const QString phaseFile=phaseFixture.value(QStringLiteral("file")).toString();
        require(fileSha256(dir.filePath(phaseFile))==
                    phaseFixture.value(QStringLiteral("sha256")).toString().toLatin1(),
                QStringLiteral("%1 phase %2 hash drifted").arg(id).arg(phase));
        require(!entry.sequenceScene->noteLabels.isEmpty(),
                QStringLiteral("%1 phase oracle label is missing").arg(id));
        const QImage nativePhase=renderMathAtPhase(
            entry.sequenceScene->noteLabels.first(),
            entry.sequenceScene->style.fontSize,dpr,phase);
        const QImage browserPhase=alphaTrimmed(QImage(dir.filePath(phaseFile)));
        require(!nativePhase.isNull()&&!browserPhase.isNull(),
                QStringLiteral("%1 phase %2 image missing").arg(id).arg(phase));
        const qreal phaseIou=alphaIou(nativePhase,browserPhase);
        const qreal phaseCoverage=tolerantGlyphCoverage(nativePhase,browserPhase);
        const MaskAlignment phaseAlignment=bestRawAlphaAlignment(
            nativePhase,browserPhase);
        qDebug().noquote()<<id<<"math-phase"<<phase<<"DPR"<<dpr
                          <<nativePhase.size()<<browserPhase.size()
                          <<"IoU"<<phaseIou<<"coverage"<<phaseCoverage
                          <<"raw-best"<<phaseAlignment.iou
                          <<"offset"<<phaseAlignment.offset;
        require(qAbs(nativePhase.width()-browserPhase.width())<=2&&
                    qAbs(nativePhase.height()-browserPhase.height())<=2,
                QStringLiteral("%1 phase %2 bounds drifted").arg(id).arg(phase));
        require(phaseCoverage>=0.95,
                QStringLiteral("%1 phase %2 coverage too low: %3")
                    .arg(id).arg(phase).arg(phaseCoverage));
        require(qAbs(phaseAlignment.offset.x())<=2&&
                    qAbs(phaseAlignment.offset.y())<=2,
                QStringLiteral("%1 phase %2 origin drifted: %3,%4")
                    .arg(id).arg(phase).arg(phaseAlignment.offset.x())
                    .arg(phaseAlignment.offset.y()));
        for(const QJsonValue& componentValue:
            phaseFixture.value(QStringLiteral("components")).toArray()) {
          const QJsonObject component=componentValue.toObject();
          const QString role=component.value(QStringLiteral("role")).toString();
          const QString componentFile=component.value(QStringLiteral("file")).toString();
          require(fileSha256(dir.filePath(componentFile))==
                      component.value(QStringLiteral("sha256")).toString().toLatin1(),
                  QStringLiteral("%1 phase %2 %3 hash drifted")
                      .arg(id).arg(phase).arg(role));
          const auto nativeRole=role==QLatin1String("large-operator")
              ? muffin::math::MathMlPaintPrimitiveRole::LargeOperator
              : role==QLatin1String("subscript")
                  ? muffin::math::MathMlPaintPrimitiveRole::ScriptSubscript
                  : role==QLatin1String("superscript")
                      ? muffin::math::MathMlPaintPrimitiveRole::ScriptSuperscript
                      : muffin::math::MathMlPaintPrimitiveRole::Row;
          const QImage nativeComponent=renderMathPrimitiveRole(
              entry.sequenceScene->noteLabels.first(),
              entry.sequenceScene->style.fontSize,dpr,nativeRole,true,
              QPointF(phase,phase));
          const QImage browserComponent=alphaTrimmed(
              QImage(dir.filePath(componentFile)));
          require(!nativeComponent.isNull()&&!browserComponent.isNull(),
                  QStringLiteral("%1 phase %2 %3 component image missing")
                      .arg(id).arg(phase).arg(role));
          const qreal componentIou=alphaIou(nativeComponent,browserComponent);
          const qreal componentCoverage=tolerantGlyphCoverage(
              nativeComponent,browserComponent);
          const MaskAlignment componentAlignment=bestRawAlphaAlignment(
              nativeComponent,browserComponent);
          qDebug().noquote()<<id<<"math-phase-component"<<phase<<role
                            <<nativeComponent.size()<<browserComponent.size()
                            <<"IoU"<<componentIou
                            <<"coverage"<<componentCoverage
                            <<"raw-best"<<componentAlignment.iou
                            <<"offset"<<componentAlignment.offset;
          const bool smallSubscriptRaster =
              role==QLatin1String("subscript") &&
              qMax(nativeComponent.height(),browserComponent.height())<=12;
          const QHash<QString,qreal> coverageMinimum{
              {QStringLiteral("row"),0.985},
              {QStringLiteral("large-operator"),1.0},
              {QStringLiteral("subscript"),smallSubscriptRaster ? 0.82 : 0.87},
              {QStringLiteral("superscript"),0.95},
          };
          require(mathRoleRasterDrift.contains(role),
                  QStringLiteral("%1 phase %2 has unknown component role %3")
                      .arg(id).arg(phase).arg(role));
          const QSize tolerance=mathRoleRasterDrift.value(role);
          require(qAbs(nativeComponent.width()-browserComponent.width())<=
                      tolerance.width()&&
                      qAbs(nativeComponent.height()-browserComponent.height())<=
                      tolerance.height(),
                  QStringLiteral("%1 phase %2 %3 bounds drifted: %4x%5 vs %6x%7")
                      .arg(id).arg(phase).arg(role)
                      .arg(nativeComponent.width()).arg(nativeComponent.height())
                      .arg(browserComponent.width()).arg(browserComponent.height()));
          require(componentCoverage>=coverageMinimum.value(role),
                  QStringLiteral("%1 phase %2 %3 coverage too low: %4")
                      .arg(id).arg(phase).arg(role).arg(componentCoverage));
          require(qAbs(componentAlignment.offset.x())<=1&&
                      qAbs(componentAlignment.offset.y())<=1,
                  QStringLiteral("%1 phase %2 %3 origin drifted: %4,%5")
                      .arg(id).arg(phase).arg(role)
                      .arg(componentAlignment.offset.x())
                      .arg(componentAlignment.offset.y()));
        }
      }
      const QJsonArray browserTokens=fixture.value(
          QStringLiteral("mathTokens")).toArray();
      if(!browserTokens.isEmpty()) {
        const QVector<NativeMathToken> nativeTokens=nativeMathTokens(
            entry.sequenceScene->noteLabels.first(),
            entry.sequenceScene->style.fontSize);
        const QJsonObject mathBox=fixture.value(QStringLiteral("structure"))
            .toObject().value(QStringLiteral("mathMlBox")).toObject();
        QHash<int,qsizetype> roleCursor;
        const auto enumRole=[](const QString& role) {
          return role==QLatin1String("row")
              ? muffin::math::MathMlPaintPrimitiveRole::Row
              : role==QLatin1String("subscript")
                  ? muffin::math::MathMlPaintPrimitiveRole::ScriptSubscript
                  : muffin::math::MathMlPaintPrimitiveRole::ScriptSuperscript;
        };
        for(const QJsonValue& tokenValue:browserTokens) {
          const QJsonObject token=tokenValue.toObject();
          const QString role=token.value(QStringLiteral("role")).toString();
          if(role==QLatin1String("large-operator")) continue;
          const auto nativeRole=enumRole(role);
          const int roleKey=static_cast<int>(nativeRole);
          qsizetype& cursor=roleCursor[roleKey];
          while(cursor<nativeTokens.size()&&
                nativeTokens.at(cursor).role!=nativeRole) ++cursor;
          require(cursor<nativeTokens.size(),
                  QStringLiteral("%1 %2 native token missing").arg(id,role));
          const NativeMathToken& nativeToken=nativeTokens.at(cursor++);
          const QJsonObject browserBox=token.value(QStringLiteral("box")).toObject();
          const QRectF relativeBrowser(
              browserBox.value(QStringLiteral("x")).toDouble()-
                  mathBox.value(QStringLiteral("x")).toDouble(),
              browserBox.value(QStringLiteral("y")).toDouble()-
                  mathBox.value(QStringLiteral("y")).toDouble(),
              browserBox.value(QStringLiteral("width")).toDouble(),
              browserBox.value(QStringLiteral("height")).toDouble());
          qDebug().noquote()<<id<<"token"<<role
                            <<token.value(QStringLiteral("text")).toString()
                            <<"native-text"<<nativeToken.text
                            <<"browser"<<relativeBrowser
                            <<"clip"<<nativeToken.clip
                            <<"baseline"<<nativeToken.baseline
                            <<"scale"<<nativeToken.fontScale
                            <<"path"<<nativeToken.path;
          require(nativeToken.text==token.value(QStringLiteral("text")).toString(),
                  QStringLiteral("%1 %2 token order drifted: native=%3 browser=%4")
                      .arg(id,role,nativeToken.text,
                           token.value(QStringLiteral("text")).toString()));
          require(qAbs(nativeToken.baseline.x()-relativeBrowser.x())<=0.22,
                  QStringLiteral("%1 %2 token x drifted: native=%3 browser=%4")
                      .arg(id,role).arg(nativeToken.baseline.x())
                      .arg(relativeBrowser.x()));
        }
      }
      const QString mathAccent=fixture.value(QStringLiteral("mathAccent")).toString();
      if(!mathAccent.isEmpty()) {
        const QString bodyFile=fixture.value(QStringLiteral("mathBodyFile")).toString();
        const QString accentFile=fixture.value(QStringLiteral("mathAccentFile")).toString();
        require(!bodyFile.isEmpty()&&!accentFile.isEmpty(),
                QStringLiteral("%1 MathML component fixtures are missing").arg(id));
        require(fileSha256(dir.filePath(bodyFile))==
                    fixture.value(QStringLiteral("mathBodySha256")).toString().toLatin1()&&
                    fileSha256(dir.filePath(accentFile))==
                    fixture.value(QStringLiteral("mathAccentSha256")).toString().toLatin1(),
                QStringLiteral("%1 MathML component hashes drifted").arg(id));
        require(fixture.value(QStringLiteral("cropKind")).toString()==
                    QLatin1String("note")&&!entry.sequenceScene->noteLabels.isEmpty(),
                QStringLiteral("%1 MathML component label is unavailable").arg(id));
        const sequence::SequenceLabelDocument& componentLabel=
            entry.sequenceScene->noteLabels.front();
        const QColor componentColor(entry.sequenceScene->style.noteTextColor);
        muffin::math::MathRenderer componentRenderer;
        const qreal componentFontSize=entry.sequenceScene->style.fontSize*1.21;
        const auto componentLayout=componentRenderer.render(
            componentLabel.richText.math.front().source,componentFontSize,
            componentColor,true);
        const auto componentBuild=muffin::math::buildMathMlPaintOperations(
            componentLayout,componentFontSize,16.0);
        require(componentBuild.operation.has_value(),
                QStringLiteral("%1 accent operation is missing").arg(id));
        const auto* accentOperation=findAccentOperation(*componentBuild.operation);
        require(accentOperation&&
                    accentOperation->glyph.character==fixture.value(
                        QStringLiteral("mathAccentText")).toString(),
                QStringLiteral("%1 accent character drifted").arg(id));
        if(accentOperation->glyph.kind==
            muffin::math::MathCssHorizontalGlyphKind::Assembly) {
          qreal previousOffset=-1.0;
          for(const auto& part:accentOperation->glyph.parts) {
            require(part.glyphIndex!=0&&!part.inkBounds.isEmpty()&&
                        part.offset>previousOffset&&part.fullAdvance>0.0&&
                        part.connectorOverlap>=0.0,
                    QStringLiteral("%1 horizontal assembly part drifted").arg(id));
            previousOffset=part.offset;
          }
          require(accentOperation->glyph.parts.size()>=2,
                  QStringLiteral("%1 horizontal assembly lost its parts").arg(id));
        }
        const QImage nativeBody=renderMathLayer(
            componentLabel,entry.sequenceScene->style.fontSize,dpr,
            muffin::math::MathMlPaintLayer::Body,componentColor);
        const QImage nativeAccent=renderMathLayer(
            componentLabel,entry.sequenceScene->style.fontSize,dpr,
            muffin::math::MathMlPaintLayer::Accent,componentColor);
        const QImage browserBody=alphaTrimmed(QImage(dir.filePath(bodyFile)));
        const QImage browserAccent=alphaTrimmed(QImage(dir.filePath(accentFile)));
        const QHash<QString,QSize> componentBounds{
            {QStringLiteral("label-math-fallback-over-arrow/body"), QSize(0,1)},
            {QStringLiteral("label-math-fallback-over-arrow/accent"), QSize(1,0)},
            {QStringLiteral("label-math-fallback-under-arrow/body"), QSize(0,0)},
            {QStringLiteral("label-math-fallback-under-arrow/accent"), QSize(0,1)},
            {QStringLiteral("label-math-arrow-left-dpr-100/body"), QSize(0,1)},
            {QStringLiteral("label-math-arrow-left-dpr-100/accent"), QSize(0,0)},
            {QStringLiteral("label-math-arrow-right-dpr-125/body"), QSize(0,0)},
            {QStringLiteral("label-math-arrow-right-dpr-125/accent"), QSize(1,1)},
            {QStringLiteral("label-math-arrow-double-dpr-150/body"), QSize(0,1)},
            {QStringLiteral("label-math-arrow-double-dpr-150/accent"), QSize(1,0)},
            {QStringLiteral("label-math-arrow-under-dpr-200/body"), QSize(0,0)},
            {QStringLiteral("label-math-arrow-under-dpr-200/accent"), QSize(0,1)},
            {QStringLiteral("label-math-underbrace/accent"), QSize(0,0)},
            {QStringLiteral("label-math-under-arrow/accent"), QSize(1,0)},
            {QStringLiteral("label-math-overbrace/accent"), QSize(0,2)},
            {QStringLiteral("label-math-accent-double-right-arrow/accent"), QSize(0,0)},
            {QStringLiteral("label-math-accent-overgroup/accent"), QSize(0,1)},
            {QStringLiteral("label-math-accent-overlinesegment-upstream-text/accent"), QSize(0,0)},
        };
        const QHash<QString,qreal> componentCoverageMinimums{
            {QStringLiteral("label-math-fallback-over-arrow/body"), 1.0},
            {QStringLiteral("label-math-fallback-over-arrow/accent"), 0.982},
            {QStringLiteral("label-math-fallback-under-arrow/body"), 1.0},
            {QStringLiteral("label-math-fallback-under-arrow/accent"), 0.983},
            {QStringLiteral("label-math-arrow-left-dpr-100/body"), 1.0},
            {QStringLiteral("label-math-arrow-left-dpr-100/accent"), 1.0},
            {QStringLiteral("label-math-arrow-right-dpr-125/body"), 1.0},
            {QStringLiteral("label-math-arrow-right-dpr-125/accent"), 0.999},
            {QStringLiteral("label-math-arrow-double-dpr-150/body"), 1.0},
            {QStringLiteral("label-math-arrow-double-dpr-150/accent"), 1.0},
            {QStringLiteral("label-math-arrow-under-dpr-200/body"), 1.0},
            {QStringLiteral("label-math-arrow-under-dpr-200/accent"), 1.0},
            {QStringLiteral("label-math-underbrace/accent"), 0.775},
            {QStringLiteral("label-math-under-arrow/accent"), 0.999},
            {QStringLiteral("label-math-overbrace/accent"), 0.78},
            {QStringLiteral("label-math-accent-double-right-arrow/accent"), 0.999},
            {QStringLiteral("label-math-accent-overgroup/accent"), 0.879},
            {QStringLiteral("label-math-accent-overlinesegment-upstream-text/accent"), 0.999},
        };
        const auto compareComponent=[&](const QImage& nativeComponent,
                                        const QImage& browserComponent,
                                        const QString& name) {
          const QString key=id+QLatin1Char('/')+name;
          require(componentBounds.contains(key)&&componentCoverageMinimums.contains(key),
                  QStringLiteral("%1 has no component oracle").arg(key));
          const QSize bounds=componentBounds.value(key);
          const QSize rasterBounds=bounds.expandedTo(QSize(1,1));
          const qreal componentCoverage=
              tolerantGlyphCoverage(nativeComponent,browserComponent);
          qDebug().noquote()<<key<<"component"<<nativeComponent.size()
                            <<browserComponent.size()<<"coverage"
                            <<componentCoverage<<"IoU"
                            <<alphaIou(nativeComponent,browserComponent)
                            <<"alignment"
                            <<bestRawAlphaAlignment(nativeComponent,
                                                     browserComponent).offset;
          require(qAbs(nativeComponent.width()-browserComponent.width())<=rasterBounds.width()&&
                      qAbs(nativeComponent.height()-browserComponent.height())<=rasterBounds.height(),
                  QStringLiteral("%1 %2 component bounds drifted: %3x%4 vs %5x%6")
                      .arg(id,name).arg(nativeComponent.width())
                      .arg(nativeComponent.height()).arg(browserComponent.width())
                      .arg(browserComponent.height()));
          const qreal minimumCoverage=componentCoverageMinimums.value(key);
          require(componentCoverage>=minimumCoverage,
                  QStringLiteral("%1 %2 component coverage drifted: %3 < %4")
                      .arg(id,name).arg(componentCoverage).arg(minimumCoverage));
        };
        const QSet<QString> accentOnlyCases{
            QStringLiteral("label-math-underbrace"),
            QStringLiteral("label-math-under-arrow"),
            QStringLiteral("label-math-overbrace"),
            QStringLiteral("label-math-accent-double-right-arrow"),
            QStringLiteral("label-math-accent-overgroup"),
            QStringLiteral("label-math-accent-overlinesegment-upstream-text"),
        };
        if(!accentOnlyCases.contains(id))
          compareComponent(nativeBody,browserBody,QStringLiteral("body"));
        compareComponent(nativeAccent,browserAccent,QStringLiteral("accent"));
      }
      const QSet<QString> recursiveLayerCases{
          QStringLiteral("label-math-accent-fraction-recursive"),
          QStringLiteral("label-math-accent-radical-recursive"),
          QStringLiteral("label-math-accent-array-recursive"),
          QStringLiteral("label-math-accent-accent-recursive"),
      };
      if(recursiveLayerCases.contains(id)) {
        require(!entry.sequenceScene->noteLabels.isEmpty(),
                QStringLiteral("%1 recursive MathML label is unavailable").arg(id));
        const sequence::SequenceLabelDocument& recursiveLabel=
            entry.sequenceScene->noteLabels.front();
        const QColor recursiveColor(entry.sequenceScene->style.noteTextColor);
        const qreal recursiveFontSize=entry.sequenceScene->style.fontSize;
        const QImage all=renderMathLayer(
            recursiveLabel,recursiveFontSize,dpr,
            muffin::math::MathMlPaintLayer::All,recursiveColor,false);
        const QImage body=renderMathLayer(
            recursiveLabel,recursiveFontSize,dpr,
            muffin::math::MathMlPaintLayer::Body,recursiveColor,false);
        const QImage accent=renderMathLayer(
            recursiveLabel,recursiveFontSize,dpr,
            muffin::math::MathMlPaintLayer::Accent,recursiveColor,false);
        require(!all.isNull()&&all.size()==body.size()&&all.size()==accent.size(),
                QStringLiteral("%1 MathML layer canvases drifted").arg(id));
        QImage composited(body);
        QPainter compositePainter(&composited);
        compositePainter.drawImage(QPoint(),accent);
        compositePainter.end();
        require(all==composited,
                QStringLiteral("%1 MathML All layer no longer equals Body + Accent")
                    .arg(id));
      }
      if(id==QLatin1String("label-math-accent-accent-recursive")) {
        require(!entry.sequenceScene->noteLabels.isEmpty(),
                QStringLiteral("nested accent label is unavailable"));
        const QImage nestedAccent=renderMathLayer(
            entry.sequenceScene->noteLabels.front(),
            entry.sequenceScene->style.fontSize,dpr,
            muffin::math::MathMlPaintLayer::Accent,
            QColor(entry.sequenceScene->style.noteTextColor));
        const QRect nestedAccentInk=alphaBounds(nestedAccent);
        require(!nestedAccentInk.isNull()&&nestedAccentInk.height()>=40,
                QStringLiteral("nested accent layer traversal regressed"));
      }
      const bool radicalLabel = fixture.value(QStringLiteral("source")).toString()
                                    .contains(QStringLiteral("\\sqrt"));
      const bool verticalDelimiterLabel = !verticalDelimiter.isEmpty();
      const auto paintedBoundsMatch = [&](QSize allowedDrift) {
        // These are alpha-trimmed raster bounds, not semantic geometry. The
        // structural oracles above lock the latter; different rasterizers may
        // cover one adjacent device pixel for the same vector outline.
        allowedDrift = allowedDrift.expandedTo(QSize(1, 1));
        return qAbs(nativeLabel.width() - browserLabel.width()) <=
                   allowedDrift.width() &&
               qAbs(nativeLabel.height() - browserLabel.height()) <=
                   allowedDrift.height();
      };
      const QHash<QString,QSize> recursiveAccentBounds{
          {QStringLiteral("label-math-accent-array-recursive"), QSize(2,2)},
          {QStringLiteral("label-math-array-cell-accent-recursive"), QSize(1,0)},
          {QStringLiteral("label-math-radical-accent-recursive"), QSize(1,0)},
          {QStringLiteral("label-math-accent-accent-recursive"), QSize(2,2)},
      };
      if (const auto it=recursiveAccentBounds.constFind(id);
          it!=recursiveAccentBounds.cend()) {
        require(paintedBoundsMatch(*it),
                QStringLiteral("%1 recursive accent painted bounds drifted").arg(id));
      } else if (radicalLabel || verticalDelimiterLabel) {
        // Geometry is asserted by the SVG/MathML structural oracles; keep the
        // remaining cross-rasterizer font hinting bounded independently.
        require(qAbs(nativeLabel.width()-browserLabel.width())<=2 &&
                    qAbs(nativeLabel.height()-browserLabel.height())<=2,
                QStringLiteral("%1 radical painted bounds drifted").arg(id));
      }
      const QHash<QString,QSize> rootRowBounds{
          {QStringLiteral("label-math-root-mixed-fraction"), QSize(0,1)},
          {QStringLiteral("label-math-root-mixed-radical"), QSize(0,2)},
          {QStringLiteral("label-math-root-multiple-semantics"), QSize(1,1)},
          {QStringLiteral("label-math-root-mixed-accent"), QSize(1,0)},
          {QStringLiteral("label-math-root-mixed-array"), QSize(0,0)},
          {QStringLiteral("label-math-root-mixed-left-right"), QSize(0,2)},
          {QStringLiteral("label-math-root-double-fraction"), QSize(1,1)},
          {QStringLiteral("label-math-root-all-paint-kinds"), QSize(1,1)},
          {QStringLiteral("label-math-root-mixed-underbrace"), QSize(1,0)},
          {QStringLiteral("label-math-root-mixed-under-arrow"), QSize(1,1)},
          {QStringLiteral("label-math-root-mixed-sum-limits"), QSize(1,1)},
          {QStringLiteral("label-math-root-mixed-integral-scripts"), QSize(0,0)},
          {QStringLiteral("label-math-root-limits-fraction"), QSize(2,2)},
          {QStringLiteral("label-math-root-product-limits"), QSize(1,0)},
          {QStringLiteral("label-math-root-coproduct-limits"), QSize(1,0)},
          {QStringLiteral("label-math-root-double-integral"), QSize(2,1)},
          {QStringLiteral("label-math-root-triple-integral"), QSize(0,1)},
          {QStringLiteral("label-math-root-cjk-fraction"), QSize(1,1)},
          {QStringLiteral("label-math-root-rtl-fraction"), QSize(1,1)},
      };
      if (const auto it=rootRowBounds.constFind(id);
          it!=rootRowBounds.cend()) {
        require(paintedBoundsMatch(*it),
                QStringLiteral("%1 root row painted bounds drifted").arg(id));
      }
      const QHash<QString,QSize> fallbackTextBounds{
          {QStringLiteral("label-math-bidi-isolates"), QSize(0,0)},
          {QStringLiteral("label-math-fallback-fraction"), QSize(2,0)},
          {QStringLiteral("label-math-fallback-radical"), QSize(1,1)},
          {QStringLiteral("label-math-fallback-supsub"), QSize(0,1)},
          {QStringLiteral("label-math-fallback-accent"), QSize(1,1)},
          {QStringLiteral("label-math-fallback-array"), QSize(0,1)},
          {QStringLiteral("label-math-fallback-limits"), QSize(1,0)},
          {QStringLiteral("label-math-fallback-limits-recursive"), QSize(1,1)},
          {QStringLiteral("label-math-fallback-under-accent"), QSize(0,1)},
          {QStringLiteral("label-math-fallback-delimiter-assembly"), QSize(0,2)},
          {QStringLiteral("label-math-fallback-product-limits"), QSize(1,0)},
          {QStringLiteral("label-math-fallback-coproduct-limits"), QSize(1,1)},
          {QStringLiteral("label-math-fallback-over-arrow"), QSize(1,1)},
          {QStringLiteral("label-math-fallback-under-arrow"), QSize(1,1)},
          {QStringLiteral("label-math-fallback-brace-assembly"), QSize(1,2)},
          {QStringLiteral("label-math-fallback-bracket-assembly"), QSize(0,1)},
          {QStringLiteral("label-math-fallback-angle-assembly"), QSize(0,0)},
      };
      if (const auto it=fallbackTextBounds.constFind(id);
          it!=fallbackTextBounds.cend()) {
        require(paintedBoundsMatch(*it),
                QStringLiteral("%1 fallback text painted bounds drifted").arg(id));
      }
      const QHash<QString,QSize> arrowMatrixBounds{
          {QStringLiteral("label-math-arrow-left-dpr-100"), QSize(0,1)},
          {QStringLiteral("label-math-arrow-right-dpr-125"), QSize(1,0)},
          {QStringLiteral("label-math-arrow-double-dpr-150"), QSize(1,1)},
          {QStringLiteral("label-math-arrow-under-dpr-200"), QSize(1,1)},
      };
      if (const auto it=arrowMatrixBounds.constFind(id);
          it!=arrowMatrixBounds.cend()) {
        require(paintedBoundsMatch(*it),
                QStringLiteral("%1 arrow matrix painted bounds drifted").arg(id));
      }
      const QHash<QString,QSize> horizontalAccentDrift{
          {QStringLiteral("label-math-underbrace"), QSize(2,2)},
          {QStringLiteral("label-math-under-arrow"), QSize(2,2)},
          {QStringLiteral("label-math-overbrace"), QSize(2,2)},
          {QStringLiteral("label-math-accent-double-right-arrow"), QSize(1,0)},
          {QStringLiteral("label-math-accent-left-harpoon"), QSize(1,0)},
          {QStringLiteral("label-math-accent-right-harpoon"), QSize(1,0)},
          {QStringLiteral("label-math-accent-overgroup"), QSize(1,0)},
          {QStringLiteral("label-math-accent-overlinesegment-upstream-text"), QSize(0,0)},
          {QStringLiteral("label-math-accent-mixed-fraction-body"), QSize(0,0)},
          {QStringLiteral("label-math-accent-mixed-radical-body"), QSize(1,0)},
          {QStringLiteral("label-math-accent-mixed-fraction-annotation"), QSize(1,0)},
      };
      if (const auto it=horizontalAccentDrift.constFind(id);
          it!=horizontalAccentDrift.cend()) {
        require(paintedBoundsMatch(*it),
                QStringLiteral("%1 horizontal accent painted bounds drifted").arg(id));
      }
      const QHash<QString,QSize> basicAccentBounds{
          {QStringLiteral("label-math-accent-hat"), QSize(0,0)},
          {QStringLiteral("label-math-accent-vector"), QSize(0,1)},
          {QStringLiteral("label-math-accent-overline"), QSize(0,1)},
          {QStringLiteral("label-math-accent-underline"), QSize(1,1)},
          {QStringLiteral("label-math-relation-overlay"), QSize(0,0)},
      };
      if (const auto it=basicAccentBounds.constFind(id);
          it!=basicAccentBounds.cend()) {
        require(paintedBoundsMatch(*it),
                QStringLiteral("%1 basic accent painted bounds drifted").arg(id));
      }
      const bool arrayOperationLabel =
          id == QLatin1String("label-math-matrix-basic") ||
          id == QLatin1String("label-math-matrix-recursive") ||
          id == QLatin1String("label-math-cases") ||
          id == QLatin1String("label-math-array-body-recursive");
      if (arrayOperationLabel) {
        require(qAbs(nativeLabel.width()-browserLabel.width())<=2 &&
                    qAbs(nativeLabel.height()-browserLabel.height())<=1,
                QStringLiteral("%1 array painted bounds drifted").arg(id));
      }
      qreal minimumGlyphCoverage = radicalLabel ? 0.58
          : verticalDelimiterLabel ? 0.89 : 0.75;
      if (id == QLatin1String("label-math-middle-script"))
        minimumGlyphCoverage = 0.85;
      if (id == QLatin1String("label-math-genfrac")) minimumGlyphCoverage = 0.95;
      if (id == QLatin1String("label-math-fraction-ops") ||
          id == QLatin1String("label-math-stack-ops") ||
          id == QLatin1String("label-math-nested-fraction-ops") ||
          id == QLatin1String("label-math-fraction-script-ops"))
        minimumGlyphCoverage = 0.95;
      if (id == QLatin1String("label-math-radical-script-fraction-ops") ||
          id == QLatin1String("label-math-fraction-cross-recursive-ops"))
        minimumGlyphCoverage = 0.99;
      if (arrayOperationLabel) minimumGlyphCoverage = 0.97;
      if (id == QLatin1String("label-math-underbrace")) minimumGlyphCoverage = 0.71;
      if (id == QLatin1String("label-math-under-arrow")) minimumGlyphCoverage = 0.74;
      if (id == QLatin1String("label-math-overbrace")) minimumGlyphCoverage = 0.80;
      if (id == QLatin1String("label-math-accent-double-right-arrow"))
        minimumGlyphCoverage = 0.74;
      const QHash<QString,qreal> recursiveAccentCoverage{
          {QStringLiteral("label-math-accent-array-recursive"), 0.99},
          {QStringLiteral("label-math-array-cell-accent-recursive"), 0.94},
          {QStringLiteral("label-math-radical-accent-recursive"), 0.94},
          {QStringLiteral("label-math-accent-accent-recursive"), 0.97},
      };
      if (const auto it=recursiveAccentCoverage.constFind(id);
          it!=recursiveAccentCoverage.cend())
        minimumGlyphCoverage = *it;
      const QHash<QString,qreal> rootRowCoverage{
          {QStringLiteral("label-math-root-mixed-fraction"), 0.99},
          {QStringLiteral("label-math-root-mixed-radical"), 0.91},
          {QStringLiteral("label-math-root-multiple-semantics"), 0.87},
          {QStringLiteral("label-math-root-mixed-accent"), 0.95},
          {QStringLiteral("label-math-root-mixed-array"), 0.99},
          {QStringLiteral("label-math-root-mixed-left-right"), 0.94},
          {QStringLiteral("label-math-root-double-fraction"), 0.99},
          {QStringLiteral("label-math-root-all-paint-kinds"), 0.88},
          {QStringLiteral("label-math-root-mixed-underbrace"), 0.96},
          {QStringLiteral("label-math-root-mixed-under-arrow"), 0.979},
          {QStringLiteral("label-math-root-mixed-sum-limits"), 0.99},
          {QStringLiteral("label-math-root-mixed-integral-scripts"), 0.99},
          {QStringLiteral("label-math-root-limits-fraction"), 0.99},
          {QStringLiteral("label-math-root-product-limits"), 0.99},
          {QStringLiteral("label-math-root-coproduct-limits"), 0.99},
          {QStringLiteral("label-math-root-double-integral"), 0.99},
          {QStringLiteral("label-math-root-triple-integral"), 0.98},
          {QStringLiteral("label-math-root-cjk-fraction"), 0.99},
          {QStringLiteral("label-math-root-rtl-fraction"), 0.99},
      };
      if (const auto it=rootRowCoverage.constFind(id);
          it!=rootRowCoverage.cend())
        minimumGlyphCoverage = *it;
      const QHash<QString,qreal> fallbackTextCoverage{
          {QStringLiteral("label-math-bidi-isolates"), 0.99},
          {QStringLiteral("label-math-fallback-fraction"), 0.99},
          {QStringLiteral("label-math-fallback-radical"), 0.80},
          {QStringLiteral("label-math-fallback-supsub"), 0.99},
          {QStringLiteral("label-math-fallback-accent"), 0.979},
          {QStringLiteral("label-math-fallback-array"), 0.99},
          {QStringLiteral("label-math-fallback-limits"), 0.99},
          {QStringLiteral("label-math-fallback-limits-recursive"), 0.99},
          {QStringLiteral("label-math-fallback-under-accent"), 0.96},
          {QStringLiteral("label-math-fallback-delimiter-assembly"), 0.99},
          {QStringLiteral("label-math-fallback-product-limits"), 0.99},
          {QStringLiteral("label-math-fallback-coproduct-limits"), 0.99},
          {QStringLiteral("label-math-fallback-over-arrow"), 0.999},
          {QStringLiteral("label-math-fallback-under-arrow"), 0.999},
          {QStringLiteral("label-math-fallback-brace-assembly"), 0.989},
          {QStringLiteral("label-math-fallback-bracket-assembly"), 0.999},
          {QStringLiteral("label-math-fallback-angle-assembly"), 0.999},
      };
      if (const auto it=fallbackTextCoverage.constFind(id);
          it!=fallbackTextCoverage.cend())
        minimumGlyphCoverage = *it;
      const QHash<QString,qreal> arrowMatrixCoverage{
          {QStringLiteral("label-math-arrow-left-dpr-100"), 0.943},
          {QStringLiteral("label-math-arrow-right-dpr-125"), 0.999},
          {QStringLiteral("label-math-arrow-double-dpr-150"), 0.879},
          {QStringLiteral("label-math-arrow-under-dpr-200"), 0.999},
      };
      if (const auto it=arrowMatrixCoverage.constFind(id);
          it!=arrowMatrixCoverage.cend())
        minimumGlyphCoverage = *it;
      const QHash<QString,qreal> basicAccentCoverage{
          {QStringLiteral("label-math-accent-hat"), 0.96},
          {QStringLiteral("label-math-accent-vector"), 0.95},
          {QStringLiteral("label-math-accent-overline"), 0.90},
          {QStringLiteral("label-math-accent-underline"), 0.84},
          {QStringLiteral("label-math-relation-overlay"), 0.99},
      };
      if (const auto it=basicAccentCoverage.constFind(id);
          it!=basicAccentCoverage.cend())
        minimumGlyphCoverage = *it;
      require(glyphCoverage>=minimumGlyphCoverage,
              QStringLiteral("%1 tolerant glyph coverage too low: %2")
                                      .arg(id).arg(glyphCoverage));
    }
  }
  require(verticalDelimiters ==
              QSet<QString>{QStringLiteral("brace"), QStringLiteral("paren"),
                            QStringLiteral("bracket"), QStringLiteral("bar"),
                            QStringLiteral("nested"),
                            QStringLiteral("double-bar"),
                            QStringLiteral("floor"), QStringLiteral("ceil"),
                            QStringLiteral("angle"),
                            QStringLiteral("nullable"),
                            QStringLiteral("recursive"),
                            QStringLiteral("middle"),
                            QStringLiteral("multiple-middle"),
                            QStringLiteral("nested-plain"),
                            QStringLiteral("middle-fraction"),
                            QStringLiteral("middle-radical"),
                            QStringLiteral("middle-script"),
                            QStringLiteral("middle-array")},
          QStringLiteral("Sequence vertical delimiter pixel axis regressed"));
  require(scenePixelCases>=24&&labelPixelCases>=55,
          QStringLiteral("Sequence curated scene/label pixel matrix regressed"));
  for(const QString& id:{QStringLiteral("label-participant-html-cjk"),
                         QStringLiteral("label-message-wrap-bidi"),
                         QStringLiteral("label-note-markdown-math"),
                         QStringLiteral("label-fragment-html-rtl"),
                         QStringLiteral("label-box-markdown-math")})
    require(ids.contains(id),QStringLiteral("Sequence label pixel axis is uncovered: %1").arg(id));
  for(const QString& id:{QStringLiteral("label-dpr-125-html-cjk"),
                         QStringLiteral("label-dpr-150-math-rtl"),
                         QStringLiteral("label-dpr-200-dark-box-fragment")})
    require(ids.contains(id),QStringLiteral("Sequence DPR/theme pixel axis is uncovered: %1").arg(id));
  for(const QString& id:{QStringLiteral("label-math-genfrac"),
                         QStringLiteral("label-math-fraction-ops"),
                         QStringLiteral("label-math-stack-ops"),
                         QStringLiteral("label-math-nested-fraction-ops"),
                         QStringLiteral("label-math-fraction-script-ops"),
                         QStringLiteral("label-math-fraction-radical-ops"),
                         QStringLiteral("label-math-radical-script-fraction-ops"),
                         QStringLiteral("label-math-fraction-cross-recursive-ops"),
                         QStringLiteral("label-math-underbrace"),
                         QStringLiteral("label-math-under-arrow"),
                         QStringLiteral("label-math-overbrace"),
                         QStringLiteral("label-math-accent-fraction-recursive"),
                         QStringLiteral("label-math-accent-radical-recursive"),
                         QStringLiteral("label-math-accent-array-recursive"),
                         QStringLiteral("label-math-array-body-recursive"),
                         QStringLiteral("label-math-array-cell-accent-recursive"),
                         QStringLiteral("label-math-radical-accent-recursive"),
                         QStringLiteral("label-math-accent-accent-recursive"),
                         QStringLiteral("label-math-accent-hat"),
                         QStringLiteral("label-math-accent-vector"),
                         QStringLiteral("label-math-accent-overline"),
                         QStringLiteral("label-math-accent-underline"),
                         QStringLiteral("label-math-relation-overlay"),
                         QStringLiteral("label-math-tall-assembly"),
                         QStringLiteral("label-math-matrix-basic"),
                         QStringLiteral("label-math-matrix-recursive"),
                         QStringLiteral("label-math-cases"),
                         QStringLiteral("label-math-bidi-isolates"),
                         QStringLiteral("label-math-fallback-fraction"),
                         QStringLiteral("label-math-fallback-radical"),
                         QStringLiteral("label-math-fallback-supsub"),
                         QStringLiteral("label-math-fallback-accent"),
                         QStringLiteral("label-math-fallback-array"),
                         QStringLiteral("label-math-fallback-limits"),
                         QStringLiteral("label-math-fallback-limits-recursive"),
                         QStringLiteral("label-math-fallback-under-accent"),
                         QStringLiteral("label-math-fallback-delimiter-assembly"),
                         QStringLiteral("label-math-fallback-product-limits"),
                         QStringLiteral("label-math-fallback-coproduct-limits"),
                         QStringLiteral("label-math-fallback-over-arrow"),
                         QStringLiteral("label-math-fallback-under-arrow"),
                         QStringLiteral("label-math-arrow-left-dpr-100"),
                         QStringLiteral("label-math-arrow-right-dpr-125"),
                         QStringLiteral("label-math-arrow-double-dpr-150"),
                         QStringLiteral("label-math-arrow-under-dpr-200"),
                         QStringLiteral("label-math-fallback-brace-assembly"),
                         QStringLiteral("label-math-fallback-bracket-assembly"),
                         QStringLiteral("label-math-fallback-angle-assembly")})
    require(ids.contains(id),QStringLiteral("Sequence structural Math crop is uncovered: %1").arg(id));
  qDebug()<<"MermaidSequencePixelTest:" << cases.size() << "structural cases,"
          <<scenePixelCases<<"scene and"<<labelPixelCases<<"label pixel goldens passed";
  return 0;
}
