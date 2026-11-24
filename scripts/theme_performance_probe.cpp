#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>

#include "diagnostics/ProcessMemory.h"
#include "diagnostics/ThemePerformance.h"
#include "document/DocumentSession.h"
#include "render/DecorationPainter.h"
#include "render/DocumentLayout.h"
#include "render/InlineLayout.h"
#include "render/LayoutBox.h"
#include "theme/DocumentCounterCache.h"
#include "theme/ThemeManager.h"

using namespace muffin;
namespace {
struct ProbeTotals {
  qsizetype calls = 0;
  double total = 0, maximum = 0;
};
QHash<QString, ProbeTotals> detailTotals;
QJsonArray shadowRasters;
QJsonArray boxRasters;
QJsonArray compactBoxRasters;
void messages(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  if (qstrcmp(context.category, "muffin.perf.detail") == 0 || qstrcmp(context.category, "muffin.perf.css") == 0 ||
      qstrcmp(context.category, "muffin.perf.paint") == 0) {
    const auto parts = message.split(' ', Qt::SkipEmptyParts);
    if (parts.size() == 3 && parts[0] == "paint.boxCompact") {
      compactBoxRasters.append(QJsonArray{parts[1].toInt(), parts[2].toInt()});
      return;
    }
    if (parts.size() == 10 && parts[0] == "paint.boxRaster") {
      boxRasters.append(QJsonObject{
          {"width", parts[1].toInt()},
          {"height", parts[2].toInt()},
          {"blur_radius", parts[3].toInt()},
          {"core", QJsonArray{parts[4].toDouble(), parts[5].toDouble(), parts[6].toDouble(), parts[7].toDouble()}},
          {"corner_radius", parts[8].toDouble()},
          {"hit", parts[9].toInt() != 0}});
      return;
    }
    if (parts.size() == 8 && parts[0] == "paint.shadowRaster") {
      shadowRasters.append(QJsonObject{{"width", parts[1].toInt()},
                                       {"height", parts[2].toInt()},
                                       {"clip_rects", parts[3].toInt()},
                                       {"blur", parts[4].toDouble()},
                                       {"padding", parts[5].toInt()},
                                       {"clip_width", parts[6].toDouble()},
                                       {"clip_height", parts[7].toDouble()}});
      return;
    }
    bool ok = false;
    const double elapsed = parts.size() > 1 ? parts[1].toDouble(&ok) : 0;
    if (ok) {
      auto& total = detailTotals[parts[0]];
      ++total.calls;
      total.total += elapsed;
      total.maximum = qMax(total.maximum, elapsed);
    }
    return;
  }
  if (type >= QtWarningMsg)
    std::fprintf(stderr, "%s\n", qPrintable(message.left(600)));
  if (type == QtFatalMsg)
    std::abort();
}
QJsonObject measure(const QString& label, const std::function<void()>& operation) {
  std::fprintf(stderr, "starting %s\n", qPrintable(label));
  std::fflush(stderr);
  resetThemePerformance();
  resetCssFilterRasterStats();
  resetTextShadowRasterStats();
  DecorationPainter::resetBoxShadowRasterStats();
  detailTotals.clear();
  shadowRasters = {};
  boxRasters = {};
  compactBoxRasters = {};
  QElapsedTimer timer;
  timer.start();
  operation();
  QJsonObject row{{"stage", label}, {"wall_ms", timer.nsecsElapsed() / 1e6}};
  const auto samples = themePerformanceSamples();
  const char* names[]{"css", "counters", "layout", "paint", "background", "shadow", "text"};
  for (size_t i = 0; i < samples.size(); ++i) {
    row[names[i]] = QJsonObject{{"exclusive_ms", samples[i].exclusiveNs / 1e6},
                                {"calls", qint64(samples[i].calls)},
                                {"work", qint64(samples[i].work)}};
  }
  const auto raster = cssFilterRasterStats();
  const auto text = themeTextPaintStats();
  const auto shadows = textShadowRasterStats();
  const auto boxes = DecorationPainter::boxShadowRasterStats();
  QJsonObject detail;
  for (auto it = detailTotals.cbegin(); it != detailTotals.cend(); ++it)
    detail[it.key()] = QJsonObject{{"calls", it->calls}, {"inclusive_ms", it->total}, {"max_ms", it->maximum}};
  row["detail_scopes"] = detail;
  if (!boxRasters.isEmpty())
    row["box_shadow_rasters"] = boxRasters;
  if (!compactBoxRasters.isEmpty())
    row["box_shadow_compact_masks"] = compactBoxRasters;
  if (!shadowRasters.isEmpty())
    row["text_shadow_rasters"] = shadowRasters;
  row["text_paint_work"] = QJsonObject{{"calls", qint64(text.calls)},
                                       {"lines", qint64(text.lines)},
                                       {"format_ranges", qint64(text.formatRanges)},
                                       {"selection_ranges", qint64(text.selectionRanges)}};
  row["text_shadow_work"] = QJsonObject{{"requests", qint64(shadows.requests)},
                                        {"hits", qint64(shadows.hits)},
                                        {"misses", qint64(shadows.misses)},
                                        {"culled", qint64(shadows.culled)},
                                        {"rasterized_pixels", qint64(shadows.rasterizedPixels)},
                                        {"largest_surface_pixels", qint64(shadows.largestSurfacePixels)},
                                        {"cache_bytes", shadows.cacheBytes},
                                        {"cache_entries", shadows.cacheEntries}};
  row["box_shadow_work"] = QJsonObject{{"requests", qint64(boxes.requests)},
                                       {"hits", qint64(boxes.hits)},
                                       {"misses", qint64(boxes.misses)},
                                       {"mask_pixels", qint64(boxes.maskPixels)},
                                       {"largest_mask_pixels", qint64(boxes.largestMaskPixels)},
                                       {"output_pixels", qint64(boxes.outputPixels)},
                                       {"cache_bytes", boxes.cacheBytes},
                                       {"cache_entries", boxes.cacheEntries}};
  row["working_set_bytes"] = diag::workingSetBytes();
  row["raster_allocated_pixels"] = qint64(raster.allocatedPixels);
  row["raster_largest_surface_pixels"] = qint64(raster.largestSurfacePixels);
  std::fprintf(stderr, "%s\n", QJsonDocument(row).toJson(QJsonDocument::Compact).constData());
  std::fflush(stderr);
  return row;
}
QString workload(int bytes) {
  QString text;
  const auto paragraph = QString("Plain text with **bold**, *italic*, ~~strike~~, `code`, and "
                                 "[Muffin](https://example.com). ")
                             .repeated(12) +
                         "\n\n";
  while (text.size() < bytes) {
    if (text.size() % 65536 < 1200)
      text += "# Heading\n\n";
    text += paragraph;
  }
  return text;
}
} // namespace
int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QLoggingCategory::setFilterRules(
      QString(qEnvironmentVariableIsSet("MUFFIN_THEME_LAYOUT_DETAIL") ? "muffin.perf.detail.debug=true"
                                                                      : "muffin.perf.detail.debug=false") +
      (qEnvironmentVariableIsSet("MUFFIN_THEME_CSS_DETAIL") ? "\nmuffin.perf.css.debug=true"
                                                            : "\nmuffin.perf.css.debug=false") +
      (qEnvironmentVariableIsSet("MUFFIN_THEME_PAINT_DETAIL") ? "\nmuffin.perf.paint.debug=true"
                                                              : "\nmuffin.perf.paint.debug=false"));
  qInstallMessageHandler(messages);
  if (argc < 3) {
    std::fprintf(stderr, "Usage: MuffinThemePerformanceProbe output.json MiB [theme.css]\n");
    return 2;
  }
  const double mib = QString::fromLocal8Bit(argv[2]).toDouble();
  if (mib < .01 || mib > 100)
    return 2;
  const auto path =
      argc > 3 ? QString::fromLocal8Bit(argv[3])
               : QStringLiteral(MUFFIN_SOURCE_DIR "/tests/fixtures/theme/vlook-fancy-dark/vlook-fancy-dark.css");
  QJsonArray rows;
  if (qEnvironmentVariableIsSet("MUFFIN_THEME_LIFECYCLE_ONLY")) {
    QTemporaryDir settings;
    if (!settings.isValid())
      return 2;
    QCoreApplication::setOrganizationName("MuffinBenchmarks");
    QCoreApplication::setApplicationName("ThemeLoadProbe");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    enableThemePerformance(true);
    std::unique_ptr<ThemeManager> manager;
    rows.append(
        measure("manager_scan", [&] { manager = std::make_unique<ThemeManager>(QFileInfo(path).absolutePath()); }));
    const auto id = QFileInfo(path).completeBaseName().toLower();
    rows.append(measure("manager_select_cold", [&] {
      if (!manager->setTheme(id))
        qFatal("Theme manager selection failed");
    }));
    auto theme = RenderTheme::github();
    rows.append(measure("manager_snapshot_cold", [&] { theme = manager->currentTheme(); }));
    rows.append(measure("manager_project_cold", [&] { theme.updateForViewport(960, 900); }));
    const auto definition = manager->currentDefinition();
    for (int i = 0; i < 2; ++i) {
      rows.append(measure(QString("manager_select_repeat_%1").arg(i), [&] {
        if (!manager->setTheme(id))
          qFatal("Repeated manager selection failed");
      }));
      rows.append(measure(QString("manager_snapshot_repeat_%1").arg(i), [&] {
        theme = manager->currentTheme();
        theme.updateForViewport(960, 900);
      }));
      if (manager->currentDefinition().sourceSheet != definition.sourceSheet)
        qFatal("Unchanged definition unexpectedly reloaded");
    }
    int callbacks = 0;
    const auto connection = QObject::connect(
        manager.get(), &ThemeManager::themeChanged, manager.get(),
        [&](const QString& selected) {
          if (selected != id)
            return;
          // Match MainWindow::applyTheme's editor snapshot and chrome definition
          // reads inside the synchronous selection notification.
          theme = manager->currentTheme();
          const auto chrome = manager->definition(selected);
          if (chrome.sourceSheet != definition.sourceSheet)
            qFatal("Selection callback reloaded unchanged CSS");
          theme.updateForViewport(960, 900);
          ++callbacks;
        },
        Qt::DirectConnection);
    const auto away = id == "github" ? QStringLiteral("night") : QStringLiteral("github");
    for (int i = 0; i < 2; ++i) {
      if (!manager->setTheme(away))
        qFatal("Switch-away selection failed");
      rows.append(measure(QString("manager_apply_cycle_%1").arg(i), [&] {
        if (!manager->setTheme(id))
          qFatal("Selection with callbacks failed");
      }));
    }
    QObject::disconnect(connection);
    if (callbacks != 2)
      qFatal("Selection callback count differs");
    QFile output(QString::fromLocal8Bit(argv[1]));
    if (!output.open(QIODevice::WriteOnly))
      return 2;
    output.write(QJsonDocument(QJsonObject{{"theme", path},
                                           {"qpa", QGuiApplication::platformName()},
                                           {"qt_version", qVersion()},
                                           {"MiB", mib},
                                           {"lifecycle", true},
                                           {"selection_callbacks", callbacks},
                                           {"unchanged_definition_reused", true},
                                           {"measurements", rows}})
                     .toJson(QJsonDocument::Indented));
    return 0;
  }
  DocumentSession session;
  const QString inputPath = qEnvironmentVariable("MUFFIN_THEME_PROBE_DOCUMENT");
  QByteArray inputDigest;
  rows.append(measure("parse", [&] {
    if (inputPath.isEmpty())
      session.setMarkdownText(workload(mib * 1024 * 1024), false);
    else {
      QFile file(inputPath);
      if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open benchmark document");
      const auto bytes = file.readAll();
      inputDigest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
      session.setMarkdownText(QString::fromUtf8(bytes), false);
    }
  }));
  enableThemePerformance(true);
  const bool managerDocument = qEnvironmentVariableIsSet("MUFFIN_THEME_MANAGER_DOCUMENT");
  const bool realSwitch = managerDocument || qEnvironmentVariableIsSet("MUFFIN_THEME_REAL_SWITCH");
  std::unique_ptr<ThemeManager> documentManager;
  const auto selectedId = QFileInfo(path).completeBaseName().toLower();
  ThemeDefinition definition;
  RenderTheme theme;
  int selectionCallbacks = 0;
  if (managerDocument) {
    documentManager = std::make_unique<ThemeManager>(QFileInfo(path).absolutePath());
    // Ensure selecting the built-in default emits a notification too. This
    // warms only the away theme; the requested theme still loads cold.
    if (selectedId == "github" && !documentManager->setTheme("night"))
      qFatal("Cannot prime away theme");
    QObject::connect(
        documentManager.get(), &ThemeManager::themeChanged, documentManager.get(),
        [&](const QString& id) {
          if (id != selectedId)
            return;
          auto next = documentManager->currentTheme();
          definition = documentManager->definition(id);
          next.updateForViewport(960, 900);
          if (selectionCallbacks)
            next.reuseDocumentSemantics(theme);
          theme = std::move(next);
          ++selectionCallbacks;
        },
        Qt::DirectConnection);
  }
  QElapsedTimer selectionClock;
  QJsonArray selectionToPaint;
  selectionClock.start();
  if (managerDocument) {
    rows.append(measure("manager_cold_select_snapshot", [&] {
      if (!documentManager->setTheme(selectedId))
        qFatal("Document theme selection failed");
    }));
  } else {
    rows.append(measure("theme_load", [&] { definition = ThemeDefinition::fromCss(path, "benchmark"); }));
    rows.append(measure("theme_snapshot", [&] {
      theme = RenderTheme::fromDefinition(definition);
      theme.updateForViewport(960, 900);
    }));
  }
  DocumentLayout layout;
  const auto recordCounterStorage = [&] {
    const auto stats = theme.documentCounterStats();
    auto row = rows.last().toObject();
    row["counter_snapshot"] = QJsonObject{{"blocks", stats.blocks},
                                          {"semantic_shapes", stats.semanticShapes},
                                          {"semantic_bytes", stats.semanticBytes},
                                          {"unshared_semantic_bytes", stats.unsharedSemanticBytes},
                                          {"memoized_blocks", stats.memoizedBlocks},
                                          {"shared_states", stats.sharedStates},
                                          {"semantic_builds", stats.semanticBuilds},
                                          {"candidate_scans", stats.candidateScans}};
    rows.replace(rows.size() - 1, row);
  };
  rows.append(measure("theme_switch_lazy", [&] {
    layout.rebuild(session.document(), theme, 960, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  }));
  recordCounterStorage();
  rows.append(measure("first_viewport_layout", [&] { layout.visibleBlocks({0, 0, 960, 900}, theme); }));
  QImage lastImage;
  QJsonArray visuals;
  const auto captureVisual = [&](const QString& stage) {
    if (!qEnvironmentVariableIsSet("MUFFIN_FIRST_LAYOUT_ONLY"))
      return;
    QJsonArray geometry;
    const auto rectJson = [](QRectF r) { return QJsonArray{r.x(), r.y(), r.width(), r.height()}; };
    for (const auto* block : layout.visibleBlocks({0, 0, 960, 900}, theme)) {
      QJsonArray cursors;
      if (const auto* text = block->inlineLayout()) {
        for (int offset : {0, 20, 40, 80}) {
          const auto rect = text->cursorRectForSourceOffset(offset);
          cursors.append(QJsonObject{
              {"source", offset}, {"rect", rectJson(rect)}, {"hit", text->hitTestSourceOffset(rect.center())}});
        }
      }
      geometry.append(QJsonObject{{"rect", rectJson(block->rect())}, {"cursors", cursors}});
    }
    const QByteArray bytes(reinterpret_cast<const char*>(lastImage.constBits()), lastImage.sizeInBytes());
    visuals.append(QJsonObject{
        {"stage", stage},
        {"geometry", geometry},
        {"pixels_sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
  };
  auto paint = [&] {
    QImage image(960, 900, QImage::Format_ARGB32_Premultiplied);
    image.fill(theme.viewportBackgroundColor());
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto rect = layout.pageRect(theme, 900);
    if (const auto* page = theme.elementStyle("#write"))
      paintLayoutBox(painter, LayoutBox::place("#write", *page, theme.usedBoxForStyle(*page, rect.width()), rect,
                                               theme.paragraphFont()));
    for (const auto* block : layout.visibleBlocks({0, 0, 960, 900}, theme))
      block->paint(painter, theme, 0);
    painter.end();
    lastImage = std::move(image);
  };
  rows.append(measure("first_viewport_paint", paint));
  selectionToPaint.append(
      QJsonObject{{"stage", "cold_selection_to_paint"}, {"wall_ms", selectionClock.nsecsElapsed() / 1e6}});
  captureVisual("first_viewport_paint");
  rows.append(measure("warm_viewport_paint", paint));
  if (qEnvironmentVariableIsSet("MUFFIN_FIRST_LAYOUT_ONLY")) {
    const auto selected = theme;
    const auto selectedSource = definition.sourceSheet;
    for (int round = 0; round < 2; ++round) {
      rows.append(measure(QString("switch_away_%1").arg(round), [&] {
        auto away = [&] {
          if (!managerDocument)
            return RenderTheme::github();
          if (!documentManager->setTheme(selectedId == "github" ? "night" : "github"))
            qFatal("Document switch-away failed");
          return documentManager->currentTheme();
        }();
        away.updateForViewport(960, 900);
        if (realSwitch)
          away.reuseDocumentSemantics(theme);
        layout.rebuild(session.document(), away, 960, {}, {}, DocumentLayout::BuildPolicy::Lazy);
        layout.visibleBlocks({0, 0, 960, 900}, away);
        if (realSwitch)
          theme = std::move(away);
      }));
      selectionClock.start();
      if (managerDocument) {
        rows.append(measure(QString("manager_repeat_select_snapshot_%1").arg(round), [&] {
          if (!documentManager->setTheme(selectedId))
            qFatal("Document repeated selection failed");
          if (definition.sourceSheet != selectedSource)
            qFatal("Unchanged document theme reloaded");
        }));
      }
      rows.append(measure(QString("switch_back_%1").arg(round), [&] {
        if (managerDocument) {
          // The synchronous selection callback already installed the detached
          // snapshot and handed over unchanged document semantics.
        } else if (realSwitch) {
          auto next = selected.detachedPrototype();
          next.reuseDocumentSemantics(theme);
          theme = std::move(next);
        } else {
          theme = selected;
        }
        layout.rebuild(session.document(), theme, 960, {}, {}, DocumentLayout::BuildPolicy::Lazy);
      }));
      recordCounterStorage();
      rows.append(
          measure(QString("switch_back_layout_%1").arg(round), [&] { layout.visibleBlocks({0, 0, 960, 900}, theme); }));
      rows.append(measure(QString("switch_back_paint_%1").arg(round), paint));
      selectionToPaint.append(QJsonObject{{"stage", QString("repeat_selection_to_paint_%1").arg(round)},
                                          {"wall_ms", selectionClock.nsecsElapsed() / 1e6}});
      captureVisual(QString("switch_back_paint_%1").arg(round));
    }
  } else {
    const auto id = session.document().root().children().at(1)->id();
    rows.append(measure("cursor_block", [&] {
      SelectionRange selection;
      selection.anchor.blockId = selection.focus.blockId = id;
      selection.anchor.text.nodeId = selection.focus.text.nodeId = id;
      selection.anchor.text.textOffset = selection.focus.text.textOffset = 20;
      layout.rebuildBlock(id, session.document(), theme, selection, DocumentLayout::BlockUpdateReason::Selection);
    }));
    rows.append(measure("edit", [&] {
      if (session.applyTextEdit(2, 0, "Edited ") == muffin::TextEditResult::Rejected)
        qFatal("Benchmark edit failed");
      const auto range = session.lastLocalTopLevelRangeChange();
      if (range.isValid())
        layout.rebuildTopLevelRange(range, session.document(), theme, {});
      else
        layout.rebuildBlock(session.document().root().children().front()->id(), session.document(), theme, {});
    }));
    recordCounterStorage();
    const auto applyEdit = [&](qsizetype start, const QString& text) {
      if (session.applyTextEdit(start, 0, text) == muffin::TextEditResult::Rejected)
        qFatal("Benchmark edit failed");
      const auto range = session.lastLocalTopLevelRangeChange();
      if (!range.isValid() || !layout.rebuildTopLevelRange(range, session.document(), theme, {}).rebuilt)
        qFatal("Benchmark range rebuild failed");
    };
    rows.append(measure("edit_plain", [&] {
      const auto& paragraph = *session.document().root().children().at(1);
      applyEdit(paragraph.sourceRange().byteStart + 2, "Plain edit ");
    }));
    recordCounterStorage();
    rows.append(measure("edit_numbering", [&] { applyEdit(0, "# Inserted heading\n\n"); }));
    recordCounterStorage();
    // The reference runs after all comparable measurements: its allocations
    // must not inflate the edit working-set samples or warm their identity
    // caches.
  }
  rows.append(measure("counter_full_reference", [&] {
    const auto expected = theme.resolveDocumentCounters(session.document());
    auto full = RenderTheme::fromDefinition(definition);
    full.updateForViewport(960, 900);
    if (full.resolveDocumentCounters(session.document()) != expected)
      qFatal("Full counter reference differs");
    for (const auto& node : session.document().root().children())
      if (theme.generatedCounterFingerprint(node->id()) != full.generatedCounterFingerprint(node->id()))
        qFatal("Full generated-content fingerprint differs");
  }));
  enableThemePerformance(false);
  QFile output(QString::fromLocal8Bit(argv[1]));
  if (!output.open(QIODevice::WriteOnly))
    return 2;
  QJsonObject result{{"theme", path},
                     {"qpa", QGuiApplication::platformName()},
                     {"switch_model", realSwitch ? "detached_with_semantic_seed" : "legacy_selected_copy"},
                     {"manager_document", managerDocument},
                     {"selection_callbacks", selectionCallbacks},
                     {"selection_to_paint", selectionToPaint},
                     {"qt_version", qVersion()},
                     {"detailed", qEnvironmentVariableIsSet("MUFFIN_THEME_LAYOUT_DETAIL")},
                     {"css_detailed", qEnvironmentVariableIsSet("MUFFIN_THEME_CSS_DETAIL")},
                     {"paint_detailed", qEnvironmentVariableIsSet("MUFFIN_THEME_PAINT_DETAIL")},
                     {"MiB", mib},
                     {"document", inputPath},
                     {"document_sha256", QString::fromLatin1(inputDigest)},
                     {"characters", qint64(session.document().markdownText().size())},
                     {"blocks", qint64(layout.slotCount())},
                     {"viewport", "960x900 DPR1"},
                     {"full_counter_reference_matches", true},
                     {"visuals", visuals},
                     {"measurements", rows}};
  if (qEnvironmentVariableIsSet("MUFFIN_FIRST_LAYOUT_ONLY"))
    lastImage.save(QString::fromLocal8Bit(argv[1]) + ".png");
  output.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
  return 0;
}
