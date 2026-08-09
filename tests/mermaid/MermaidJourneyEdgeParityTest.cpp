#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/journey/JourneyDiagram.h"
#include "mermaid/journey/JourneyScene.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

std::shared_ptr<const journey::JourneyScene> renderScene(const QString& source) {
  editor::MermaidRenderCache cache;
  const editor::MermaidRenderEntry entry =
      cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          QStringLiteral("Journey source did not produce a scene: ") +
              entry.errorMessage);
  const auto scene =
      std::dynamic_pointer_cast<const journey::JourneyScene>(entry.scene);
  require(bool(scene), QStringLiteral("Journey scene has the wrong type"));
  return scene;
}

QImage paintScene(const journey::JourneyScene& scene) {
  const QRectF bounds = scene.renderBounds();
  QImage image(qCeil(bounds.width()), qCeil(bounds.height()),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.translate(-bounds.left(), -bounds.top());
  scene.paint(painter, MermaidPaintOptions{});
  painter.end();
  return image;
}

int alphaPixels(const QImage& image, QRect rect) {
  rect = rect.intersected(image.rect());
  int count = 0;
  for (int y = rect.top(); y <= rect.bottom(); ++y)
    for (int x = rect.left(); x <= rect.right(); ++x)
      count += image.pixelColor(x, y).alpha() != 0;
  return count;
}

int darkPixels(const QImage& image, QRect rect) {
  rect = rect.intersected(image.rect());
  int count = 0;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      const QColor c = image.pixelColor(x, y);
      count += c.alpha() > 32 && c.red() < 80 && c.green() < 80 && c.blue() < 80;
    }
  }
  return count;
}

int nearColorPixels(const QImage& image, QRect rect, const QColor& expected,
                    int tolerance = 8) {
  rect = rect.intersected(image.rect());
  int count = 0;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      const QColor c = image.pixelColor(x, y);
      count += c.alpha() > 32 &&
               qAbs(c.red() - expected.red()) <= tolerance &&
               qAbs(c.green() - expected.green()) <= tolerance &&
               qAbs(c.blue() - expected.blue()) <= tolerance;
    }
  }
  return count;
}

QRect imageRect(const journey::JourneyScene& scene, const QRectF& rect) {
  return QRect(qFloor(rect.x() - scene.bounds.x()),
               qFloor(rect.y() - scene.bounds.y()), qCeil(rect.width()),
               qCeil(rect.height()));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  // Hash comments win lexically over semicolons in INITIAL state.
  const journey::JourneyData comments = journey::JourneyDiagram::parse(
      QStringLiteral("journey\nsection A # ; legal\ntask: 5 # ; legal"));
  require(comments.sections == QStringList{QStringLiteral("A ")} &&
              comments.tasks.size() == 1,
          QStringLiteral("Hash-comment semicolons must remain legal"));

  const auto negativeWrap = renderScene(QStringLiteral(
      "%%{init: {\"journey\": {\"maxLabelWidth\": -1}}}%%\n"
      "journey\nsection S\ntask: 5: Bob"));
  require(negativeWrap->actors.size() == 1 &&
              negativeWrap->actors.first().lines ==
                  QStringList{QStringLiteral("-"), QStringLiteral("B-"),
                              QStringLiteral("o-"), QStringLiteral("b")},
          QStringLiteral("Negative maxLabelWidth must follow JS character wrapping"));

  const auto slotFallback = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"fillType0\": \"#ff0000\", "
      "\"fillType1\": \"\"}, \"journey\": {\"textPlacement\": \"tspan\"}}}%%\n"
      "journey\nsection One\na: 5\nsection Two\nb: 5"));
  require(slotFallback->sections.size() == 2 &&
              slotFallback->sections.at(0).cssFillActive &&
              !slotFallback->sections.at(1).cssFillActive &&
              slotFallback->sections.at(1).fill ==
                  slotFallback->sections.at(1).presentationFill,
          QStringLiteral("Each fillType slot must independently fall back"));

  const auto noFill = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"fillType0\": \"none\"}, "
      "\"journey\": {\"textPlacement\": \"tspan\"}}}%%\n"
      "journey\nsection Hidden\ntask: 5"));
  const QImage noFillImage = paintScene(*noFill);
  const QRect sectionInterior =
      imageRect(*noFill, noFill->sections.first().rect.adjusted(5, 5, -5, -5));
  require(alphaPixels(noFillImage, sectionInterior) == 0,
          QStringLiteral("fillType:none must hide both section fill and label"));

  const auto noTitle = renderScene(QStringLiteral(
      "%%{init: {\"journey\": {\"titleColor\": \"none\"}}}%%\n"
      "journey\ntitle Hidden\nsection S\ntask: 5"));
  const QImage noTitleImage = paintScene(*noTitle);
  require(alphaPixels(noTitleImage,
                      imageRect(*noTitle,
                                QRectF(noTitle->leftMarginResolved - 2.0, -5.0,
                                       120.0, 40.0))) == 0,
          QStringLiteral("titleColor:none must suppress title paint"));

  const auto clipped = renderScene(
      QStringLiteral("journey\nsection S\nzero: 0"));
  const QImage clippedImage = paintScene(*clipped);
  require(alphaPixels(clippedImage,
                      QRect(0, clippedImage.height() - 25,
                            clippedImage.width(), 25)) == 0,
          QStringLiteral("The 25px root-only tail must stay transparent"));

  const auto prototype = renderScene(
      QStringLiteral("journey\nsection S\ntask: 5: __proto__, constructor"));
  require(prototype->hasPrototypeActor &&
              prototype->actors.size() == 1 &&
              prototype->actors.first().name == QStringLiteral("constructor") &&
              prototype->tasks.first().people ==
                  QStringList{QStringLiteral("__proto__"),
                              QStringLiteral("constructor")} &&
              prototype->interactions.size() == 2,
          QStringLiteral("Plain-object __proto__ actor behavior drifted"));

  const auto noRootText = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"textColor\": \"none\"}}}%%\n"
      "journey\nsection S\ntask: 5"));
  const QImage noRootTextImage = paintScene(*noRootText);
  const QPoint axisProbe(qRound(noRootText->leftMarginResolved + 100.0),
                         qRound(noRootText->config.height * 4.0 -
                                noRootText->bounds.y()));
  require(noRootTextImage.pixelColor(axisProbe).alpha() == 0,
          QStringLiteral("textColor:none must suppress the bottom axis"));
  require(darkPixels(noRootTextImage,
                     imageRect(*noRootText,
                               noRootText->tasks.first().rect.adjusted(
                                   5, 5, -5, -5))) > 5,
          QStringLiteral("CSS color:none must leave foreignObject text visible"));

  // D3 feeds taskFontSize to both CSS and arithmetic. Preserve the CSS used
  // value separately from Number(raw), and keep SVG title font-size semantics.
  const auto fontUnits = renderScene(QStringLiteral(
      "%%{init: {\"journey\": {\"textPlacement\": \"tspan\", "
      "\"taskFontSize\": \"2em\", \"titleFontSize\": \"20\"}}}%%\n"
      "journey\ntitle T\nsection S\na<br>b: 5"));
  require(qAbs(fontUnits->config.taskFontSize - 32.0) < 0.001 &&
              std::isnan(fontUnits->config.taskFontLineStep) &&
              qAbs(fontUnits->config.titleFontSize - 20.0) < 0.001,
          QStringLiteral("Journey raw font-size CSS/arithmetic semantics drifted"));
  const auto fontBool = renderScene(QStringLiteral(
      "%%{init: {\"journey\": {\"textPlacement\": \"tspan\", "
      "\"taskFontSize\": true, \"titleFontSize\": true}}}%%\n"
      "journey\ntitle T\nsection S\na<br>b: 5"));
  require(qAbs(fontBool->config.taskFontSize - 16.0) < 0.001 &&
              qAbs(fontBool->config.taskFontLineStep - 1.0) < 0.001 &&
              qAbs(fontBool->config.titleFontSize - 16.0) < 0.001,
          QStringLiteral("Boolean Journey font sizes must inherit but retain JS dy"));
  const auto fontComposite = renderScene(QStringLiteral(
      "%%{init: {\"journey\": {\"taskFontSize\": [20], "
      "\"titleFontSize\": {}, \"textPlacement\": []}}}%%\n"
      "journey\ntitle T\nsection S\ntask: 5"));
  require(qAbs(fontComposite->config.taskFontSize - 14.0) < 0.001 &&
              qAbs(fontComposite->config.taskFontLineStep - 14.0) < 0.001 &&
              fontComposite->config.textPlacement == QLatin1String("fo"),
          QStringLiteral("Composite Journey scalar overrides must be ignored"));

  // JS '+' is intentionally observable for string-valued layout config.
  const auto stringLeft = renderScene(QStringLiteral(
      "%%{init: {\"journey\": {\"leftMargin\": \"20\"}}}%%\n"
      "journey\nsection S\ntask: 5: Bob"));
  const qreal expectedStringLeft =
      (QStringLiteral("20") + editor::jsNumberToString(
                                   stringLeft->actors.first().maxLineWidth))
          .toDouble();
  require(qAbs(stringLeft->leftMarginResolved - expectedStringLeft) < 0.001,
          QStringLiteral("String leftMargin must use JavaScript concatenation"));
  const auto stringY = renderScene(QStringLiteral(
      "%%{init: {\"journey\": {\"diagramMarginY\": \"20\"}}}%%\n"
      "journey\nsection S\ntask: 5"));
  require(stringY->tasks.first().rect.y() == 10020.0,
          QStringLiteral("String diagramMarginY must preserve JS '+' ordering"));
  const auto boolSize = renderScene(QStringLiteral(
      "%%{init: {\"journey\": {\"width\": true, \"height\": true}}}%%\n"
      "journey\nsection S\ntask: 5"));
  require(boolSize->config.width == 1.0 && boolSize->config.height == 1.0 &&
              boolSize->tasks.first().rect.width() == 0.0 &&
              boolSize->tasks.first().rect.height() == 0.0,
          QStringLiteral("Boolean width/height need separate layout and SVG values"));

  const auto emptyTextColor = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"textColor\": \"\"}}}%%\n"
      "journey\ntitle T\nsection S\nTask: 4: Bob"));
  const QImage emptyTextImage = paintScene(*emptyTextColor);
  const int taskLineX = qRound(emptyTextColor->tasks.first().faceCenter.x() -
                               emptyTextColor->bounds.x());
  const int taskLineY = qRound(emptyTextColor->tasks.first().rect.bottom() + 25.0 -
                               emptyTextColor->bounds.y());
  const QPoint emptyAxis(qRound(emptyTextColor->leftMarginResolved + 50.0 -
                                emptyTextColor->bounds.x()),
                         qRound(emptyTextColor->config.height * 4.0 -
                                emptyTextColor->bounds.y()));
  require(emptyTextImage.pixelColor(taskLineX, taskLineY).alpha() > 0 &&
              emptyTextImage.pixelColor(emptyAxis).alpha() > 0,
          QStringLiteral("Empty textColor must fall back to line presentation paint"));

  const auto initialText = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"textColor\": \"initial\"}}}%%\n"
      "journey\nsection S\nTask: 4"));
  const QImage initialTextImage = paintScene(*initialText);
  const QPoint initialAxis(qRound(initialText->leftMarginResolved + 30.0 -
                                  initialText->bounds.x()),
                           qRound(initialText->config.height * 4.0 -
                                  initialText->bounds.y()));
  const QRect markerRect(qRound(initialText->canvasWidth -
                                initialText->leftMarginResolved - 28.0 -
                                initialText->bounds.x()),
                         initialAxis.y() - 12, 36, 24);
  require(initialTextImage.pixelColor(initialAxis).alpha() == 0 &&
              darkPixels(initialTextImage, markerRect) > 5,
          QStringLiteral("textColor:initial hides lines but not the root-fill marker"));

  const auto revertedFill = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"fillType0\": \"revert-layer\"}, "
      "\"journey\": {\"textPlacement\": \"tspan\"}}}%%\n"
      "journey\nsection S\ntask: 5"));
  const QImage revertedFillImage = paintScene(*revertedFill);
  const QRect revertedSection = imageRect(*revertedFill,
      revertedFill->sections.first().rect.adjusted(3, 3, -3, -3));
  require(nearColorPixels(revertedFillImage, revertedSection,
                          QColor(QStringLiteral("#191970"))) > 100 &&
              nearColorPixels(revertedFillImage, revertedSection, Qt::white) > 3,
          QStringLiteral("fillType:revert-layer must restore navy + white presentations"));

  const auto inheritedNone = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"textColor\": \"none\", "
      "\"fillType0\": \"inherit\"}, \"journey\": {"
      "\"textPlacement\": \"tspan\", \"titleColor\": \"bogus\"}}}%%\n"
      "journey\ntitle Hidden\nsection S\ntask: 5"));
  const QImage inheritedNoneImage = paintScene(*inheritedNone);
  require(alphaPixels(inheritedNoneImage,
                      imageRect(*inheritedNone,
                                inheritedNone->sections.first().rect.adjusted(
                                    4, 4, -4, -4))) == 0 &&
              alphaPixels(inheritedNoneImage,
                          imageRect(*inheritedNone,
                                    QRectF(inheritedNone->leftMarginResolved,
                                           -5.0, 100.0, 35.0))) == 0,
          QStringLiteral("Inherited root none must hide fillType and title paint"));

  std::puts("MermaidJourneyEdgeParityTest: passed");
  return 0;
}
