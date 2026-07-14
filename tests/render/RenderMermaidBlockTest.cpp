// Milestone I integration test: a ```mermaid code fence renders as the native
// diagram through the real DocumentLayout → BlockLayoutBuilder → BlockLayout paint
// pipeline (via MermaidRenderCache in sync mode), falls back to source on error,
// and respects the "show as source" setting.

#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSettings>

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

// Count non-transparent pixels in a rendered block (the diagram actually drew).
qint64 opaquePixels(const BlockLayout* block, const RenderTheme& theme) {
  if (!block) return -1;
  QImage img(static_cast<int>(block->rect().width()), static_cast<int>(block->rect().height()),
             QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter painter(&img);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.translate(-block->rect().left(), -block->rect().top());
  block->paint(painter, theme, /*scrollY=*/0.0);
  painter.end();
  qint64 count = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x)
      if (qAlpha(img.pixel(x, y)) > 16) ++count;
  return count;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("Muffin-test"));
  QSettings().remove(QStringLiteral("editor/showMermaidAsSource"));  // start clean

  const RenderTheme theme = RenderTheme::defaultTheme();

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
    require(block->rect().height() > 10.0, QStringLiteral("rendered block must have height"));
    require(opaquePixels(block, theme) > 50, QStringLiteral("the diagram must draw pixels"));
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
    require(block != nullptr && !block->isMermaidRendered(),
            QStringLiteral("show-as-source must keep the fence as source, not render"));
    QSettings().remove(QStringLiteral("editor/showMermaidAsSource"));
  }

  // --- a malformed mermaid fence falls back to source (Error) ---
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
    require(!block->mermaidErrorMessage().isEmpty(),
            QStringLiteral("Error state must carry a message for the annotation"));
    // The error annotation reserves a strip → block is taller than the source alone.
    require(opaquePixels(block, theme) >= 0, QStringLiteral("painting the error block must not crash"));
  }

  qDebug().noquote() << "RenderMermaidBlockTest: mermaid fence renders via DocumentLayout + cache; show-as-source + error fall back to source";
  return 0;
}
