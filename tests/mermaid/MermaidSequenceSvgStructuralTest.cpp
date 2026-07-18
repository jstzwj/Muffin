#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceDiagram.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

int nativeMathCount(const sequence::SequenceScene& scene) {
  int count=0;
  const auto add=[&](const auto& labels) {
    for(const auto& label:labels) count+=label.richText.math.size();
  };
  add(scene.boxLabels); add(scene.participantLabels); add(scene.messageLabels);
  add(scene.noteLabels); add(scene.fragmentLabels); add(scene.fragmentKindLabels);
  return count;
}
}  // namespace

int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  require(argc==2,QStringLiteral("Expected sequence structural manifest"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly),QStringLiteral("Cannot open sequence structural manifest"));
  const QJsonObject root=QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString()==QLatin1String("11.16.0")&&
              root.value(QStringLiteral("fontMode")).toString()==
                  QLatin1String("bundled-noto-stix-two-math-2.13b171")&&
              root.value(QStringLiteral("fixtureSha256")).toString()==
                  QLatin1String("48b085f32f41e1f9a9b43879392c4ad113b3853852473790c3574839bfaec1e4"),
          QStringLiteral("Sequence SVG structural fixture drifted"));

  editor::MermaidRenderCache cache;
  int mathCases=0,foreignObjectCases=0,ariaCases=0,labelCases=0,domEntries=0,markerEntries=0;
  const QJsonArray cases=root.value(QStringLiteral("cases")).toArray();
  require(cases.size()==113,QStringLiteral("Sequence SVG structural matrix regressed"));
  for(const QJsonValue& value:cases) {
    const QJsonObject fixture=value.toObject();
    const QString id=fixture.value(QStringLiteral("id")).toString();
    const QString source=fixture.value(QStringLiteral("source")).toString();
    const QJsonObject structure=fixture.value(QStringLiteral("structure")).toObject();
    require(structure.value(QStringLiteral("role")).toString()==
                QLatin1String("graphics-document document")&&
                structure.value(QStringLiteral("ariaRoleDescription")).toString()==
                QLatin1String("sequence"),
            QStringLiteral("%1 root ARIA role drifted").arg(id));

    const QStringList viewBox=structure.value(QStringLiteral("viewBox")).toString()
                                  .split(QRegularExpression(QStringLiteral("\\s+")),Qt::SkipEmptyParts);
    require(viewBox.size()==4&&viewBox[2].toDouble()>0.0&&viewBox[3].toDouble()>0.0,
            QStringLiteral("%1 viewBox is invalid").arg(id));
    require(qAbs(viewBox[2].toDouble()-fixture.value(QStringLiteral("width")).toDouble())<1.01&&
                qAbs(viewBox[3].toDouble()-fixture.value(QStringLiteral("height")).toDouble())<1.01,
            QStringLiteral("%1 viewBox/canvas structure mismatch").arg(id));

    const int textCount=structure.value(QStringLiteral("text")).toInt();
    const int tspanCount=structure.value(QStringLiteral("tspan")).toInt();
    const int foreignCount=structure.value(QStringLiteral("foreignObject")).toInt();
    const int mathCount=structure.value(QStringLiteral("math")).toInt();
    const int clipCount=structure.value(QStringLiteral("clipPath")).toInt();
    require(textCount>0&&tspanCount>0&&foreignCount>=0&&mathCount>=0&&clipCount>=0,
            QStringLiteral("%1 SVG element counts are invalid").arg(id));
    require((mathCount>0)==(foreignCount>0),
            QStringLiteral("%1 MathML/foreignObject pairing drifted").arg(id));
    const QJsonObject mathBox=structure.value(QStringLiteral("mathMlBox")).toObject();
    const QJsonArray mathRuns=structure.value(QStringLiteral("mathMlRuns")).toArray();
    if(mathCount>0) {
      require(mathBox.value(QStringLiteral("width")).toDouble()>0.0&&
                  mathBox.value(QStringLiteral("height")).toDouble()>0.0&&
                  mathBox.value(QStringLiteral("fontFamily")).toString()
                      .contains(QLatin1String("STIX Two Math"))&&
                  !mathRuns.isEmpty(),
              QStringLiteral("%1 MathML bbox/run oracle is incomplete").arg(id));
      for(const QJsonValue& runValue:mathRuns) {
        const QJsonObject run=runValue.toObject();
        const QString tag=run.value(QStringLiteral("tag")).toString();
        const qreal width=run.value(QStringLiteral("width")).toDouble();
        const bool zeroAdvanceOperator=tag==QLatin1String("mo")&&
            !run.value(QStringLiteral("text")).toString().isEmpty()&&
            qFuzzyIsNull(width);
        require(!tag.isEmpty()&&width>=0.0&&
                    (width>0.0||zeroAdvanceOperator)&&
                    run.value(QStringLiteral("height")).toDouble()>0.0,
                QStringLiteral("%1 MathML run coordinate is invalid").arg(id));
      }
      if(!fixture.value(QStringLiteral("mathAccent")).toString().isEmpty()) {
        bool hasBody=false,hasAccent=false;
        for(const QJsonValue& runValue:mathRuns) {
          const QString tag=runValue.toObject()
                                .value(QStringLiteral("tag")).toString();
          hasBody=hasBody||tag!=QLatin1String("mo");
          hasAccent=hasAccent||tag==QLatin1String("mo");
        }
        require(hasBody&&hasAccent,
                QStringLiteral("%1 MathML accent/body structure drifted").arg(id));
      }
    } else {
      require(mathBox.isEmpty()&&mathRuns.isEmpty(),
              QStringLiteral("%1 non-Math label gained MathML coordinates").arg(id));
    }
    mathCases+=mathCount>0; foreignObjectCases+=foreignCount>0;
    const QJsonArray markers=structure.value(QStringLiteral("markers")).toArray();
    require(structure.value(QStringLiteral("defs")).toInt()>=markers.size()&&markers.size()>=8,
            QStringLiteral("%1 defs/marker count drifted").arg(id));
    for(const QJsonValue& markerValue:markers) {
      const QJsonObject marker=markerValue.toObject();
      require(!marker.value(QStringLiteral("id")).toString().isEmpty()&&
                  !marker.value(QStringLiteral("refX")).toString().isEmpty()&&
                  !marker.value(QStringLiteral("refY")).toString().isEmpty()&&
                  !marker.value(QStringLiteral("markerWidth")).toString().isEmpty()&&
                  !marker.value(QStringLiteral("markerHeight")).toString().isEmpty()&&
                  !marker.value(QStringLiteral("orient")).toString().isEmpty()&&
                  !marker.value(QStringLiteral("childTag")).toString().isEmpty(),
              QStringLiteral("%1 marker structural attributes drifted").arg(id));
    }
    markerEntries+=markers.size();
    const QJsonArray classSet=structure.value(QStringLiteral("classSet")).toArray();
    require(classSet.size()>=8,QStringLiteral("%1 SVG class set regressed").arg(id));

    const QJsonArray order=structure.value(QStringLiteral("domOrder")).toArray();
    const QJsonArray textNodes=structure.value(QStringLiteral("textNodes")).toArray();
    require(!order.isEmpty()&&textNodes.size()==textCount+foreignCount,
            QStringLiteral("%1 DOM/text node structure drifted").arg(id));
    for(const QJsonValue& entry:order)
      require(entry.toString().contains(QLatin1Char(':')),
              QStringLiteral("%1 DOM order entry lost tag/class identity").arg(id));
    domEntries+=order.size();

    const QString cropKind=fixture.value(QStringLiteral("cropKind")).toString();
    if(!cropKind.isEmpty()) {
      ++labelCases;
      const QString tag=structure.value(QStringLiteral("labelTag")).toString();
      const QString cssClass=structure.value(QStringLiteral("labelClass")).toString();
      require(tag==QLatin1String("text")||tag==QLatin1String("foreignObject"),
              QStringLiteral("%1 label container tag drifted").arg(id));
      if(cropKind==QLatin1String("participant")) require(cssClass.contains(QLatin1String("actor")),
          QStringLiteral("%1 participant class drifted").arg(id));
      if(cropKind==QLatin1String("message")) require(cssClass.contains(QLatin1String("messageText")),
          QStringLiteral("%1 message class drifted").arg(id));
      if(cropKind==QLatin1String("fragment")) require(cssClass.contains(QLatin1String("loopText")),
          QStringLiteral("%1 fragment class drifted").arg(id));
      require(structure.value(QStringLiteral("labelAttributes")).isObject()&&
                  structure.value(QStringLiteral("labelParentAttributes")).isObject(),
              QStringLiteral("%1 label/container attributes missing").arg(id));
    }

    const auto entry=cache.getSync(cache.makeKey(source),source);
    require(entry.status==editor::MermaidRenderStatus::Ready&&entry.sequenceScene,
            QStringLiteral("%1 native structural scene failed: %2")
                .arg(id,entry.errorMessage));
    const auto& scene=*entry.sequenceScene;
    require(scene.participantLabels.size()==scene.participants.size()&&
                scene.messageLabels.size()==scene.messages.size()&&
                scene.noteLabels.size()==scene.notes.size()&&
                scene.fragmentLabels.size()==scene.fragments.size()&&
                scene.boxLabels.size()==scene.boxes.size(),
            QStringLiteral("%1 native label/container ordering drifted").arg(id));
    require((nativeMathCount(scene)>0)==(mathCount>0),
            QStringLiteral("%1 native/MathML structural classification mismatch").arg(id));

    const QString labelledBy=structure.value(QStringLiteral("ariaLabelledBy")).toString();
    if(!labelledBy.isEmpty()) {
      ++ariaCases;
      const auto data=sequence::SequenceDiagram::parse(source).data();
      require(!data.accTitle.isEmpty()&&!data.accDescription.isEmpty()&&
                  structure.value(QStringLiteral("ariaTitle")).toString()==data.accTitle&&
                  structure.value(QStringLiteral("ariaDescription")).toString()==data.accDescription&&
                  labelledBy.startsWith(QLatin1String("chart-title-")),
              QStringLiteral("%1 native/browser accessibility title mismatch").arg(id));
    }
    if(id==QLatin1String("structural-combined-order")) {
      require(scene.boxes.size()==1&&scene.participants.size()==3&&scene.activations.size()==1&&
                  scene.notes.size()==1&&scene.fragments.size()==1&&scene.sequenceNumbers.size()>=3&&
                  std::all_of(scene.participants.cbegin(),scene.participants.cend(),
                              [](const auto& actor){return !actor.drawBottom;}),
              QStringLiteral("combined SVG/native container order coverage regressed"));
    }
  }
  require(mathCases>=5&&foreignObjectCases>=5&&ariaCases>=1&&labelCases>=12&&domEntries>=500&&
              markerEntries>=160,
          QStringLiteral("Sequence SVG structural coverage regressed"));
  qDebug()<<"MermaidSequenceSvgStructuralTest:"<<cases.size()<<"cases,"<<domEntries
          <<"ordered DOM entries passed";
  return 0;
}
