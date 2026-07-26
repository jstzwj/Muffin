#include "mermaid/math/MathMlCssLayout.h"
#include "math/MathRenderer.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSet>

#include <cstdlib>
#include <random>

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QString atom(std::mt19937_64& random) {
  static const QStringList atoms{
      QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"),
      QStringLiteral("1"), QStringLiteral("\\alpha"),
      QStringLiteral("\\sum"), QStringLiteral("\\int"),
      QStringLiteral("\\text{office}"), QStringLiteral("\\text{\u4e2d\u6587}"),
      QStringLiteral("\\text{\u0633\u0644\u0627\u0645}"),
      QStringLiteral("x\\ne y")};
  return atoms.at(static_cast<qsizetype>(random() % atoms.size()));
}

QString expression(std::mt19937_64& random, int depth) {
  if (depth <= 0) return atom(random);
  const QString body = expression(random, depth - 1);
  switch (random() % 12) {
    case 0:
      return QStringLiteral("\\frac{%1}{%2}")
          .arg(body, expression(random, depth - 1));
    case 1: return QStringLiteral("\\sqrt{%1}").arg(body);
    case 2:
      return QStringLiteral("{%1}^{%2}_{%3}")
          .arg(body, atom(random), atom(random));
    case 3: return QStringLiteral("\\hat{%1}").arg(body);
    case 4: return QStringLiteral("\\vec{%1}").arg(body);
    case 5: return QStringLiteral("\\overline{%1}").arg(body);
    case 6: return QStringLiteral("\\underline{%1}").arg(body);
    case 7: return QStringLiteral("\\overrightarrow{%1}").arg(body);
    case 8: return QStringLiteral("\\left(%1\\right)").arg(body);
    case 9:
      return QStringLiteral("\\begin{matrix}%1&%2\\\\%3&%4\\end{matrix}")
          .arg(body, atom(random), atom(random), atom(random));
    case 10:
      return QStringLiteral("%1+%2").arg(body,
                                            expression(random, depth - 1));
    default:
      return QStringLiteral("%1\\le %2").arg(body, atom(random));
  }
}

}  // namespace

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

  for (int index = 0; index < kCaseCount; ++index) {
    const QString source = expression(random, 1 + index % 3);
    const muffin::math::MathLayoutResult layout = renderer.render(
        source, kRenderFontPixelSize, Qt::black, true);
    require(layout.valid(),
            QStringLiteral("Math operation fuzz render failed at #%1: %2 (%3)")
                .arg(index).arg(source, layout.error));
    const muffin::math::MathMlPaintOperationBuildResult build =
        muffin::math::buildMathMlPaintOperations(
            layout, kRenderFontPixelSize);
    require(build.operation.has_value() && !build.failure.has_value(),
            QStringLiteral("Math operation fuzz fallback at #%1: %2 (%3)")
                .arg(index)
                .arg(source,
                     build.failure
                         ? QString::fromUtf8(QJsonDocument(
                               build.failure->toJson())
                               .toJson(QJsonDocument::Compact))
                         : QStringLiteral("missing operation")));
    rootKinds.insert(static_cast<int>(build.operation->kind()));
  }

  require(rootKinds.size() >= 7,
          QStringLiteral("Math operation fuzz root-kind coverage regressed"));
  require(timer.elapsed() < 2000,
          QStringLiteral("Math operation fuzz exceeded 2 seconds: %1 ms")
              .arg(timer.elapsed()));
  qInfo() << "MermaidMathMlOperationFuzzTest:" << kCaseCount
          << "operation trees passed in" << timer.elapsed() << "ms";
  return 0;
}
