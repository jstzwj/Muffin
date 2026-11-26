#include "mermaid/math/MathMlCssLayout.h"
#include "math/MathRenderer.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSet>

#if !defined(Q_OS_WIN)
#include <ctime>
#else
// Only the process timing calls come from windows.h: keep it lean, and keep
// its min/max macros away from the standard headers included below.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdlib>
#include <random>

namespace {

constexpr bool addressSanitizerEnabled() {
#if defined(__SANITIZE_ADDRESS__)
  return true;
#elif defined(__clang__)
  return __has_feature(address_sanitizer);
#else
  return false;
#endif
}

// ASan's allocation checks make this corpus about 2.5x slower on the Linux
// runner. Keep the Release budget and allow 3x for instrumented builds; all
// inputs, operation checks and sanitizer failures still run unchanged.
constexpr qint64 kTimeBudgetMs = addressSanitizerEnabled() ? 6000 : 2000;

// CPU time this process has consumed so far, in milliseconds, or -1 when the
// system does not report it.
qint64 processCpuTimeMs() {
#if defined(Q_OS_WIN)
  FILETIME creation{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user))
    return -1;
  const auto ticks = [](const FILETIME& time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
  return qint64((ticks(kernel) + ticks(user)) / 10000); // FILETIME counts 100 ns
#else
  const std::clock_t ticks = std::clock();
  return ticks == std::clock_t(-1) ? -1 : qint64(qint64(ticks) * 1000 / CLOCKS_PER_SEC);
#endif
}

// Like QElapsedTimer, but for the CPU time of this process. The budget below
// guards the amount of work the corpus makes the renderer do, and only CPU
// time measures that: a wall clock also charges the time other processes own
// the core for, which turned a load peak on a shared runner into a false
// failure (the same corpus needs about one second of CPU time).
struct CpuTimer {
  qint64 elapsedMs() const {
    const qint64 nowMs = processCpuTimeMs();
    return startedMs < 0 || nowMs < 0 ? -1 : nowMs - startedMs;
  }

private:
  qint64 startedMs = processCpuTimeMs();
};

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition)
    fail(message);
}

QString atom(std::mt19937_64& random) {
  static const QStringList atoms{QStringLiteral("x"),
                                 QStringLiteral("y"),
                                 QStringLiteral("z"),
                                 QStringLiteral("1"),
                                 QStringLiteral("\\alpha"),
                                 QStringLiteral("\\sum"),
                                 QStringLiteral("\\int"),
                                 QStringLiteral("\\text{office}"),
                                 QStringLiteral("\\text{\u4e2d\u6587}"),
                                 QStringLiteral("\\text{\u0633\u0644\u0627\u0645}"),
                                 QStringLiteral("x\\ne y")};
  return atoms.at(static_cast<qsizetype>(random() % atoms.size()));
}

QString expression(std::mt19937_64& random, int depth) {
  if (depth <= 0)
    return atom(random);
  const QString body = expression(random, depth - 1);
  switch (random() % 12) {
  case 0:
    return QStringLiteral("\\frac{%1}{%2}").arg(body, expression(random, depth - 1));
  case 1:
    return QStringLiteral("\\sqrt{%1}").arg(body);
  case 2:
    return QStringLiteral("{%1}^{%2}_{%3}").arg(body, atom(random), atom(random));
  case 3:
    return QStringLiteral("\\hat{%1}").arg(body);
  case 4:
    return QStringLiteral("\\vec{%1}").arg(body);
  case 5:
    return QStringLiteral("\\overline{%1}").arg(body);
  case 6:
    return QStringLiteral("\\underline{%1}").arg(body);
  case 7:
    return QStringLiteral("\\overrightarrow{%1}").arg(body);
  case 8:
    return QStringLiteral("\\left(%1\\right)").arg(body);
  case 9:
    return QStringLiteral("\\begin{matrix}%1&%2\\\\%3&%4\\end{matrix}")
        .arg(body, atom(random), atom(random), atom(random));
  case 10:
    return QStringLiteral("%1+%2").arg(body, expression(random, depth - 1));
  default:
    return QStringLiteral("%1\\le %2").arg(body, atom(random));
  }
}

} // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty())
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);

  constexpr qreal kRenderFontPixelSize = 16.0 * 1.21;
  constexpr int kCaseCount = 400;
  std::mt19937_64 random(0x4D4154484D4CULL);
  muffin::math::MathRenderer renderer;
  QSet<int> rootKinds;
  QElapsedTimer timer;
  timer.start();
  const CpuTimer cpuTimer;

  for (int index = 0; index < kCaseCount; ++index) {
    const QString source = expression(random, 1 + index % 3);
    const muffin::math::MathLayoutResult layout = renderer.render(source, kRenderFontPixelSize, Qt::black, true);
    require(layout.valid(),
            QStringLiteral("Math operation fuzz render failed at #%1: %2 (%3)").arg(index).arg(source, layout.error));
    const muffin::math::MathMlPaintOperationBuildResult build =
        muffin::math::buildMathMlPaintOperations(layout, kRenderFontPixelSize);
    require(build.operation.has_value() && !build.failure.has_value(),
            QStringLiteral("Math operation fuzz fallback at #%1: %2 (%3)")
                .arg(index)
                .arg(source, build.failure ? QString::fromUtf8(
                                                 QJsonDocument(build.failure->toJson()).toJson(QJsonDocument::Compact))
                                           : QStringLiteral("missing operation")));
    rootKinds.insert(static_cast<int>(build.operation->kind()));
  }

  require(rootKinds.size() >= 7, QStringLiteral("Math operation fuzz root-kind coverage regressed"));
  const qint64 elapsedMs = timer.elapsed();
  const qint64 cpuMs = cpuTimer.elapsedMs();
  const qint64 measuredMs = cpuMs >= 0 ? cpuMs : elapsedMs;
  require(measuredMs < kTimeBudgetMs,
          QStringLiteral("Math operation fuzz exceeded %1 ms CPU budget (%2): %3 ms CPU, %4 ms wall")
              .arg(kTimeBudgetMs)
              .arg(addressSanitizerEnabled() ? QStringLiteral("ASan") : QStringLiteral("Release"))
              .arg(measuredMs)
              .arg(elapsedMs));
  qInfo() << "MermaidMathMlOperationFuzzTest:" << kCaseCount << "operation trees passed in" << measuredMs
          << "ms of CPU time," << elapsedMs << "ms of wall time; budget" << kTimeBudgetMs << "ms; ASan"
          << addressSanitizerEnabled();
  return 0;
}
