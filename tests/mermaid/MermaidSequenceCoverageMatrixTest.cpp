#include <QDebug>
#include <QDir>
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
void collectMathMlTags(const QJsonObject& node,QSet<QString>* tags) {
  tags->insert(node.value(QStringLiteral("tag")).toString());
  for(const QJsonValue& child:node.value(QStringLiteral("children")).toArray())
    collectMathMlTags(child.toObject(),tags);
}
void collectPngReferences(const QJsonValue& value,QSet<QString>* files) {
  if(value.isString()) {
    const QString name=value.toString();
    if(name.endsWith(QLatin1String(".png"))) files->insert(name);
  } else if(value.isArray()) {
    for(const QJsonValue& child:value.toArray()) collectPngReferences(child,files);
  } else if(value.isObject()) {
    for(const QJsonValue& child:value.toObject()) collectPngReferences(child,files);
  }
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
  const QJsonObject mathml=load(dir+QStringLiteral("/mathml-css-box.json"));

  const QJsonArray dbCases=db.value(QStringLiteral("cases")).toArray();
  const QJsonArray negativeCases=fuzz.value(QStringLiteral("negativeCases")).toArray();
  const QJsonArray layoutCases=layout.value(QStringLiteral("cases")).toArray();
  const QJsonArray labelCases=label.value(QStringLiteral("cases")).toArray();
  const QJsonArray pixelCases=pixel.value(QStringLiteral("cases")).toArray();
  const QJsonArray mathmlCases=mathml.value(QStringLiteral("cases")).toArray();
  const QSet<QString> dbIds=uniqueIds(dbCases,QStringLiteral("sequence-db"));
  const QSet<QString> negativeIds=uniqueIds(negativeCases,QStringLiteral("sequence-differential-fuzz"));
  const QSet<QString> layoutIds=uniqueIds(layoutCases,QStringLiteral("sequence-layout"));
  const QSet<QString> labelIds=uniqueIds(labelCases,QStringLiteral("sequence-label"));
  const QSet<QString> pixelIds=uniqueIds(pixelCases,QStringLiteral("sequence-pixel"));
  const QSet<QString> mathmlIds=uniqueIds(mathmlCases,QStringLiteral("mathml-css-box"));
  QSet<QString> referencedPngs;
  collectPngReferences(pixel,&referencedPngs);
  const QStringList diskPngList=QDir(dir+QStringLiteral("/sequence-pixel"))
      .entryList({QStringLiteral("*.png")},QDir::Files,QDir::Name);
  const QSet<QString> diskPngs(diskPngList.cbegin(),diskPngList.cend());
  require(referencedPngs==diskPngs&&referencedPngs.size()==311,
          QStringLiteral("Sequence PNG fixture references/orphans regressed: manifest=%1 disk=%2")
              .arg(referencedPngs.size()).arg(diskPngs.size()));

  QSet<int> productions;
  for(const QJsonValue& value:dbCases)
    for(const QJsonValue& reduction:value.toObject().value(QStringLiteral("reductions")).toArray())
      productions.insert(reduction.toInt());
  QSet<int> declaredCoveredProductions;
  for(const QJsonValue& value:db.value(QStringLiteral("productions")).toArray())
    if(value.toObject().value(QStringLiteral("status")).toString()==QLatin1String("covered"))
      declaredCoveredProductions.insert(value.toObject().value(QStringLiteral("id")).toInt());
  require(dbCases.size()>=13&&productions.size()==98&&
              productions==declaredCoveredProductions,
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

  require(layoutCases.size()>=17&&labelCases.size()>=24&&pixelCases.size()>=20,
          QStringLiteral("sequence layout/label/pixel case coverage regressed"));
  QSet<QString> layoutAxes;
  for(const QJsonValue& value:layoutCases)
    for(const QJsonValue& axis:value.toObject().value(QStringLiteral("axes")).toArray())
      layoutAxes.insert(axis.toString());
  for(const QString& axis:{QStringLiteral("sequence-config"),QStringLiteral("viewport-config"),
                           QStringLiteral("wrap-margin-stage"),QStringLiteral("wrap-final-stage"),
                           QStringLiteral("participant-lifecycle"),QStringLiteral("activation-lifecycle")})
    require(layoutAxes.contains(axis),
            QStringLiteral("sequence final layout axis missing: %1").arg(axis));
  const QJsonObject configContract=layout.value(QStringLiteral("configContract")).toObject();
  require(configContract.value(QStringLiteral("layout")).toArray().size()==14&&
              configContract.value(QStringLiteral("viewport")).toArray().size()==3&&
              configContract.value(QStringLiteral("upstreamInert")).toArray()==
                  QJsonArray{QStringLiteral("messageMargin")},
          QStringLiteral("sequence final config ownership matrix regressed"));
  for(const QString& id:{QStringLiteral("participant-types"),QStringLiteral("create-destroy-markers"),
                         QStringLiteral("activation-note"),QStringLiteral("nested-fragment"),
                         QStringLiteral("label-box-markdown-math"),QStringLiteral("central-autonumber"),
                         QStringLiteral("structural-aria"),QStringLiteral("structural-combined-order")})
    require(pixelIds.contains(id),QStringLiteral("sequence semantic axis missing: %1").arg(id));
  for(const QString& id:{QStringLiteral("label-math-radical-script-fraction-ops"),
                         QStringLiteral("label-math-fraction-cross-recursive-ops"),
                         QStringLiteral("label-math-accent-fraction-recursive"),
                         QStringLiteral("label-math-accent-radical-recursive"),
                         QStringLiteral("label-math-accent-array-recursive"),
                         QStringLiteral("label-math-array-body-recursive"),
                         QStringLiteral("label-math-array-cell-accent-recursive"),
                         QStringLiteral("label-math-radical-accent-recursive"),
                         QStringLiteral("label-math-accent-accent-recursive"),
                         QStringLiteral("label-math-accent-text-shaping"),
                         QStringLiteral("label-math-accent-double-right-arrow"),
                         QStringLiteral("label-math-accent-left-harpoon"),
                         QStringLiteral("label-math-accent-right-harpoon"),
                         QStringLiteral("label-math-accent-overgroup"),
                         QStringLiteral("label-math-accent-overlinesegment-upstream-text"),
                         QStringLiteral("label-math-accent-hat"),
                         QStringLiteral("label-math-accent-vector"),
                         QStringLiteral("label-math-accent-overline"),
                         QStringLiteral("label-math-accent-underline"),
                         QStringLiteral("label-math-relation-overlay"),
                         QStringLiteral("label-math-accent-mixed-fraction-body"),
                         QStringLiteral("label-math-accent-mixed-radical-body"),
                         QStringLiteral("label-math-accent-mixed-fraction-annotation"),
                         QStringLiteral("label-math-root-mixed-fraction"),
                         QStringLiteral("label-math-root-mixed-radical"),
                         QStringLiteral("label-math-root-multiple-semantics"),
                         QStringLiteral("label-math-root-mixed-accent"),
                         QStringLiteral("label-math-root-mixed-array"),
                         QStringLiteral("label-math-root-mixed-left-right"),
                         QStringLiteral("label-math-root-double-fraction"),
                         QStringLiteral("label-math-root-all-paint-kinds"),
                         QStringLiteral("label-math-root-mixed-underbrace"),
                         QStringLiteral("label-math-root-mixed-under-arrow"),
                         QStringLiteral("label-math-root-mixed-sum-limits"),
                         QStringLiteral("label-math-root-mixed-integral-scripts"),
                         QStringLiteral("label-math-root-limits-fraction"),
                         QStringLiteral("label-math-root-product-limits"),
                         QStringLiteral("label-math-root-coproduct-limits"),
                         QStringLiteral("label-math-root-double-integral"),
                         QStringLiteral("label-math-root-triple-integral"),
                         QStringLiteral("label-math-root-sum-limits"),
                         QStringLiteral("label-math-root-contour-integral"),
                         QStringLiteral("label-math-root-big-union"),
                         QStringLiteral("label-math-root-big-intersection"),
                         QStringLiteral("label-math-root-cjk-fraction"),
                         QStringLiteral("label-math-root-rtl-fraction"),
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
                         QStringLiteral("label-math-fallback-angle-assembly"),
                         QStringLiteral("label-math-matrix-basic"),
                         QStringLiteral("label-math-matrix-recursive"),
                         QStringLiteral("label-math-cases"),
                         QStringLiteral("label-math-tall-assembly"),
                         QStringLiteral("label-math-tall-paren-assembly"),
                         QStringLiteral("label-math-tall-bracket-assembly"),
                         QStringLiteral("label-math-tall-bar-assembly"),
                         QStringLiteral("label-math-nested-delimiters"),
                         QStringLiteral("label-math-tall-double-bar"),
                         QStringLiteral("label-math-tall-floor"),
                         QStringLiteral("label-math-tall-ceil"),
                         QStringLiteral("label-math-tall-angle"),
                         QStringLiteral("label-math-nullable-delimiter"),
                         QStringLiteral("label-math-delimiter-recursive"),
                         QStringLiteral("label-math-middle-delimiter"),
                         QStringLiteral("label-math-multiple-middle-delimiter"),
                         QStringLiteral("label-math-nested-plain-delimiter"),
                         QStringLiteral("label-math-middle-fraction"),
                         QStringLiteral("label-math-middle-radical"),
                         QStringLiteral("label-math-middle-script"),
                         QStringLiteral("label-math-middle-array")})
    require(pixelIds.contains(id),
            QStringLiteral("sequence recursive Math paint axis missing: %1").arg(id));

  QSet<QString> themes; QSet<QString> dprs;
  int mathCases=0,bidiCases=0,cjkCases=0,cropCases=0,scenePixelCases=0,
      markerCases=0,ariaCases=0;
  for(const QJsonValue& value:pixelCases) {
    const QJsonObject item=value.toObject();
    themes.insert(item.value(QStringLiteral("theme")).toString(QStringLiteral("default")));
    dprs.insert(QString::number(item.value(QStringLiteral("dpr")).toDouble(1.0),'g',3));
    const QString source=item.value(QStringLiteral("source")).toString();
    mathCases+=source.contains(QStringLiteral("$$"));
    bidiCases+=source.contains(QChar(0x0645))||source.contains(QChar(0x05e9));
    cjkCases+=source.contains(QChar(0x4e2d))||source.contains(QChar(0x5ba2));
    cropCases+=!item.value(QStringLiteral("cropFile")).toString().isEmpty();
    scenePixelCases+=!item.value(QStringLiteral("file")).toString().isEmpty();
    const QJsonObject structure=item.value(QStringLiteral("structure")).toObject();
    markerCases+=structure.value(QStringLiteral("markers")).toArray().size()>=8;
    ariaCases+=!structure.value(QStringLiteral("ariaLabelledBy")).toString().isEmpty();
  }
  // redux-color / redux-dark-color joined the theme axis with the actor +
  // activation rotation cases.
  require(themes==QSet<QString>{QStringLiteral("default"),QStringLiteral("dark"),
                                QStringLiteral("redux-color"),
                                QStringLiteral("redux-dark-color")}&&dprs.size()>=4&&
              mathCases>=3&&bidiCases>=4&&cjkCases>=5&&scenePixelCases>=24&&
              cropCases>=68&&
              markerCases==pixelCases.size()&&ariaCases>=1,
          QStringLiteral("sequence theme/DPR/label/SVG coverage regressed"));

  require(labelIds.contains(QStringLiteral("note-math-fraction"))&&
              labelIds.contains(QStringLiteral("note-math-sqrt-sub-sup"))&&
              labelIds.contains(QStringLiteral("note-math-matrix"))&&
              labelIds.contains(QStringLiteral("note-math-multiple-spans"))&&
              labelIds.contains(QStringLiteral("message-bidi-isolates"))&&
              labelIds.contains(QStringLiteral("participant-wrap-prefix")),
          QStringLiteral("sequence label structure axis regressed"));
  QSet<QString> mathmlTags; QSet<QString> mathmlDprs; QSet<int> mathmlFonts;
  for(const QJsonValue& value:mathmlCases) {
    const QJsonObject item=value.toObject();
    collectMathMlTags(item.value(QStringLiteral("tree")).toObject(),&mathmlTags);
    mathmlDprs.insert(QString::number(item.value(QStringLiteral("dpr")).toDouble(),'g',3));
    mathmlFonts.insert(item.value(QStringLiteral("fontSize")).toInt());
  }
  require(mathml.value(QStringLiteral("mermaidVersion")).toString()==QLatin1String("11.16.0")&&
              mathmlCases.size()>=50&&mathmlDprs.size()>=4&&mathmlFonts.size()>=3&&
              mathmlIds.contains(QStringLiteral("fraction-nested"))&&
              mathmlIds.contains(QStringLiteral("fraction-sup"))&&
              mathmlIds.contains(QStringLiteral("fraction-radical"))&&
              mathmlIds.contains(QStringLiteral("sqrt-fraction"))&&
              mathmlIds.contains(QStringLiteral("radical-script-fraction"))&&
              mathmlIds.contains(QStringLiteral("script-radical-fraction"))&&
              mathmlIds.contains(QStringLiteral("fraction-cross-recursive"))&&
              mathmlIds.contains(QStringLiteral("matrix-3x3")),
          QStringLiteral("sequence recursive MathML CSS box coverage regressed"));
  for(const QString& tag:{QStringLiteral("math"),QStringLiteral("mfrac"),
                          QStringLiteral("msup"),QStringLiteral("msub"),
                          QStringLiteral("msubsup"),QStringLiteral("msqrt"),
                          QStringLiteral("mroot"),QStringLiteral("mtable")})
    require(mathmlTags.contains(tag),QStringLiteral("sequence MathML tag axis missing: %1").arg(tag));
  require(!dbIds.isEmpty()&&!negativeIds.isEmpty()&&!layoutIds.isEmpty(),
          QStringLiteral("sequence coverage fixtures unexpectedly empty"));
  qDebug()<<"MermaidSequenceCoverageMatrixTest:"<<productions.size()<<"productions,"
          <<codes.size()<<"diagnostics,"<<labelCases.size()<<"labels,"
          <<mathmlCases.size()<<"MathML boxes and"<<pixelCases.size()<<"pixel/SVG cases passed";
  return 0;
}
