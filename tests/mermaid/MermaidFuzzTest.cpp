// Milestone H4 fuzz test. Property-based: for every generated input, parse must
// be a TOTAL function — it either returns FlowchartData or throws
// FlowchartParseError, and NEVER crashes (segfault / stack-overflow), hangs, or
// OOMs. The H1 resource limits + H3 recursion caps are what guarantee
// termination on pathological input (they throw LimitExceeded); this test
// verifies that contract holds across adversarial categories. Deterministic
// (seeded mt19937_64) so a failure is reproducible — the seed is printed.
//
// No upstream oracle is needed: the property "parse terminates cleanly" is the
// oracle. (Differential native-vs-upstream AST fuzzing belongs in the offline
// compatibility generator, not here.)

#include "mermaid/flowchart/Flowchart.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QString>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

using namespace muffin::mermaid::flowchart;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

quint64 residentSetBytes() {
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
    return counters.WorkingSetSize;
#elif defined(Q_OS_UNIX)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef Q_OS_MACOS
    return static_cast<quint64>(usage.ru_maxrss);
#else
    return static_cast<quint64>(usage.ru_maxrss) * 1024;
#endif
  }
#endif
  return 0;
}

// Deterministic adversarial-input generators. Each returns a syntactically
// flowchart-shaped string designed to stress one robustness surface; with the
// default FlowchartLimits these should throw LimitExceeded (not OOM/hang).

QString genDeepNesting(std::mt19937_64& rng) {
  QString s = QStringLiteral("flowchart TB\n");
  const int n = 20 + (rng() % 400);  // 20..419 nested subgraphs (>> maxSubgraphDepth)
  for (int i = 0; i < n; ++i) s += QStringLiteral("subgraph S%1\n").arg(i);
  s += QStringLiteral("A[Deep]\n");
  for (int i = 0; i < n; ++i) s += QStringLiteral("end\n");
  return s;
}

QString genHugeFanout(std::mt19937_64& rng) {
  QString s = QStringLiteral("flowchart LR\n");
  const int n = 600 + (rng() % 2000);  // always > maxEdges (500)
  for (int i = 0; i < n; ++i) s += QStringLiteral("A --> N%1\n").arg(i);
  return s;
}

QString genHugeVertices(std::mt19937_64& rng) {
  QString s = QStringLiteral("flowchart TB\n");
  const int n = 2200 + (rng() % 3000);  // always > maxVertices (2000)
  for (int i = 0; i < n; ++i) s += QStringLiteral("V%1[Node]\n").arg(i);
  return s;
}

QString genLongLabel(std::mt19937_64& rng) {
  const int n = 12000 + (rng() % 10000);  // always > maxLineLength (10000)
  return QStringLiteral("flowchart TB\nA[\"") + QString(n, QLatin1Char('x')) + QStringLiteral("\"]\n");
}

QString genMalformedEdges(std::mt19937_64& rng) {
  const QStringList shapes = {
      QStringLiteral("flowchart TB\nA --><><><> B"),
      QStringLiteral("flowchart TB\nA -->"),
      QStringLiteral("flowchart TB\n--> B"),
      QStringLiteral("flowchart TB\nA -.-.-.-> B"),
      QStringLiteral("flowchart TB\nA --> B & & C"),
      QStringLiteral("flowchart TB\nA[]B\nend"),
  };
  return shapes.at(static_cast<int>(rng() % shapes.size()));
}

QString genUnicode(std::mt19937_64& rng) {
  // RTL override, zero-width chars, combining marks, emoji (surrogate pair),
  // CJK — written as \\u escapes so no raw bidi control sits in the source (which
  // would trip a trojan-source warning). Must parse without crash.
  const QStringList labels = {
      QStringLiteral("\\u202e\\u202dreversed"),
      QStringLiteral("a\\u200bb\\u200cc"),
      QStringLiteral("e\\u0301\\u0302"),
      QStringLiteral("\\uD83D\\uDE00"),
      QStringLiteral("\\u4e2d\\u6587"),
  };
  return QStringLiteral("flowchart TB\nA[\"%1\"] --> B[\"%2\"]\n")
      .arg(labels.at(static_cast<int>(rng() % labels.size())), labels.at(static_cast<int>(rng() % labels.size())));
}

QString genInjection(std::mt19937_64& rng) {
  const QStringList payloads = {
      QStringLiteral("flowchart TB\nA[Alpha]\nclick A href \"javascript:alert(1)\""),
      QStringLiteral("flowchart TB\nA[Alpha]\nclick A href \"data:text/html,<script>\""),
      QStringLiteral("flowchart TB\nA[Alpha]\nclick A href \"vbscript:msgbox\""),
      QStringLiteral("flowchart TB\nA[\"<img src=x onerror=alert(1)>\"]"),
  };
  return payloads.at(static_cast<int>(rng() % payloads.size()));
}

QString genRegexStress(std::mt19937_64& rng) {
  // Pathological sequences in a style line; the line length always exceeds
  // maxLineLength so it throws before the regex does unbounded work.
  const int n = 12000 + (rng() % 8000);
  QString dots(n, QLatin1Char('.'));
  return QStringLiteral("flowchart TB\nA --> B\nlinkStyle 0 stroke:#f00,stroke-dasharray:%1\n").arg(dots);
}

struct Generator {
  const char* name;
  QString (*fn)(std::mt19937_64&);
  bool expectLimit;  // true ⇒ should throw LimitExceeded under default limits
};

const std::vector<Generator> kGenerators = {
    {"deepNesting", genDeepNesting, true},
    {"hugeFanout", genHugeFanout, true},
    {"hugeVertices", genHugeVertices, true},
    {"longLabel", genLongLabel, true},
    {"malformedEdges", genMalformedEdges, false},
    {"unicode", genUnicode, false},
    {"injection", genInjection, false},
    {"regexStress", genRegexStress, true},
};
}  // namespace

int main() {
  {
    QString source = QStringLiteral("flowchart TB\n");
    for (int i = 0; i <= FlowchartLimits{}.maxVertices; ++i)
      source += QStringLiteral("V%1[Node]\n").arg(i);
    bool rejected = false;
    try {
      Flowchart::parse(source);
    } catch (const FlowchartParseError& error) {
      rejected = error.category() == FlowchartErrorCategory::LimitExceeded;
      require(error.diagnostic().span.line ==
                  FlowchartLimits{}.maxVertices + 2 &&
                  error.diagnostic().span.column == 1,
              QStringLiteral("Simple-vertex preflight span drifted"));
    }
    require(rejected,
            QStringLiteral("Simple-vertex preflight did not enforce the limit"));
  }

  const uint64_t seedEnv = qgetenv("MUFFIN_FUZZ_SEED").toULongLong();
  const uint64_t seed = seedEnv ? seedEnv : 0xC0FFEEuLL;
  std::mt19937_64 rng(seed);

  const int itersPerCategory = 250;
  int totalParsed = 0, totalThrew = 0, totalLimit = 0;
  quint64 processPeakBytes = residentSetBytes();

  for (const Generator& gen : kGenerators) {
    const QByteArray only = qgetenv("MUFFIN_FUZZ_ONLY");
    if (!only.isEmpty() && only != gen.name) continue;
    int parsed = 0, threw = 0, limit = 0;
    qint64 maximumElapsedMs = 0;
    qsizetype maximumInputBytes = 0;
    quint64 categoryPeakBytes = residentSetBytes();
    for (int i = 0; i < itersPerCategory; ++i) {
      const QString source = gen.fn(rng);
      maximumInputBytes = std::max(maximumInputBytes, source.size() * 2);
      if (qgetenv("MUFFIN_FUZZ_TRACE").size()) {
        fprintf(stderr, "[%s #%d] %s\n", gen.name, i, QString(source).replace(QLatin1Char('\n'), QStringLiteral("\\n")).toUtf8().constData());
        fflush(stderr);
      }
      QElapsedTimer timer;
      timer.start();
      FlowchartErrorCategory cat = FlowchartErrorCategory::Syntax;
      bool threwError = false;
      try {
        Flowchart::parse(source);  // default limits = Muffin safety boundary
        ++parsed;
      } catch (const FlowchartParseError& error) {
        threwError = true;
        cat = error.category();
        if (cat == FlowchartErrorCategory::LimitExceeded) ++limit;
        else ++threw;
      }
      // Watchdog: every input must terminate quickly (limits bound the work).
      const qint64 elapsedMs = timer.elapsed();
      maximumElapsedMs = std::max(maximumElapsedMs, elapsedMs);
      categoryPeakBytes = std::max(categoryPeakBytes, residentSetBytes());
      require(elapsedMs < 2000, QStringLiteral("'%1' input #%2 hung (%3 ms)")
                                        .arg(QString::fromLatin1(gen.name)).arg(i).arg(elapsedMs));
      if (gen.expectLimit) {
        require(threwError && cat == FlowchartErrorCategory::LimitExceeded,
                QStringLiteral("'%1' input #%2 should have thrown LimitExceeded (seed=%3)")
                    .arg(QString::fromLatin1(gen.name)).arg(i).arg(seed));
      }
      (void)cat;
    }
    totalParsed += parsed;
    totalThrew += threw;
    totalLimit += limit;
    processPeakBytes = std::max(processPeakBytes, categoryPeakBytes);
    require(categoryPeakBytes == 0 || categoryPeakBytes < 512ull * 1024ull * 1024ull,
            QStringLiteral("'%1' exceeded 512 MiB resident memory (%2 MiB)")
                .arg(QString::fromLatin1(gen.name))
                .arg(categoryPeakBytes / (1024.0 * 1024.0), 0, 'f', 1));
    qDebug().noquote() << QStringLiteral("  %1: %2 parsed, %3 non-limit, %4 LimitExceeded; max %5 ms, %6 KiB input, %7 MiB RSS")
                              .arg(QString::fromLatin1(gen.name)).arg(parsed).arg(threw).arg(limit)
                              .arg(maximumElapsedMs).arg(maximumInputBytes / 1024.0, 0, 'f', 1)
                              .arg(categoryPeakBytes / (1024.0 * 1024.0), 0, 'f', 1);
  }

  qDebug().noquote() << QStringLiteral("MermaidFuzzTest: %1 inputs (seed=0x%2) — all terminated cleanly; %3 parsed, %4 non-limit, %5 LimitExceeded")
                            .arg(kGenerators.size() * itersPerCategory).arg(seed, 0, 16).arg(totalParsed).arg(totalThrew).arg(totalLimit);
  qDebug().noquote() << QStringLiteral("  process resident-set high water: %1 MiB")
                            .arg(processPeakBytes / (1024.0 * 1024.0), 0, 'f', 1);
  return 0;
}
