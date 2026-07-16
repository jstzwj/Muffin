#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdlib>

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote()<<message; std::exit(1); }
void require(bool condition,const QString& message) { if(!condition) fail(message); }
QJsonObject load(const QString& path) {
  QFile file(path); require(file.open(QIODevice::ReadOnly),QStringLiteral("Cannot open %1").arg(path));
  return QJsonDocument::fromJson(file.readAll()).object();
}
QSet<QString> uniqueIds(const QJsonArray& cases,const QString& fixture) {
  QSet<QString> ids;
  for(const QJsonValue& value:cases) {
    const QString id=value.toObject().value(QStringLiteral("id")).toString();
    require(!id.isEmpty()&&!ids.contains(id),QStringLiteral("%1 duplicate/empty case id: %2").arg(fixture,id));
    ids.insert(id);
  }
  return ids;
}
}  // namespace

int main(int argc,char** argv) {
  require(argc==2,QStringLiteral("Expected sequence DB fixture path"));
  const QFileInfo dbInfo(QString::fromLocal8Bit(argv[1]));
  const QString dir=dbInfo.absolutePath();
  const QJsonObject db=load(dbInfo.absoluteFilePath());
  const QJsonObject fuzz=load(dir+QStringLiteral("/sequence-differential-fuzz.json"));
  const QJsonObject layout=load(dir+QStringLiteral("/sequence-layout.json"));
  const QJsonObject label=load(dir+QStringLiteral("/sequence-label.json"));
  const QJsonObject pixel=load(dir+QStringLiteral("/sequence-pixel/manifest.json"));

  const QJsonArray dbCases=db.value(QStringLiteral("cases")).toArray();
  const QJsonArray negativeCases=fuzz.value(QStringLiteral("negativeCases")).toArray();
  const QJsonArray layoutCases=layout.value(QStringLiteral("cases")).toArray();
  const QJsonArray labelCases=label.value(QStringLiteral("cases")).toArray();
  const QJsonArray pixelCases=pixel.value(QStringLiteral("cases")).toArray();
  const QSet<QString> dbIds=uniqueIds(dbCases,QStringLiteral("sequence-db"));
  const QSet<QString> negativeIds=uniqueIds(negativeCases,QStringLiteral("sequence-differential-fuzz"));
  const QSet<QString> layoutIds=uniqueIds(layoutCases,QStringLiteral("sequence-layout"));
  const QSet<QString> labelIds=uniqueIds(labelCases,QStringLiteral("sequence-label"));
  const QSet<QString> pixelIds=uniqueIds(pixelCases,QStringLiteral("sequence-pixel"));

  QSet<int> productions;
  for(const QJsonValue& value:dbCases)
    for(const QJsonValue& reduction:value.toObject().value(QStringLiteral("reductions")).toArray())
      productions.insert(reduction.toInt());
  require(dbCases.size()>=13&&productions.size()>=98,
          QStringLiteral("sequence grammar production coverage regressed"));

  QSet<QString> operators,codes,stages;
  for(const QJsonValue& value:negativeCases) {
    const QJsonObject item=value.toObject();
    operators.insert(item.value(QStringLiteral("operator")).toString());
    codes.insert(item.value(QStringLiteral("expectedNativeCode")).toString());
    stages.insert(item.value(QStringLiteral("upstreamError")).toObject()
                      .value(QStringLiteral("stage")).toString());
  }
  require(negativeCases.size()>=15&&operators.size()>=6&&codes.size()>=8&&stages.size()>=3,
          QStringLiteral("sequence diagnostic mutation coverage regressed"));

  require(layoutCases.size()>=14&&labelCases.size()>=24&&pixelCases.size()>=20,
          QStringLiteral("sequence layout/label/pixel case coverage regressed"));
  for(const QString& id:{QStringLiteral("participant-types"),QStringLiteral("create-destroy-markers"),
                         QStringLiteral("activation-note"),QStringLiteral("nested-fragment"),
                         QStringLiteral("label-box-markdown-math"),QStringLiteral("central-autonumber"),
                         QStringLiteral("structural-aria"),QStringLiteral("structural-combined-order")})
    require(pixelIds.contains(id),QStringLiteral("sequence semantic axis missing: %1").arg(id));

  QSet<QString> themes; QSet<QString> dprs;
  int mathCases=0,bidiCases=0,cjkCases=0,cropCases=0,markerCases=0,ariaCases=0;
  for(const QJsonValue& value:pixelCases) {
    const QJsonObject item=value.toObject();
    themes.insert(item.value(QStringLiteral("theme")).toString(QStringLiteral("default")));
    dprs.insert(QString::number(item.value(QStringLiteral("dpr")).toDouble(1.0),'g',3));
    const QString source=item.value(QStringLiteral("source")).toString();
    mathCases+=source.contains(QStringLiteral("$$"));
    bidiCases+=source.contains(QChar(0x0645))||source.contains(QChar(0x05e9));
    cjkCases+=source.contains(QChar(0x4e2d))||source.contains(QChar(0x5ba2));
    cropCases+=!item.value(QStringLiteral("cropFile")).toString().isEmpty();
    const QJsonObject structure=item.value(QStringLiteral("structure")).toObject();
    markerCases+=structure.value(QStringLiteral("markers")).toArray().size()>=8;
    ariaCases+=!structure.value(QStringLiteral("ariaLabelledBy")).toString().isEmpty();
  }
  require(themes==QSet<QString>{QStringLiteral("default"),QStringLiteral("dark")}&&dprs.size()>=4&&
              mathCases>=3&&bidiCases>=4&&cjkCases>=5&&cropCases>=11&&
              markerCases==pixelCases.size()&&ariaCases>=1,
          QStringLiteral("sequence theme/DPR/label/SVG coverage regressed"));

  require(labelIds.contains(QStringLiteral("note-math-fraction"))&&
              labelIds.contains(QStringLiteral("note-math-sqrt-sub-sup"))&&
              labelIds.contains(QStringLiteral("note-math-matrix"))&&
              labelIds.contains(QStringLiteral("note-math-multiple-spans"))&&
              labelIds.contains(QStringLiteral("message-bidi-isolates"))&&
              labelIds.contains(QStringLiteral("participant-wrap-prefix")),
          QStringLiteral("sequence label structure axis regressed"));
  require(!dbIds.isEmpty()&&!negativeIds.isEmpty()&&!layoutIds.isEmpty(),
          QStringLiteral("sequence coverage fixtures unexpectedly empty"));
  qDebug()<<"MermaidSequenceCoverageMatrixTest:"<<productions.size()<<"productions,"
          <<codes.size()<<"diagnostics,"<<labelCases.size()<<"labels and"
          <<pixelCases.size()<<"pixel/SVG cases passed";
  return 0;
}
