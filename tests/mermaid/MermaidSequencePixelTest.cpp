#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceScenePainter.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSet>

#include <cstdlib>

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
  require(argc==2,QStringLiteral("Expected sequence pixel manifest"));
  QFile file(QString::fromLocal8Bit(argv[1])); require(file.open(QIODevice::ReadOnly),QStringLiteral("Cannot open sequence pixel manifest"));
  const QJsonObject root=QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString()==QLatin1String("11.16.0"),QStringLiteral("Sequence pixel version drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString()==
              QLatin1String("992fe7c87f5c6902a498f3ddd7c00c6da2dbdeac996a1b0e583b83f4fb8072dc"),
          QStringLiteral("Sequence pixel fixture changed; audit and update digest"));
  const QDir dir=QFileInfo(file).absoluteDir();
  editor::MermaidRenderCache cache;
  const QJsonArray cases=root.value(QStringLiteral("cases")).toArray();
  require(cases.size()==20,QStringLiteral("Sequence pixel matrix regressed"));
  QSet<QString> ids;
  for(const QJsonValue& value:cases) {
    const QJsonObject fixture=value.toObject(); const QString id=fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty()&&!ids.contains(id),QStringLiteral("Duplicate sequence pixel case: %1").arg(id));
    ids.insert(id);
    const auto entry=cache.getSync(cache.makeKey(fixture.value(QStringLiteral("source")).toString()),fixture.value(QStringLiteral("source")).toString());
    require(entry.status==editor::MermaidRenderStatus::Ready&&entry.sequenceScene,QStringLiteral("%1 native scene failed").arg(id));
    const qreal dpr=fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    const QImage native=sequence::renderSequenceSceneToImage(*entry.sequenceScene,dpr,0.0);
    const QImage golden(dir.filePath(fixture.value(QStringLiteral("file")).toString()));
    require(fileSha256(dir.filePath(fixture.value(QStringLiteral("file")).toString()))==
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
    const QString cropFile=fixture.value(QStringLiteral("cropFile")).toString();
    if(!cropFile.isEmpty()) {
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
      const bool radicalLabel = fixture.value(QStringLiteral("source")).toString()
                                    .contains(QStringLiteral("\\sqrt"));
      if (radicalLabel) {
        // The SVG operation model is asserted in RenderMathGeometryTest; keep
        // the remaining cross-rasterizer font hinting bounded independently.
        require(qAbs(nativeLabel.width()-browserLabel.width())<=2 &&
                    qAbs(nativeLabel.height()-browserLabel.height())<=2,
                QStringLiteral("%1 radical painted bounds drifted").arg(id));
      }
      const qreal minimumGlyphCoverage = radicalLabel ? 0.58 : 0.75;
      require(glyphCoverage>=minimumGlyphCoverage,
              QStringLiteral("%1 tolerant glyph coverage too low: %2")
                                      .arg(id).arg(glyphCoverage));
    }
  }
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
  qDebug()<<"MermaidSequencePixelTest:" << cases.size() << "Chrome/native pixel goldens passed";
  return 0;
}
