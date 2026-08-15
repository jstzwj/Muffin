#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceScene.h"
#include "mermaid/sequence/SequenceScenePainter.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cstdlib>
#include <numeric>

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
              QLatin1String("8ec403ca2a0a7b97f27a88386703ffd5ccc1d061ef6a0f1068cf9b5df25b7aaa"),
          QStringLiteral("Sequence SVG structural fixture drifted"));

  editor::MermaidRenderCache cache;
  int mathCases=0,foreignObjectCases=0,ariaCases=0,labelCases=0,domEntries=0,markerEntries=0;
  const QJsonArray cases=root.value(QStringLiteral("cases")).toArray();
  require(cases.size()==121,QStringLiteral("Sequence SVG structural matrix regressed"));
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
    const auto* sequenceScene=dynamic_cast<const sequence::SequenceScene*>(entry.scene.get());
    require(entry.status==editor::MermaidRenderStatus::Ready&&sequenceScene!=nullptr,
            QStringLiteral("%1 native structural scene failed: %2")
                .arg(id,entry.errorMessage));
    const auto& scene=*sequenceScene;
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

  // ---- pen-level parity: dash patterns and line widths (QPen dash entries
  // are multiples of the pen width, so the values must be normalized) ----
  {
    const QString source=QStringLiteral(
        "sequenceDiagram\n"
        "participant A as Alice\n"
        "participant B as Bob\n"
        "A->>B: solid\n"
        "B-->>A: dotted\n"
        "loop retry\n"
        "  A->>B: ping\n"
        "  alt ok\n"
        "    B-->>A: pong\n"
        "  else fail\n"
        "    B-->>A: error\n"
        "  end\n"
        "end\n");
    const auto entryOf=[&](const QString& src) {
      auto entry=cache.getSync(cache.makeKey(src),src);
      require(entry.status==editor::MermaidRenderStatus::Ready,
              QStringLiteral("dash-parity render failed"));
      auto* raw=dynamic_cast<const sequence::SequenceScene*>(entry.scene.get());
      require(raw!=nullptr,QStringLiteral("dash-parity scene cast"));
      return std::pair{std::move(entry),raw};
    };
    // Keep the cache entries alive: the scene data is owned by them.
    const auto [dashEntry,scene]=entryOf(source);
    const QImage image=sequence::renderSequenceSceneToImage(*scene);
    const qreal pad=8.0;
    const auto toImage=QPointF(scene->bounds.left()-pad,scene->bounds.top()-pad);
    const auto imageX=[&](qreal x){return qRound(x-toImage.x());};
    const auto imageY=[&](qreal y){return qRound(y-toImage.y());};
    const auto alphaAt=[&](int x,int y) {
      return (x>=0&&y>=0&&x<image.width()&&y<image.height())
          ? image.pixelColor(x,y).alpha():0;
    };
    struct Run { int length; };
    const auto inkRuns=[&](const QVector<int>& alphas,int threshold) {
      QVector<Run> runs; int current=0;
      for(int a:alphas) {
        if(a>=threshold) ++current;
        else if(current>0) { runs.append({current}); current=0; }
      }
      if(current>0) runs.append({current});
      return runs;
    };
    const auto maxRun=[&](const char* what,const QVector<Run>& runs) {
      require(!runs.isEmpty(),
              QStringLiteral("%1: no ink found (coordinate mapping?)").arg(what));
      return std::max_element(runs.cbegin(),runs.cend(),
          [](const Run& a,const Run& b){return a.length<b.length;})->length;
    };
    const auto columnAlphas=[&](int x,int y0,int y1) {
      QVector<int> alphas;
      for(int y=y0;y<=y1;++y) alphas.append(alphaAt(x,y));
      return alphas;
    };
    const auto rowAlphas=[&](int y,int x0,int x1) {
      QVector<int> alphas;
      for(int x=x0;x<=x1;++x) alphas.append(alphaAt(x,y));
      return alphas;
    };

    // loop/alt border: stroke-width 2px, dasharray 2,2 — no solid stretch,
    // ~50% duty cycle. The left/right border columns coincide with the outer
    // actors' lifelines (the rect hugs the anchors), so measure the BOTTOM
    // border row across the mid-span where neither lifelines (at the ends)
    // nor the fragment label (top-left) paint.
    require(!scene->fragments.isEmpty(),QStringLiteral("dash-parity fragment"));
    const sequence::SequenceLayoutFragment& fragment=scene->fragments.first();
    if(qEnvironmentVariableIsSet("MUFFIN_SAVE_DASH")) {
      image.save(QStringLiteral("dash-parity.png"));
      for(const auto& f:scene->fragments)
        qDebug()<<"fragment"<<f.kind<<"rect"<<f.rect<<"sections"<<f.sectionY;
      for(const auto& b:scene->boxes)
        qDebug()<<"box"<<b.rect;
      for(const auto& a:scene->participants)
        qDebug()<<"actor"<<a.id<<"anchor"<<a.anchorX<<"life"<<a.lifelineStartY<<a.lifelineStopY;
      for(const auto& m:scene->messages)
        qDebug()<<"msg"<<m.label<<"y"<<m.lineY<<"x"<<m.startX<<m.stopX<<"dashed"<<m.dashed;
      qDebug()<<"bounds"<<scene->bounds;
    }
    const auto midSpanRow=[&](qreal sceneY,const QRectF& rect) {
      const int y=imageY(sceneY);
      int bestY=y,bestInk=-1;
      for(int yy=y-1;yy<=y+1;++yy) {
        const QVector<int> row=rowAlphas(
            yy,imageX(rect.left()+rect.width()*0.35),
            imageX(rect.left()+rect.width()*0.65));
        const int ink=std::accumulate(row.cbegin(),row.cend(),0);
        if(ink>bestInk) { bestInk=ink; bestY=yy; }
      }
      return rowAlphas(bestY,imageX(rect.left()+rect.width()*0.35),
                       imageX(rect.left()+rect.width()*0.65));
    };
    {
      const QVector<int> border=midSpanRow(fragment.rect.bottom(),fragment.rect);
      const auto borderRuns=inkRuns(border,128);
      const int borderMaxRun=maxRun("loopLine border",borderRuns);
      const int borderInk=std::count_if(border.cbegin(),border.cend(),
                                        [](int a){return a>=128;});
      require(borderMaxRun<=4,
              QStringLiteral("loopLine dash too long: %1px (2,2 at 2px pen)")
                  .arg(borderMaxRun));
      require(borderInk*100/border.size()>=25&&borderInk*100/border.size()<=75,
              QStringLiteral("loopLine duty cycle %1% not ~50% (dash 2,2)")
                  .arg(borderInk*100/border.size()));
    }

    // Section separator (inline dasharray 3,3 at 2px): runs ~3px, ~50% duty.
    // The nested alt/else carries the section lines; scan the mid-span row.
    const sequence::SequenceLayoutFragment* sectioned=nullptr;
    for(const auto& candidate:scene->fragments)
      if(!candidate.sectionY.isEmpty()) { sectioned=&candidate; break; }
    if(sectioned) {
      const QVector<int> section=midSpanRow(sectioned->sectionY.first(),
                                             sectioned->rect);
      const auto sectionRuns=inkRuns(section,100);
      const int sectionMaxRun=maxRun("section separator",sectionRuns);
      const int sectionInk=std::count_if(section.cbegin(),section.cend(),
                                         [](int a){return a>=100;});
      require(sectionMaxRun>=2&&sectionMaxRun<=7,
              QStringLiteral("section dash run %1px not ~3px (3,3 at 2px pen)")
                  .arg(sectionMaxRun));
      require(sectionInk*100/section.size()>=25&&sectionInk*100/section.size()<=75,
              QStringLiteral("section duty cycle %1% not ~50%")
                  .arg(sectionInk*100/section.size()));
    }

    // Messages: .messageLine0/1 stroke-width 1.5 (CSS beats the attr 2);
    // dotted = inline dasharray 3,3 — runs ~3px, ~50% duty; solid = one long
    // continuous run.
    const sequence::SequenceLayoutMessage* dotted=nullptr;
    const sequence::SequenceLayoutMessage* solid=nullptr;
    for(const auto& message:scene->messages) {
      if(message.dashed&&!dotted) dotted=&message;
      if(!message.dashed&&!solid) solid=&message;
    }
    require(dotted&&solid,QStringLiteral("dash-parity messages"));
    const auto rowProfile=[&](const sequence::SequenceLayoutMessage& message) {
      const int y=imageY(message.lineY);
      const int x0=qMin(imageX(message.startX),imageX(message.stopX))+4;
      const int x1=qMax(imageX(message.startX),imageX(message.stopX))-4;
      int bestY=y,bestInk=-1;
      for(int yy=y-1;yy<=y+1;++yy) {
        const QVector<int> row=rowAlphas(yy,x0,x1);
        const int ink=std::accumulate(row.cbegin(),row.cend(),0);
        if(ink>bestInk) { bestInk=ink; bestY=yy; }
      }
      return rowAlphas(bestY,x0,x1);
    };
    {
      const QVector<int> dottedRow=rowProfile(*dotted);
      const auto dottedRuns=inkRuns(dottedRow,100);
      const int dottedMaxRun=maxRun("dotted message",dottedRuns);
      const int dottedInk=std::count_if(dottedRow.cbegin(),dottedRow.cend(),
                                        [](int a){return a>=100;});
      require(dottedMaxRun>=2&&dottedMaxRun<=6,
              QStringLiteral("dotted message dash run %1px not ~3px (3,3 at 1.5px pen)")
                  .arg(dottedMaxRun));
      require(dottedInk*100/dottedRow.size()>=30&&dottedInk*100/dottedRow.size()<=70,
              QStringLiteral("dotted message duty cycle %1% not ~50%")
                  .arg(dottedInk*100/dottedRow.size()));
    }
    {
      const QVector<int> solidRow=rowProfile(*solid);
      const auto solidRuns=inkRuns(solidRow,100);
      const int solidMaxRun=maxRun("solid message",solidRuns);
      require(solidMaxRun>=30,
              QStringLiteral("solid message is not one continuous 1.5px line"));
    }

    // Lifelines: the bare `line` tag rule in the sheet forces stroke-width
    // 2px SOLID (nothing declares a dasharray; computed style probed none).
    {
      const auto& actor=scene->participants.first();
      const int x=imageX(actor.anchorX);
      const int y0=imageY(actor.lifelineStartY)+3;
      const int y1=imageY(actor.lifelineStopY)-3;
      int bestX=x,bestInk=-1;
      for(int xx=x-2;xx<=x+2;++xx) {
        const QVector<int> column=columnAlphas(xx,y0,y1);
        const int ink=std::accumulate(column.cbegin(),column.cend(),0);
        if(ink>bestInk) { bestInk=ink; bestX=xx; }
      }
      const QVector<int> lifeline=columnAlphas(bestX,y0,y1);
      const auto lifelineRuns=inkRuns(lifeline,128);
      const int lifelineMaxRun=maxRun("lifeline",lifelineRuns);
      require(lifelineMaxRun>=(y1-y0)*8/10,
              QStringLiteral("lifeline not solid 2px (max run %1 of %2)")
                  .arg(lifelineMaxRun).arg(y1-y0));
      int wideColumns=0;
      const int midY=imageY((actor.lifelineStartY+actor.lifelineStopY)/2);
      for(int xx=bestX-2;xx<=bestX+2;++xx)
        if(alphaAt(xx,midY)>=200) ++wideColumns;
      require(wideColumns>=2,
              QStringLiteral("lifeline width %1 columns, expected a 2px stroke")
                  .arg(wideColumns));
    }

    // look:handDrawn is INERT for sequence (upstream never branches on it in
    // the sequence renderer; probed: classic vs handDrawn SVGs differ only in
    // render-id counters). The scene must be identical either way.
    {
      const QString handDrawnSource=QStringLiteral(
          "%%{init: {\"look\": \"handDrawn\", \"handDrawnSeed\": 7}}%%\n")+source;
      const auto [handDrawnEntry,handDrawnScene]=entryOf(handDrawnSource);
      require(handDrawnScene->toJsonObject()==scene->toJsonObject(),
              QStringLiteral("look:handDrawn changed the sequence scene (upstream ignores it)"));
    }
  }

  qDebug()<<"MermaidSequenceSvgStructuralTest:"<<cases.size()<<"cases,"<<domEntries
          <<"ordered DOM entries passed";
  return 0;
}
