#include "document/DocumentSession.h"
#include "editor/EditorView.h"
#include "render/DocumentLayout.h"
#include "render/ImageLoader.h"
#include "render/LayoutResources.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "theme/CssThemeMapper.h"
#include "theme/ThemeFontDecoder.h"
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QtMath>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <iostream>

using namespace muffin;
namespace {
int failures = 0;
void require(bool value, const char* message) {
  if (!value) {
    ++failures;
    std::cerr << message << '\n';
  }
}
QImage image(QSize size, QColor color) {
  QImage result(size, QImage::Format_ARGB32_Premultiplied);
  result.fill(color);
  return result;
}
QImage paint(DocumentLayout& layout, const RenderTheme& theme, int width) {
  QImage result(width, qMin(2400, qMax(1, qCeil(layout.totalHeight()))), QImage::Format_ARGB32_Premultiplied);
  result.fill(theme.backgroundColor());
  QPainter painter(&result);
  for (const auto* block : layout.promotedBlocks())
    block->paint(painter, theme, 0);
  return result;
}
void sameTree(const BlockLayout& actual, const BlockLayout& expected) {
  require(actual.rect() == expected.rect(), "incremental/full box geometry agrees");
  require(actual.children().size() == expected.children().size(), "incremental/full child count agrees");
  require(actual.listMarker() == expected.listMarker() && actual.listContentIndent() == expected.listContentIndent(),
          "incremental/full list labels and shared gutters agree");
  if (const auto* text = actual.inlineLayout()) {
    require(expected.inlineLayout() && text->displayText() == expected.inlineLayout()->displayText(),
            "incremental/full displayed text agrees");
    require(text->cursorRectForSourceOffset(1) == expected.inlineLayout()->cursorRectForSourceOffset(1),
            "incremental/full caret agrees");
    require(text->selectionRectsForSourceOffsets(0, 2) == expected.inlineLayout()->selectionRectsForSourceOffsets(0, 2),
            "incremental/full selection agrees");
  }
  for (size_t i = 0; i < std::min(actual.children().size(), expected.children().size()); ++i)
    sameTree(*actual.children()[i], *expected.children()[i]);
}
void sameFresh(DocumentLayout& layout, const DocumentSession& session, const RenderTheme& theme, int width,
               bool pixels = true) {
  for (const auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
    DocumentLayout fresh;
    fresh.rebuild(session.document(), theme, width, {}, {}, policy);
    fresh.buildAll(theme);
    require(qAbs(layout.totalHeight() - fresh.totalHeight()) < .01, "incremental/full/lazy total height agrees");
    for (const auto* block : layout.promotedBlocks()) {
      const auto* expected = fresh.block(block->nodeId());
      require(expected, "full layout contains reused block identity");
      if (expected)
        sameTree(*block, *expected);
    }
    if (pixels) {
      // Qt's FreeType outline rasterizer can produce different edge coverage on
      // its first draw of a large face (>64px), despite identical glyph IDs and
      // positions. Prime both font/render paths before comparing layout output;
      // geometry, caret/selection and the final pixels still compare exactly.
      paint(layout, theme, width);
      paint(fresh, theme, width);
      const auto actual = paint(layout, theme, width), expected = paint(fresh, theme, width);
      if (actual != expected) {
        QRect difference;
        if (actual.size() == expected.size())
          for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x)
              if (actual.pixel(x, y) != expected.pixel(x, y))
                difference = difference.united(QRect(x, y, 1, 1));
        qWarning().noquote() << "Pixel mismatch: zoom" << theme.zoomPercent() << "width" << width << "policy"
                             << int(policy) << "difference" << difference << "source"
                             << session.markdownText().toString().left(240);
        for (const auto* block : layout.promotedBlocks()) {
          const auto* other = fresh.block(block->nodeId());
          if (!other)
            continue;
          const auto a = block->inlineTextOrigin(), b = other->inlineTextOrigin();
          qWarning().noquote() << "Origins" << int(block->type()) << QString::number(a.x(), 'g', 17)
                               << QString::number(a.y(), 'g', 17) << QString::number(b.x(), 'g', 17)
                               << QString::number(b.y(), 'g', 17);
        }
        const auto stem = QString("resource-pixels-%1-%2").arg(failures).arg(int(policy));
        actual.save(stem + "-actual.png");
        expected.save(stem + "-expected.png");
      }
      require(actual == expected, "incremental/full/lazy pixels agree");
    }
  }
}
RenderTheme formattingTheme(const QString& display, bool fixedImage = false, bool subgrid = false) {
  return RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("#write{max-width:none;padding:0;margin:0;display:" + display +
                                  ";grid-template-columns:210px 190px;align-items:start}blockquote{display:grid;" +
                                  (subgrid ? QString("grid-column:span 2;grid-template-columns:subgrid;")
                                           : QString("grid-template-columns:100px 100px;")) +
                                  "align-items:start;padding:0;margin:0}"
                                  "p{font:16px Arial;line-height:20px;margin:0;min-width:0}img{max-width:100%" +
                                  (fixedImage ? QString(";width:80px;height:40px") : QString()) + "}",
                              "resource-reuse", {}));
}
void resourceVersions() {
  auto& resources = LayoutResources::instance();
  const QString key = "test:resource-version";
  resources.publish(key, "80x40");
  {
    LayoutResourceScope outer;
    LayoutResourceDependencies snapshot;
    {
      LayoutResourceScope inner;
      resources.read(key);
      snapshot = inner.dependencies;
    }
    resources.publish(key, "80x40");
    require(resources.matches(snapshot, false) && !resources.matches(snapshot),
            "paint and metric versions differ independently");
    require(outer.dependencies.contains(key), "nested resource reads propagate to enclosing dependency scope");
    resources.publish(key, "80x60");
    require(!resources.matches(snapshot, false), "changed natural size invalidates measurements");
  }

  const QString recycled = "test:recycled-resource";
  resources.publish(recycled, "same-size");
  auto old = resources.read(recycled);
  const auto retiredPaint = old.paint;
  LayoutResourceDependencies saved{{recycled, old}};
  resources.release(recycled, false);
  require(resources.matches(saved), "payload eviction preserves a live layout's version");
  resources.publish(recycled, "same-size");
  require(resources.matches(saved, false) && !resources.matches(saved),
          "reloading equal metrics refreshes paint without reflowing a live layout");
  resources.release(recycled);
  require(!resources.matches(saved, false), "explicit reset invalidates live dependencies");
  old = {};
  saved.clear();
  require(resources.read(recycled).paint == 0, "the final dependency releases the retired resource record");
  resources.publish(recycled, "same-size");
  require(resources.read(recycled).paint > retiredPaint,
          "recreated keys use monotonic versions rather than reviving old snapshots");
  resources.release(recycled);
}

void boundedImageLifetimes() {
  auto& loader = ImageLoader::instance();
  loader.setCacheLimits(0, 0);
  require(loader.cacheStats().entries == 0, "unused image cache can be drained");
  const auto baseline = loader.cacheStats();
  loader.setCacheLimits(512, 2);
  loader.store("test:cache-a", image({8, 8}, Qt::red));
  loader.store("test:cache-b", image({8, 8}, Qt::green));
  loader.cached("test:cache-a");
  loader.store("test:cache-c", image({8, 8}, Qt::blue));
  require(loader.cached("test:cache-b").isNull() && !loader.cached("test:cache-a").isNull(),
          "byte and entry budgets evict the least recently used unowned image");

  QTemporaryDir directory;
  const auto path = directory.filePath("leased.png");
  image({64, 64}, Qt::red).save(path);
  const auto url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
  auto first = loader.retain(url);
  auto second = loader.retain(url);
  require(loader.image(url).size() == QSize(64, 64), "a live image can exceed the warm-cache budget");
  require(loader.cacheStats().watchedFiles == baseline.watchedFiles + 1, "leased local images are watched");
  first.reset();
  require(!loader.cached(url).isNull(), "one reader closing does not release another reader's image");
  second.reset();
  QCoreApplication::processEvents();
  require(loader.cached(url).isNull() && loader.cacheStats().watchedFiles == baseline.watchedFiles &&
              loader.cacheStats().watchedDirectories == baseline.watchedDirectories,
          "the final reader releases pixels and file/directory watches");
  auto reopened = loader.retain(url);
  require(!loader.image(url).isNull(), "a released local image reloads correctly");
  reopened.reset();
  QCoreApplication::processEvents();

  QTcpServer slowServer;
  require(slowServer.listen(QHostAddress::LocalHost), "slow image test server listens");
  const auto remote = QString("http://127.0.0.1:%1/slow.png").arg(slowServer.serverPort());
  auto pending = loader.retain(remote);
  loader.image(remote);
  require(loader.isPending(remote), "leased download starts");
  pending.reset();
  QCoreApplication::processEvents();
  require(!loader.isPending(remote) && loader.cached(remote).isNull(),
          "closing the last reader cancels a slow download without repopulating the cache");
  loader.setCacheLimits(0, 0);
  require(loader.cacheStats().entries == 0 && loader.cacheStats().users == 0,
          "image payload and leases return to baseline");
  loader.setCacheLimits(32 * 1024 * 1024, 256);
}
void imageUpdates() {
  for (const auto& display : {QString("flex"), QString("grid")})
    for (bool fixed : {false, true})
      for (bool subgrid : {false, true}) {
        if (subgrid && display != "grid")
          continue;
        const auto url =
            "https://example.invalid/" + display + (fixed ? "fixed" : "natural") + (subgrid ? "subgrid" : "") + ".png";
        auto& loader = ImageLoader::instance();
        loader.store(url, image({60, 30}, Qt::red));
        DocumentSession session;
        session.setMarkdownText("> ![picture](" + url + ")\n>\n> neighbor\n\noutside", false);
        auto theme = formattingTheme(display, fixed, subgrid);
        DocumentLayout layout;
        layout.rebuild(session.document(), theme, 420);
        const auto quote = session.document().root().children()[0]->id();
        const auto* before = layout.block(quote);
        const auto* sibling = before->children()[1].get();
        const auto outsideId = session.document().root().children()[1]->id();
        const auto* outside = layout.block(outsideId);
        const auto red = paint(layout, theme, 420);
        loader.store(url, image({60, 30}, Qt::blue));
        auto update = layout.refreshResources(theme);
        require(update.updatedBlocks == 1 && !update.geometryChanged,
                "same-size image update only dirties its owning subtree");
        require(layout.block(quote) == before && layout.block(quote)->children()[1].get() == sibling,
                "paint-only update preserves layout and text shaping");
        require(layout.formattingReuseStats().solvedContexts == 0 && layout.formattingReuseStats().builtLayouts == 0,
                "paint-only image skips measurement and track solving");
        require(paint(layout, theme, 420) != red, "paint-only image update displays new pixels");
        sameFresh(layout, session, theme, 420);
        loader.store(url, image({120, 90}, Qt::green));
        update = layout.refreshResources(theme);
        require(update.updatedBlocks == 1, "image size update finds exact dependency owner");
        require(layout.block(quote)->children()[1].get() == sibling && layout.block(outsideId) == outside,
                "nested image reflow reuses unaffected native children");
        if (fixed)
          require(layout.formattingReuseStats().solvedContexts == 0 && !update.geometryChanged,
                  "fixed image contribution stops reflow at its container boundary");
        else
          require(update.geometryChanged, "natural image size change propagates layout geometry");
        sameFresh(layout, session, theme, 420);
        require(layout.refreshResources(theme).updatedBlocks == 0,
                "consumed resource versions do not trigger another refresh");
      }
}
void asyncAndLocalImages() {
  QTemporaryDir directory;
  require(directory.isValid(), "local image temp directory");
  const auto path = directory.filePath("watched.png");
  image({50, 30}, Qt::red).save(path);
  const auto url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
  DocumentSession session;
  session.setMarkdownText("![local](<" + path + ">)\n\nsuffix", false);
  auto theme = formattingTheme("grid");
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 420);
  ImageLoader::instance().image(path);
  image({90, 70}, Qt::blue).save(path);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000 && ImageLoader::instance().cached(url).size() != QSize(90, 70)) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
  }
  require(ImageLoader::instance().cached(url).size() == QSize(90, 70),
          "file watcher updates the shared image resource");
  require(ImageLoader::instance().cached(path).size() == QSize(90, 70),
          "file watcher updates every URL alias for the same file");
  require(layout.refreshResources(theme).updatedBlocks == 1, "local image watcher invalidates owning block");
  sameFresh(layout, session, theme, 420);
  QTcpServer server;
  require(server.listen(QHostAddress::LocalHost), "loopback image server");
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  image({75, 45}, Qt::red).save(&buffer, "PNG");
  QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
    auto* socket = server.nextPendingConnection();
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, png] {
      socket->readAll();
      socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " + QByteArray::number(png.size()) +
                    "\r\nConnection: close\r\n\r\n" + png);
      socket->disconnectFromHost();
    });
    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  });
  const auto remote = QString("http://127.0.0.1:%1/image.png").arg(server.serverPort());
  DocumentSession download;
  download.setMarkdownText("![download](" + remote + ")\n\nneighbor", false);
  DocumentLayout pending;
  pending.rebuild(download.document(), theme, 420);
  timer.restart();
  while (timer.elapsed() < 3000 && ImageLoader::instance().cached(remote).isNull()) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
  }
  require(!ImageLoader::instance().cached(remote).isNull(), "real asynchronous loopback download completes");
  require(pending.refreshResources(theme).updatedBlocks == 1, "download refreshes the image owner");
  sameFresh(pending, download, theme, 420);
}
void editorResources() {
  const QString url = "https://example.invalid/editor-resource.png";
  auto& loader = ImageLoader::instance();
  loader.store(url, image({60, 30}, Qt::red));
  DocumentSession session;
  QString source = "![image](" + url + ")\n\n";
  for (int i = 0; i < 100; ++i)
    source += QString("Paragraph %1 for viewport pinning.\n\n").arg(i);
  session.setMarkdownText(source, false);
  EditorView view;
  view.resize(700, 350);
  view.setTheme(formattingTheme("block"));
  view.setDocument(session.document());
  view.show();
  QCoreApplication::processEvents();
  const auto imageId = session.document().root().children()[0]->id();
  const auto* retainedImage = view.blockLayoutForNode(imageId);
  loader.store(url, image({60, 30}, Qt::blue));
  loader.store(url, image({60, 30}, Qt::green));
  require(view.blockLayoutForNode(imageId) == retainedImage, "resource signals defer editor work to the event loop");
  QCoreApplication::processEvents();
  require(view.blockLayoutForNode(imageId) == retainedImage,
          "coalesced paint-only notifications retain editor image layout");
  const auto textId = session.document().root().children()[8]->id();
  view.scrollToNode(textId);
  CursorPosition cursor;
  cursor.blockId = textId;
  cursor.text.nodeId = textId;
  cursor.text.textOffset = cursor.text.sourceOffset = 2;
  view.setCursorPosition(cursor);
  QCoreApplication::processEvents();
  const auto* retainedText = view.blockLayoutForNode(textId);
  const auto previousHeight = view.nodeRect(imageId).height();
  const auto viewportCaret = view.mapDocumentToViewport(view.effectiveCursorRect().topLeft());
  loader.store(url, image({60, 80}, Qt::blue));
  loader.store(url, image({60, 130}, Qt::red));
  QCoreApplication::processEvents();
  require(view.nodeRect(imageId).height() > previousHeight, "editor resource callback applies new image geometry");
  require(view.blockLayoutForNode(textId) == retainedText, "editor asynchronous reflow retains unchanged text layout");
  require(qAbs(viewportCaret.y() - view.mapDocumentToViewport(view.effectiveCursorRect().topLeft()).y()) <= 1,
          "resource reflow preserves viewport caret position below an updated image");
  require(view.cursorHit().blockId == textId && view.cursorHit().sourceOffset == 2,
          "resource reflow preserves logical caret and hit coordinates");
  loader.store(url, image({60, 180}, Qt::green));
  view.setLoading(true);
  QCoreApplication::processEvents();
  require(view.nodeRect(imageId).height() < 180, "resource callback skips stale document during asynchronous open");
  view.setLoading(false);
  QCoreApplication::processEvents();
  require(view.nodeRect(imageId).height() >= 180,
          "leaving loading consumes deferred resources even without installing a document");
  view.setLoading(true);
  loader.store(url, image({60, 240}, Qt::blue));
  QCoreApplication::processEvents();
  view.setDocument(session.document());
  view.setLoading(false);
  QCoreApplication::processEvents();
  require(view.nodeRect(imageId).height() >= 240, "installed document consumes current resource versions after open");
}
void hiddenEditorResources() {
  auto& loader = ImageLoader::instance();
  auto& resources = LayoutResources::instance();
  const QString url = "https://example.invalid/hidden-editor.png";
  loader.store(url, image({60, 30}, Qt::red));
  DocumentSession session;
  session.setMarkdownText(
      "![image](" + url + ")\n\n" +
          QString::fromUtf8("Font fallback 中英文 **bold** *italic* and wrapping words. ").repeated(12),
      false);
  const auto definition = CssThemeMapper::fromCss(
      "#write{max-width:none;padding:0;margin:0}p{font:16px MuffinDeferredFixture,monospace;margin:0}",
      "deferred-resource-font", {});
  const auto imageId = session.document().root().children()[0]->id();
  const auto textId = session.document().root().children()[1]->id();
  EditorView view;
  view.resize(520, 400);
  view.setTheme(RenderTheme::fromDefinition(definition));
  view.setDocument(session.document());
  view.show();
  QCoreApplication::processEvents();
  CursorPosition cursor;
  cursor.blockId = cursor.text.nodeId = textId;
  cursor.text.textOffset = 5;
  cursor.text.sourceOffset = session.document().root().children()[1]->sourceRange().byteStart + 5;
  view.setCursorPosition(cursor);
  const auto originalHeight = view.nodeRect(imageId).height();
  const auto originalFont = view.blockLayoutForNode(textId)->reuse.resources.value(LayoutResources::fontKey());

  // Queue while visible, then hide before dispatch: that callback must preserve
  // the invalidation without doing the hidden layout work.
  loader.store(url, image({60, 90}, Qt::blue));
  view.hide();
  QCoreApplication::processEvents();
  require(view.nodeRect(imageId).height() == originalHeight, "queued visible refresh defers if hidden before dispatch");
  loader.store(url, image({60, 120}, Qt::green));
  loader.store(url, image({60, 160}, Qt::blue));
  QFile font(QStringLiteral(MUFFIN_BINARY_DIR "/theme-fonts/github/open-sans-v17-latin-ext_latin-regular.ttf"));
  require(font.open(QIODevice::ReadOnly), "read fixed native font for deferred registration");
  const auto native = isolateThemeFont(font.readAll(), "MuffinDeferredFixture", 400, false);
  const int fontId = QFontDatabase::addApplicationFontFromData(native);
  require(fontId >= 0, "register new private family while editor is hidden");
  QCoreApplication::processEvents();
  require(resources.read(LayoutResources::fontKey()).geometry > originalFont.geometry,
          "actual font registration publishes a new metric generation");
  require(view.nodeRect(imageId).height() == originalHeight &&
              view.blockLayoutForNode(textId)->reuse.resources.value(LayoutResources::fontKey()) == originalFont,
          "hidden image and font notifications retain old geometry and dependency snapshots");

  view.show();
  view.refreshPendingResources(); // Same synchronous barrier used by a tab owner.
  require(view.nodeRect(imageId).height() >= 160 &&
              view.blockLayoutForNode(textId)->reuse.resources.value(LayoutResources::fontKey()) ==
                  resources.read(LayoutResources::fontKey()),
          "activation consumes the latest image and font versions before geometry queries");
  EditorView fresh;
  fresh.resize(520, 400);
  fresh.setTheme(RenderTheme::fromDefinition(definition));
  fresh.setDocument(session.document());
  fresh.show();
  fresh.setCursorPosition(cursor);
  QCoreApplication::processEvents();
  sameTree(*view.blockLayoutForNode(textId), *fresh.blockLayoutForNode(textId));
  require(view.effectiveCursorRect() == fresh.effectiveCursorRect(),
          "deferred font metrics agree with fresh caret geometry");
  const auto hit = view.hitTest(view.mapDocumentToViewport(view.effectiveCursorRect().center()));
  require(hit.blockId == textId && hit.sourceOffset == cursor.text.sourceOffset,
          "activated font fallback preserves logical source position and visible hit coordinates");
  const auto* consumed = view.blockLayoutForNode(textId);
  view.hide();
  view.show();
  QCoreApplication::processEvents();
  require(view.blockLayoutForNode(textId) == consumed, "show without another invalidation retains measured text");

  view.setLoading(true);
  view.hide();
  loader.store(url, image({60, 210}, Qt::red));
  view.setLoading(false); // Cancellation while hidden must retain the update.
  QCoreApplication::processEvents();
  require(view.nodeRect(imageId).height() < 210, "cancelled hidden load leaves resource work deferred");
  view.show();
  QCoreApplication::processEvents();
  require(view.nodeRect(imageId).height() >= 210, "show alone consumes a deferred update after load cancellation");

  loader.store(url, image({60, 250}, Qt::green));
  view.hide();
  view.show();
  view.setTheme(RenderTheme::fromDefinition(definition));
  const auto* rebuilt = view.blockLayoutForNode(textId);
  QCoreApplication::processEvents();
  require(view.nodeRect(imageId).height() >= 250 && view.blockLayoutForNode(textId) == rebuilt,
          "theme rebuild consumes pending resources and queued show does not replace that layout");

  auto* closing = new EditorView;
  closing->resize(520, 400);
  closing->setDocument(session.document());
  closing->show();
  loader.store(url, image({60, 280}, Qt::blue));
  closing->hide();
  delete closing;
  QCoreApplication::processEvents(); // The queued callback must not outlive its receiver.
  require(QFontDatabase::removeApplicationFont(fontId), "remove deferred registration fixture");
}
void nestedEdits() {
  for (const auto& display : {QString("flex"), QString("grid")}) {
    DocumentSession session;
    session.setMarkdownText("> first\n>\n> unchanged\n\noutside", false);
    const auto quote = session.document().root().children()[0]->id();
    const auto sibling = session.document().root().children()[0]->children()[1]->id();
    auto theme = formattingTheme(display);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 420);
    const auto* retained = layout.block(sibling);
    for (int i = 0; i < 3; ++i) {
      require(session.applyTextDelta(2, 0, "a long prefix ", true, {{quote, 0, BlockType::BlockQuote}}),
              "nested real edit with editor identity hint");
      const auto range = session.lastLocalTopLevelRangeChange();
      if (range.isValid())
        require(layout.rebuildTopLevelRange(range, session.document(), theme, {}).rebuilt, "nested edit range rebuild");
      else
        require(layout.rebuildBlock(quote, session.document(), theme, {}).rebuilt, "nested edit block rebuild");
      require(layout.block(sibling) == retained, "nested unchanged child preserves layout identity across edits");
      const auto* current = layout.block(sibling);
      const auto caret = current->inlineLayout()->cursorRectForSourceOffset(2).translated(current->inlineTextOrigin());
      require(layout.hitTest(caret.center(), theme).sourceOffset == current->contentSourceStart() + 2,
              "reused nested child hit and absolute source offset agree after movement");
      sameFresh(layout, session, theme, 420);
    }
  }
}
void htmlResources() {
  for (bool block : {false, true}) {
    const auto url = QString("https://example.invalid/html-%1.png").arg(block);
    ImageLoader::instance().store(url, image({50, 30}, Qt::red));
    DocumentSession session;
    const auto markup = QString("<img src=\"%1\" style=\"width:50px;height:30px\">").arg(url);
    session.setMarkdownText(block
                                ? "<div>" + markup + "</div>\n\nneighbor"
                                : "before <span style=\"display:inline-grid\">" + markup + "</span> after\n\nneighbor",
                            false);
    auto theme = formattingTheme("grid");
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 420);
    const auto before = paint(layout, theme, 420);
    ImageLoader::instance().store(url, image({50, 30}, Qt::blue));
    auto update = layout.refreshResources(theme);
    require(update.updatedBlocks == 1 && !update.geometryChanged,
            "HTML resource snapshot identifies paint-only update");
    require(paint(layout, theme, 420) != before, "retained HTML image does not keep private stale pixels");
    sameFresh(layout, session, theme, 420);
    ImageLoader::instance().store(url, image({120, 80}, Qt::green));
    require(layout.refreshResources(theme).updatedBlocks == 1, "HTML image metrics invalidate owning block");
    sameFresh(layout, session, theme, 420);
  }
}
void mixedMarkdownResources() {
  const QString url = QStringLiteral("https://example.invalid/mixed-markdown-resources.png");
  auto& loader = ImageLoader::instance();
  for (const auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
    loader.store(url, image({60, 30}, Qt::red));
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("![before](%1)\n\n<div align=\"center\">\n\n![inside](%1)\n\n"
                                           "</div>\n\n![after](%1)\n")
                                .arg(url),
                            false);
    auto theme = RenderTheme::github();
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 520, {}, {}, policy);
    layout.buildAll(theme);
    const auto before = layout.totalHeight();
    loader.store(url, image({60, 130}, Qt::blue));
    const auto deferred = layout.refreshResources(theme, {}, true);
    require(deferred.needsRebuild && deferred.updatedBlocks == 3 && layout.totalHeight() == before,
            "preview detects stale HTML resource geometry without synchronously rebuilding it");
    const auto update = layout.refreshResources(theme);
    require(update.updatedBlocks == 3 && update.geometryChanged && layout.totalHeight() > before,
            "one resource update refreshes every mixed Markdown/HTML owner, including lazy layouts");
    sameFresh(layout, session, theme, 520);
    const auto settled = layout.refreshResources(theme, {}, true);
    require(!settled.needsRebuild && settled.updatedBlocks == 0, "mixed flow consumes the resource update once");
  }
}
void listMarkerEdits() {
  for (const auto& display : {QString("block"), QString("grid")}) {
    DocumentSession session;
    session.setMarkdownText("1. first\n2. second\n\nneighbor", false);
    auto theme = formattingTheme(display);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 420);
    const auto list = session.document().root().children()[0]->id();
    const auto second = session.document().root().children()[0]->children()[1]->id();
    const auto oldIndent = layout.block(second)->listContentIndent();
    require(session.applyTextDelta(0, 1, "123456", true, {{list, 0, BlockType::List}}),
            "edit ordered marker while preserving list identity");
    auto range = session.lastLocalTopLevelRangeChange();
    if (range.isValid())
      layout.rebuildTopLevelRange(range, session.document(), theme, {});
    else
      layout.rebuildBlock(list, session.document(), theme, {});
    require(layout.block(second) && layout.block(second)->listContentIndent() > oldIndent,
            "wider authored marker invalidates the shared gutter of unchanged list items");
    sameFresh(layout, session, theme, 420);
    const auto* retained = layout.block(second);
    require(session.applyTextDelta(8, 0, "new ", true, {{list, 0, BlockType::List}}),
            "edit list body without changing marker geometry");
    range = session.lastLocalTopLevelRangeChange();
    if (range.isValid())
      layout.rebuildTopLevelRange(range, session.document(), theme, {});
    else
      layout.rebuildBlock(list, session.document(), theme, {});
    require(layout.block(second) == retained,
            "list body edits retain siblings with unchanged marker and gutter dependencies");
    sameFresh(layout, session, theme, 420);
  }
}
void htmlStructuralEdits() {
  for (const auto& display : {QString("block"), QString("flex"), QString("grid")}) {
    DocumentSession session;
    session.setMarkdownText("# Title\n\nbefore <span><kbd>key</kbd></span> after\n\nneighbor", false);
    auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
        "#write{max-width:none;padding:0;margin:0;display:" + display +
            ";grid-template-columns:200px 200px;align-items:start}p{font:16px Arial;margin:0;min-width:0}"
            "kbd{padding:2px;border:1px solid black}h1 + p span kbd{padding:14px;color:red}",
        "html-structural-reuse", {}));
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 600);
    const auto paragraph = session.document().root().children()[1]->id();
    const auto before = paint(layout, theme, 600);
    require(session.applyTextDelta(0, 2, {}, true), "edit HTML ancestor selector through real Markdown parse");
    const auto range = session.lastLocalTopLevelRangeChange();
    require(range.isValid() && layout.rebuildTopLevelRange(range, session.document(), theme, {}).rebuilt,
            "structural edit refreshes formatting context containing inline HTML");
    require(layout.block(paragraph), "structural edit preserves inline HTML owner identity");
    require(paint(layout, theme, 600) != before, "structural edit updates inline HTML presentation");
    sameFresh(layout, session, theme, 600);
  }
}
void fontsAndMermaid() {
  auto& resources = LayoutResources::instance();
  DocumentSession session;
  session.setMarkdownText("first\n\nsecond", false);
  auto theme = formattingTheme("grid");
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 420);
  const auto before = resources.read(LayoutResources::fontKey());
  // A real application-font database notification exercises versioning even if
  // the test's Arial text happens not to use the newly registered face.
  QDirIterator fonts(QStringLiteral(MUFFIN_BINARY_DIR "/theme-fonts"), {"*.ttf"}, QDir::Files,
                     QDirIterator::Subdirectories);
  if (fonts.hasNext()) {
    const int id = QFontDatabase::addApplicationFont(fonts.next());
    require(id >= 0, "register generated native font");
    require(resources.read(LayoutResources::fontKey()).geometry > before.geometry,
            "font database change advances metric generation");
    require(layout.refreshResources(theme).updatedBlocks == 2, "font generation invalidates text-dependent blocks");
    sameFresh(layout, session, theme, 420);
    QFontDatabase::removeApplicationFont(id);
  } else
    require(false, "generated native fonts available for integration test");
  using mermaid::editor::MermaidRenderCache;
  MermaidRenderCache cache;
  // Warm the renderer's one-time font registration before asserting that an
  // ordinary diagram completion does not invalidate unrelated text metrics.
  const QString mermaidSource = "flowchart LR\nA-->B";
  cache.getSync(MermaidRenderCache::makeKey(mermaidSource), mermaidSource);
  cache.clear();
  DocumentSession diagram;
  diagram.setMarkdownText("```mermaid\nflowchart LR\nA-->B\n```\n\nneighbor", false);
  DocumentLayout diagrams;
  diagrams.setMermaidRenderCache(&cache);
  diagrams.rebuild(diagram.document(), theme, 420);
  const auto fence = diagram.document().root().children()[0]->id();
  const auto neighbor = diagram.document().root().children()[1]->id();
  const auto* retained = diagrams.block(neighbor);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 10000 && diagrams.block(fence)->mermaidState() != BlockLayout::MermaidState::Ready) {
    QCoreApplication::processEvents();
    diagrams.refreshResources(theme);
    QThread::msleep(1);
  }
  require(diagrams.block(fence)->mermaidState() == BlockLayout::MermaidState::Ready,
          "asynchronous Mermaid replaces loading geometry");
  require(diagrams.block(neighbor) == retained, "Mermaid completion retains unrelated block");
  for (auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
    DocumentLayout fresh;
    fresh.setMermaidRenderCache(&cache);
    fresh.rebuild(diagram.document(), theme, 420, {}, {}, policy);
    fresh.buildAll(theme);
    require(paint(diagrams, theme, 420) == paint(fresh, theme, 420),
            "Mermaid resource refresh agrees with full/lazy painting");
  }
  const auto key = MermaidRenderCache::makeKey(mermaidSource);
  const auto version = resources.read(cache.resourceKey(key));
  cache.clear();
  require(resources.read(cache.resourceKey(key)).geometry > version.geometry,
          "clearing Mermaid cache invalidates retained entries");
}
void realThemes() {
  for (const auto& name : {QString("newsprint"), QString("github"), QString("night")})
    for (int zoom : {100, 200})
      for (int width : {900, 1500}) {
        const auto definition = ThemeDefinition::builtIn(name);
        require(definition.has_value(), "real bundled theme exists");
        if (!definition)
          continue;
        auto theme = RenderTheme::fromDefinition(*definition, zoom);
        theme.updateForViewport(width, 900);
        DocumentSession session;
        session.setMarkdownText(
            "# A realistic heading with several words to exercise wrapping\n\nText with **strong** and `code`.\n\n> A quote\n>\n> Another "
            "paragraph\n\n| H | V |\n|---|---|\n| one | two |",
            false);
        DocumentLayout layout;
        layout.rebuild(session.document(), theme, width);
        const auto id = session.document().root().children()[1]->id();
        const auto start = session.document().root().children()[1]->sourceRange().byteStart;
        require(session.applyTextDelta(start, 0, "Inserted text ", true, {{id, start, BlockType::Paragraph}}),
                "real theme text edit");
        const auto range = session.lastLocalTopLevelRangeChange();
        if (range.isValid())
          layout.rebuildTopLevelRange(range, session.document(), theme, {});
        else
          layout.rebuildBlock(id, session.document(), theme, {});
        sameFresh(layout, session, theme, width);
      }
}
void responsiveResize() {
  for (const auto& source :
       {QString("# Heading\n\nParagraph with enough words to wrap around the fixed page column.\n\nTail"),
        QString("<div style='font-size:3vw'>HTML with viewport font</div>\n\nTail"),
        QString("Text <span style='font-size:3vw'>inline viewport font</span> tail\n\nTail"),
        QString("Text <b style='font-size:3vw'>simple HTML viewport font</b> tail\n\nTail")}) {
    DocumentSession session;
    session.setMarkdownText(source, false);
    auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
        "#write{max-width:500px;padding:20px}p{margin:12px 0}@media(min-width:900px){p{font-size:24px}}", "resize",
        {}));
    theme.updateForViewport(700, 600);
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 700, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    layout.buildAll(theme);
    for (int width : {850, 950, 1050, 800, 420, 1050}) {
      const auto id = session.document().root().children().front()->id();
      const auto* previous = layout.block(id);
      const bool changed = theme.updateForViewport(width, 600);
      const bool translated = !changed && layout.relayoutForViewportWidth(theme, width);
      if (!translated) {
        layout.rebuild(session.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
        layout.buildAll(theme);
      }
      if (width == 850) {
        require(!changed, "ordinary resize reuses theme projections");
        require(translated, "lazy resize retains the document index, including HTML viewport styles");
        if (source.startsWith('#'))
          require(layout.block(id) == previous, "unchanged column reuses native layout objects");
      }
      layout.buildAll(theme); // lazy viewport invalidation measures detail on demand
      sameFresh(layout, session, theme, width);
      const auto* paragraph = layout.block(session.document().root().children().back()->id());
      const auto caret = paragraph->inlineLayout()->cursorRect(1).translated(paragraph->inlineTextOrigin());
      const auto hit = layout.hitTest(caret.center(), theme);
      require(hit.isValid() && hit.textOffset == 1, "resize caret/hit-test round trip");
    }
  }
  DocumentSession session;
  session.setMarkdownText("first\n\nsecond", false);
  BlockLayoutBuilder builder;
  auto fixed = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("p{height:100px;box-sizing:border-box;padding:10px}", "fixed", {}));
  builder.refreshRenderSettings();
  require(builder.estimateHeight(*session.document().root().children()[0], fixed, 500).height == 100,
          "lazy estimates honor explicit box sizing/height");
  auto constrained = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss("p{min-height:130px;box-sizing:border-box}", "constrained", {}));
  builder.refreshRenderSettings();
  require(builder.estimateHeight(*session.document().root().children()[1], constrained, 500).height == 130,
          "new estimate pass discards the previous theme's cached box constraints");
}

QJsonObject timing(const std::vector<double>& values) {
  auto sorted = values;
  std::sort(sorted.begin(), sorted.end());
  return {{"median_ms", sorted[sorted.size() / 2]},
          {"p95_ms", sorted[qMin(sorted.size() - 1, size_t(sorted.size() * .95))]},
          {"max_ms", sorted.back()}};
}
void performance() {
  const int count = qBound(20,
                           qEnvironmentVariableIntValue("MUFFIN_LAYOUT_BENCH_BLOCKS") > 0
                               ? qEnvironmentVariableIntValue("MUFFIN_LAYOUT_BENCH_BLOCKS")
                               : 120,
                           10000);
  const int iterations = qBound(2,
                                qEnvironmentVariableIntValue("MUFFIN_LAYOUT_BENCH_ITERS") > 0
                                    ? qEnvironmentVariableIntValue("MUFFIN_LAYOUT_BENCH_ITERS")
                                    : 4,
                                100);
  QString source;
  for (int i = 0; i < count; ++i)
    source +=
        QString("Paragraph %1 with **bold**, `code`, and enough text to wrap under responsive themes.\n\n").arg(i);
  DocumentSession session;
  session.setMarkdownText(source, false);
  auto theme = RenderTheme::newsprint();
  theme.updateForViewport(1100, 900);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 1100, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  layout.ensureBuilt(0, qMin<qsizetype>(10, layout.slotCount() - 1), theme);
  std::vector<double> edits, resize, loads, fullEdits, fullLoads, resizeStyles, resizeEstimates;
  int currentWidth = 1100;
  for (int i = 0; i < iterations; ++i) {
    const auto id = session.document().root().children()[0]->id();
    QElapsedTimer timer;
    timer.start();
    require(session.applyTextDelta(0, 0, "x", true, {{id, 0, BlockType::Paragraph}}), "benchmark actual typing");
    const auto range = session.lastLocalTopLevelRangeChange();
    if (range.isValid())
      layout.rebuildTopLevelRange(range, session.document(), theme, {});
    else
      layout.rebuildBlock(id, session.document(), theme, {});
    edits.push_back(timer.nsecsElapsed() / 1e6);
    timer.restart();
    DocumentLayout fullEdit;
    fullEdit.rebuild(session.document(), theme, currentWidth, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    fullEdit.ensureBuilt(0, qMin<qsizetype>(10, fullEdit.slotCount() - 1), theme);
    fullEdits.push_back(timer.nsecsElapsed() / 1e6);
    const int width = i % 2 ? 1100 : 780;
    timer.restart();
    theme.updateForViewport(width, 900);
    const double stylesMs = timer.nsecsElapsed() / 1e6;
    resizeStyles.push_back(stylesMs);
    layout.rebuild(session.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    layout.ensureBuilt(0, qMin<qsizetype>(10, layout.slotCount() - 1), theme);
    resize.push_back(timer.nsecsElapsed() / 1e6);
    resizeEstimates.push_back(resize.back() - stylesMs);
    currentWidth = width;
    const auto url = QString("https://example.invalid/bench-%1.png").arg(i);
    ImageLoader::instance().store(url, {});
    DocumentSession images;
    images.setMarkdownText("![image](" + url + ")\n\n" + source, false);
    DocumentLayout loaded;
    loaded.rebuild(images.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    loaded.ensureBuilt(0, qMin<qsizetype>(10, loaded.slotCount() - 1), theme);
    timer.restart();
    ImageLoader::instance().store(url, image({640, 480}, Qt::blue));
    loaded.refreshResources(theme);
    loads.push_back(timer.nsecsElapsed() / 1e6);
    require(loaded.formattingReuseStats().builtLayouts == 1,
            "long document image completion builds only the image block");
    timer.restart();
    DocumentLayout fullLoad;
    fullLoad.rebuild(images.document(), theme, width, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    fullLoad.ensureBuilt(0, qMin<qsizetype>(10, fullLoad.slotCount() - 1), theme);
    fullLoads.push_back(timer.nsecsElapsed() / 1e6);
  }
  QJsonObject result{{"blocks", count},
                     {"iterations", iterations},
                     {"theme", "newsprint"},
                     {"typing", timing(edits)},
                     {"typing_full_reference", timing(fullEdits)},
                     {"resize", timing(resize)},
                     {"resize_styles", timing(resizeStyles)},
                     {"resize_layout", timing(resizeEstimates)},
                     {"image_update", timing(loads)},
                     {"image_full_reference", timing(fullLoads)}};
  const auto bytes = QJsonDocument(result).toJson();
  std::cout << bytes.toStdString();
  const auto output = qEnvironmentVariable("MUFFIN_LAYOUT_BENCH_OUTPUT");
  if (!output.isEmpty()) {
    QFile file(output);
    require(file.open(QIODevice::WriteOnly), "open benchmark report");
    file.write(bytes);
  }
}
} // namespace
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("MuffinResourceTests");
  QCoreApplication::setApplicationName("ResourceIncrementalLayout");
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  resourceVersions();
  boundedImageLifetimes();
  imageUpdates();
  asyncAndLocalImages();
  editorResources();
  hiddenEditorResources();
  nestedEdits();
  listMarkerEdits();
  htmlResources();
  mixedMarkdownResources();
  htmlStructuralEdits();
  fontsAndMermaid();
  realThemes();
  responsiveResize();
  performance();
  return failures ? 1 : 0;
}
