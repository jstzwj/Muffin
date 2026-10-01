#include "math/MathMacroExpander.h"
#include "math/MathParseError.h"
#include "math/MathParseTask.h"
#include "math/MathParser.h"

#include <QCoreApplication>
#include <QThread>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace muffin::math;

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

QString nested(const QString& prefix, const QString& suffix, int count) {
  return prefix.repeated(count) + QStringLiteral("x") + suffix.repeated(count);
}

template <typename Function>
void expectError(Function&& function, const QString& message) {
  try {
    function();
  } catch (const MathParseError& error) {
    check(error.message() == message, "unexpected nesting error message");
    return;
  }
  throw std::runtime_error("deep input must throw a parse error");
}

struct LiveFrame {
  int& count;
  explicit LiveFrame(int& value) : count(value) { ++count; }
  ~LiveFrame() { --count; }
};

MathParseTask<int> continuationChain(int remaining, bool fail, int& live) {
  LiveFrame frame(live);
  if (remaining == 0) {
    if (fail) throw MathParseError(QStringLiteral("continuation error"));
    co_return 0;
  }
  co_return 1 + co_await continuationChain(remaining - 1, fail, live);
}

void testContinuations() {
  // Both resumption and error/destructor propagation must use constant native
  // stack. A recursive task implementation overflows on this chain.
  int live = 0;
  check(continuationChain(20000, false, live).run() == 20000,
        "continuations must return child results");
  check(live == 0, "successful continuations must release every frame");
  expectError([&] { continuationChain(20000, true, live).run(); },
              QStringLiteral("continuation error"));
  check(live == 0, "failed continuations must release every frame");
}

void testParserNesting() {
  // Preserve the original 512-frame logical limit: each brace group holds an
  // expression and an atom frame. 255 groups plus the final atom are accepted.
  {
    const auto nodes = MathParser(nested(QStringLiteral("{"), QStringLiteral("}"), 255)).parse();
    const QVector<MathParseNode>* body = &nodes;
    for (int i = 0; i < 255; ++i) {
      check(body->size() == 1 && body->first().type == MathNodeType::Group,
            "nested braces must retain their group nodes");
      body = &body->first().body;
    }
    check(body->size() == 1 && body->first().text == QStringLiteral("x"),
          "nested braces must retain their innermost atom");
  }
  const QString depthError = QStringLiteral("LaTeX input is too deeply nested");
  expectError([&] {
    MathParser(nested(QStringLiteral("{"), QStringLiteral("}"), 256)).parse();
  }, depthError);

  const QVector<QPair<QString, QString>> forms{
      {QStringLiteral("{"), QStringLiteral("}")},
      {QStringLiteral("\\sqrt{"), QStringLiteral("}")},
      {QStringLiteral("\\frac{1}{"), QStringLiteral("}")},
      {QStringLiteral("\\left("), QStringLiteral("\\right)")},
      {QStringLiteral("\\text{"), QStringLiteral("}")},
      {QStringLiteral("x^{"), QStringLiteral("}")},
      {QStringLiteral("\\begin{matrix}"), QStringLiteral("\\end{matrix}")},
      {QStringLiteral("x\\over "), QString()}};
  for (const auto& form : forms) {
    for (bool throwOnError : {false, true}) {
      MathSettings settings;
      settings.throwOnError = throwOnError;
      expectError([&] { MathParser(nested(form.first, form.second, 2000), settings).parse(); },
                  depthError);
    }
  }
  // Raw argument extraction has its own recursive text/font/accent path.
  expectError([&] {
    MathParser(QStringLiteral("\\href{") +
               nested(QStringLiteral("\\textbf{"), QStringLiteral("}"), 2000) +
               QStringLiteral("}{x}")).parse();
  }, depthError);

  // An error must unwind the complete logical depth budget, including the
  // guard whose constructor rejected entry. Retrying the same parser at the
  // unconsumed atom remains within the budget.
  MathParser parser(nested(QStringLiteral("{"), QStringLiteral("}"), 256));
  expectError([&] { parser.parse(); }, depthError);
  check(parser.parse().size() == 1, "nesting failure must release the parser depth budget");
}

void testMacroNesting() {
  MathMacroExpander expander;
  check(expander.expand(QStringLiteral("\\def\\a{A}\\def\\b{B}\\expandafter\\a\\b")) == QStringLiteral("AB"),
        "expandafter must preserve token order");
  check(expander.expand(QStringLiteral("\\expandafter\\relax\\noexpand\\a")) == QStringLiteral("\\relax"),
        "expandafter must preserve noexpand token flags");
  expectError([&] {
    expander.expand(QStringLiteral("\\expandafter").repeated(500) + QStringLiteral("\\relax"));
  }, QStringLiteral("Too many nested \\expandafter"));
  check(expander.expand(QStringLiteral("\\expandafter\\a\\b")) == QStringLiteral("AB"),
        "macro nesting failure must allow later expansion");

  // edef/xdef use child expanders. Those children must use the same work stack
  // and expansion budget, rather than starting another native recursion chain.
  const QString definitions = nested(QStringLiteral("\\edef\\a{"), QStringLiteral("}\\a"), 300);
  check(MathMacroExpander().expand(definitions) == QStringLiteral("x"),
        "nested expanded definitions must retain their replacement");
  MathSettings settings;
  settings.maxExpand = 8;
  expectError([&] {
    MathMacroExpander(settings).expand(nested(QStringLiteral("\\edef\\a{"), QStringLiteral("}\\a"), 3000));
  },
              QStringLiteral("Too many expansions: infinite loop or need to increase maxExpand setting"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  std::exception_ptr failure;
  std::unique_ptr<QThread> worker(QThread::create([&] {
    try {
      testContinuations();
      testParserNesting();
      testMacroNesting();
    } catch (...) {
      failure = std::current_exception();
    }
  }));
  // Deliberately smaller than the Windows GUI-thread stack. This applies to
  // every platform, including ARM64, and catches regressions on x64 as well.
  worker->setStackSize(256 * 1024);
  worker->start();
  if (!worker->wait(60000)) {
    std::cerr << "Parser stack regression test timed out\n";
    std::terminate();
  }
  try {
    if (failure) std::rethrow_exception(failure);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
