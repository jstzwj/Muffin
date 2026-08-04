// Milestone I integration test: a ```mermaid code fence renders as the native
// diagram through the real DocumentLayout → BlockLayoutBuilder → BlockLayout paint
// pipeline (via MermaidRenderCache in sync mode), keeps source plus a visible
// diagnostic panel on error/unsupported input, and respects render settings.

#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QSet>

#include <algorithm>
#include <cstdlib>

using namespace muffin;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

// First top-level CodeFence node id in the document (or invalid).
NodeId firstCodeFenceId(const MarkdownDocument& document) {
  for (const auto& child : document.root().children()) {
    if (child->type() == BlockType::CodeFence) return child->id();
  }
  return {};
}

QImage renderBlockImage(const BlockLayout* block, const RenderTheme& theme) {
  if (!block) return {};
  QImage img(static_cast<int>(block->rect().width()), static_cast<int>(block->rect().height()),
             QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter painter(&img);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.translate(-block->rect().left(), -block->rect().top());
  block->paint(painter, theme, /*scrollY=*/0.0);
  painter.end();
  return img;
}

QImage renderBlockSlice(const BlockLayout& block, const RenderTheme& theme,
                        qreal scrollY, QSize viewport = QSize(360, 240)) {
  QImage image(viewport, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setClipRect(image.rect());
  block.paint(painter, theme, scrollY);
  painter.end();
  return image;
}

std::shared_ptr<const mermaid::flowscene::FlowScene> largeFlowScene() {
  auto scene = std::make_shared<mermaid::flowscene::FlowScene>();
  scene->bounds = QRectF(0.0, 0.0, 240.0, 6400.0);
  for (int index = 0; index < 160; ++index) {
    mermaid::flowscene::FlowSceneNode node;
    node.id = QStringLiteral("n%1").arg(index);
    node.shapeType = QStringLiteral("rect");
    node.shapeKind = QStringLiteral("rect");
    node.cx = 120.0;
    node.cy = 20.0 + index * 40.0;
    node.width = 100.0;
    node.height = 26.0;
    node.fill = QStringLiteral("#ececff");
    node.stroke = QStringLiteral("#9370db");
    node.strokeWidth = QStringLiteral("1px");
    scene->nodes.append(std::move(node));
  }
  return scene;
}

// Count non-transparent pixels in a rendered block (the diagram actually drew).
qint64 opaquePixels(const BlockLayout* block, const RenderTheme& theme) {
  const QImage img = renderBlockImage(block, theme);
  if (img.isNull()) return -1;
  qint64 count = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x)
      if (qAlpha(img.pixel(x, y)) > 16) ++count;
  return count;
}

qint64 opaquePixelsInRect(const BlockLayout* block, const RenderTheme& theme,
                          QRectF documentRect) {
  const QImage img = renderBlockImage(block, theme);
  if (img.isNull()) return -1;
  documentRect.translate(-block->rect().topLeft());
  const QRect pixels = documentRect.toAlignedRect().intersected(img.rect());
  qint64 count = 0;
  for (int y = pixels.top(); y <= pixels.bottom(); ++y)
    for (int x = pixels.left(); x <= pixels.right(); ++x)
      if (qAlpha(img.pixel(x, y)) > 16) ++count;
  return count;
}

QColor colorAt(const BlockLayout* block, const RenderTheme& theme,
               QPointF documentPoint) {
  const QImage img = renderBlockImage(block, theme);
  const QPoint local = (documentPoint - block->rect().topLeft()).toPoint();
  return img.rect().contains(local) ? img.pixelColor(local) : QColor();
}

QPointF mermaidScenePointToDocument(const BlockLayout& block,
                                    const RenderTheme& theme,
                                    const QRectF& sceneBounds,
                                    QPointF scenePoint) {
  const QRectF content = block.rect().marginsRemoved(theme.codePadding());
  const QSizeF natural = block.mermaidNaturalSize();
  const qreal scale = qMin<qreal>(1.0, content.width() / natural.width());
  const qreal drawWidth = natural.width() * scale;
  const qreal drawHeight = natural.height() * scale;
  const qreal dx = content.left() +
      qMax<qreal>(0.0, (content.width() - drawWidth) / 2.0);
  const qreal dy = content.top() +
      qMax<qreal>(0.0, (content.height() - drawHeight) / 2.0);
  const auto& metadata = block.mermaidMetadata();
  const qreal contentOffsetX = qMax<qreal>(
      0.0, (natural.width() - metadata.contentSize.width()) / 2.0);
  return QPointF(
      dx + scale * (contentOffsetX + scenePoint.x() - sceneBounds.left()),
      dy + scale * (metadata.titleHeight + metadata.diagramPadding +
                    scenePoint.y() - sceneBounds.top()));
}

qint64 pixelsNearColorInRect(
    const BlockLayout* block, const RenderTheme& theme,
    QRectF documentRect, const QColor& expected, int tolerance = 120) {
  const QImage img = renderBlockImage(block, theme);
  if (img.isNull()) return -1;
  documentRect.translate(-block->rect().topLeft());
  const QRect pixels = documentRect.toAlignedRect().intersected(img.rect());
  qint64 count = 0;
  for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
    for (int x = pixels.left(); x <= pixels.right(); ++x) {
      const QColor actual = img.pixelColor(x, y);
      const int distance = qAbs(actual.red() - expected.red()) +
                           qAbs(actual.green() - expected.green()) +
                           qAbs(actual.blue() - expected.blue());
      if (actual.alpha() > 64 && distance <= tolerance) ++count;
    }
  }
  return count;
}

QColor sourceOverOpaque(const QColor& foreground, const QColor& background) {
  const int alpha = foreground.alpha();
  const auto blend = [alpha](int front, int back) {
    return (front * alpha + back * (255 - alpha) + 127) / 255;
  };
  return QColor(blend(foreground.red(), background.red()),
                blend(foreground.green(), background.green()),
                blend(foreground.blue(), background.blue()));
}

SelectionRange focusedSelection(NodeId blockId, qsizetype offset = 0) {
  SelectionRange selection;
  selection.anchor.blockId = blockId;
  selection.anchor.text.nodeId = blockId;
  selection.anchor.text.textOffset = offset;
  selection.focus = selection.anchor;
  return selection;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
#if defined(Q_OS_LINUX)
  qWarning("skipped on Linux: font/rendering golden coupled to x86 Windows (TODO, docs/mermaid-architecture.md step 5)");
  return 0;
#endif
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("Muffin-test"));
  QSettings().remove(QStringLiteral("editor/showMermaidAsSource"));  // start clean
  QSettings().remove(QStringLiteral("markdown/diagrams"));

  const RenderTheme theme = RenderTheme::defaultTheme();

  // --- BlockLayout maps a scrolled dirty clip into scene coordinates ---
  {
    const auto scene = largeFlowScene();
    const QMarginsF padding = theme.codePadding();
    const QSizeF naturalSize = scene->bounds.size();
    const QRectF blockRect(24.0, 100.0, 300.0,
                           naturalSize.height() + padding.top() + padding.bottom());
    BlockLayout fullPaint;
    fullPaint.setType(BlockType::CodeFence);
    fullPaint.setRect(blockRect);
    fullPaint.setMermaidScene(scene, naturalSize);
    fullPaint.setMermaidState(BlockLayout::MermaidState::Ready);

    BlockLayout culledPaint;
    culledPaint.setType(BlockType::CodeFence);
    culledPaint.setRect(blockRect);
    culledPaint.setMermaidScene(scene, naturalSize);
    culledPaint.setMermaidState(BlockLayout::MermaidState::Ready);
    culledPaint.setMermaidViewportCullingEnabled(true);

    for (const qreal sceneOffset : {0.0, 2480.0, 5760.0, 6400.0}) {
      const qreal scroll = blockRect.top() + padding.top() + sceneOffset;
      const QImage expected = renderBlockSlice(fullPaint, theme, scroll);
      const QImage actual = renderBlockSlice(culledPaint, theme, scroll);
      require(actual == expected,
              QStringLiteral("viewport culling changed BlockLayout pixels at offset %1")
                  .arg(sceneOffset));
    }

    mermaid::MermaidRenderMetadata metadata;
    metadata.title = QStringLiteral("Large titled scene");
    metadata.titleColor = QStringLiteral("#112233");
    metadata.titleHeight = 40.0;
    metadata.diagramPadding = 12.0;
    metadata.contentSize = naturalSize;
    const QSizeF titledSize(
        naturalSize.width() + 2.0 * metadata.diagramPadding,
        naturalSize.height() + metadata.titleHeight +
            2.0 * metadata.diagramPadding);
    const QRectF titledRect(
        blockRect.left(), blockRect.top(), blockRect.width(),
        titledSize.height() + padding.top() + padding.bottom());
    BlockLayout titledFullPaint;
    titledFullPaint.setType(BlockType::CodeFence);
    titledFullPaint.setRect(titledRect);
    titledFullPaint.setMermaidScene(scene, titledSize, metadata);
    titledFullPaint.setMermaidState(BlockLayout::MermaidState::Ready);
    BlockLayout titledCulledPaint;
    titledCulledPaint.setType(BlockType::CodeFence);
    titledCulledPaint.setRect(titledRect);
    titledCulledPaint.setMermaidScene(scene, titledSize, metadata);
    titledCulledPaint.setMermaidState(BlockLayout::MermaidState::Ready);
    titledCulledPaint.setMermaidViewportCullingEnabled(true);
    for (const qreal canvasOffset : {0.0, 40.0, 52.0, 2532.0,
                                     5812.0, 6452.0}) {
      const qreal scroll = titledRect.top() + padding.top() + canvasOffset;
      const QImage expected =
          renderBlockSlice(titledFullPaint, theme, scroll);
      const QImage actual =
          renderBlockSlice(titledCulledPaint, theme, scroll);
      require(actual == expected,
              QStringLiteral(
                  "titled viewport culling changed pixels at offset %1")
                  .arg(canvasOffset));
    }
  }

  // --- a valid mermaid fence renders the diagram ---
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("```mermaid\nflowchart TB\nA[Alpha] --> B[Beta]\n```\n"), false);
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0);
    const NodeId fenceId = firstCodeFenceId(session.document());
    require(fenceId.isValid(), QStringLiteral("expected a code-fence block"));
    const BlockLayout* block = layout.block(fenceId);
    require(block != nullptr, QStringLiteral("mermaid block should be built"));
    require(block->isMermaidRendered(), QStringLiteral("valid mermaid fence should render the diagram"));
    require(block->mermaidScene() != nullptr, QStringLiteral("rendered block must carry a scene"));
    require(!block->mermaidViewportCullingEnabled(),
            QStringLiteral("sync export/print layout must keep full-scene painting"));
    require(block->rect().height() > 10.0, QStringLiteral("rendered block must have height"));
    require(opaquePixels(block, theme) > 50, QStringLiteral("the diagram must draw pixels"));

    // Reusing the ready cache through the normal editor path must enable dirty
    // viewport culling without rebuilding the scene.
    DocumentLayout editorLayout;
    editorLayout.setMermaidRenderCache(&cache);
    editorLayout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* editorBlock = editorLayout.block(fenceId);
    require(editorBlock != nullptr && editorBlock->isMermaidRendered() &&
                editorBlock->mermaidViewportCullingEnabled(),
            QStringLiteral("editor layout must enable Mermaid viewport culling"));
  }

  // --- diagram titles share the block canvas and preserve link hit testing ---
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral(
        "```mermaid\n"
        "---\ntitle: Checkout flow\n---\n"
        "%%{init: {\"themeVariables\": {\"textColor\": \"#ff00ff\"}}}%%\n"
        "flowchart TB\n"
        "accTitle: Accessible checkout flow\n"
        "accDescr: Checkout states and transitions\n"
        "A[Start] --> B[Done]\n"
        "click A href \"https://example.com/start\" \"Open start\"\n"
        "```\n"), false);
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* block = layout.block(firstCodeFenceId(session.document()));
    require(block != nullptr && block->isMermaidRendered() &&
                block->mermaidScene() != nullptr,
            QStringLiteral("titled Mermaid block must render"));
    const auto& metadata = block->mermaidMetadata();
    require(metadata.title == QLatin1String("Checkout flow") &&
                metadata.accessibleTitle ==
                    QLatin1String("Accessible checkout flow") &&
                metadata.accessibleDescription ==
                    QLatin1String("Checkout states and transitions") &&
                metadata.titleHeight >= 40.0 &&
                metadata.diagramPadding == 8.0,
            QStringLiteral("BlockLayout lost Mermaid presentation metadata"));

    const QRectF content = block->rect().marginsRemoved(theme.codePadding());
    const QSizeF natural = block->mermaidNaturalSize();
    const qreal scale = qMin<qreal>(1.0, content.width() / natural.width());
    const qreal drawWidth = natural.width() * scale;
    const qreal drawHeight = natural.height() * scale;
    const qreal dx = content.left() +
        qMax<qreal>(0.0, (content.width() - drawWidth) / 2.0);
    const qreal dy = content.top() +
        qMax<qreal>(0.0, (content.height() - drawHeight) / 2.0);
    require(pixelsNearColorInRect(
                block, theme,
                QRectF(dx, dy, drawWidth, metadata.titleHeight * scale),
                QColor(QStringLiteral("#ff00ff")), 80) > 2,
            QStringLiteral("shared Mermaid title painter produced no title pixels"));

    const auto* flowScene =
        dynamic_cast<const mermaid::flowscene::FlowScene*>(block->mermaidScene());
    require(flowScene != nullptr,
            QStringLiteral("titled Mermaid block must hold a flowchart scene"));
    const auto node = std::find_if(
        flowScene->nodes.cbegin(), flowScene->nodes.cend(),
        [](const auto& value) { return value.id == QLatin1String("A"); });
    require(node != flowScene->nodes.cend(),
            QStringLiteral("linked flowchart node A must exist"));
    const qreal contentOffsetX = qMax<qreal>(
        0.0, (natural.width() - metadata.contentSize.width()) / 2.0);
    const QPointF nodePoint(
        dx + scale * (contentOffsetX + node->cx - flowScene->bounds.left()),
        dy + scale * (metadata.titleHeight + metadata.diagramPadding +
                      node->cy - flowScene->bounds.top()));
    const HitTestResult nodeHit = block->hitTest(nodePoint, theme, nullptr);
    require(nodeHit.mermaidRendered &&
                nodeHit.linkHref == QLatin1String("https://example.com/start") &&
                nodeHit.toolTip == QLatin1String("Open start"),
            QStringLiteral("title offset or export-ready hit state drifted"));
  }

  // --- sequence participant menus share paint, hit, and URL safety geometry ---
  {
    const QString body = QStringLiteral(
        "sequenceDiagram\n"
        "participant A as Browser\n"
        "participant B as API\n"
        "links A: {\"Docs\":\"https://example.com/docs\","
        "\"Blocked\":\"javascript:alert(1)\"}\n"
        "A->>B: request\n");
    const auto build = [&](const QString& source,
                           DocumentSession& session,
                           mermaid::editor::MermaidRenderCache& cache,
                           DocumentLayout& layout) {
      session.setMarkdownText(
          QStringLiteral("```mermaid\n") + source +
              QStringLiteral("```\n"), false);
      layout.setMermaidRenderCache(&cache);
      layout.setMermaidSyncMode(true);
      layout.rebuild(session.document(), theme, 800.0);
      return layout.block(firstCodeFenceId(session.document()));
    };

    DocumentSession forcedSession;
    mermaid::editor::MermaidRenderCache forcedCache;
    DocumentLayout forcedLayout;
    const BlockLayout* forced = build(
        QStringLiteral(
            "%%{init: {\"sequence\": {\"forceMenus\": true}}}%%\n") +
            body,
        forcedSession, forcedCache, forcedLayout);
    const auto* forcedSeq =
        dynamic_cast<const mermaid::sequence::SequenceScene*>(forced->mermaidScene());
    require(forced && forcedSeq && forcedSeq->forceMenus &&
                forcedSeq->menus.size() == 1,
            QStringLiteral("forceMenus sequence scene did not expose one menu"));
    const auto& forcedScene = *forcedSeq;
    const auto& forcedMenu = forcedScene.menus.first();
    const auto docs = std::find_if(
        forcedMenu.items.cbegin(), forcedMenu.items.cend(),
        [](const auto& item) { return item.label == QLatin1String("Docs"); });
    const auto blocked = std::find_if(
        forcedMenu.items.cbegin(), forcedMenu.items.cend(),
        [](const auto& item) { return item.label == QLatin1String("Blocked"); });
    require(docs != forcedMenu.items.cend() &&
                blocked != forcedMenu.items.cend(),
            QStringLiteral("sequence menu items were lost"));
    const QPointF docsPoint = mermaidScenePointToDocument(
        *forced, theme, forcedScene.bounds, docs->hitRect.center());
    const QPointF blockedPoint = mermaidScenePointToDocument(
        *forced, theme, forcedScene.bounds, blocked->hitRect.center());
    require(forced->hitTest(docsPoint, theme).linkHref ==
                QLatin1String("https://example.com/docs") &&
                forced->hitTest(blockedPoint, theme).linkHref.isEmpty(),
            QStringLiteral("sequence menu URL safety contract failed"));

    DocumentSession toggleSession;
    mermaid::editor::MermaidRenderCache toggleCache;
    DocumentLayout toggleLayout;
    const BlockLayout* toggle =
        build(body, toggleSession, toggleCache, toggleLayout);
    const auto* toggleSeq =
        dynamic_cast<const mermaid::sequence::SequenceScene*>(toggle->mermaidScene());
    require(toggle && toggleSeq && !toggleSeq->forceMenus,
            QStringLiteral("default sequence menu must start closed"));
    const auto& toggleScene = *toggleSeq;
    const auto actor = std::find_if(
        toggleScene.participants.cbegin(), toggleScene.participants.cend(),
        [](const auto& value) { return value.id == QLatin1String("A"); });
    require(actor != toggleScene.participants.cend(),
            QStringLiteral("sequence menu actor A missing"));
    const QPointF actorPoint = mermaidScenePointToDocument(
        *toggle, theme, toggleScene.bounds,
        actor->topPaintedBounds.center());
    require(toggle->hitTest(actorPoint, theme).mermaidMenuActorId ==
                QLatin1String("A"),
            QStringLiteral("participant click did not request menu toggle"));
    QSet<QString> openMenus{QStringLiteral("A")};
    const auto openDocs = std::find_if(
        toggleScene.menus.first().items.cbegin(),
        toggleScene.menus.first().items.cend(),
        [](const auto& item) { return item.label == QLatin1String("Docs"); });
    require(openDocs != toggleScene.menus.first().items.cend(),
            QStringLiteral("toggle menu Docs item missing"));
    const QPointF openDocsPoint = mermaidScenePointToDocument(
        *toggle, theme, toggleScene.bounds,
        openDocs->hitRect.center());
    require(toggle->hitTest(openDocsPoint, theme, nullptr, &openMenus)
                    .linkHref == QLatin1String("https://example.com/docs"),
            QStringLiteral("toggled sequence menu did not expose its safe link"));
  }

  // --- state diagrams use the same rendered block pipeline ---
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral(
        "```mermaid\nstateDiagram-v2\n[*] --> Idle\nIdle --> [*]\n```\n"), false);
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* block = layout.block(firstCodeFenceId(session.document()));
    require(block != nullptr && block->isMermaidRendered(),
            QStringLiteral("state diagram fence should render through BlockLayout"));
    require(block->rect().height() > 10.0 && opaquePixels(block, theme) > 50,
            QStringLiteral("state diagram block must have geometry and painted pixels"));
  }

  // --- erDiagram renders natively through the same pipeline (regression for
  //     the on-screen gate that used to enumerate four families and omit ER) ---
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral(
        "```mermaid\nerDiagram\n"
        "CUSTOMER ||--o{ ORDER : places\n"
        "ORDER ||--o{ ITEM : contains\n```\n"), false);
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* block = layout.block(firstCodeFenceId(session.document()));
    require(block != nullptr && block->isMermaidRendered(),
            QStringLiteral("erDiagram fence should render through BlockLayout"));
    require(block->mermaidScene() != nullptr,
            QStringLiteral("rendered ER block must carry a native scene"));
    const auto* erScene =
        dynamic_cast<const mermaid::er::ErScene*>(block->mermaidScene());
    require(erScene != nullptr && !erScene->entities.isEmpty(),
            QStringLiteral("ER block must hold an ErScene with entities"));
    require(block->rect().height() > 10.0 && opaquePixels(block, theme) > 50,
            QStringLiteral("ER block must have geometry and painted pixels"));
  }

  // --- the Markdown diagrams setting disables rendering ---
  {
    QSettings().setValue(QStringLiteral("markdown/diagrams"), false);
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("```mermaid\nflowchart TB\nA --> B\n```\n"), false);
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* block = layout.block(firstCodeFenceId(session.document()));
    require(block != nullptr && !block->isMermaidRendered() &&
                block->mermaidState() == BlockLayout::MermaidState::None,
            QStringLiteral("disabled diagrams setting must keep Mermaid fences as source"));
    QSettings().remove(QStringLiteral("markdown/diagrams"));
  }

  // --- "show as source" disables rendering (source fallback) ---
  {
    QSettings().setValue(QStringLiteral("editor/showMermaidAsSource"), true);
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("```mermaid\nflowchart TB\nA --> B\n```\n"), false);
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0);
    const NodeId fenceId = firstCodeFenceId(session.document());
    const BlockLayout* block = layout.block(fenceId);
    require(block != nullptr && !block->isMermaidRendered() &&
                block->mermaidState() == BlockLayout::MermaidState::Ready,
            QStringLiteral("show-as-source must keep the fence as source, not render"));
    QSettings().remove(QStringLiteral("editor/showMermaidAsSource"));
  }

  // --- a malformed Mermaid fence keeps source and paints a separate panel ---
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("```mermaid\nflowchart TB\nA --> B\nlinkStyle 9 stroke:red\n```\n"), false);
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0);
    const NodeId fenceId = firstCodeFenceId(session.document());
    const BlockLayout* block = layout.block(fenceId);
    require(block != nullptr, QStringLiteral("malformed mermaid block should be built"));
    require(!block->isMermaidRendered(),
            QStringLiteral("malformed mermaid must fall back to source, not render a broken diagram"));
    require(block->mermaidState() == BlockLayout::MermaidState::Error,
            QStringLiteral("malformed mermaid must be in Error state (got %1)").arg((int)block->mermaidState()));
    require(!block->mermaidDiagnosticMessage().isEmpty(),
            QStringLiteral("Error state must carry a diagnostic message"));
    const mermaid::MermaidDiagnostic& diagnostic = block->mermaidDiagnostic();
    const qsizetype expectedOffset = block->literal().indexOf(QLatin1Char('9'));
    require(diagnostic.stage == QLatin1String("semantic") &&
                diagnostic.code == QLatin1String("link-style-bounds") &&
                diagnostic.span.offset == expectedOffset &&
                diagnostic.span.line == 3 && diagnostic.span.column == 11 &&
                block->mermaidDiagnosticMessage().contains(
                    QStringLiteral("Line 3, column 11")),
            QStringLiteral("diagnostic panel must expose structured line/column details"));

    const QRectF panel = block->mermaidDiagnosticRect(theme);
    const QRectF sourceContent = block->literalContentRect(theme);
    const qreal sourceBoxBottom = sourceContent.bottom() + theme.codePadding().bottom();
    require(panel.isValid() && panel.top() > sourceBoxBottom,
            QStringLiteral("diagnostic panel must be below and separate from the source box"));
    require(panel.bottom() <= block->rect().bottom() + 0.01,
            QStringLiteral("diagnostic panel must fit inside the block layout"));
    require(opaquePixelsInRect(block, theme, panel) >
                panel.width() * panel.height() * 0.75,
            QStringLiteral("diagnostic panel background must be visibly painted"));
    const QPointF gapPoint(panel.center().x(),
                           (sourceBoxBottom + panel.top()) / 2.0);
    require(colorAt(block, theme, gapPoint).alpha() < 16,
            QStringLiteral("source box and diagnostic panel must have a visible gap"));
    const QColor accentPixel = colorAt(
        block, theme, QPointF(panel.left() + 2.5, panel.center().y()));
    require(accentPixel.isValid() && accentPixel.alpha() > 200 &&
                accentPixel != theme.codeBackgroundColor(),
            QStringLiteral("diagnostic panel must paint an accent stripe"));

    const QVector<QRectF> sourceMarks =
        block->mermaidDiagnosticSourceRects(theme);
    require(!sourceMarks.isEmpty(),
            QStringLiteral("diagnostic must expose its marked source range"));
    QColor errorWash = theme.alertAccent(AlertKind::Caution);
    errorWash.setAlpha(32);
    const QColor paintedErrorWash =
        sourceOverOpaque(errorWash, theme.codeBlockBackgroundColor());
    qint64 markedPixels = 0;
    for (const QRectF& mark : sourceMarks) {
      markedPixels += pixelsNearColorInRect(
          block, theme, mark, paintedErrorWash, 18);
    }
    require(markedPixels > 0,
            QStringLiteral("diagnostic source range must paint an error wash"));

    const HitTestResult diagnosticHit =
        block->hitTest(panel.center(), theme);
    require(diagnosticHit.isValid() &&
                diagnosticHit.zone == HitTestResult::Zone::Code &&
                diagnosticHit.textOffset == expectedOffset,
            QStringLiteral("clicking the diagnostic panel must target the source error offset"));
  }

  // --- focused Mermaid source is validated without replacing the editor ---
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral(
        "```mermaid\nflowchart TB\nA --> B\nlinkStyle 9 stroke:red\n```\n"), false);
    const NodeId fenceId = firstCodeFenceId(session.document());
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0,
                   focusedSelection(fenceId, 4));
    const BlockLayout* block = layout.block(fenceId);
    require(block != nullptr && !block->isMermaidRendered() &&
                block->mermaidState() == BlockLayout::MermaidState::Error &&
                block->mermaidDiagnosticRect(theme).isValid(),
            QStringLiteral("focused invalid Mermaid must keep source and show its diagnostic"));
  }

  // --- a focused valid fence stays source; leaving focus renders it ---
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral(
        "```mermaid\nflowchart LR\nA --> B\n```\n"), false);
    const NodeId fenceId = firstCodeFenceId(session.document());
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0,
                   focusedSelection(fenceId, 4));
    const BlockLayout* focused = layout.block(fenceId);
    require(focused != nullptr && !focused->isMermaidRendered() &&
                focused->mermaidState() == BlockLayout::MermaidState::Ready &&
                !focused->mermaidDiagnosticRect(theme).isValid(),
            QStringLiteral("focused valid Mermaid must remain editable source"));

    layout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* unfocused = layout.block(fenceId);
    require(unfocused != nullptr && unfocused->isMermaidRendered(),
            QStringLiteral("valid Mermaid must render after focus leaves"));
  }

  // --- unsupported Mermaid families keep source and explain why ---
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral(
        "```mermaid\ngantt\ntitle A\ndateFormat X\nsection S\nt1 :a, 1, 2d\n```\n"),
        false);
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    layout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* block = layout.block(firstCodeFenceId(session.document()));
    require(block != nullptr && !block->isMermaidRendered() &&
                block->mermaidState() == BlockLayout::MermaidState::Unsupported &&
                !block->mermaidDiagnosticMessage().isEmpty() &&
                block->mermaidDiagnosticRect(theme).isValid(),
            QStringLiteral("unsupported Mermaid family must show a diagnostic panel"));
  }

  // --- correcting invalid source removes the panel and restores rendering ---
  {
    DocumentSession session;
    mermaid::editor::MermaidRenderCache cache;
    DocumentLayout layout;
    layout.setMermaidRenderCache(&cache);
    layout.setMermaidSyncMode(true);
    session.setMarkdownText(QStringLiteral(
        "```mermaid\nflowchart TB\nA --> B\nlinkStyle 9 stroke:red\n```\n"), false);
    layout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* invalid = layout.block(firstCodeFenceId(session.document()));
    require(invalid != nullptr && invalid->mermaidDiagnosticRect(theme).isValid(),
            QStringLiteral("invalid source must start with a diagnostic"));

    session.setMarkdownText(QStringLiteral(
        "```mermaid\nflowchart TB\nA --> B\n```\n"), false);
    layout.rebuild(session.document(), theme, 800.0);
    const BlockLayout* corrected = layout.block(firstCodeFenceId(session.document()));
    require(corrected != nullptr && corrected->isMermaidRendered() &&
                corrected->mermaidDiagnosticMessage().isEmpty() &&
                !corrected->mermaidDiagnosticRect(theme).isValid(),
            QStringLiteral("corrected Mermaid must clear the diagnostic and render"));
  }

  QSettings().remove(QStringLiteral("editor/showMermaidAsSource"));
  QSettings().remove(QStringLiteral("markdown/diagrams"));
  qDebug().noquote() << "RenderMermaidBlockTest: native diagrams render; focused/error/unsupported source uses a visible diagnostic panel";
  return 0;
}
