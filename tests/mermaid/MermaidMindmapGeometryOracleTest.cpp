#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/mindmap/MindmapScene.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;
namespace {
[[noreturn]] void fail(const QString& s){std::fprintf(stderr,"FAIL: %s\n",qPrintable(s));std::exit(1);}
void require(bool v,const QString&s){if(!v)fail(s);}
std::shared_ptr<const mindmap::MindmapScene> render(const QString&s,editor::MermaidRenderEntry*out=nullptr){
  editor::MermaidRenderCache cache; auto e=cache.getSync(cache.makeKey(s),s); if(out)*out=e;
  return e.status==editor::MermaidRenderStatus::Ready?std::dynamic_pointer_cast<const mindmap::MindmapScene>(e.scene):nullptr;
}
QVector<qreal> nums(const QString&s){static const QRegularExpression r(QStringLiteral(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"));QVector<qreal>v;auto i=r.globalMatch(s);while(i.hasNext())v<<i.next().captured().toDouble();return v;}
bool close(qreal a,qreal b,qreal t=.7){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=t;}
void compare(const QJsonObject& f,QStringList& errors){
  const QString id=f.value("id").toString(); editor::MermaidRenderEntry entry; auto scene=render(f.value("source").toString(),&entry);
  if(f.value("status").toString()==QLatin1String("error")){if(scene)errors<<id+": expected error";return;}
  if(!scene){errors<<id+": "+entry.errorMessage;return;}
  const QJsonObject e=f.value("expected").toObject(); const auto vb=nums(e.value("root").toObject().value("attrs").toObject().value("viewBox").toString());
  constexpr qreal tol = 0.001;
  if(vb.size()==4&&(!close(scene->bounds.x(),vb[0],tol)||!close(scene->bounds.y(),vb[1],tol)||!close(scene->bounds.width(),vb[2],tol)||!close(scene->bounds.height(),vb[3],tol)))
    errors<<QStringLiteral("%1: viewBox native %2,%3 %4x%5 content %10,%11 %12x%13 browser %6,%7 %8x%9").arg(id).arg(scene->bounds.x()).arg(scene->bounds.y()).arg(scene->bounds.width()).arg(scene->bounds.height()).arg(vb[0]).arg(vb[1]).arg(vb[2]).arg(vb[3]).arg(scene->contentBounds.x()).arg(scene->contentBounds.y()).arg(scene->contentBounds.width()).arg(scene->contentBounds.height());
  const QJsonArray ns=e.value("nodes").toArray(); if(scene->nodes.size()!=ns.size())errors<<id+": node count";
  for(qsizetype i=0;i<std::min(scene->nodes.size(),ns.size());++i){const auto o=ns[i].toObject();const auto tr=nums(o.value("attrs").toObject().value("transform").toString());const auto b=o.value("shape").toObject().value("bbox").toObject();const auto&n=scene->nodes[i];
    if(tr.size()<2||!close(n.center.x(),tr[0],tol)||!close(n.center.y(),tr[1],tol))errors<<QStringLiteral("%1: node %2 center %3,%4").arg(id).arg(i).arg(n.center.x()).arg(n.center.y());
    const auto group=o.value("bbox").toObject();
    const QRectF nativeGroup=n.paintedBounds.united(n.label.bounds);
    if(!close(nativeGroup.x(),group.value("x").toDouble(),tol)||!close(nativeGroup.y(),group.value("y").toDouble(),tol)||!close(nativeGroup.width(),group.value("width").toDouble(),tol)||!close(nativeGroup.height(),group.value("height").toDouble(),tol))errors<<QStringLiteral("%1: node %2 group %3,%4 %5x%6 browser %7,%8 %9x%10").arg(id).arg(i).arg(nativeGroup.x()).arg(nativeGroup.y()).arg(nativeGroup.width()).arg(nativeGroup.height()).arg(group.value("x").toDouble()).arg(group.value("y").toDouble()).arg(group.value("width").toDouble()).arg(group.value("height").toDouble());
    if(!close(n.localBounds.width(),b.value("width").toDouble(),tol)||!close(n.localBounds.height(),b.value("height").toDouble(),tol))errors<<QStringLiteral("%1: node %2 shape %3x%4").arg(id).arg(i).arg(n.localBounds.width()).arg(n.localBounds.height());
    const auto lb=o.value("label").toObject().value("bbox").toObject();
    if(!close(n.label.bounds.width(),lb.value("width").toDouble(),tol)||!close(n.label.bounds.height(),lb.value("height").toDouble(),tol))errors<<QStringLiteral("%1: node %2 label %3x%4 browser %5x%6").arg(id).arg(i).arg(n.label.bounds.width()).arg(n.label.bounds.height()).arg(lb.value("width").toDouble()).arg(lb.value("height").toDouble());
  }
  const QJsonArray es=e.value("edges").toArray(); if(scene->edges.size()!=es.size())errors<<id+": edge count";
  for(qsizetype i=0;i<std::min(scene->edges.size(),es.size());++i){const QString d=es[i].toObject().value("attrs").toObject().value("d").toString();if(scene->edges[i].path!=d)errors<<QStringLiteral("%1: edge %2 path native=%3 browser=%4").arg(id).arg(i).arg(scene->edges[i].path,d);}
}
}
int main(int argc,char**argv){qputenv("QT_QPA_PLATFORM","offscreen");QGuiApplication app(argc,argv);MermaidFontRegistry::ensureLoaded();require(argc==2,"fixture arg");QFile file(QString::fromLocal8Bit(argv[1]));require(file.open(QIODevice::ReadOnly),file.errorString());auto root=QJsonDocument::fromJson(file.readAll()).object();require(root.value("fixtureSha256").toString()==QLatin1String("4e5527dffcdcac99584f0db575b422c9917f381c5a080deea6d2671f557ec9b5"),"fixture drift");const QJsonObject upstream=root.value("upstream").toObject();require(upstream.value("package").toString()==QLatin1String("mermaid")&&upstream.value("version").toString()==QLatin1String("11.16.0")&&upstream.value("moduleSha256").toString()==QLatin1String("fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b"),"upstream drift");const QJsonObject layout=upstream.value("layout").toObject();require(layout.value("cytoscape").toString()==QLatin1String("3.34.0")&&layout.value("coseBilkent").toString()==QLatin1String("4.1.0")&&layout.value("quality").toString()==QLatin1String("proof"),"layout provenance drift");require(root.value("cases").toArray().size()==20,"geometry case count");QStringList errors;for(auto v:root.value("cases").toArray())compare(v.toObject(),errors);if(!errors.isEmpty())fail(errors.join('\n'));}
