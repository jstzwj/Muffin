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
              QLatin1String("b8603569e232912a7822d79cd235dd7d8040a013c671ef74594a0c07db83bd93"),
          QStringLiteral("Sequence pixel fixture changed; audit and update digest"));
  const QDir dir=QFileInfo(file).absoluteDir();
  editor::MermaidRenderCache cache;
  const QJsonArray cases=root.value(QStringLiteral("cases")).toArray();
  require(cases.size()==4,QStringLiteral("Sequence pixel matrix regressed"));
  for(const QJsonValue& value:cases) {
    const QJsonObject fixture=value.toObject(); const QString id=fixture.value(QStringLiteral("id")).toString();
    const auto entry=cache.getSync(cache.makeKey(fixture.value(QStringLiteral("source")).toString()),fixture.value(QStringLiteral("source")).toString());
    require(entry.status==editor::MermaidRenderStatus::Ready&&entry.sequenceScene,QStringLiteral("%1 native scene failed").arg(id));
    const QImage native=sequence::renderSequenceSceneToImage(*entry.sequenceScene,1.0,0.0);
    const QImage golden(dir.filePath(fixture.value(QStringLiteral("file")).toString()));
    require(!native.isNull()&&!golden.isNull(),QStringLiteral("%1 pixel image missing").arg(id));
    const qreal nativeRatio=qreal(native.width())/native.height(), goldenRatio=qreal(golden.width())/golden.height();
    require(qAbs(nativeRatio-goldenRatio)/goldenRatio<=0.35,
            QStringLiteral("%1 canvas ratio mismatch: %2 vs %3").arg(id).arg(nativeRatio).arg(goldenRatio));
    const qreal iou=alphaIou(native,golden);
    require(iou>=0.20,QStringLiteral("%1 alpha IoU too low: %2").arg(id).arg(iou));
    const Stats ns=stats(native), gs=stats(golden);
    require(ns.opaque>100&&gs.opaque>100,QStringLiteral("%1 rendered blank").arg(id));
    const auto average=[](qint64 sum,int count){return count?qreal(sum)/count:0.0;};
    const qreal colorDistance=qAbs(average(ns.red,ns.opaque)-average(gs.red,gs.opaque))+
        qAbs(average(ns.green,ns.opaque)-average(gs.green,gs.opaque))+
        qAbs(average(ns.blue,ns.opaque)-average(gs.blue,gs.opaque));
    require(colorDistance<260.0,QStringLiteral("%1 mean color drift: %2").arg(id).arg(colorDistance));
  }
  qDebug()<<"MermaidSequencePixelTest:" << cases.size() << "Chrome/native pixel goldens passed";
  return 0;
}
