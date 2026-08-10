#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace muffin::mermaid;
namespace { [[noreturn]]void fail(const QString&s){std::fprintf(stderr,"FAIL: %s\n",qPrintable(s));std::exit(1);}void req(bool v,const QString&s){if(!v)fail(s);}QImage native(const QString&s){auto u=editor::MermaidRenderCache::renderMermaidSourceToPng(s,1).dataUrl;QImage i;i.loadFromData(QByteArray::fromBase64(u.mid(u.indexOf(',')+1).toLatin1()),"PNG");return i.convertToFormat(QImage::Format_RGBA8888);}qreal iou(const QImage&a,const QImage&b){qint64 n=0,d=0;for(int y=0;y<b.height();++y)for(int x=0;x<b.width();++x){bool p=a.pixelColor(x,y).alpha()>=32,q=b.pixelColor(x,y).alpha()>=32;n+=p&&q;d+=p||q;}return d?qreal(n)/d:1;}qreal rgba(const QImage&a,const QImage&b){qreal d=0;qint64 n=0;for(int y=0;y<b.height();++y)for(int x=0;x<b.width();++x){auto p=a.pixelColor(x,y),q=b.pixelColor(x,y);if(p.alpha()<16&&q.alpha()<16)continue;d+=std::abs(p.red()-q.red())+std::abs(p.green()-q.green())+std::abs(p.blue()-q.blue())+std::abs(p.alpha()-q.alpha());++n;}return n?1-d/(n*1020):1;}}
int main(int argc,char**argv){qputenv("QT_QPA_PLATFORM","offscreen");QGuiApplication app(argc,argv);MermaidFontRegistry::ensureLoaded();req(argc==2,"manifest arg");QFileInfo info(QString::fromLocal8Bit(argv[1]));QFile f(info.absoluteFilePath());req(f.open(QIODevice::ReadOnly),f.errorString());auto root=QJsonDocument::fromJson(f.readAll()).object();req(root.value("fixtureSha256").toString()==QLatin1String("e73e83d95f92f5dfa600f07a1128b90bc74fcba663dd5c06e102e3cc1f6cf4b6"),"fixture drift");for(auto v:root.value("cases").toArray()){auto o=v.toObject();QString id=o.value("id").toString();QFile p(info.absolutePath()+'/'+o.value("file").toString());req(p.open(QIODevice::ReadOnly),id+": png");QByteArray bytes=p.readAll();req(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==o.value("sha256").toString().toLatin1(),id+": sha");QImage ref;ref.loadFromData(bytes,"PNG");auto got=native(o.value("source").toString());req(got.size()==ref.size(),QStringLiteral("%1 size %2x%3 vs %4x%5").arg(id).arg(got.width()).arg(got.height()).arg(ref.width()).arg(ref.height()));qreal ai=iou(got,ref),rs=rgba(got,ref);std::fprintf(stderr,"%s IoU %.4f RGBA %.4f\n",qPrintable(id),ai,rs);qreal floor=id=="hand-drawn"?.72:.88;req(ai>=floor&&rs>=floor,id+": pixel parity");}}
