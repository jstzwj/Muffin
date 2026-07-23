#include "../TestUtils.h"

#include "LargeDocumentFixture.h"

#include "diagnostics/ProcessMemory.h"
#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/EditorController.h"
#include "io/FileController.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

#include <algorithm>

using namespace muffin;

namespace {

constexpr qsizetype kDefaultSizeMb = 1;
constexpr qsizetype kMaximumSizeMb = 100;
constexpr qsizetype kVisibleSlotCount = 30;
constexpr int kEditCount = 3;

struct TextDigest {
  qsizetype characters = 0;
  qsizetype utf8Bytes = 0;
  QByteArray sha256;

  bool operator==(const TextDigest&) const = default;
};

struct TreeFingerprint {
  qsizetype topLevelBlocks = 0;
  qint64 blockNodes = 0;
  qint64 inlineNodes = 0;
  QByteArray sha256;

  bool operator==(const TreeFingerprint&) const = default;
};

class PerformanceReport {
public:
  void addPhase(QString name, qint64 elapsedNs) {
    const double elapsedMs = elapsedNs / 1000000.0;
    const double workingSetMiB = diag::workingSetBytes() / (1024.0 * 1024.0);
    qInfo().noquote() << "LargeDocumentRoundTripPhase"
                      << name
                      << "elapsedMs=" << elapsedMs
                      << "workingSetMiB=" << workingSetMiB;
    QJsonObject phase;
    phase.insert(QStringLiteral("name"), name);
    phase.insert(QStringLiteral("elapsedMs"), elapsedMs);
    phase.insert(QStringLiteral("workingSetMiB"), workingSetMiB);
    phases_.append(phase);
  }

  void setNumber(const QString& key, qint64 value) {
    metadata_.insert(key, static_cast<double>(value));
  }

  void setString(const QString& key, const QString& value) {
    metadata_.insert(key, value);
  }

  void addParserRun(const QString& name, const ParsePerformanceMetrics& metrics) {
    QJsonArray phases;
    for (const ParsePhasePerformance& metric : metrics.phases) {
      const double elapsedMs = metric.elapsedNs / 1000000.0;
      const double workingSetMiB =
          metric.workingSetBytesAfter / (1024.0 * 1024.0);
      qInfo().noquote() << "LargeDocumentParserPhase"
                        << name
                        << metric.name
                        << "elapsedMs=" << elapsedMs
                        << "workingSetMiB=" << workingSetMiB;
      QJsonObject phase;
      phase.insert(QStringLiteral("name"), metric.name);
      phase.insert(QStringLiteral("elapsedMs"), elapsedMs);
      phase.insert(QStringLiteral("workingSetMiB"), workingSetMiB);
      phases.append(phase);
    }

    QJsonObject run;
    run.insert(
        QStringLiteral("totalElapsedMs"),
        metrics.totalElapsedNs / 1000000.0);
    run.insert(
        QStringLiteral("workingSetMiBBefore"),
        metrics.workingSetBytesBefore / (1024.0 * 1024.0));
    run.insert(
        QStringLiteral("workingSetMiBAfter"),
        metrics.workingSetBytesAfter / (1024.0 * 1024.0));
    run.insert(QStringLiteral("phases"), phases);
    parserRuns_.insert(name, run);
  }

  QByteArray finish() {
    QJsonObject result;
    result.insert(QStringLiteral("schemaVersion"), 2);
    result.insert(QStringLiteral("status"), QStringLiteral("passed"));
    result.insert(QStringLiteral("metadata"), metadata_);
    result.insert(QStringLiteral("phases"), phases_);
    result.insert(QStringLiteral("parserRuns"), parserRuns_);
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
  }

private:
  QJsonObject metadata_;
  QJsonArray phases_;
  QJsonObject parserRuns_;
};

void addInteger(QCryptographicHash& hash, qint64 value) {
  const quint64 encoded = qToLittleEndian(static_cast<quint64>(value));
  hash.addData(QByteArrayView(reinterpret_cast<const char*>(&encoded), sizeof(encoded)));
}

void addString(QCryptographicHash& hash, const QString& value) {
  const QByteArray utf8 = value.toUtf8();
  addInteger(hash, utf8.size());
  hash.addData(utf8);
}

void addInlineRange(QCryptographicHash& hash, InlineRange range) {
  addInteger(hash, range.start);
  addInteger(hash, range.end);
}

void addDefinitionRange(QCryptographicHash& hash, DefinitionFieldRange range) {
  addInteger(hash, range.start);
  addInteger(hash, range.end);
}

void addDefinitionFingerprint(
    const DefinitionBlock& definition,
    QCryptographicHash& hash) {
  addInteger(hash, static_cast<int>(definition.kind));
  addString(hash, definition.label);
  addString(hash, definition.destination);
  addString(hash, definition.title);
  addString(hash, definition.note);
  addString(hash, definition.sourceText);
  addDefinitionRange(hash, definition.labelRange);
  addDefinitionRange(hash, definition.destinationRange);
  addDefinitionRange(hash, definition.titleRange);
  addDefinitionRange(hash, definition.noteRange);
  addDefinitionRange(hash, definition.markerRange);
  addDefinitionRange(hash, definition.sourceRange);
  addInteger(hash, static_cast<int>(definition.destinationDelimiter));
  addInteger(hash, static_cast<int>(definition.titleDelimiter));
  addInteger(hash, definition.titleQuoted);
  addInteger(hash, definition.virtualTemplate);
}

void addInlineFingerprint(
    const InlineNode& node,
    QCryptographicHash& hash,
    TreeFingerprint& result) {
  ++result.inlineNodes;
  addInteger(hash, static_cast<int>(node.type()));
  addString(hash, node.text());
  addString(hash, node.marker());
  addString(hash, node.href());
  addString(hash, node.title());
  addString(hash, node.alt());
  addInteger(hash, node.isAutolink());
  const InlineSourceRanges ranges = node.sourceRanges();
  addInlineRange(hash, ranges.source);
  addInlineRange(hash, ranges.content);
  addInlineRange(hash, ranges.openMarker);
  addInlineRange(hash, ranges.closeMarker);
  addInteger(hash, node.children().size());
  for (const InlineNode& child : node.children()) {
    addInlineFingerprint(child, hash, result);
  }
}

void addBlockFingerprint(
    const MarkdownNode& node,
    QCryptographicHash& hash,
    TreeFingerprint& result) {
  ++result.blockNodes;
  addInteger(hash, static_cast<int>(node.type()));
  const SourceRange range = node.sourceRange();
  addInteger(hash, range.byteStart);
  addInteger(hash, range.byteEnd);
  addInteger(hash, range.lineStart);
  addInteger(hash, range.lineEnd);
  addInteger(hash, range.columnStart);
  addInteger(hash, range.columnEnd);
  addString(hash, node.literal());
  addInteger(hash, node.headingLevel());
  addInteger(hash, node.setext());
  addInteger(hash, static_cast<int>(node.listKind()));
  addInteger(hash, node.listStart());
  addInteger(hash, node.listTight());
  addInteger(hash, node.taskChecked());
  addInteger(hash, node.isTaskItem());
  addString(hash, node.codeLanguage());
  addInteger(hash, node.isIndentedCode());
  addInteger(hash, static_cast<int>(node.mathDelimiter()));
  addInteger(hash, static_cast<int>(node.alertKind()));
  addInteger(hash, static_cast<int>(node.frontMatterFormat()));
  addDefinitionFingerprint(node.definition(), hash);
  const QVector<TableAlignment> alignments = node.tableAlignments();
  addInteger(hash, alignments.size());
  for (TableAlignment alignment : alignments) {
    addInteger(hash, static_cast<int>(alignment));
  }
  addInteger(hash, node.tableRowIsHeader());
  addInteger(hash, node.inlines().size());
  for (const InlineNode& inlineNode : node.inlines()) {
    addInlineFingerprint(inlineNode, hash, result);
  }
  addInteger(hash, node.children().size());
  for (const auto& child : node.children()) {
    addBlockFingerprint(*child, hash, result);
  }
}

TreeFingerprint fingerprint(const MarkdownDocument& document) {
  TreeFingerprint result;
  result.topLevelBlocks = document.root().children().size();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  addBlockFingerprint(document.root(), hash, result);
  result.sha256 = hash.result();
  return result;
}

QString describeFingerprintDifference(
    const TreeFingerprint& actual,
    const TreeFingerprint& expected) {
  return QStringLiteral(
             "top-level blocks %1/%2, block nodes %3/%4, inline nodes %5/%6, hash %7/%8")
      .arg(actual.topLevelBlocks)
      .arg(expected.topLevelBlocks)
      .arg(actual.blockNodes)
      .arg(expected.blockNodes)
      .arg(actual.inlineNodes)
      .arg(expected.inlineNodes)
      .arg(QString::fromLatin1(actual.sha256.toHex()))
      .arg(QString::fromLatin1(expected.sha256.toHex()));
}

TextDigest digest(const QByteArray& utf8, qsizetype characters) {
  return {
      characters,
      utf8.size(),
      QCryptographicHash::hash(utf8, QCryptographicHash::Sha256),
  };
}

TextDigest digest(const PieceTable& text) {
  return digest(text.toUtf8(), text.size());
}

qsizetype requestedSizeMb() {
  const QByteArray value = qgetenv("MUFFIN_BENCH_SIZE_MB");
  if (value.isEmpty()) {
    return kDefaultSizeMb;
  }
  bool ok = false;
  const qint64 parsed = value.toLongLong(&ok);
  require(ok && parsed >= 1 && parsed <= kMaximumSizeMb,
          QStringLiteral("MUFFIN_BENCH_SIZE_MB must be between 1 and %1")
              .arg(kMaximumSizeMb));
  return static_cast<qsizetype>(parsed);
}

qint64 parseTimeoutMs(qsizetype sizeMb) {
  const QByteArray value = qgetenv("MUFFIN_BENCH_TIMEOUT_MS");
  if (!value.isEmpty()) {
    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    require(ok && parsed >= 1000,
            QStringLiteral("MUFFIN_BENCH_TIMEOUT_MS must be at least 1000"));
    return parsed;
  }
  return std::max<qint64>(120000, static_cast<qint64>(sizeMb) * 5000);
}

void waitForParse(DocumentSession& session, qint64 timeoutMs) {
  QElapsedTimer timer;
  timer.start();
  while (session.isAsyncParseInProgress() && timer.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(2);
  }
  require(!session.isAsyncParseInProgress(),
          QStringLiteral("Async parse exceeded %1 ms").arg(timeoutMs));
}

void writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
          QStringLiteral("Could not create large-document fixture"));
  require(file.write(bytes) == bytes.size(),
          QStringLiteral("Could not write complete large-document fixture"));
}

QByteArray readBytes(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Could not read saved large-document fixture"));
  return file.readAll();
}

QVector<MarkdownNode*> editableParagraphs(DocumentSession& session) {
  static const QString prefix = QStringLiteral("Roundtrip editable paragraph ");
  QVector<MarkdownNode*> result;
  for (const auto& child : session.document().root().children()) {
    if (child->type() != BlockType::Paragraph) {
      continue;
    }
    const SourceRange range = child->sourceRange();
    if (range.byteStart < 0 || range.byteStart + prefix.size() > session.markdownText().size()) {
      continue;
    }
    if (session.markdownText().mid(range.byteStart, prefix.size()) == prefix) {
      result.append(child.get());
    }
  }
  return result;
}

void setCursor(EditorController& editor, MarkdownNode& block, qsizetype textOffset) {
  CursorPosition cursor;
  cursor.blockId = block.id();
  cursor.text.nodeId = block.id();
  cursor.text.textOffset = textOffset;
  editor.selection().setCursorPosition(cursor);
}

void writeReport(const QByteArray& report) {
  qInfo().noquote() << "LargeDocumentRoundTripTest" << report;
  const QString outputPath = QString::fromLocal8Bit(qgetenv("MUFFIN_BENCH_JSON"));
  if (outputPath.isEmpty()) {
    return;
  }
  QFile output(outputPath);
  require(output.open(QIODevice::WriteOnly | QIODevice::Truncate),
          QStringLiteral("Could not open MUFFIN_BENCH_JSON output"));
  require(output.write(report) == report.size(),
          QStringLiteral("Could not write complete MUFFIN_BENCH_JSON output"));
  require(output.write("\n") == 1,
          QStringLiteral("Could not terminate MUFFIN_BENCH_JSON output"));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTests"));
  QCoreApplication::setApplicationName(QStringLiteral("LargeDocumentRoundTripTest"));
  QSettings().clear();
  QSettings().setValue(QStringLiteral("files/autoSaveOnSwitch"), false);

  const qsizetype sizeMb = requestedSizeMb();
  const qsizetype targetBytes = sizeMb * 1024 * 1024;
  const qint64 timeoutMs = parseTimeoutMs(sizeMb);
  PerformanceReport report;
  report.setNumber(QStringLiteral("requestedSizeMiB"), sizeMb);
  report.setNumber(QStringLiteral("markdownNodeSizeBytes"), sizeof(MarkdownNode));
  report.setNumber(QStringLiteral("inlineNodeSizeBytes"), sizeof(InlineNode));
  report.setNumber(QStringLiteral("sourceRangeSizeBytes"), sizeof(SourceRange));
  report.setNumber(QStringLiteral("inlineSourceRangesSizeBytes"), sizeof(InlineSourceRanges));

  QElapsedTimer timer;
  timer.start();
  QString source = muffin::test::makeMixedMarkdown(targetBytes);
  QByteArray sourceBytes = source.toUtf8();
  report.addPhase(QStringLiteral("fixture.generate"), timer.nsecsElapsed());
  require(sourceBytes.size() >= targetBytes,
          QStringLiteral("Generated fixture did not reach requested UTF-8 size"));
  const TextDigest originalDigest = digest(sourceBytes, source.size());

  QTemporaryDir directory;
  require(directory.isValid(), QStringLiteral("Could not create temporary directory"));
  const QString path = directory.filePath(QStringLiteral("large-document.md"));
  timer.restart();
  writeBytes(path, sourceBytes);
  report.addPhase(QStringLiteral("file.writeFixture"), timer.nsecsElapsed());

  FileController files;
  TextDigest editedDigest;
  TreeFingerprint editedTree;
  qsizetype layoutSlots = 0;
  qsizetype promotedSlots = 0;
  qsizetype pieceTablePieces = 0;
  int fullRefreshes = 0;
  int blockRefreshes = 0;
  int rangeRefreshes = 0;
  {
    DocumentSession session;
    session.setFullParsePerformanceMetricsEnabled(true);
    timer.restart();
    require(files.open(session, nullptr, path),
            QStringLiteral("FileController could not open generated fixture"));
    waitForParse(session, timeoutMs);
    report.addPhase(QStringLiteral("file.openAndParse"), timer.nsecsElapsed());
    report.setNumber(QStringLiteral("parserReportedMs"), session.lastParseElapsedMs());
    require(!session.lastFullParsePerformanceMetrics().isEmpty(),
            QStringLiteral("Initial full parse did not retain performance metrics"));
    report.addParserRun(
        QStringLiteral("open"), session.lastFullParsePerformanceMetrics());
    require(session.markdownText().toString() == source,
            QStringLiteral("Initial FileController open changed source text"));
    require(session.fileFormat().encodingName.compare(
                QStringLiteral("UTF-8"), Qt::CaseInsensitive) == 0,
            QStringLiteral("Generated UTF-8 fixture was detected as another encoding"));
    require(session.fileFormat().lineEnding == TextLineEnding::Lf,
            QStringLiteral("Generated LF fixture was detected with another line ending"));

    const TreeFingerprint originalTree = fingerprint(session.document());
    QVector<MarkdownNode*> candidates = editableParagraphs(session);
    require(candidates.size() >= kEditCount,
            QStringLiteral("Mixed fixture did not create enough editable paragraphs"));
    source.clear();
    source.squeeze();
    sourceBytes.clear();
    sourceBytes.squeeze();

    const RenderTheme theme = RenderTheme::github();
    DocumentLayout layout;
    timer.restart();
    layout.rebuild(session.document(), theme, 900.0, SelectionRange(), path,
                   DocumentLayout::BuildPolicy::Lazy);
    report.addPhase(QStringLiteral("layout.lazyRebuild"), timer.nsecsElapsed());
    require(layout.buildPolicy() == DocumentLayout::BuildPolicy::Lazy,
            QStringLiteral("Large-document layout did not retain lazy policy"));
    require(layout.slotCount() == originalTree.topLevelBlocks,
            QStringLiteral("Lazy layout slot count did not match top-level block count"));
    require(layout.promotedTopLevelIds().isEmpty(),
            QStringLiteral("Lazy rebuild eagerly promoted blocks"));

    const qsizetype visibleCount = std::min(kVisibleSlotCount, layout.slotCount());
    timer.restart();
    if (visibleCount > 0) {
      layout.ensureBuilt(0, visibleCount - 1, theme);
    }
    report.addPhase(QStringLiteral("layout.firstViewport"), timer.nsecsElapsed());
    require(layout.promotedTopLevelIds().size() == visibleCount,
            QStringLiteral("First viewport promoted an unexpected number of blocks"));
    require(layout.slotCount() > visibleCount,
            QStringLiteral("Default large fixture is too small to verify bounded lazy promotion"));

    EditorController editor;
    editor.attach(&session, nullptr);
    QObject::connect(
        &editor.brushQueue(), &BrushQueue::refreshRequested, &session,
        [&](const BrushQueue::RefreshRequest& request) {
          if (request.fullLayoutDirty) {
            ++fullRefreshes;
            layout.rebuild(session.document(), theme, 900.0, editor.selection().selection(),
                           path, DocumentLayout::BuildPolicy::Lazy);
            return;
          }
          if (request.topLevelRangeDirty.isValid()) {
            ++rangeRefreshes;
            const DocumentLayout::RangeRebuildResult rebuilt = layout.rebuildTopLevelRange(
                request.topLevelRangeDirty, session.document(), theme,
                editor.selection().selection());
            require(rebuilt.rebuilt,
                    QStringLiteral("Localized top-level layout refresh failed"));
          }
          for (NodeId blockId : request.layoutDirtyBlocks) {
            ++blockRefreshes;
            const DocumentLayout::BlockRebuildResult rebuilt = layout.rebuildBlock(
                blockId, session.document(), theme, editor.selection().selection());
            require(rebuilt.rebuilt,
                    QStringLiteral("Localized block layout refresh failed"));
          }
        });

    const QString editTokens[kEditCount] = {
        QStringLiteral("TOP_EDIT"),
        QStringLiteral("MID_EDIT"),
        QStringLiteral("END_EDIT"),
    };
    const qsizetype candidateIndexes[kEditCount] = {
        0,
        candidates.size() / 2,
        (candidates.size() - 1) * 9 / 10,
    };

    for (int edit = 0; edit < kEditCount; ++edit) {
      candidates = editableParagraphs(session);
      MarkdownNode* block = candidates.at(candidateIndexes[edit]);
      setCursor(editor, *block, 10);
      const qsizetype beforeSize = session.markdownText().size();
      const qsizetype beforeTopLevelBlocks =
          static_cast<qsizetype>(session.document().root().children().size());
      timer.restart();
      require(editor.inputController().insertText(editTokens[edit]),
              QStringLiteral("InputController rejected large-document edit %1").arg(edit));
      editor.brushQueue().flush();
      report.addPhase(QStringLiteral("edit.%1").arg(edit), timer.nsecsElapsed());
      require(session.lastParseWasLocalEdit(),
              QStringLiteral("Large-document edit %1 fell back to a full parse").arg(edit));
      require(static_cast<qsizetype>(session.document().root().children().size()) ==
                  beforeTopLevelBlocks,
              QStringLiteral("Plain-text edit %1 changed the top-level block count").arg(edit));
      if (session.lastLocalEditChangedTopLevelStructure()) {
        require(session.lastLocalTopLevelRangeChange().isValid(),
                QStringLiteral("Structural local edit %1 did not expose a valid range").arg(edit));
      }
      require(session.markdownText().size() == beforeSize + editTokens[edit].size(),
              QStringLiteral("Large-document edit %1 changed an unexpected text length").arg(edit));
    }

    require(fullRefreshes == 0,
            QStringLiteral("Forward edits requested a full-document layout refresh"));
    editedDigest = digest(session.markdownText());
    editedTree = fingerprint(session.document());

    timer.restart();
    for (int edit = 0; edit < kEditCount; ++edit) {
      require(editor.canUndo(), QStringLiteral("Undo stack ended before all edits were reverted"));
      editor.undo();
      editor.brushQueue().flush();
      require(session.lastParseWasLocalEdit(),
              QStringLiteral("Large-document undo fell back to a full parse"));
    }
    report.addPhase(QStringLiteral("edit.undoAll"), timer.nsecsElapsed());
    require(!editor.canUndo(), QStringLiteral("Unexpected command remained on the undo stack"));
    require(digest(session.markdownText()) == originalDigest,
            QStringLiteral("Undo did not restore the exact original source"));
    require(fingerprint(session.document()) == originalTree,
            QStringLiteral("Undo did not restore the original document structure"));

    timer.restart();
    for (int edit = 0; edit < kEditCount; ++edit) {
      require(editor.canRedo(), QStringLiteral("Redo stack ended before all edits were restored"));
      editor.redo();
      editor.brushQueue().flush();
      require(session.lastParseWasLocalEdit(),
              QStringLiteral("Large-document redo fell back to a full parse"));
    }
    report.addPhase(QStringLiteral("edit.redoAll"), timer.nsecsElapsed());
    require(!editor.canRedo(), QStringLiteral("Unexpected command remained on the redo stack"));
    require(digest(session.markdownText()) == editedDigest,
            QStringLiteral("Redo did not restore the exact edited source"));
    require(fingerprint(session.document()) == editedTree,
            QStringLiteral("Redo did not restore the edited document structure"));
    require(fullRefreshes == 0,
            QStringLiteral("Edit/undo/redo requested a full-document layout refresh"));
    require(layout.promotedTopLevelIds().size() <= visibleCount + kEditCount,
            QStringLiteral("Localized editing promoted an unbounded number of lazy layout slots"));
    require(session.markdownText().pieceCount() <= 32,
            QStringLiteral("Edit/undo/redo caused excessive piece-table fragmentation"));

    timer.restart();
    require(files.save(session, nullptr) == SaveOutcome::Saved,
            QStringLiteral("FileController could not save edited large document"));
    report.addPhase(QStringLiteral("file.save"), timer.nsecsElapsed());
    require(!session.document().isModified(),
            QStringLiteral("Successful save left the document modified"));
    const QByteArray savedBytes = readBytes(path);
    require(digest(savedBytes, session.markdownText().size()) == editedDigest,
            QStringLiteral("Saved UTF-8 bytes did not match edited source"));

    layoutSlots = layout.slotCount();
    promotedSlots = layout.promotedTopLevelIds().size();
    pieceTablePieces = session.markdownText().pieceCount();
    timer.restart();
  }
  report.addPhase(QStringLiteral("session.releaseEdited"), timer.nsecsElapsed());

  DocumentSession reopened;
  reopened.setFullParsePerformanceMetricsEnabled(true);
  timer.restart();
  require(files.open(reopened, nullptr, path),
          QStringLiteral("FileController could not reopen saved large document"));
  waitForParse(reopened, timeoutMs);
  report.addPhase(QStringLiteral("file.reopenAndParse"), timer.nsecsElapsed());
  report.setNumber(
      QStringLiteral("reopenParserReportedMs"), reopened.lastParseElapsedMs());
  require(!reopened.lastFullParsePerformanceMetrics().isEmpty(),
          QStringLiteral("Reopened full parse did not retain performance metrics"));
  report.addParserRun(
      QStringLiteral("reopen"), reopened.lastFullParsePerformanceMetrics());
  require(digest(reopened.markdownText()) == editedDigest,
          QStringLiteral("Reopened source did not match saved edited source"));
  const TreeFingerprint reopenedTree = fingerprint(reopened.document());
  require(reopenedTree == editedTree,
          QStringLiteral("Reopened document structure did not match pre-save structure: %1")
              .arg(describeFingerprintDifference(reopenedTree, editedTree)));

  report.setNumber(QStringLiteral("sourceCharacters"), editedDigest.characters);
  report.setNumber(QStringLiteral("sourceUtf8Bytes"), editedDigest.utf8Bytes);
  report.setNumber(QStringLiteral("topLevelBlocks"), editedTree.topLevelBlocks);
  report.setNumber(QStringLiteral("blockNodes"), editedTree.blockNodes);
  report.setNumber(QStringLiteral("inlineNodes"), editedTree.inlineNodes);
  report.setNumber(QStringLiteral("layoutSlots"), layoutSlots);
  report.setNumber(QStringLiteral("promotedSlots"), promotedSlots);
  report.setNumber(QStringLiteral("pieceTablePieces"), pieceTablePieces);
  report.setNumber(QStringLiteral("blockRefreshes"), blockRefreshes);
  report.setNumber(QStringLiteral("rangeRefreshes"), rangeRefreshes);
  report.setNumber(QStringLiteral("fullRefreshes"), fullRefreshes);
  report.setString(QStringLiteral("sourceSha256"),
                   QString::fromLatin1(editedDigest.sha256.toHex()));
  report.setString(QStringLiteral("structureSha256"),
                   QString::fromLatin1(editedTree.sha256.toHex()));
  writeReport(report.finish());
  return 0;
}
