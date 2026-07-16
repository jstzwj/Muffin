#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceDiagram.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

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
              root.value(QStringLiteral("fixtureSha256")).toString()==
                  QLatin1String("305fac299f6f9cead1a89874a7dc44bf98fbe5c0f6ce525dba1f75bf5d293112"),
          QStringLiteral("Sequence SVG structural fixture drifted"));

  editor::MermaidRenderCache cache;
  int mathCases=0,foreignObjectCases=0,ariaCases=0,labelCases=0,domEntries=0;
  const QJsonArray cases=root.value(QStringLiteral("cases")).toArray();
  require(cases.size()==19,QStringLiteral("Sequence SVG structural matrix regressed"));
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
    mathCases+=mathCount>0; foreignObjectCases+=foreignCount>0;

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
            QStringLiteral("%1 native structural scene failed").arg(id));
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
      require(!data.accTitle.isEmpty()&&labelledBy.startsWith(QLatin1String("chart-title-")),
              QStringLiteral("%1 native/browser accessibility title mismatch").arg(id));
    }
  }
  require(mathCases>=3&&foreignObjectCases>=3&&ariaCases>=1&&labelCases>=10&&domEntries>=500,
          QStringLiteral("Sequence SVG structural coverage regressed"));
  qDebug()<<"MermaidSequenceSvgStructuralTest:"<<cases.size()<<"cases,"<<domEntries
          <<"ordered DOM entries passed";
  return 0;
}
