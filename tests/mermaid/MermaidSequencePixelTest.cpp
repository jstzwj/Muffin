#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceScenePainter.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool value, const QString& message) { if (!value) fail(message); }

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
}

int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  require(argc==2,QStringLiteral("Expected sequence pixel manifest"));
  QFile file(QString::fromLocal8Bit(argv[1])); require(file.open(QIODevice::ReadOnly),QStringLiteral("Cannot open sequence pixel manifest"));
  const QJsonObject root=QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString()==QLatin1String("11.16.0"),QStringLiteral("Sequence pixel version drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString()==
              QLatin1String("bc06d7d5c0e25fbbc10a427fb32878d1defb4e9c3918f7b885645b4ca03bc991"),
          QStringLiteral("Sequence pixel fixture changed; audit and update digest"));
  const QDir dir=QFileInfo(file).absoluteDir();
  editor::MermaidRenderCache cache;
  const QJsonArray cases=root.value(QStringLiteral("cases")).toArray();
  require(cases.size()==12,QStringLiteral("Sequence pixel matrix regressed"));
  QSet<QString> ids;
  for(const QJsonValue& value:cases) {
    const QJsonObject fixture=value.toObject(); const QString id=fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty()&&!ids.contains(id),QStringLiteral("Duplicate sequence pixel case: %1").arg(id));
    ids.insert(id);
    const auto entry=cache.getSync(cache.makeKey(fixture.value(QStringLiteral("source")).toString()),fixture.value(QStringLiteral("source")).toString());
    require(entry.status==editor::MermaidRenderStatus::Ready&&entry.sequenceScene,QStringLiteral("%1 native scene failed").arg(id));
    const QImage native=sequence::renderSequenceSceneToImage(*entry.sequenceScene,1.0,0.0);
    const QImage golden(dir.filePath(fixture.value(QStringLiteral("file")).toString()));
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
    require(iou>=0.80,QStringLiteral("%1 alpha IoU too low: %2").arg(id).arg(iou));
    const Stats ns=stats(native), gs=stats(golden);
    require(ns.opaque>100&&gs.opaque>100,QStringLiteral("%1 rendered blank").arg(id));
    const auto average=[](qint64 sum,int count){return count?qreal(sum)/count:0.0;};
    const qreal colorDistance=qAbs(average(ns.red,ns.opaque)-average(gs.red,gs.opaque))+
        qAbs(average(ns.green,ns.opaque)-average(gs.green,gs.opaque))+
        qAbs(average(ns.blue,ns.opaque)-average(gs.blue,gs.opaque));
    require(colorDistance<260.0,QStringLiteral("%1 mean color drift: %2").arg(id).arg(colorDistance));
  }
  for(const QString& id:{QStringLiteral("label-participant-html-cjk"),
                         QStringLiteral("label-message-wrap-bidi"),
                         QStringLiteral("label-note-markdown-math"),
                         QStringLiteral("label-fragment-html-rtl"),
                         QStringLiteral("label-box-markdown-math")})
    require(ids.contains(id),QStringLiteral("Sequence label pixel axis is uncovered: %1").arg(id));
  qDebug()<<"MermaidSequencePixelTest:" << cases.size() << "Chrome/native pixel goldens passed";
  return 0;
}
