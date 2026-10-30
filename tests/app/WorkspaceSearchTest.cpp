#include "app/WorkspaceSearch.h"
#include "app/WorkspaceReplace.h"
#include "app/SearchPanelWidget.h"
#include "io/FileController.h"
#include "document/PieceTable.h"
#include "../TestUtils.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QLabel>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QDialog>
#include <QTextStream>
#include <QPushButton>
#include <algorithm>

using namespace muffin;
namespace {
void write(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "write fixture");
  require(file.write(bytes) == bytes.size(), "write all bytes");
}
QByteArray read(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "read fixture");
  return file.readAll();
}
WorkspaceQuery query(const QString& root) {
  WorkspaceQuery q;
  q.root = root;
  q.pattern = QString::fromUtf8("术语");
  q.extensions = {"md"};
  return q;
}
void search() {
  QTemporaryDir dir;
  const auto path = dir.filePath("deep/note.md");
  write(path, QString::fromUtf8("😀\n术语 in a link [label](术语)\n").toUtf8());
  auto q = query(dir.path());
  auto cancel = std::make_shared<std::atomic_bool>(false);
  auto result = WorkspaceSearch::run(q, {}, cancel);
  require(result.complete && result.matches == 2 && result.files.size() == 1, "recursive content matches");
  require(result.files[0].hits[0].line == 2 && result.files[0].hits[0].match.start == 3,
          "UTF-16 offsets and source lines");
  OpenSearchBuffers buffers{{WorkspaceFileIndex::pathKey(path), {QString::fromUtf8("unsaved 术语"), true, false}}};
  result = WorkspaceSearch::run(q, buffers, cancel);
  require(result.matches == 1 && result.files[0].modified && !result.files[0].replaceable,
          "dirty buffer wins over disk, cannot batch replace");
  const auto diskOriginal = read(path);
  write(path, QByteArray("binary\0disk", 11));
  result = WorkspaceSearch::run(q, buffers, cancel);
  require(result.matches == 1 && !result.files[0].replaceable,
          "unreadable disk content does not hide the dirty buffer");
  write(path, diskOriginal);
  q.includePath = "other";
  require(WorkspaceSearch::run(q, {}, cancel).matches == 0, "path filter");
  q.includePath.clear();
  q.maxMatches = 1;
  require(!WorkspaceSearch::run(q, {}, cancel).complete, "truncated search cannot be replaced");
  q.maxMatches = 10000;
  q.options.regularExpression = true;
  q.pattern = "[";
  require(!WorkspaceSearch::run(q, {}, cancel).error.isEmpty(), "invalid regex is reported");
  q.pattern = "(?=.)|.";
  const auto zeroWidth = WorkspaceSearch::run(q, {}, cancel);
  const auto documentMatches =
      DocumentSearch::findAll(QString::fromUtf8("😀\n术语 in a link [label](术语)\n"), q.pattern, q.options);
  require(zeroWidth.matches == documentMatches.matches.size(),
          "zero-width regex semantics match current-document search");
  write(path, QByteArray(2000, 'a') + '!');
  q.pattern = "(a+)+$";
  const auto limited = WorkspaceSearch::run(q, {}, cancel);
  require(!limited.complete && !limited.error.isEmpty(), "backtracking limit is an error, not zero matches");
}
void lockedBuffersRemainSearchable() {
  QTemporaryDir dir, recovery;
  const auto path = dir.filePath("locked.md");
  const auto original = QString::fromUtf8("术语\n");
  write(path, original.toUtf8());
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  OpenSearchBuffers buffers{{WorkspaceFileIndex::pathKey(path), {original, false, false, {}, true}}};
  auto result = WorkspaceSearch::run(query(dir.path()), buffers, cancel);
  require(result.matches == 1 && !result.files.first().replaceable && !result.files.first().modified,
          "locked clean buffers remain searchable without being marked dirty or replaceable");
  QStringList skipped;
  require(WorkspaceReplace::prepare(result, QString::fromUtf8("概念"), &skipped).isEmpty() && skipped.contains(path),
          "folder replacement preview excludes locked files");
  result = WorkspaceSearch::run(query(dir.path()), {}, cancel);
  skipped.clear();
  const auto changes = WorkspaceReplace::prepare(result, QString::fromUtf8("概念"), &skipped);
  const auto report = WorkspaceReplace::apply(dir.path(), changes, recovery.path(), cancel);
  require(report.changed.size() == 1, "unlocked replacement creates recovery data");
  buffers[WorkspaceFileIndex::pathKey(path)].text = QString::fromUtf8("概念\n");
  const auto restore = WorkspaceReplace::restore(report.backupDirectory, buffers, cancel);
  require(restore.changed.isEmpty() && restore.skipped.contains(path), "folder recovery respects a newly locked file");
}

void replacementAndRecovery() {
  QTemporaryDir dir, recovery;
  const auto path = dir.filePath("note.md"), other = dir.filePath("other.md"), mixed = dir.filePath("mixed.md");
  const auto original = QByteArray::fromHex("efbbbf") + QString::fromUtf8("术语\r\n术语").toUtf8();
  write(path, original);
  write(other, QString::fromUtf8("术语").toUtf8());
  write(mixed, QString::fromUtf8("术语\r\nsecond\n").toUtf8());
  auto cancel = std::make_shared<std::atomic_bool>(false);
  auto result = WorkspaceSearch::run(query(dir.path()), {}, cancel);
  QStringList skipped;
  auto changes = WorkspaceReplace::prepare(result, QString::fromUtf8("概念"), &skipped);
  require(changes.size() == 2 && skipped.contains(mixed), "mixed endings skipped; lossless formats admitted");
  require(read(path) == original, "preview does not write files");
  write(other, "external update");
  const auto report = WorkspaceReplace::apply(dir.path(), changes, recovery.path(), cancel);
  require(report.error.isEmpty() && report.changed.size() == 1 && report.skipped.contains(other),
          "stale file skipped at commit");
  require(read(path) == QByteArray::fromHex("efbbbf") + QString::fromUtf8("概念\r\n概念").toUtf8(),
          "BOM CRLF and missing final newline retained");
  require(WorkspaceReplace::latestRecovery(recovery.path()) == report.backupDirectory,
          "journal discovered after restart");
  OpenSearchBuffers dirty{{WorkspaceFileIndex::pathKey(path), {"draft", true, false}}};
  auto restored = WorkspaceReplace::restore(report.backupDirectory, dirty, cancel);
  require(restored.changed.isEmpty() && restored.skipped.contains(path), "restore preserves dirty buffers");
  const auto replaced = read(path);
  write(path, "new edit");
  restored = WorkspaceReplace::restore(report.backupDirectory, {}, cancel);
  require(read(path) == "new edit" && restored.skipped.contains(path), "restore refuses later edits");
  write(path, replaced);
  QString replacedText;
  TextFileFormat replacedFormat;
  require(FileController::decodeTextBytes(replaced, &replacedText, &replacedFormat), "decode current snapshot");
  OpenSearchBuffers cleanSnapshot{
      {WorkspaceFileIndex::pathKey(path), {{}, false, false, std::make_shared<PieceTable>(replacedText)}}};
  require(WorkspaceSearch::bufferMatches(cleanSnapshot.cbegin().value(), replacedText) &&
              !WorkspaceSearch::bufferMatches(cleanSnapshot.cbegin().value(), QStringLiteral("changed")),
          "snapshot comparisons validate logical content");
  restored = WorkspaceReplace::restore(report.backupDirectory, cleanSnapshot, cancel);
  require(restored.changed.contains(path) && read(path) == original && read(other) == "external update",
          "restore exact bytes without overwriting skipped files");
  // A canceled operation never touches files but remains safe to recover.
  cancel->store(true);
  const auto canceled = WorkspaceReplace::apply(dir.path(), changes, recovery.path(), cancel);
  require(canceled.changed.isEmpty() && canceled.skipped.size() == changes.size() && read(path) == original,
          "cancel at file boundary reports unprocessed files");
  require(WorkspaceReplace::latestRecovery(recovery.path()).isEmpty(),
          "unstarted canceled operation does not hide earlier recovery");
}
void encodingAndCaptures() {
  for (const auto& encoding :
       {QStringLiteral("UTF-16LE"), QStringLiteral("UTF-16BE"), QStringLiteral("windows-1252")}) {
    QTemporaryDir directory, recovery;
    TextFileFormat format;
    format.encodingName = encoding;
    format.writeBom = encoding.startsWith("UTF-16");
    format.lineEnding = TextLineEnding::Cr;
    format.ensureTrailingNewline = true;
    const auto path = directory.filePath("note.md");
    QByteArray original;
    require(FileController::encodeTextBytes(QString::fromUtf8("café 12\ncafé 34"), format, &original),
            "encode legacy fixture");
    write(path, original);
    auto q = query(directory.path());
    q.pattern = QString::fromUtf8("(café) (\\d+)");
    q.options.regularExpression = true;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    const auto result = WorkspaceSearch::run(q, {}, cancel);
    require(result.matches == 2 && result.files[0].replaceable,
            "UTF-16 and Western text are searched without losing format");
    QStringList skipped;
    const auto changes = WorkspaceReplace::prepare(result, "$2:$1", &skipped);
    QByteArray expected;
    require(FileController::encodeTextBytes(QString::fromUtf8("12:café\n34:café"), format, &expected),
            "encode expected replacement");
    require(changes.size() == 1 && changes[0].after == expected,
            "captures preserve encoding, CR endings, and absent final newline");
    const auto applied = WorkspaceReplace::apply(directory.path(), changes, recovery.path(), cancel);
    require(applied.changed.contains(path) && read(path) == expected, "encoded replacement committed");
    const auto restored = WorkspaceReplace::restore(applied.backupDirectory, {}, cancel);
    require(restored.changed.contains(path) && read(path) == original, "encoded original restored byte for byte");
  }
}
void panelGeneration() {
  QTemporaryDir dir;
  write(dir.filePath("note.md"), QString::fromUtf8("术语").toUtf8());
  SearchPanelWidget panel;
  panel.setRoot(dir.path());
  auto* field = panel.findChild<QLineEdit*>("WorkspaceSearchQuery");
  auto* tree = panel.findChild<QTreeWidget*>("WorkspaceSearchResults");
  field->setText(QString::fromUtf8("术语"));
  panel.startSearch({});
  field->setText("nothing");
  panel.startSearch({});
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 300) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
  require(tree->topLevelItemCount() == 0 && panel.result().matches == 0, "late old query does not pollute new results");
}
void narrowMatchPreview() {
  QTemporaryDir directory;
  write(directory.filePath("note.md"), QByteArray(100, 'a') + "needle" + QByteArray(100, 'b'));
  SearchPanelWidget panel;
  panel.setRoot(directory.path());
  panel.findChild<QLineEdit*>("WorkspaceSearchQuery")->setText("needle");
  panel.startSearch({});
  auto* tree = panel.findChild<QTreeWidget*>("WorkspaceSearchResults");
  QElapsedTimer timer;
  timer.start();
  while (panel.result().matches != 1 && timer.elapsed() < 5000) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
  require(tree->topLevelItemCount() == 1, "long line match is found");
  require(!tree->topLevelItem(0)->text(0).contains('\n'), "root result does not repeat its filename as a path");
  QImage image(230, 28, QImage::Format_ARGB32);
  image.fill(Qt::white);
  QPainter painter(&image);
  QStyleOptionViewItem option;
  option.initFrom(tree);
  option.rect = image.rect();
  option.widget = tree;
  const auto group = tree->model()->index(0, 0);
  tree->itemDelegate()->paint(&painter, option, tree->model()->index(0, 0, group));
  painter.end();
  bool visibleMatch = false;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      if (image.pixelColor(x, y) == tree->palette().color(QPalette::Link))
        visibleMatch = true;
    }
  require(visibleMatch, "narrow sidebar elides context before the hit so its highlight remains visible");
}
void coverageDetails() {
  QTemporaryDir directory;
  write(directory.filePath("build/ignored.md"), "needle");
  SearchPanelWidget panel;
  panel.setRoot(directory.path());
  panel.findChild<QLineEdit*>("WorkspaceSearchQuery")->setText("needle");
  panel.startSearch({});
  QElapsedTimer timer;
  timer.start();
  while (panel.result().details.isEmpty() && timer.elapsed() < 5000) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
  auto* summary = panel.findChild<QLabel*>("WorkspaceSearchSummary");
  require(summary->text().contains("href=\"details\""), "zero matches still explain excluded coverage");
  QTimer::singleShot(0, &panel, [&] {
    auto* dialog = panel.findChild<QDialog*>("WorkspaceSearchDetails");
    require(dialog && dialog->isVisible(), "summary opens coverage details");
    auto* tree = dialog->findChild<QTreeWidget*>();
    require(tree && tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->text(0) == "build" &&
                !tree->topLevelItem(0)->text(1).isEmpty(),
            "coverage details show a relative path and reason");
    dialog->accept();
  });
  require(QMetaObject::invokeMethod(summary, "linkActivated", Q_ARG(QString, QStringLiteral("details"))),
          "activate details link");
}
void configuredScope() {
  SettingsOverride extensions("search/extensions", "txt"), ignored("search/ignorePatterns", "drafts/\n*.bak"),
      hidden("search/includeHidden", false), git("search/readGitIgnore", true), ignore("search/readIgnore", true),
      size("search/maxFileBytes", 1024 * 1024), regex("search/maxRegexBytes", 1024 * 1024),
      openingExtensions("files/textExtensions", "cpp"), openingSize("files/sizeLimitBytes", 1024);
  QTemporaryDir dir;
  write(dir.filePath("note.txt"), "needle");
  write(dir.filePath("markdown.md"), "needle");
  write(dir.filePath("drafts/draft.txt"), "needle");
  write(dir.filePath("project.txt"), "needle");
  write(dir.filePath(".gitignore"), "project.txt\n");
  write(dir.filePath("large.txt"), QByteArray(2 * 1024 * 1024, 'x') + "needle");
  SearchPanelWidget panel;
  panel.setRoot(dir.path());
  panel.findChild<QLineEdit*>("WorkspaceSearchQuery")->setText("needle");
  auto search = [&] {
    panel.startSearch({});
    QElapsedTimer timer;
    timer.start();
    auto* summary = panel.findChild<QLabel*>("WorkspaceSearchSummary");
    while (summary->text().contains("Searching") && timer.elapsed() < 5000) {
      QApplication::processEvents();
      QThread::msleep(5);
    }
    require(!summary->text().contains("Searching"), "configured search finishes");
  };
  search();
  require(panel.result().matches == 1 && panel.result().files.first().path == dir.filePath("note.txt") &&
              panel.result().query.maxFileBytes == 1024 * 1024,
          QStringLiteral("configured search: matches=%1 files=%2 extensions=%3 limit=%4")
              .arg(panel.result().matches)
              .arg(panel.result().files.size())
              .arg(panel.result().query.extensions.join(','))
              .arg(panel.result().query.maxFileBytes));
  const auto hasReason = [&](WorkspaceSkipReason reason) {
    return std::any_of(panel.result().details.begin(), panel.result().details.end(),
                       [reason](const auto& detail) { return detail.reason == reason; });
  };
  require(hasReason(WorkspaceSkipReason::TooLarge) && hasReason(WorkspaceSkipReason::IgnoredFile) &&
              hasReason(WorkspaceSkipReason::IgnoredDirectory),
          "coverage distinguishes size and ignore exclusions");
  QSettings().setValue("search/readGitIgnore", false);
  QSettings().setValue("search/maxFileBytes", 3 * 1024 * 1024);
  search();
  require(panel.result().matches == 3 && panel.result().searchedFiles == 3,
          "the next search captures changed scope without restarting the editor");
  QSettings().setValue("search/readIgnore", true);
  write(dir.filePath(".ignore"), QByteArray("broken\0rule", 11));
  search();
  require(!panel.result().complete && hasReason(WorkspaceSkipReason::UnreadableIgnoreFile),
          "unreadable ignore rules mark coverage incomplete and prevent unsafe directory replacement");
}
void streamingAndParallelism() {
  QTemporaryDir directory;
  QString text(255, u'a');
  text += QString::fromUtf8("😀朝圣之路\r\n");
  text += QString(70000, u'x') + QString::fromUtf8("朝圣之路\rending\n朝圣之路");
  QString normalized = text;
  normalized.replace("\r\n", "\n");
  normalized.replace(u'\r', u'\n');
  const QStringList encodings{"UTF-8", "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE"};
  for (int i = 0; i < encodings.size(); ++i) {
    TextFileFormat format;
    format.encodingName = encodings[i];
    format.writeBom = true;
    QByteArray bytes;
    require(FileController::encodeTextBytes(text, format, &bytes), "encode streaming fixture");
    write(directory.filePath(QString::number(i) + ".md"), bytes);
  }
  auto q = query(directory.path());
  q.pattern = QString::fromUtf8("朝圣之路");
  q.maxReplaceBytes = 1;
  q.chunkBytes = 256;
  const auto expected = DocumentSearch::findAll(normalized, q.pattern, q.options);
  auto cancel = std::make_shared<std::atomic_bool>(false);
  for (int workers : {1, 4}) {
    q.workerCount = workers;
    const auto result = WorkspaceSearch::run(q, {}, cancel);
    QString diagnostics =
        QStringLiteral("serial and parallel streaming cover all Unicode files: searched=%1 hits=%2 error=%3")
            .arg(result.searchedFiles)
            .arg(result.matches)
            .arg(result.error);
    for (const auto& detail : result.details)
      diagnostics += QStringLiteral("; %1 reason=%2").arg(detail.path).arg(int(detail.reason));
    require(result.complete && result.searchedFiles == 5 && result.matches == 15, diagnostics);
    for (const auto& file : result.files) {
      require(file.fingerprint == WorkspaceSearch::fingerprint(normalized),
              "streamed normalization produces the editor fingerprint");
      for (int i = 0; i < expected.matches.size(); ++i) {
        const auto& hit = file.hits[i];
        require(hit.match.start == expected.matches[i].start && hit.match.length == expected.matches[i].length,
                "matches spanning byte, character and rolling-window boundaries have exact UTF-16 offsets");
        require(hit.line == normalized.left(hit.match.start).count(u'\n') + 1,
                "streaming preserves normalized line numbers");
        require(hit.context.mid(hit.highlight, hit.match.length) == q.pattern, "snippet highlights the match");
        require(hit.context.size() < 170 && hit.match.captures.isEmpty(),
                "results retain bounded snippets instead of full text or captures");
      }
    }
  }
  const auto path = directory.filePath("0.md");
  auto snapshot = std::make_shared<PieceTable>(QString(200000, u'z') + q.pattern);
  snapshot->replace(17, 18, QString::fromUtf8("😀"));
  OpenSearchBuffers buffers{{WorkspaceFileIndex::pathKey(path), {{}, true, false, snapshot}}};
  const auto withDraft = WorkspaceSearch::run(q, buffers, cancel);
  const auto draft =
      std::find_if(withDraft.files.begin(), withDraft.files.end(), [&](const auto& file) { return file.path == path; });
  require(draft != withDraft.files.end() && draft->hits.size() == 1 && draft->modified &&
              draft->fingerprint == WorkspaceSearch::fingerprint(*snapshot),
          "piece-table snapshots override disk without UI-thread materialization");
  q.maxResultBytes = 1;
  const auto bounded = WorkspaceSearch::run(q, {}, cancel);
  require(!bounded.complete && bounded.resultLimitReached && bounded.matches == 0,
          "result memory limit is explicit and blocks replacement");
  q.maxResultBytes = 16 * 1024 * 1024;
  q.maxMatches = 2;
  const auto limited = WorkspaceSearch::run(q, {}, cancel);
  require(!limited.complete && limited.matches == 2 && limited.resultLimitReached,
          "parallel workers share the global match limit");
  cancel->store(true);
  require(!WorkspaceSearch::run(q, {}, cancel).complete, "pre-canceled scans remain incomplete");
}
void regexCompatibilityAndStalePreview() {
  QTemporaryDir directory;
  const auto path = directory.filePath("note.md");
  const auto text = QString::fromUtf8("😀 alpha\nbeta alpha\nbeta");
  write(path, text.toUtf8());
  auto q = query(directory.path());
  q.options.regularExpression = true;
  auto cancel = std::make_shared<std::atomic_bool>(false);
  for (const auto& pattern : QStringList{"alpha\\nbeta", "(?<=alpha)\\nbeta", "(?=.)|.", "^|$", "\\b", ""}) {
    q.pattern = pattern;
    const auto found = WorkspaceSearch::run(q, {}, cancel);
    const auto expected = DocumentSearch::findAll(text, pattern, q.options);
    require(found.complete && found.matches == expected.matches.size(),
            "native PCRE2 preserves current-document regex semantics");
    if (found.matches)
      for (int i = 0; i < expected.matches.size(); ++i)
        require(found.files[0].hits[i].match.start == expected.matches[i].start &&
                    found.files[0].hits[i].match.length == expected.matches[i].length,
                "regex ranges, including empty matches, agree with Qt");
  }
  q.pattern = "alpha";
  const auto result = WorkspaceSearch::run(q, {}, cancel);
  write(path, QString(text).replace("beta", "BETA").toUtf8());
  QStringList skipped;
  require(WorkspaceReplace::prepare(result, "new", &skipped).isEmpty() && skipped.contains(path),
          "lazy replacement preview rejects even same-size disk changes since search");
  q.maxRegexBytes = 1;
  const auto oversized = WorkspaceSearch::run(q, {}, cancel);
  require(oversized.matches == 0 && oversized.details.size() == 1 &&
              oversized.details[0].reason == WorkspaceSkipReason::RegexTooLarge,
          "regex resource limit has a distinct skip reason");
  write(directory.filePath("build/ignored.md"), "alpha");
  const auto ignored = WorkspaceSearch::run(q, {}, cancel);
  require(std::any_of(ignored.details.begin(), ignored.details.end(),
                      [](const auto& detail) { return detail.reason == WorkspaceSkipReason::IgnoredDirectory; }),
          "fixed directory exclusions are visible in coverage details");
  QString wide;
  for (int i = 0; i < 100; ++i)
    wide += u'a' + QString(4095, u'x') + u'\n';
  write(path, wide.toUtf8());
  q.maxRegexBytes = 32 * 1024 * 1024;
  q.pattern = "(?ms)^a(?=(.*))";
  const auto captures = WorkspaceSearch::run(q, {}, cancel);
  require(captures.complete && captures.matches == 100 && captures.files[0].hits[0].match.captures.isEmpty(),
          "large lookahead captures stay out of search results");
  skipped.clear();
  const auto preview = WorkspaceReplace::prepare(captures, "b", &skipped);
  require(preview.size() == 1 && preview[0].afterText.size() == wide.size() && preview[0].afterText.count(u'b') == 100,
          "preview expands one match at a time without retaining every captured tail");
}
void largeFile() {
  QTemporaryDir directory;
  const auto path = directory.filePath("large.md");
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "create large note");
  const QByteArray chunk = QByteArray(1024 * 1024 - 1, 'a') + '\n';
  for (int i = 0; i < 100; ++i)
    require(file.write(chunk) == chunk.size(), "write 100 MiB fixture");
  file.write(QString::fromUtf8("😀 large needle\r\n").toUtf8());
  file.close();
  auto q = query(directory.path());
  q.pattern = "needle";
  QElapsedTimer timer;
  timer.start();
  const auto result = WorkspaceSearch::run(q, {}, std::make_shared<std::atomic_bool>(false));
  require(result.complete && result.matches == 1 && result.searchedFiles == 1, "ordinary search covers a 100 MiB note");
  require(result.files[0].hits[0].match.start == 100 * 1024 * 1024 + 9 && result.files[0].hits[0].line == 101,
          "large note hit has the exact editor offset and line");
  require(!result.files[0].replaceable, "large-file search and replacement limits remain independent");
  QTextStream(stdout) << "100 MiB search: " << timer.elapsed()
                      << " ms; retained context: " << result.files[0].hits[0].context.size() << " UTF-16 units\n";
}
void benchmark(const QString& path, const QString& pattern) {
  WorkspaceQuery q;
  q.root = QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath();
  q.includePath = QFileInfo(path).isDir() ? QString() : QFileInfo(path).fileName();
  q.extensions = {"md"};
  q.pattern = pattern;
  for (int workers : {1, 4}) {
    q.workerCount = workers;
    QElapsedTimer timer;
    timer.start();
    qint64 first = -1;
    const auto result = WorkspaceSearch::run(q, {}, std::make_shared<std::atomic_bool>(false), [&](const auto&) {
      if (first < 0)
        first = timer.elapsed();
    });
    qint64 snippets = 0;
    for (const auto& file : result.files)
      for (const auto& hit : file.hits)
        snippets += hit.context.size() * 2;
    QTextStream(stdout) << workers << " workers: " << result.searchedFiles << " files, " << result.matches
                        << " hits, first=" << first << " ms, total=" << timer.elapsed() << " ms, context=" << snippets
                        << " bytes, complete=" << result.complete << '\n';
  }
}
}
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir settingsDirectory;
  QCoreApplication::setOrganizationName("MuffinTests");
  QCoreApplication::setApplicationName("WorkspaceSearch");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
  const auto arguments = app.arguments();
  if (arguments.size() >= 4 && arguments[1] == "--benchmark") {
    benchmark(arguments[2], arguments[3]);
    return 0;
  }
  runTest("workspace contents", search);
  runTest("locked buffers remain searchable", lockedBuffersRemainSearchable);
  runTest("preview write and durable recovery", replacementAndRecovery);
  runTest("encodings and regex captures", encodingAndCaptures);
  runTest("query generations", panelGeneration);
  runTest("narrow match context", narrowMatchPreview);
  runTest("search coverage details", coverageDetails);
  runTest("configured search coverage", configuredScope);
  runTest("streaming and parallel search", streamingAndParallelism);
  runTest("regex compatibility and lazy preview", regexCompatibilityAndStalePreview);
  runTest("100 MiB search", largeFile);
}
