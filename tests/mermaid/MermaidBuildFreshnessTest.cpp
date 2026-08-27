// Build-freshness gate — closes the MSB8028 fake-green structurally.
//
// Rounds 8/9 of the state review shipped six COMPILE ERRORS that two "green"
// ctest runs never saw: the VS incremental build skipped relinking the test
// executables, and ctest happily ran the OLD binaries against the NEW
// assertions. "exit 0" proved nothing; the check that caught it was
// comparing exe mtimes against source mtimes by hand. This test automates
// that comparison inside the same ctest run that would otherwise go green
// on stale binaries.
//
// The target→source/lib mapping comes from the CONFIGURE-TIME manifest
// (cmake/MuffinTestFunctions.cmake + MuffinTests.cmake append every
// registered test, both first-party libraries, and the app). The earlier
// name-derivation ("MuffinX" → tests/mermaid/X.cpp) silently missed tests
// whose name deliberately differs from their source file
// (MuffinMermaidC4EdgeParityTest compiles MermaidC4GeometryOracleTest.cpp)
// and never covered the src/theme (and other) trees MuffinCore compiles.
//
// Invariants (per dependency domain, so normal incremental builds pass):
//   1. Each first-party .lib is newer than every listed source it compiles
//      that exists on disk (the lib itself was not skipped).
//   2. Every registered test exe is newer than its OWN listed sources and
//      than each first-party lib it links (a skipped relink after a lib
//      rebuild is exactly the incident class).
//   3. The app is newer than both libs and its own listed sources.
// Editing one test source therefore only requires that test's exe to
// rebuild; editing a lib source requires the lib AND its dependents to
// rebuild — which is precisely what "verified" means.

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cstdio>

namespace {

int fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  return 1;
}

QString formatTime(const QDateTime& time) {
  return time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

qint64 modifiedMs(const QFileInfo& info) {
  return info.lastModified().toMSecsSinceEpoch();
}

struct ManifestEntry {
  QString kind;       // TEST / LIB / APP
  QString target;
  QStringList sources;
  QStringList links;  // TEST only
};

// One source/reference barrier for a binary: the newest mtime among the
// listed files that exist (relative paths resolve against the source root;
// generated artifacts that only appear during the build are skipped).
qint64 sourceBarrierMs(const QStringList& sources, const QString& sourceRoot,
                       QString* newestName) {
  qint64 newest = 0;
  for (const QString& entry : sources) {
    if (entry.isEmpty() || entry.contains(QStringLiteral("$<"))) continue;
    const QString path = QFileInfo(entry).isAbsolute()
        ? entry : sourceRoot + QLatin1Char('/') + entry;
    const QFileInfo info(path);
    if (!info.exists()) continue;
    if (modifiedMs(info) > newest) {
      newest = modifiedMs(info);
      if (newestName) *newestName = info.fileName();
    }
  }
  return newest;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QString sourceRoot = QStringLiteral(MUFFIN_SOURCE_ROOT);
  const QDir exeDir(QCoreApplication::applicationDirPath());

  QFile manifest(QStringLiteral(MUFFIN_BUILD_MANIFEST));
  if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text))
    return fail(QStringLiteral("build-freshness manifest missing: %1")
                    .arg(manifest.fileName()));

  // lib name -> (artifact info, newest-source barrier)
  struct LibState {
    QFileInfo artifact;
    qint64 effectiveMs = 0;  // max(.lib, .dll) — the .dll is the authoritative
                             // link product under SHARED; MSVC incremental
                             // links preserve the import lib's timestamp when
                             // its export content is unchanged, so the .lib
                             // alone can legitimately lag a fresh relink.
    qint64 sourceMs = 0;
    QString newestSource;
  };
  QHash<QString, LibState> libs;
  QVector<ManifestEntry> binaries;

  while (!manifest.atEnd()) {
    const QString line = QString::fromUtf8(manifest.readLine()).trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
    const QStringList fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 3)
      return fail(QStringLiteral("malformed manifest line: %1").arg(line));
    ManifestEntry entry;
    entry.kind = fields.at(0);
    entry.target = fields.at(1);
    entry.sources = fields.at(2).split(QLatin1Char(';'));
    if (fields.size() > 3)
      entry.links = fields.at(3).split(QLatin1Char(';'));
    if (entry.kind == QLatin1String("LIB")) {
      LibState state;
      state.artifact = QFileInfo(
          exeDir.absoluteFilePath(entry.target + QStringLiteral(".lib")));
      state.effectiveMs = modifiedMs(state.artifact);
      const QFileInfo dll(exeDir.absoluteFilePath(
          entry.target + QStringLiteral(".dll")));
      if (dll.exists())
        state.effectiveMs = std::max(state.effectiveMs, modifiedMs(dll));
      state.sourceMs = sourceBarrierMs(entry.sources, sourceRoot,
                                       &state.newestSource);
      libs.insert(entry.target, state);
    } else {
      binaries.append(entry);
    }
  }
  if (libs.isEmpty())
    return fail(QStringLiteral("manifest lists no libraries"));

  // Invariant 1: the libs themselves must not be the skipped step.
  for (auto it = libs.cbegin(); it != libs.cend(); ++it) {
    const LibState& state = it.value();
    if (!state.artifact.exists())
      return fail(QStringLiteral("%1.lib not found in %2")
                      .arg(it.key(), exeDir.absolutePath()));
    if (state.sourceMs > 0 &&
        state.effectiveMs < state.sourceMs)
      return fail(QStringLiteral(
                      "STALE %1.lib (%2 < %3 %4) — the build skipped "
                      "recompiling the library; every downstream ctest "
                      "result would come from the previous code. Rebuild "
                      "before trusting any result.")
                      .arg(it.key(),
                           formatTime(state.artifact.lastModified()),
                           formatTime(QDateTime::fromMSecsSinceEpoch(
                               state.sourceMs)),
                           state.newestSource));
  }

  // Invariants 2+3: every binary must be newer than its own sources and the
  // first-party libs it links (the app links both).
  QStringList stale;
  int checked = 0;
  for (const ManifestEntry& entry : binaries) {
    const bool isApp = entry.kind == QLatin1String("APP");
    const QFileInfo binary(QFileInfo(exeDir.absoluteFilePath(
        entry.target + QStringLiteral(".exe"))));
    if (!binary.exists())
      return fail(QStringLiteral("manifest target %1 not built (%2)")
                      .arg(entry.target, binary.absoluteFilePath()));
    ++checked;
    qint64 barrierMs = sourceBarrierMs(entry.sources, sourceRoot, nullptr);
    QString barrierName = QStringLiteral("own sources");
    const QStringList linked = isApp
        ? QStringList{QStringLiteral("MuffinCore"),
                      QStringLiteral("MuffinUi")}
        : entry.links;
    for (const QString& link : linked) {
      const auto lib = libs.constFind(link);
      if (lib == libs.constEnd()) continue;  // third-party / Qt module
      if (modifiedMs(lib.value().artifact) > barrierMs) {
        barrierMs = modifiedMs(lib.value().artifact);
        barrierName = lib.key() + QStringLiteral(".lib");
      }
    }
    if (modifiedMs(binary) < barrierMs)
      stale.append(QStringLiteral("%1 (%2 < %3 %4)")
                       .arg(binary.absoluteFilePath(),
                            formatTime(binary.lastModified()),
                            formatTime(QDateTime::fromMSecsSinceEpoch(
                                barrierMs)),
                            barrierName));
  }
  if (checked == 0)
    return fail(QStringLiteral("manifest lists no binaries"));
  if (!stale.isEmpty())
    return fail(QStringLiteral(
                    "STALE BINARIES — the build skipped relinking "
                    "(MSB8028 class failure); a green ctest on these would "
                    "be testing OLD binaries. Rebuild (cmake --build "
                    "--preset conan-release) before trusting any result. "
                    "Stale:\n%1")
                    .arg(stale.join(QStringLiteral("\n"))));
  std::printf("MermaidBuildFreshnessTest: %d binaries fresh against %d libs "
              "(manifest %s)\n",
              checked, int(libs.size()),
              qPrintable(formatTime(libs.value(QStringLiteral("MuffinCore"))
                                       .artifact.lastModified())));
  return 0;
}
