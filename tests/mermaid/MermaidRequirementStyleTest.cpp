// requirementDiagram advanced styling — classDef/class/style cascade + box paint.
//
// Two layers:
//  1. Parser-level: the event-ordered cssStyles/cssClasses model that mirrors
//     RequirementDB (setCssStyle return-abort, setClass skip-missing, defineClass
//     append+retroactive), the style-state tokenizer (lexical errors + dasharray
//     /semicolon mangling), verified directly against RequirementDiagramData.
//  2. Scene-level: resolved box paint (compileStyles last-wins → fill/stroke/
//     stroke-width/dash, invalid fallbacks, divider follows stroke) verified via
//     the production render path (MermaidRenderCache → RequirementScene).
//
// All expectations verified against real mermaid 11.16.0 (G:/github/req-probe
// probe{,2}-report.json): cascade is event-order last-wins (requirement bakes
// class styles into node.cssStyles at setClass time — it does NOT use
// cssCompiledStyles like flowchart), setCssStyle `return` aborts the whole id
// list, defineClass appends + retroactively updates bound nodes, and the style
// lexer rejects rgb()/hsl()/opacity/decimal-em/'%'/'"'-in-value as Parse errors.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/requirement/RequirementDiagram.h"
#include "mermaid/requirement/RequirementScene.h"
#include "theme/CssCalc.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

requirement::RequirementDiagramData parseData(const QString& source) {
  return requirement::RequirementDiagram::parse(source).data();
}

template <typename Fn>
bool throwsParseError(Fn fn) {
  try {
    fn();
    return false;
  } catch (const requirement::RequirementParseError&) {
    return true;
  }
}

const requirement::RequirementSceneNode* nodeOf(const editor::MermaidRenderEntry& e,
                                                 const QString& id) {
  const auto* scene = dynamic_cast<const requirement::RequirementScene*>(e.scene.get());
  require(scene != nullptr, QStringLiteral("missing Requirement scene"));
  for (const auto& n : scene->nodes)
    if (n.id == id) return &n;
  return nullptr;
}

// Renders `source` (a styled requirementDiagram) and returns the resolved node
// `id` plus the scene (for base/fallback color comparisons).
struct Rendered {
  const requirement::RequirementScene* scene;
  const requirement::RequirementSceneNode* node;
};
Rendered renderNode(editor::MermaidRenderCache& cache, const QString& source, const QString& id) {
  Rendered out{nullptr, nullptr};
  const auto entry = cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("styled requirement did not render: ") + entry.errorMessage);
  out.scene = dynamic_cast<const requirement::RequirementScene*>(entry.scene.get());
  require(out.scene != nullptr, QStringLiteral("missing Requirement scene"));
  out.node = nodeOf(entry, id);
  require(out.node != nullptr, QStringLiteral("node '%1' not found").arg(id));
  return out;
}

const requirement::RequirementNode* reqNode(const requirement::RequirementDiagramData& d,
                                            const QString& name) {
  for (const auto& n : d.requirements)
    if (n.name == name) return &n;
  return nullptr;
}

// Paints `scene` into a fresh transparent QImage at 1:1 scene coordinates
// (translated so the scene's bounding-box origin maps to pixel (0,0)). Used by
// the painter RGBA/ink tests below — these exercise the REAL painter, not just
// the resolved scene fields.
QImage paintScene(const requirement::RequirementScene& scene) {
  QImage img(scene.bounds.size().toSize(), QImage::Format_ARGB32);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  p.translate(-scene.bounds.topLeft());
  MermaidPaintOptions opts;  // cullToVisibleRect=false -> paint everything
  scene.paint(p, opts);
  p.end();
  return img;
}

// Box rect of node 0 in image (pixel) coordinates.
QRectF nodeImageRect(const requirement::RequirementScene& scene) {
  const auto& n = scene.nodes.at(0);
  return QRectF(n.center.x() - n.size.width() / 2.0 - scene.bounds.x(),
                n.center.y() - n.size.height() / 2.0 - scene.bounds.y(),
                n.size.width(), n.size.height());
}

// Counts interior pixels of node 0 matching `pred` (3px inset skips the outline
// stroke). Used by the painter fill tests so a stray text/divider pixel can't
// flip a single-point sample.
template <typename Pred>
int countInterior(const QImage& img, const requirement::RequirementScene& scene, Pred pred) {
  const QRectF r = nodeImageRect(scene);
  int count = 0;
  for (int y = qRound(r.top()) + 3; y < qRound(r.bottom()) - 3; ++y)
    for (int x = qRound(r.left()) + 3; x < qRound(r.right()) - 3; ++x)
      if (pred(img.pixel(x, y))) ++count;
  return count;
}

// Theme boxStroke/dividerColor = #9370DB = rgb(147,112,219) (blue-dominant).
// The box fill #ECECFF (236,236,255) is excluded (B-R = 19 < 30). Used by the
// outline/divider visibility painter tests.
bool isThemePurple(QRgb px) {
  return qAlpha(px) >= 100 && qBlue(px) > 160 &&
         qBlue(px) - qRed(px) > 30 && qBlue(px) - qGreen(px) > 30;
}

// Image-pixel Y of node 0's divider line (body top), translated into the
// painted image's coordinate space.
int dividerImageY(const requirement::RequirementScene& scene) {
  const auto& n = scene.nodes.at(0);
  return qRound(n.center.y() + n.dividerY - scene.bounds.y());
}

// Counts theme-purple pixels in a horizontal strip [y0, y1] across node 0's box
// width (minus `xMargin` each side to avoid the rounded corners). Used to detect
// the outline (sampled at the top edge) vs the divider (sampled at dividerY)
// independently — proving the outline can be hidden while the divider paints.
int countPurpleStrip(const QImage& img, const requirement::RequirementScene& scene,
                     int y0, int y1, int xMargin) {
  const QRectF r = nodeImageRect(scene);
  int count = 0;
  for (int y = y0; y <= y1; ++y)
    for (int x = qRound(r.left()) + xMargin; x < qRound(r.right()) - xMargin; ++x)
      if (isThemePurple(img.pixel(x, y))) ++count;
  return count;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  editor::MermaidRenderCache cache;

  const QString head = QStringLiteral("requirementDiagram\n");

  // ===== 1. Event-order cascade (last-wins), mirroring the probe's area1 =====
  {
    // classDef + class then inline style -> inline wins (event order).
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\n"
        "classDef C fill:#aaaaaa\nclass A C\nstyle A fill:#bbbbbb");
    require(reqNode(d, "A")->cssStyles ==
                QStringList{QStringLiteral("fill:#aaaaaa"), QStringLiteral("fill:#bbbbbb")},
            QStringLiteral("class then style -> [aaaaaa, bbbbbb]; got %1")
                .arg(reqNode(d, "A")->cssStyles.join(QLatin1String(" | "))));
  }
  {
    // inline style then classDef+class -> class wins (class styles pushed later).
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\n"
        "style A fill:#bbbbbb\nclassDef C fill:#aaaaaa\nclass A C");
    require(reqNode(d, "A")->cssStyles ==
                QStringList{QStringLiteral("fill:#bbbbbb"), QStringLiteral("fill:#aaaaaa")},
            QStringLiteral("style then class -> [bbbbbb, aaaaaa]"));
  }
  {
    // retroactive: bind to C before C is defined, then define C -> C applies.
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\n"
        "class A C\nclassDef C fill:#aaaaaa");
    require(reqNode(d, "A")->cssStyles == QStringList{QStringLiteral("fill:#aaaaaa")},
            QStringLiteral("retroactive classDef -> [aaaaaa]; got %1")
                .arg(reqNode(d, "A")->cssStyles.join(QLatin1String(" | "))));
    require(reqNode(d, "A")->cssClasses.contains(QStringLiteral("C")),
            QStringLiteral("retroactive: A bound to C"));
  }
  {
    // duplicate classDef APPENDS; a later `class` folds in the full list.
    const auto d = parseData(head +
        "classDef C fill:#aaaaaa\nclassDef C fill:#bbbbbb\n"
        "requirement A {\n id: 1\n}\nclass A C");
    require(reqNode(d, "A")->cssStyles ==
                QStringList{QStringLiteral("fill:#aaaaaa"), QStringLiteral("fill:#bbbbbb")},
            QStringLiteral("dup classDef appends -> [aaaaaa, bbbbbb]; got %1")
                .arg(reqNode(d, "A")->cssStyles.join(QLatin1String(" | "))));
  }
  {
    // multi-class conflict: A then B -> B wins (pushed second).
    const auto d = parseData(head +
        "classDef A fill:#aaaaaa\nclassDef B fill:#bbbbbb\n"
        "requirement N {\n id: 1\n}\nclass N A,B");
    require(reqNode(d, "N")->cssStyles ==
                QStringList{QStringLiteral("fill:#aaaaaa"), QStringLiteral("fill:#bbbbbb")},
            QStringLiteral("multi-class A,B -> [aaaaaa, bbbbbb]"));
    require(reqNode(d, "N")->cssClasses.contains(QStringLiteral("A")) &&
                reqNode(d, "N")->cssClasses.contains(QStringLiteral("B")),
            QStringLiteral("multi-class binds both class names"));
  }

  // ===== 2. Multi-node idList (style + class) =====
  {
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\nrequirement B {\n id: 2\n}\n"
        "style A,B fill:#aaaaaa");
    require(reqNode(d, "A")->cssStyles == QStringList{QStringLiteral("fill:#aaaaaa")},
            QStringLiteral("style A,B -> A styled"));
    require(reqNode(d, "B")->cssStyles == QStringList{QStringLiteral("fill:#aaaaaa")},
            QStringLiteral("style A,B -> B styled"));
  }
  {
    const auto d = parseData(head +
        "classDef C fill:#aaaaaa\n"
        "requirement A {\n id: 1\n}\nrequirement B {\n id: 2\n}\n"
        "class A,B C");
    require(reqNode(d, "A")->cssStyles == QStringList{QStringLiteral("fill:#aaaaaa")},
            QStringLiteral("class A,B C -> A bound"));
    require(reqNode(d, "B")->cssStyles == QStringList{QStringLiteral("fill:#aaaaaa")},
            QStringLiteral("class A,B C -> B bound"));
  }

  // ===== 3. setCssStyle return-abort vs setClass skip-missing =====
  {
    // Missing node FIRST -> return aborts the whole list; A is untouched.
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\nstyle Missing,A fill:#ff0000");
    require(reqNode(d, "A")->cssStyles.isEmpty(),
            QStringLiteral("style Missing,A -> A untouched (return-abort)"));
  }
  {
    // Missing node in the MIDDLE -> nodes before it are styled, after are not.
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\nrequirement B {\n id: 2\n}\n"
        "style A,Missing,B fill:#ff0000");
    require(reqNode(d, "A")->cssStyles == QStringList{QStringLiteral("fill:#ff0000")},
            QStringLiteral("style A,Missing,B -> A styled (before the abort)"));
    require(reqNode(d, "B")->cssStyles.isEmpty(),
            QStringLiteral("style A,Missing,B -> B untouched (after the abort)"));
  }
  {
    // setClass SKIPS a missing node and continues (no abort).
    const auto d = parseData(head +
        "classDef C fill:#ff0000\n"
        "requirement A {\n id: 1\n}\nrequirement B {\n id: 2\n}\n"
        "class A,Missing,B C");
    require(reqNode(d, "A")->cssStyles == QStringList{QStringLiteral("fill:#ff0000")},
            QStringLiteral("class A,Missing,B C -> A styled (skip-missing)"));
    require(reqNode(d, "B")->cssStyles == QStringList{QStringLiteral("fill:#ff0000")},
            QStringLiteral("class A,Missing,B C -> B styled (continues past missing)"));
  }

  // ===== 4. `:::` declaration binding (folds current class styles) =====
  {
    const auto d = parseData(head +
        "classDef C fill:#aaaaaa\n"
        "requirement A ::: C {\n id: 1\n}");
    require(reqNode(d, "A")->cssStyles == QStringList{QStringLiteral("fill:#aaaaaa")},
            QStringLiteral("::: C at declaration folds C's styles"));
    require(reqNode(d, "A")->cssClasses.contains(QStringLiteral("C")),
            QStringLiteral("::: binds the class name"));
  }

  // ===== 5. Style-state lexical errors (Parse error) =====
  {
    const QList<QPair<QString, QString>> bad = {
        {QStringLiteral("rgb"),     QStringLiteral("style A fill:rgb(255,0,0)")},
        {QStringLiteral("rgba"),    QStringLiteral("style A fill:rgba(255,0,0,0.5)")},
        {QStringLiteral("hsl"),     QStringLiteral("style A fill:hsl(0,100%,50%)")},
        {QStringLiteral("opacity"), QStringLiteral("style A fill-opacity:0.2")},
        {QStringLiteral("percent"), QStringLiteral("style A stroke-width:4%")},
        {QStringLiteral("decimalem"), QStringLiteral("style A letter-spacing:0.2em")},
        {QStringLiteral("quotedfont"), QStringLiteral("style A font-family:\"Courier New\"")},
        {QStringLiteral("trailcomma"), QStringLiteral("style A fill:#ff0000,")},
        {QStringLiteral("dblcomma"), QStringLiteral("style A fill:#ff0000,,stroke:blue")},
        {QStringLiteral("leadcomma"), QStringLiteral("style ,A fill:#ff0000")},
        {QStringLiteral("class-trailcomma"), QStringLiteral("class A C,")},
        {QStringLiteral("class-noname"), QStringLiteral("class A")},
    };
    for (const auto& kv : bad) {
      const QString src = head + "requirement A {\n id: 1\n}\n" + kv.second;
      require(throwsParseError([&] { requirement::RequirementDiagram::parse(src); }),
              QStringLiteral("'%1' must be a Parse error: %2").arg(kv.first, kv.second));
    }
    // Quoted node id in the idList is ACCEPTED (style state opens a qString).
    bool quotedThrew = false;
    try {
      parseData(head + "requirement A {\n id: 1\n}\nclass \"A\" C");
    } catch (const requirement::RequirementParseError&) { quotedThrew = true; }
    require(!quotedThrew, QStringLiteral("quoted node id in class idList is valid"));
  }

  // ===== 6. dasharray / semicolon mangling (accepted-but-mangled) =====
  {
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\nstyle A stroke-dasharray:5,2");
    require(reqNode(d, "A")->cssStyles ==
                QStringList{QStringLiteral("stroke-dasharray:5"), QStringLiteral("2")},
            QStringLiteral("dasharray 5,2 -> comma-split ['stroke-dasharray:5','2']; got %1")
                .arg(reqNode(d, "A")->cssStyles.join(QLatin1String(" | "))));
  }
  {
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\nstyle A stroke-dasharray:5 2");
    require(reqNode(d, "A")->cssStyles == QStringList{QStringLiteral("stroke-dasharray:52")},
            QStringLiteral("dasharray '5 2' -> space eaten ['stroke-dasharray:52']; got %1")
                .arg(reqNode(d, "A")->cssStyles.join(QLatin1String(" | "))));
  }
  {
    const auto d = parseData(head +
        "requirement A {\n id: 1\n}\nstyle A fill:#ff0000;stroke:blue");
    require(reqNode(d, "A")->cssStyles ==
                QStringList{QStringLiteral("fill:#ff0000;stroke:blue")},
            QStringLiteral("semicolon stays in the component; got %1")
                .arg(reqNode(d, "A")->cssStyles.join(QLatin1String(" | "))));
  }

  // ===== 7. Scene-level box resolution (compileStyles last-wins) =====
  {
    // Default (no styles) -> theme base, 1.3 border, no dash, valid stroke.
    const auto r = renderNode(cache, head + "requirement A {\n id: 1\n}", QStringLiteral("A"));
    require(r.node->fill == r.scene->style.boxFill,
            QStringLiteral("default fill = theme boxFill; got %1").arg(r.node->fill));
    require(r.node->outlineStroke == r.scene->style.boxStroke,
            QStringLiteral("default outline = theme boxStroke"));
    require(r.node->dividerStroke == r.scene->style.dividerColor,
            QStringLiteral("default divider = theme dividerColor"));
    require(r.node->outlineVisible && r.node->dividerVisible,
            QStringLiteral("default outline + divider both visible"));
    require(qAbs(r.node->strokeWidth - 1.3) < 1e-6,
            QStringLiteral("default strokeWidth = 1.3; got %1").arg(r.node->strokeWidth));
    require(r.node->dashArray.size() == 2 && r.node->dashArray.at(0) == 0.0 &&
                r.node->dashArray.at(1) == 0.0,
            QStringLiteral("default dashArray = {0,0}"));
  }
  {
    // fill + stroke + stroke-width + dash all applied (last-wins over base).
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\n"
        "style A fill:#ff0000,stroke:#00aa00,stroke-width:4,stroke-dasharray:5,2",
        QStringLiteral("A"));
    require(r.node->fill == QStringLiteral("#ff0000"),
            QStringLiteral("inline fill; got %1").arg(r.node->fill));
    require(r.node->outlineStroke == QStringLiteral("#00aa00"),
            QStringLiteral("inline outline stroke; got %1").arg(r.node->outlineStroke));
    require(r.node->dividerStroke == QStringLiteral("#00aa00"),
            QStringLiteral("explicit stroke applies to the divider too"));
    require(qAbs(r.node->strokeWidth - 4.0) < 1e-6,
            QStringLiteral("inline stroke-width 4; got %1").arg(r.node->strokeWidth));
    // stroke-dasharray:5,2 -> comma-split -> getStrokeDashArray("5") -> {5,5}.
    require(r.node->dashArray.size() == 2 && qAbs(r.node->dashArray.at(0) - 5.0) < 1e-6 &&
                qAbs(r.node->dashArray.at(1) - 5.0) < 1e-6,
            QStringLiteral("dash 5,2 -> {5,5} (comma-split, '2' lost)"));
  }
  {
    // Invalid fill -> inherited foreground; box unaffected otherwise.
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\nstyle A fill:notacolor", QStringLiteral("A"));
    require(r.node->fill == r.scene->style.foregroundFallback,
            QStringLiteral("invalid fill -> foreground fallback; got %1").arg(r.node->fill));
    require(r.node->outlineVisible, QStringLiteral("invalid fill leaves outline visible"));
    require(r.node->outlineStroke == r.scene->style.boxStroke,
            QStringLiteral("invalid fill leaves outline at theme base"));
  }
  {
    // Invalid stroke -> OUTLINE hidden (NoPen) but the DIVIDER keeps the theme
    // color (probe5-report.json: the divider path carries the theme stroke
    // attribute; only the outline inherits/defaults to none).
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\nstyle A stroke:notacolor", QStringLiteral("A"));
    require(!r.node->outlineVisible,
            QStringLiteral("invalid stroke -> outline hidden"));
    require(r.node->dividerVisible,
            QStringLiteral("invalid stroke -> divider still visible"));
    require(r.node->dividerStroke == r.scene->style.dividerColor,
            QStringLiteral("invalid stroke -> divider keeps theme color"));
    require(r.node->fill == r.scene->style.boxFill,
            QStringLiteral("invalid stroke leaves fill at theme base"));
  }
  {
    // stroke:inherit -> same as invalid: outline hidden, divider keeps theme.
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\nstyle A stroke:inherit", QStringLiteral("A"));
    require(!r.node->outlineVisible, QStringLiteral("stroke:inherit -> outline hidden"));
    require(r.node->dividerVisible, QStringLiteral("stroke:inherit -> divider visible"));
    require(r.node->dividerStroke == r.scene->style.dividerColor,
            QStringLiteral("stroke:inherit -> divider keeps theme color"));
  }
  {
    // stroke:currentColor -> black on outline AND divider.
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\nstyle A stroke:currentColor", QStringLiteral("A"));
    require(r.node->outlineStroke == QStringLiteral("#000000"),
            QStringLiteral("stroke:currentColor -> black outline; got %1").arg(r.node->outlineStroke));
    require(r.node->dividerStroke == QStringLiteral("#000000"),
            QStringLiteral("stroke:currentColor -> black divider"));
    require(r.node->outlineVisible && r.node->dividerVisible,
            QStringLiteral("currentColor keeps both visible"));
  }
  {
    // ASCII case-insensitive paint keywords (CSS/SVG keywords are case-insensitive).
    {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A fill:NONE", QStringLiteral("A"));
      require(r.node->fillNone, QStringLiteral("fill:NONE -> fillNone (case-insensitive)"));
    }
    {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A stroke:CurrentColor", QStringLiteral("A"));
      require(r.node->outlineStroke == QStringLiteral("#000000"),
              QStringLiteral("stroke:CurrentColor -> black (case-insensitive)"));
    }
    {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A stroke:NONE", QStringLiteral("A"));
      require(!r.node->outlineVisible && !r.node->dividerVisible,
              QStringLiteral("stroke:NONE -> outline + divider both hidden (case-insensitive)"));
    }
  }
  {
    // stroke-width valid zero (0/00/000px/0em) -> outline + divider both hidden
    // (SVG stroke-width:0 is invisible; Qt would else draw a 1px hairline).
    for (const QString& w : {QStringLiteral("0"), QStringLiteral("00"),
                             QStringLiteral("000px"), QStringLiteral("0em")}) {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A stroke-width:" + w, QStringLiteral("A"));
      require(qAbs(r.node->strokeWidth - 0.0) < 1e-6,
              QStringLiteral("stroke-width:%1 -> 0.0; got %2").arg(w).arg(r.node->strokeWidth));
      require(!r.node->outlineVisible && !r.node->dividerVisible,
              QStringLiteral("stroke-width:%1 -> outline + divider both hidden").arg(w));
    }
  }
  {
    // stroke-width negative / non-empty invalid (-1/-1em/foo) -> CSS INITIAL
    // 1.0px (the malformed declaration is dropped), visibility unchanged: both
    // paths still visible at the default theme stroke.
    for (const QString& w : {QStringLiteral("-1"), QStringLiteral("-1em"),
                             QStringLiteral("foo")}) {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A stroke-width:" + w, QStringLiteral("A"));
      require(qAbs(r.node->strokeWidth - 1.0) < 1e-6,
              QStringLiteral("stroke-width:%1 -> CSS initial 1.0; got %2")
                  .arg(w).arg(r.node->strokeWidth));
      require(r.node->outlineVisible && r.node->dividerVisible,
              QStringLiteral("stroke-width:%1 keeps both visible").arg(w));
    }
  }
  {
    // stroke-width:4em -> 4 * root font (16) = 64.
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\nstyle A stroke-width:4em", QStringLiteral("A"));
    require(qAbs(r.node->strokeWidth - 64.0) < 1e-6,
            QStringLiteral("stroke-width:4em -> 4*16=64; got %1").arg(r.node->strokeWidth));
  }
  {
    // stroke-width bare number '4' -> 4 (no px suffix).
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\nstyle A stroke-width:4", QStringLiteral("A"));
    require(qAbs(r.node->strokeWidth - 4.0) < 1e-6,
            QStringLiteral("stroke-width:4 -> 4; got %1").arg(r.node->strokeWidth));
  }
  {
    // px unit is ASCII case-insensitive (4PX/4Px -> 4; 0PX -> 0 invisible): the
    // CSS engine parses units regardless of case.
    for (const QString& w : {QStringLiteral("4PX"), QStringLiteral("4Px")}) {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A stroke-width:" + w, QStringLiteral("A"));
      require(qAbs(r.node->strokeWidth - 4.0) < 1e-6,
              QStringLiteral("stroke-width:%1 -> 4.0 (case-insensitive px); got %2")
                  .arg(w).arg(r.node->strokeWidth));
    }
    {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A stroke-width:0PX", QStringLiteral("A"));
      require(qAbs(r.node->strokeWidth - 0.0) < 1e-6,
              QStringLiteral("stroke-width:0PX -> 0.0; got %1").arg(r.node->strokeWidth));
      require(!r.node->outlineVisible && !r.node->dividerVisible,
              QStringLiteral("stroke-width:0PX -> outline + divider both hidden"));
    }
    // Commit 1 strict scope: bare / px / em. Other CSS lengths (rem/pt/pc/in/cm/
    // mm/q/ex/ch and viewport units) ARE valid upstream but are deferred to
    // Step 0D — until then they resolve to the CSS initial 1.0 (known gap, not
    // asserted here to avoid locking in the fallback).
  }

  // Newly-supported CSS length units (Commit 1 follow-up: full resolver). Scene
  // path at the default root font (16) and kMmdcDefaultCssViewport (800x600).
  {
    // Fixed units (1px = 1/96in) — independent of root font and viewport.
    const auto r1 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:4rem", QStringLiteral("A"));
    require(qAbs(r1.node->strokeWidth - 64.0) < 1e-6,
            QStringLiteral("stroke-width:4rem -> 64 (rem=16); got %1").arg(r1.node->strokeWidth));
    const auto r2 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:1in", QStringLiteral("A"));
    require(qAbs(r2.node->strokeWidth - 96.0) < 1e-6,
            QStringLiteral("stroke-width:1in -> 96; got %1").arg(r2.node->strokeWidth));
    const auto r3 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:4pt", QStringLiteral("A"));
    require(qAbs(r3.node->strokeWidth - 4.0 * 96.0 / 72.0) < 1e-6,
            QStringLiteral("stroke-width:4pt -> 5.333; got %1").arg(r3.node->strokeWidth));
    const auto r4 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:1cm", QStringLiteral("A"));
    require(qAbs(r4.node->strokeWidth - 96.0 / 2.54) < 1e-4,
            QStringLiteral("stroke-width:1cm -> 37.795; got %1").arg(r4.node->strokeWidth));
    // Viewport units against the 800x600 default raster profile.
    const auto r5 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:4vw", QStringLiteral("A"));
    require(qAbs(r5.node->strokeWidth - 32.0) < 1e-6,
            QStringLiteral("stroke-width:4vw -> 32 (vw of 800); got %1").arg(r5.node->strokeWidth));
    const auto r6 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:4vh", QStringLiteral("A"));
    require(qAbs(r6.node->strokeWidth - 24.0) < 1e-6,
            QStringLiteral("stroke-width:4vh -> 24 (vh of 600); got %1").arg(r6.node->strokeWidth));
    const auto r7 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:4vmin", QStringLiteral("A"));
    require(qAbs(r7.node->strokeWidth - 24.0) < 1e-6,
            QStringLiteral("stroke-width:4vmin -> 24 (min 600); got %1").arg(r7.node->strokeWidth));
    const auto r8 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:4vmax", QStringLiteral("A"));
    require(qAbs(r8.node->strokeWidth - 32.0) < 1e-6,
            QStringLiteral("stroke-width:4vmax -> 32 (max 800); got %1").arg(r8.node->strokeWidth));
    // Font-metric units (ex/ch): parsed as Valid (not the 1.0 fallback). The
    // exact px is the configured font's metric — asserted precisely in §9.
    const auto r9 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:4ex", QStringLiteral("A"));
    require(r9.node->strokeWidth > 10.0,
            QStringLiteral("stroke-width:4ex -> font metric (not 1.0 fallback); got %1").arg(r9.node->strokeWidth));
    const auto r10 = renderNode(cache, head + "requirement A {\n id: 1\n}\nstyle A stroke-width:4ch", QStringLiteral("A"));
    require(r10.node->strokeWidth > 10.0,
            QStringLiteral("stroke-width:4ch -> font metric (not 1.0 fallback); got %1").arg(r10.node->strokeWidth));
  }
  {
    // classDef + class reaches the resolved box (cascade through the DB path).
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\nclassDef C fill:#ff0000\nclass A C", QStringLiteral("A"));
    require(r.node->fill == QStringLiteral("#ff0000"),
            QStringLiteral("classDef+class fill; got %1").arg(r.node->fill));
  }
  {
    // styles2Map colon truncation: `fill:red:blue` -> key=fill, value=red (JS
    // `const [k,v] = s.split(":")` keeps only the first two segments).
    const auto r = renderNode(cache, head +
        "requirement A {\n id: 1\n}\nstyle A fill:red:blue", QStringLiteral("A"));
    require(r.node->fill == QStringLiteral("red"),
            QStringLiteral("fill:red:blue -> fill=red (colon truncation); got %1").arg(r.node->fill));
  }
  {
    // SVG paint:none — fill:none paints NoBrush; stroke:none hides BOTH the
    // outline and the divider (NoPen).
    {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A fill:none", QStringLiteral("A"));
      require(r.node->fillNone, QStringLiteral("fill:none -> fillNone flag"));
      require(r.node->outlineVisible, QStringLiteral("fill:none leaves outline visible"));
    }
    {
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A stroke:none", QStringLiteral("A"));
      require(!r.node->outlineVisible && !r.node->dividerVisible,
              QStringLiteral("stroke:none -> outline + divider both hidden"));
      require(!r.node->fillNone, QStringLiteral("stroke:none leaves fill painted"));
    }
    {
      // currentColor resolves to the default `color` property (black) until the
      // text/color phase wires `color`.
      const auto r = renderNode(cache, head +
          "requirement A {\n id: 1\n}\nstyle A fill:currentColor", QStringLiteral("A"));
      require(r.node->fill == QStringLiteral("#000000"),
              QStringLiteral("fill:currentColor -> black; got %1").arg(r.node->fill));
    }
  }
  {
    // Empty quoted ids are a Parse error in every style-state idList position.
    const QList<QString> bad = {
        head + "requirement A {\n id: 1\n}\nclass A \"\"",
        head + "requirement A {\n id: 1\n}\nclass \"\" C",
        head + "requirement A {\n id: 1\n}\nclassDef \"\" fill:#ff0000",
        head + "requirement A {\n id: 1\n}\nstyle \"\" fill:#ff0000",
    };
    for (const QString& src : bad)
      require(throwsParseError([&] { requirement::RequirementDiagram::parse(src); }),
              QStringLiteral("empty quoted id must be a Parse error: %1").arg(src));
  }

  // ===== 8. Painter RGBA/ink (the REAL painter, not just resolved fields) =====
  {
    // fill:#ff0000 -> the box interior is overwhelmingly red (count, not a
    // single sample, so a text/divider pixel can't flip it).
    const QString src = head + "requirement A {\n id: 1\n}\nstyle A fill:#ff0000";
    const auto entry = cache.getSync(cache.makeKey(src), src);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            QStringLiteral("fill:red did not render: ") + entry.errorMessage);
    const auto* sc = dynamic_cast<const requirement::RequirementScene*>(entry.scene.get());
    require(sc != nullptr && !sc->nodes.isEmpty(), QStringLiteral("scene/node missing"));
    const QImage img = paintScene(*sc);
    const int red = countInterior(img, *sc, [](QRgb px) {
      return qAlpha(px) >= 200 && qRed(px) > 180 && qGreen(px) < 80 && qBlue(px) < 80;
    });
    require(red > 200, QStringLiteral("fill:#ff0000 -> many red interior pixels; got %1").arg(red));
  }
  {
    // fill:none -> NoBrush: no red fill AND the interior is mostly transparent.
    const QString src = head + "requirement A {\n id: 1\n}\nstyle A fill:none";
    const auto entry = cache.getSync(cache.makeKey(src), src);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            QStringLiteral("fill:none did not render: ") + entry.errorMessage);
    const auto* sc = dynamic_cast<const requirement::RequirementScene*>(entry.scene.get());
    const QImage img = paintScene(*sc);
    const int red = countInterior(img, *sc, [](QRgb px) {
      return qAlpha(px) >= 200 && qRed(px) > 180 && qGreen(px) < 80 && qBlue(px) < 80;
    });
    const int transparent = countInterior(img, *sc, [](QRgb px) { return qAlpha(px) < 32; });
    require(red == 0, QStringLiteral("fill:none -> no red fill pixels; got %1").arg(red));
    require(transparent > 200,
            QStringLiteral("fill:none -> mostly transparent interior; got %1 transparent").arg(transparent));
  }
  {
    // invalid fill -> the interior takes the foreground fallback (#333/#ccc =
    // grey). Text (#131300) is greenish-dark (G-B large) so it is excluded.
    const QString src = head + "requirement A {\n id: 1\n}\nstyle A fill:notacolor";
    const auto entry = cache.getSync(cache.makeKey(src), src);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            QStringLiteral("invalid fill did not render: ") + entry.errorMessage);
    const auto* sc = dynamic_cast<const requirement::RequirementScene*>(entry.scene.get());
    const QImage img = paintScene(*sc);
    const int grey = countInterior(img, *sc, [](QRgb px) {
      return qAlpha(px) >= 200 && std::abs(qRed(px) - qGreen(px)) < 15 &&
             std::abs(qGreen(px) - qBlue(px)) < 15;
    });
    require(grey > 200, QStringLiteral("invalid fill -> grey foreground pixels; got %1").arg(grey));
  }
  {
    // stroke-width:4 + stroke-dasharray:5 -> top-edge ink runs ~5px (Qt dash
    // units are pen-width multiples; without the /strokeWidth fix this renders
    // ~20px runs). Scan the straight part of the top edge for green ink runs.
    const QString src = head +
        "requirement A {\n id: 1\n}\nstyle A stroke:#00aa00,stroke-width:4,stroke-dasharray:5";
    const auto entry = cache.getSync(cache.makeKey(src), src);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            QStringLiteral("dash case did not render: ") + entry.errorMessage);
    const auto* sc = dynamic_cast<const requirement::RequirementScene*>(entry.scene.get());
    const QImage img = paintScene(*sc);
    const QRectF r = nodeImageRect(*sc);
    const int y = qRound(r.top());        // top edge = stroke center (4px wide)
    const int x0 = qRound(r.left()) + 10;  // past the rounded corner
    const int x1 = qRound(r.right()) - 10;
    int maxRun = 0, run = 0, inkPixels = 0;
    for (int x = x0; x < x1; ++x) {
      const QRgb px = img.pixel(x, y);
      const bool ink = qAlpha(px) >= 100 && qGreen(px) > 100 && qRed(px) < 90 && qBlue(px) < 90;
      if (ink) {
        ++run; ++inkPixels;
        if (run > maxRun) maxRun = run;
      } else {
        run = 0;
      }
    }
    require(inkPixels > 0, QStringLiteral("dash case: expected green ink on the top edge"));
    // Fixed: 5px ink runs (<=12 with antialiasing). Unscaled bug: ~20px runs.
    require(maxRun <= 12,
            QStringLiteral("dash sw:4+dash:5 -> <=12px ink runs (Qt pen-width scaling); "
                           "maxRun=%1 (unscaled bug would be ~20)")
                .arg(maxRun));
  }

  // ===== 8b. Painter outline-vs-divider visibility (the REAL painter) =====
  // The outline (box border) and the divider are independent: an
  // inherit/invalid stroke hides the outline but the divider still paints in
  // the theme color (#9370DB); `none` (or stroke-width<=0) hides both. Sample
  // the top edge (outline only) and the divider row (divider only) for
  // theme-purple ink — proves the painter honors the split visibility flags.
  {
    const auto outlineInk = [](const QImage& img, const requirement::RequirementScene& scene) {
      const int topY = qRound(nodeImageRect(scene).top());
      return countPurpleStrip(img, scene, topY - 1, topY + 1, 10);
    };
    const auto dividerInk = [](const QImage& img, const requirement::RequirementScene& scene) {
      return countPurpleStrip(img, scene, dividerImageY(scene) - 1, dividerImageY(scene) + 1, 10);
    };
    auto paintCase = [&cache, &head](const QString& styleLine) {
      const QString src = head + "requirement A {\n id: 1\n}\n" + styleLine;
      const auto entry = cache.getSync(cache.makeKey(src), src);
      require(entry.status == editor::MermaidRenderStatus::Ready,
              QStringLiteral("painter case did not render (%1): ").arg(styleLine) +
                  entry.errorMessage);
      const auto* sc = dynamic_cast<const requirement::RequirementScene*>(entry.scene.get());
      require(sc != nullptr && !sc->nodes.isEmpty(),
              QStringLiteral("scene/node missing for %1").arg(styleLine));
      struct Result { QImage img; const requirement::RequirementScene* sc; };
      return Result{paintScene(*sc), sc};
    };
    {
      // invalid stroke -> no outline ink, but divider keeps theme purple.
      const auto r = paintCase(QStringLiteral("style A stroke:notacolor"));
      require(outlineInk(r.img, *r.sc) == 0,
              QStringLiteral("invalid stroke -> no outline ink on top edge; got %1")
                  .arg(outlineInk(r.img, *r.sc)));
      require(dividerInk(r.img, *r.sc) > 30,
              QStringLiteral("invalid stroke -> divider still paints theme color; got %1")
                  .arg(dividerInk(r.img, *r.sc)));
    }
    {
      // stroke:inherit -> same: no outline ink, divider theme purple.
      const auto r = paintCase(QStringLiteral("style A stroke:inherit"));
      require(outlineInk(r.img, *r.sc) == 0,
              QStringLiteral("stroke:inherit -> no outline ink; got %1").arg(outlineInk(r.img, *r.sc)));
      require(dividerInk(r.img, *r.sc) > 30,
              QStringLiteral("stroke:inherit -> divider theme ink; got %1").arg(dividerInk(r.img, *r.sc)));
    }
    {
      // stroke:none -> no outline ink AND no divider ink.
      const auto r = paintCase(QStringLiteral("style A stroke:none"));
      require(outlineInk(r.img, *r.sc) == 0,
              QStringLiteral("stroke:none -> no outline ink; got %1").arg(outlineInk(r.img, *r.sc)));
      require(dividerInk(r.img, *r.sc) == 0,
              QStringLiteral("stroke:none -> no divider ink; got %1").arg(dividerInk(r.img, *r.sc)));
    }
    {
      // stroke-width:0 -> both NoPen (Qt cosmetic-hairline guard).
      const auto r = paintCase(QStringLiteral("style A stroke-width:0"));
      require(outlineInk(r.img, *r.sc) == 0,
              QStringLiteral("stroke-width:0 -> no outline ink; got %1").arg(outlineInk(r.img, *r.sc)));
      require(dividerInk(r.img, *r.sc) == 0,
              QStringLiteral("stroke-width:0 -> no divider ink; got %1").arg(dividerInk(r.img, *r.sc)));
    }
    {
      // stroke:NONE (case-insensitive) -> both hidden.
      const auto r = paintCase(QStringLiteral("style A stroke:NONE"));
      require(outlineInk(r.img, *r.sc) == 0,
              QStringLiteral("stroke:NONE -> no outline ink; got %1").arg(outlineInk(r.img, *r.sc)));
      require(dividerInk(r.img, *r.sc) == 0,
              QStringLiteral("stroke:NONE -> no divider ink; got %1").arg(dividerInk(r.img, *r.sc)));
    }
    // stroke-width valid zero -> no ink on either path.
    for (const QString& w : {QStringLiteral("0"), QStringLiteral("00"),
                             QStringLiteral("0PX")}) {
      const auto r = paintCase(QStringLiteral("style A stroke-width:") + w);
      const int oi = outlineInk(r.img, *r.sc);
      const int di = dividerInk(r.img, *r.sc);
      require(oi == 0, QStringLiteral("stroke-width:%1 -> no outline ink; got %2").arg(w).arg(oi));
      require(di == 0, QStringLiteral("stroke-width:%1 -> no divider ink; got %2").arg(w).arg(di));
    }
    // stroke-width negative / non-empty invalid -> CSS initial 1px, both paths
    // still carry theme ink (visibility unchanged by the width fallback).
    for (const QString& w : {QStringLiteral("-1"), QStringLiteral("-1em"), QStringLiteral("foo")}) {
      const auto r = paintCase(QStringLiteral("style A stroke-width:") + w);
      const int oi = outlineInk(r.img, *r.sc);
      const int di = dividerInk(r.img, *r.sc);
      require(oi > 20, QStringLiteral("stroke-width:%1 -> 1px outline ink; got %2").arg(w).arg(oi));
      require(di > 20, QStringLiteral("stroke-width:%1 -> 1px divider ink; got %2").arg(w).arg(di));
    }
    {
      // stroke:inherit + stroke-width:-1 -> outline hidden, divider 1px visible
      // (the width fallback to 1.0 must not undo the stroke-decided visibility).
      const auto r = paintCase(QStringLiteral("style A stroke:inherit,stroke-width:-1"));
      const int oi = outlineInk(r.img, *r.sc);
      const int di = dividerInk(r.img, *r.sc);
      require(oi == 0, QStringLiteral("inherit + width:-1 -> outline hidden; got %1").arg(oi));
      require(di > 20, QStringLiteral("inherit + width:-1 -> divider 1px visible; got %1").arg(di));
    }
  }

  // ===== 9. Direct CSS length resolver (theme/CssCalc.h) — full data table =====
  // Property-agnostic tri-state; the stroke-width caller (§7) maps Missing->1.3,
  // Invalid->1.0, Valid-zero->NoPen. Here we assert the resolver itself, with a
  // controlled context (so ex/ch assertions are exact, not font-dependent).
  {
    using muffin::CssLengthContext;
    using muffin::CssLengthResult;
    using muffin::CssLengthStatus;
    using muffin::resolveCssLengthToPx;
    const auto ctx = [](qreal em, qreal ex, qreal ch) {
      CssLengthContext c;
      c.emPx = em; c.remPx = 16.0; c.exPx = ex; c.chPx = ch;
      c.viewportPx = QSizeF(800.0, 600.0);  // = kMmdcDefaultCssViewport
      return c;
    };
    const CssLengthContext c16 = ctx(16.0, 8.36, 8.39);
    // QString-routed helper so string literals convert (QStringView won't).
    const auto R = [&](const QString& s, const CssLengthContext& c) { return resolveCssLengthToPx(s, c); };
    const auto valid = [&](const QString& s, const CssLengthContext& c, qreal px) {
      const CssLengthResult r = resolveCssLengthToPx(s, c);
      require(r.status == CssLengthStatus::Valid,
              QStringLiteral("'%1' should be Valid").arg(s));
      require(qAbs(r.px - px) < 1e-3,
              QStringLiteral("'%1' px=%2 exp=%3").arg(s).arg(r.px).arg(px));
    };
    // Fixed units (1px = 1/96in), ASCII case-insensitive.
    valid("4", c16, 4.0); valid("4px", c16, 4.0); valid("4PX", c16, 4.0); valid("4Px", c16, 4.0);
    valid("4pt", c16, 4.0 * 96.0 / 72.0); valid("4pc", c16, 64.0); valid("1in", c16, 96.0);
    valid("1IN", c16, 96.0); valid("1cm", c16, 96.0 / 2.54);
    valid("10mm", c16, 10.0 * 96.0 / 25.4); valid("40Q", c16, 40.0 * 96.0 / 101.6);
    // Font-relative.
    valid("4em", c16, 64.0); valid("4EM", c16, 64.0); valid("4rem", c16, 64.0);
    valid("4ex", c16, 4.0 * 8.36); valid("4ch", c16, 4.0 * 8.39);
    // Viewport @ 800x600.
    valid("4vw", c16, 32.0); valid("4vh", c16, 24.0); valid("4vmin", c16, 24.0);
    valid("4vmax", c16, 32.0);
    // Zero (Valid 0 -> caller applies NoPen).
    valid("0", c16, 0.0); valid("0px", c16, 0.0); valid("0em", c16, 0.0); valid("0vw", c16, 0.0);
    // Negative / invalid -> Invalid.
    for (const QString& s : {QStringLiteral("-1"), QStringLiteral("-1em"),
                             QStringLiteral("-1rem"), QStringLiteral("-1px"),
                             QStringLiteral("foo"), QStringLiteral("4xyz"),
                             QStringLiteral("4em5"), QStringLiteral("--1")})
      require(resolveCssLengthToPx(s, c16).status == CssLengthStatus::Invalid,
              QStringLiteral("'%1' should be Invalid").arg(s));
    // Missing / empty.
    require(R("", c16).status == CssLengthStatus::Missing,
            QStringLiteral("empty -> Missing"));
    require(R("   ", c16).status == CssLengthStatus::Missing,
            QStringLiteral("whitespace -> Missing"));

    // Root font 16 vs 20: em/ex/ch scale; rem + fixed units do NOT.
    const CssLengthContext c20 = ctx(20.0, 10.45, 10.49);  // ex/ch scale with font
    require(qAbs(R("4em", c16).px - 64.0) < 1e-3 && qAbs(R("4em", c20).px - 80.0) < 1e-3,
            QStringLiteral("em scales with root font (16->64, 20->80)"));
    require(qAbs(R("4rem", c16).px - 64.0) < 1e-3 && qAbs(R("4rem", c20).px - 64.0) < 1e-3,
            QStringLiteral("rem does NOT scale with root font (16 fixed)"));
    require(qAbs(R("4ex", c16).px - 4.0 * 8.36) < 1e-3 && qAbs(R("4ex", c20).px - 4.0 * 10.45) < 1e-3,
            QStringLiteral("ex scales with font metric"));
    require(qAbs(R("4ch", c16).px - 4.0 * 8.39) < 1e-3 && qAbs(R("4ch", c20).px - 4.0 * 10.49) < 1e-3,
            QStringLiteral("ch scales with font metric"));
    require(qAbs(R("4pt", c16).px - 4.0 * 96.0 / 72.0) < 1e-3 &&
                qAbs(R("4pt", c20).px - 4.0 * 96.0 / 72.0) < 1e-3,
            QStringLiteral("fixed unit independent of root font"));
  }

  qDebug() << "MermaidRequirementStyleTest: passed";
  return 0;
}
