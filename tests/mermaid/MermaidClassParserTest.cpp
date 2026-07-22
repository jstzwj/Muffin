#include "mermaid/classdiagram/ClassDiagram.h"
#include "mermaid/classdiagram/ClassTokenizer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdlib>

using namespace muffin::mermaid::classdiagram;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }
QString json(const QJsonObject& value) {
  return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented));
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected class DB fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open class fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  const QJsonObject grammar = upstream.value(QStringLiteral("grammar")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() == QLatin1String("11.16.0") &&
              grammar.value(QStringLiteral("sha256")).toString() ==
                  QLatin1String("c2ea20022e4adf501dbbcda909a1bdb74e05a2ac53ff5276d09d00fd54a3e21e") &&
              grammar.value(QStringLiteral("url")).toString().endsWith(
                  QLatin1String("/class/parser/classDiagram.jison")),
          QStringLiteral("Class grammar version contract drifted"));
  require(root.value(QStringLiteral("fixtureDigest")).toString() ==
              QLatin1String("72774594e4624e061cfb1124d95a718480a20fce8a7b2df9e603740fdf6276c3"),
          QStringLiteral("Class fixture changed; audit DB and production coverage"));

  const QJsonArray productions = root.value(QStringLiteral("productions")).toArray();
  require(productions.size() == 138, QStringLiteral("Expected all 138 class productions"));
  int covered = 0;
  int unreachable = 0;
  for (qsizetype index = 0; index < productions.size(); ++index) {
    const QJsonObject production = productions[index].toObject();
    require(production.value(QStringLiteral("id")).toInt() == index + 1 &&
                !production.value(QStringLiteral("lhs")).toString().isEmpty() &&
                production.value(QStringLiteral("rhs")).toArray().size() ==
                    production.value(QStringLiteral("rhsLength")).toInt() &&
                !production.value(QStringLiteral("native")).toString().isEmpty(),
            QStringLiteral("Class production metadata incomplete at %1").arg(index + 1));
    if (production.value(QStringLiteral("status")).toString() == QLatin1String("covered")) {
      ++covered;
      require(!production.value(QStringLiteral("fixtures")).toArray().isEmpty(),
              QStringLiteral("Covered class production lacks a fixture"));
    } else {
      require(production.value(QStringLiteral("status")).toString() == QLatin1String("unreachable") &&
                  !production.value(QStringLiteral("reason")).toString().isEmpty(),
              QStringLiteral("Class production %1 remains unclassified").arg(index + 1));
      ++unreachable;
    }
  }
  require(covered == 116 && unreachable == 22,
          QStringLiteral("Class production matrix regressed: %1 covered, %2 unreachable")
              .arg(covered).arg(unreachable));

  QSet<QString> ids;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() >= 13, QStringLiteral("Class DB corpus is unexpectedly small"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !ids.contains(id), QStringLiteral("Duplicate class fixture id: %1").arg(id));
    ids.insert(id);
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QVector<ClassToken> tokens = ClassTokenizer(source).tokenize();
    require(!tokens.isEmpty() && tokens.front().kind == ClassTokenKind::Header,
            QStringLiteral("Class tokenizer missed header for %1").arg(id));
    for (const ClassToken& token : tokens)
      require(token.kind != ClassTokenKind::Invalid,
              QStringLiteral("Invalid class token in %1 at %2:%3: %4")
                  .arg(id).arg(token.line).arg(token.column).arg(token.text));
    QJsonObject actual;
    try {
      actual = ClassDiagram::parse(source).toJson();
    } catch (const std::exception& error) {
      fail(QStringLiteral("Class parser failed for %1: %2").arg(id, QString::fromUtf8(error.what())));
    }
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    require(actual == expected,
            QStringLiteral("Class DB mismatch for %1\nNative:\n%2\nUpstream:\n%3")
                .arg(id, json(actual), json(expected)));
  }

  int rejected = 0;
  for (const QJsonValue& value : root.value(QStringLiteral("invalidCases")).toArray()) {
    const QJsonObject fixture = value.toObject();
    bool nativeRejected = false;
    try {
      ClassDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    } catch (const ClassParseError& error) {
      nativeRejected = true;
      require(error.diagnostic().span.line >= 1 && error.diagnostic().span.column >= 0,
              QStringLiteral("Class diagnostic lacks a stable source span"));
      require(error.diagnostic().stage != ClassErrorStage::Resource,
              QStringLiteral("Syntax fixture was misclassified as a resource error"));
    }
    const bool upstreamRejected = fixture.value(QStringLiteral("rejected")).toBool();
    require(nativeRejected == upstreamRejected,
            QStringLiteral("Class acceptance mismatch for %1: native=%2 upstream=%3")
                .arg(fixture.value(QStringLiteral("id")).toString())
                .arg(nativeRejected).arg(upstreamRejected));
    rejected += nativeRejected;
  }
  require(rejected >= 5, QStringLiteral("Class negative corpus lost rejection coverage"));

  int executableCoverage = 0;
  for (const QJsonValue& value : root.value(QStringLiteral("coverageOnly")).toArray()) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool upstreamRejected = fixture.contains(QStringLiteral("upstreamError"));
    try {
      const QJsonObject actual = ClassDiagram::parse(source).toJson();
      require(!upstreamRejected,
              QStringLiteral("Native accepted upstream-invalid production fixture %1").arg(id));
      require(actual == fixture.value(QStringLiteral("expected")).toObject(),
              QStringLiteral("Class production fixture mismatch for %1\nNative:\n%2\nUpstream:\n%3")
                  .arg(id, json(actual), json(fixture.value(QStringLiteral("expected")).toObject())));
      ++executableCoverage;
    } catch (const ClassParseError&) {
      require(upstreamRejected,
              QStringLiteral("Native rejected upstream-valid production fixture %1").arg(id));
    }
  }
  require(executableCoverage >= 15,
          QStringLiteral("Executable class production corpus is unexpectedly small"));

  const QString mathEdge = QStringLiteral(
      "classDiagram\nA --> B : $$\\sqrt{x^2}$$ שלום 42");
  const ClassDiagram mathDiagram = ClassDiagram::parse(mathEdge);
  require(mathDiagram.data().relations.size() == 1 &&
              mathDiagram.data().relations.first().title ==
                  QStringLiteral("$$\\sqrt{x^2}$$ שלום 42"),
          QStringLiteral("Class Math edge label lexer state drifted"));

  const ClassDiagram genericDiagram = ClassDiagram::parse(QStringLiteral(
      "classDiagram\nclass Repository~T~[\"Repository<T>\"] {\n"
      "  +List~T~ items\n  +find(id) Optional~T~\n}"));
  require(genericDiagram.data().classes.size() == 1 &&
              genericDiagram.data().classes.first().label ==
                  QLatin1String("Repository") &&
              genericDiagram.data().classes.first().text ==
                  QLatin1String("Repository<T>") &&
              genericDiagram.data().classes.first().members.first().text ==
                  QLatin1String("\\+List&lt;T&gt; items") &&
              genericDiagram.data().classes.first().methods.first().text ==
                  QLatin1String("\\+find(id) : Optional&lt;T&gt;"),
          QStringLiteral("Class generic label/member normalization drifted"));

  const ClassDiagram breakDiagram = ClassDiagram::parse(QStringLiteral(
      "classDiagram\nclass Service[\"first<br/>second<BR />third\"]"));
  require(breakDiagram.data().classes.size() == 1 &&
              breakDiagram.data().classes.first().label ==
                  QLatin1String("first<br>second<br>third"),
          QStringLiteral("Class label break-tag normalization drifted"));

  const auto requireResourceLimit = [](const QString& source,
                                       const ClassLimits& limits,
                                       int line, int column,
                                       const QString& detail) {
    try {
      ClassDiagram::parse(source, limits);
      fail(QStringLiteral("Class resource limit was not enforced: %1").arg(detail));
    } catch (const ClassParseError& error) {
      require(error.diagnostic().code == ClassErrorCode::LimitExceeded &&
                  error.diagnostic().stage == ClassErrorStage::Resource &&
                  error.diagnostic().span.line == line &&
                  error.diagnostic().span.column == column &&
                  error.diagnostic().detail == detail,
              QStringLiteral("Class resource diagnostic drifted for %1: %2:%3 %4")
                  .arg(detail).arg(error.diagnostic().span.line)
                  .arg(error.diagnostic().span.column)
                  .arg(error.diagnostic().detail));
    }
  };
  {
    ClassLimits limits;
    limits.maxClasses = 1;
    requireResourceLimit(QStringLiteral("classDiagram\nA --> B"), limits, 2, 6,
                         QStringLiteral("Maximum class count exceeded"));
  }
  {
    ClassLimits limits;
    limits.maxRelations = 0;
    requireResourceLimit(QStringLiteral("classDiagram\nA --> B"), limits, 2, 0,
                         QStringLiteral("Maximum relation count exceeded"));
  }
  {
    ClassLimits limits;
    limits.maxNamespaces = 1;
    requireResourceLimit(QStringLiteral(
        "classDiagram\nnamespace A {\n}\nnamespace B {\n}"),
        limits, 4, 0, QStringLiteral("Maximum namespace count exceeded"));
  }
  {
    ClassLimits limits;
    limits.maxNamespaceDepth = 1;
    requireResourceLimit(QStringLiteral(
        "classDiagram\nnamespace A {\nnamespace B {\n}\n}"),
        limits, 3, 0, QStringLiteral("Maximum namespace depth exceeded"));
  }
  {
    ClassLimits limits;
    limits.maxNotes = 1;
    requireResourceLimit(QStringLiteral(
        "classDiagram\nnote \"first\"\nnote \"second\""),
        limits, 3, 0, QStringLiteral("Maximum class note count exceeded"));
  }
  {
    ClassLimits limits;
    limits.maxMembers = 1;
    requireResourceLimit(QStringLiteral(
        "classDiagram\nclass A {\n+a\n+b\n}"),
        limits, 4, 0, QStringLiteral("Maximum class member count exceeded"));
  }

  qDebug() << "MermaidClassParserTest:" << cases.size() << "DB cases,"
           << executableCoverage << "production cases," << covered
           << "of" << productions.size() << "productions covered";
  return 0;
}
