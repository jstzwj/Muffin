#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/block/BlockDiagram.h"
#include "mermaid/block/BlockScene.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { std::fprintf(stderr,"FAIL: %s\n",qPrintable(message)); std::exit(1); }
void require(bool value,const QString& message){if(!value)fail(message);}
void near(qreal actual,qreal expected,qreal tolerance,const QString& path){
  if(std::abs(actual-expected)>tolerance)fail(QStringLiteral("%1: %2 != %3").arg(path).arg(actual,0,'g',17).arg(expected,0,'g',17));
}
QJsonValue scalar(const QJsonObject& object,const char* key,const QJsonValue& fallback){
  const QJsonValue value=object.value(QLatin1String(key));
  return value.isUndefined()||value.isNull()||value.isArray()||value.isObject()?fallback:value;
}
block::BlockScene build(const QString& source,const QString& id){
  const auto pre=preprocessDiagram(source);
  const QString themeName=editor::themeFromConfig(pre.config);
  const auto theme=flowtheme::resolveFlowTheme(editor::themeIdFromName(themeName),editor::themeOverrides(pre.config));
  const QJsonObject family=pre.config.value(QStringLiteral("block")).toObject();
  block::BlockConfig config;
  config.padding=scalar(family,"padding",8.0);
  config.useMaxWidth=scalar(family,"useMaxWidth",true);
  config.htmlLabels=pre.config.value(QStringLiteral("htmlLabels")).isUndefined()?true:pre.config.value(QStringLiteral("htmlLabels")).toBool();
  config.look=pre.config.value(QStringLiteral("look")).toString();
  config.handDrawnSeed=quint32(editor::jsNumberValue(pre.config.value(QStringLiteral("handDrawnSeed"))));
  config.svgId=QStringLiteral("block-")+id;
  return block::buildBlockScene(block::BlockDiagram::parse(pre.code),config,theme);
}
QVector<qreal> numbers(const QString& text){
  QVector<qreal> result;static const QRegularExpression re(QStringLiteral(R"([-+]?(?:\d+(?:\.\d+)?|\.\d+)(?:e[-+]?\d+)?)"),QRegularExpression::CaseInsensitiveOption);
  auto it=re.globalMatch(text);while(it.hasNext())result.append(it.next().captured().toDouble());return result;
}
}

int main(int argc,char** argv){
  qputenv("QT_QPA_PLATFORM","offscreen");QGuiApplication app(argc,argv);MermaidFontRegistry::ensureLoaded();
  require(argc==2,QStringLiteral("Expected Block geometry fixture"));QFile file(QString::fromLocal8Bit(argv[1]));require(file.open(QIODevice::ReadOnly),file.errorString());
  const QByteArray bytes=file.readAll();const QJsonObject root=QJsonDocument::fromJson(bytes).object();
  require(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==QByteArrayLiteral("b5d8d7c74410623770c925e204bc0895b806374b178378d503fd04a6f767ebc1"),QStringLiteral("Block geometry fixture bytes changed"));
  require(root.value(QStringLiteral("fixtureSha256")).toString()==QLatin1String("453b606dcec4532ee1b0064916c8eba982bc7fd7107b2b2b0dbb61c6efbbd826"),QStringLiteral("Block geometry provenance changed"));
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString()==QLatin1String("11.16.0"),QStringLiteral("version"));
  const QJsonArray cases=root.value(QStringLiteral("cases")).toArray();require(cases.size()==12,QStringLiteral("case count"));
  for(const QJsonValue& value:cases){
    const QJsonObject fixture=value.toObject();const QString id=fixture.value(QStringLiteral("id")).toString();
    const block::BlockScene scene=build(fixture.value(QStringLiteral("source")).toString(),id);const QJsonObject expected=fixture.value(QStringLiteral("expected")).toObject();
    const QVector<qreal> vb=numbers(expected.value(QStringLiteral("root")).toObject().value(QStringLiteral("attrs")).toObject().value(QStringLiteral("viewBox")).toString());
    require(vb.size()==4,id+"/viewBox");near(scene.bounds.x(),vb[0],0.005,id+"/x");near(scene.bounds.y(),vb[1],0.005,id+"/y");near(scene.bounds.width(),vb[2],0.005,id+"/w");near(scene.bounds.height(),vb[3],0.005,id+"/h");
    QVector<QJsonObject> groups;for(const QJsonValue& g:expected.value(QStringLiteral("groups")).toArray()){const QJsonObject o=g.toObject();if(o.value(QStringLiteral("class")).toString().startsWith(QLatin1String("node ")))groups.append(o);}
    require(scene.nodes.size()==groups.size(),id+"/node-count");
    for(qsizetype i=0;i<scene.nodes.size();++i){const auto& node=scene.nodes.at(i);const QJsonObject group=groups.at(i);const QVector<qreal> transform=numbers(group.value(QStringLiteral("transform")).toString());const QJsonObject box=group.value(QStringLiteral("bbox")).toObject();
      near(node.center.x(),transform.value(0),0.005,id+QStringLiteral("/node%1/cx").arg(i));near(node.center.y(),transform.value(1),0.005,id+QStringLiteral("/node%1/cy").arg(i));near(node.paintSize.width(),box.value(QStringLiteral("width")).toDouble(),0.005,id+QStringLiteral("/node%1/w").arg(i));near(node.paintSize.height(),box.value(QStringLiteral("height")).toDouble(),0.005,id+QStringLiteral("/node%1/h").arg(i));
    }
    QVector<QString> paths;for(const QJsonValue& p:expected.value(QStringLiteral("primitives")).toArray()){const QJsonObject o=p.toObject();if(o.value(QStringLiteral("attrs")).toObject().value(QStringLiteral("class")).toString().contains(QStringLiteral("flowchart-link")))paths.append(o.value(QStringLiteral("attrs")).toObject().value(QStringLiteral("d")).toString());}
    require(scene.edges.size()==paths.size(),id+"/edge-count");for(qsizetype i=0;i<paths.size();++i){const auto a=numbers(scene.edges.at(i).path),b=numbers(paths.at(i));require(a.size()==b.size(),id+"/path-structure");for(qsizetype j=0;j<a.size();++j)near(a[j],b[j],0.001,id+QStringLiteral("/edge%1/%2").arg(i).arg(j));}
  }
  std::puts("MermaidBlockGeometryOracleTest: 12/12 passed");return 0;
}
