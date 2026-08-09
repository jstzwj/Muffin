#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/xychart/XYChartDiagram.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using muffin::mermaid::UnknownDiagramError;
using muffin::mermaid::detectDiagramType;
using muffin::mermaid::preprocessDiagram;
using muffin::mermaid::xychart::XYChartAxisData;
using muffin::mermaid::xychart::XYChartAxisType;
using muffin::mermaid::xychart::XYChartData;
using muffin::mermaid::xychart::XYChartDiagram;
using muffin::mermaid::xychart::XYChartOrientation;
using muffin::mermaid::xychart::XYChartParseError;
using muffin::mermaid::xychart::XYChartPlotData;
using muffin::mermaid::xychart::XYChartPlotType;

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}

void require(bool condition, const QString &message)
{
    if (!condition)
        fail(message);
}

QJsonObject loadFixture(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), QStringLiteral("Unable to open fixture: %1").arg(path));
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError,
            QStringLiteral("Invalid fixture JSON: %1").arg(error.errorString()));
    require(document.isObject(), QStringLiteral("Fixture root must be an object"));
    return document.object();
}

bool closeEnough(double actual, double expected)
{
    if (std::isinf(expected))
        return std::isinf(actual) && std::signbit(actual) == std::signbit(expected);
    if (actual == expected)
        return true;
    const double scale = std::max({1.0, std::abs(actual), std::abs(expected)});
    return std::abs(actual - expected) <= scale * 1e-12;
}

double taggedNumber(const QJsonValue &value, bool *defined = nullptr)
{
    if (defined)
        *defined = true;
    if (value.isDouble())
        return value.toDouble();

    const QJsonObject tagged = value.toObject();
    const QString kind = tagged.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("finite")) {
        const double result = tagged.value(QStringLiteral("value")).toDouble();
        return tagged.value(QStringLiteral("negativeZero")).toBool() ? -0.0 : result;
    }
    if (kind == QStringLiteral("undefined")) {
        if (defined)
            *defined = false;
        return 0.0;
    }
    if (kind == QStringLiteral("infinity"))
        return tagged.value(QStringLiteral("sign")).toInt() < 0
            ? -std::numeric_limits<double>::infinity()
            : std::numeric_limits<double>::infinity();
    if (kind == QStringLiteral("negative-zero"))
        return -0.0;
    fail(QStringLiteral("Unknown tagged number kind: %1").arg(kind));
}

void compareAxis(const XYChartAxisData &actual, const QJsonObject &expected, const QString &where)
{
    const QString expectedType = expected.value(QStringLiteral("type")).toString();
    require((actual.type == XYChartAxisType::Band) == (expectedType == QStringLiteral("band")),
            where + QStringLiteral(": axis type mismatch"));
    require(actual.title == expected.value(QStringLiteral("title")).toString(),
            where + QStringLiteral(": title mismatch"));

    if (actual.type == XYChartAxisType::Band) {
        const QJsonArray categories = expected.value(QStringLiteral("categories")).toArray();
        require(actual.categories.size() == categories.size(),
                where + QStringLiteral(": category count mismatch"));
        for (qsizetype i = 0; i < categories.size(); ++i) {
            require(actual.categories.at(i) == categories.at(i).toString(),
                    where + QStringLiteral(": category %1 mismatch").arg(i));
        }
        return;
    }

    const double expectedMin = taggedNumber(expected.value(QStringLiteral("min")));
    const double expectedMax = taggedNumber(expected.value(QStringLiteral("max")));
    require(closeEnough(actual.min, expectedMin), where + QStringLiteral(": min mismatch"));
    require(closeEnough(actual.max, expectedMax), where + QStringLiteral(": max mismatch"));
}

void comparePlot(const XYChartPlotData &actual, const QJsonObject &expected, const QString &where)
{
    const QString expectedType = expected.value(QStringLiteral("type")).toString();
    require((actual.type == XYChartPlotType::Line) == (expectedType == QStringLiteral("line")),
            where + QStringLiteral(": plot type mismatch"));
    require(actual.paletteIndex == expected.value(QStringLiteral("paletteIndex")).toInt(),
            where + QStringLiteral(": palette index mismatch"));

    const QJsonArray points = expected.value(QStringLiteral("points")).toArray();
    require(actual.points.size() == points.size(), where + QStringLiteral(": point count mismatch"));
    for (qsizetype i = 0; i < points.size(); ++i) {
        const QJsonObject point = points.at(i).toObject();
        require(actual.points.at(i).category == point.value(QStringLiteral("category")).toString(),
                where + QStringLiteral(": point %1 category mismatch").arg(i));
        bool expectedDefined = true;
        const double expectedValue = taggedNumber(point.value(QStringLiteral("value")), &expectedDefined);
        require(actual.points.at(i).valueDefined == expectedDefined,
                where + QStringLiteral(": point %1 defined-state mismatch").arg(i));
        if (expectedDefined) {
            require(closeEnough(actual.points.at(i).value, expectedValue),
                    where + QStringLiteral(": point %1 value mismatch").arg(i));
        }
    }

    const bool hasLabels = expected.value(QStringLiteral("hasPointLabels")).toBool();
    require(actual.hasPointLabels == hasLabels, where + QStringLiteral(": point-label presence mismatch"));
    if (hasLabels) {
        const QJsonArray labels = expected.value(QStringLiteral("pointLabels")).toArray();
        require(actual.pointLabels.size() == labels.size(), where + QStringLiteral(": label count mismatch"));
        for (qsizetype i = 0; i < labels.size(); ++i) {
            require(actual.pointLabels.at(i) == labels.at(i).toString(),
                    where + QStringLiteral(": label %1 mismatch").arg(i));
        }
    }
}

void compareData(const XYChartData &actual, const QJsonObject &expected, const QString &name)
{
    require(actual.orientation == (expected.value(QStringLiteral("orientation")).toString()
                                       == QStringLiteral("horizontal")
                                   ? XYChartOrientation::Horizontal
                                   : XYChartOrientation::Vertical),
            name + QStringLiteral(": orientation mismatch"));
    require(actual.title == expected.value(QStringLiteral("title")).toString(),
            name + QStringLiteral(": title mismatch"));
    require(actual.accTitle == expected.value(QStringLiteral("accTitle")).toString(),
            name + QStringLiteral(": accTitle mismatch"));
    require(actual.accDescr == expected.value(QStringLiteral("accDescr")).toString(),
            name + QStringLiteral(": accDescr mismatch"));
    compareAxis(actual.xAxis, expected.value(QStringLiteral("xAxis")).toObject(), name + QStringLiteral(" x"));
    compareAxis(actual.yAxis, expected.value(QStringLiteral("yAxis")).toObject(), name + QStringLiteral(" y"));

    const QJsonArray plots = expected.value(QStringLiteral("plots")).toArray();
    require(actual.plots.size() == plots.size(), name + QStringLiteral(": plot count mismatch"));
    for (qsizetype i = 0; i < plots.size(); ++i)
        comparePlot(actual.plots.at(i), plots.at(i).toObject(), name + QStringLiteral(" plot %1").arg(i));
}

void runFixture(const QJsonObject &fixture)
{
    require(fixture.value(QStringLiteral("upstream")).toObject()
                    .value(QStringLiteral("version")).toString() == QStringLiteral("11.16.0"),
            QStringLiteral("Grammar fixture must target Mermaid 11.16.0"));
    require(fixture.value(QStringLiteral("fixtureSha256")).toString()
                == QStringLiteral("e6a5e4c93ee1798bfad4b418496b96cc25c400a12515f0788cc94fa9f928bff8"),
            QStringLiteral("Grammar fixture changed; audit its provenance"));

    const QJsonArray cases = fixture.value(QStringLiteral("cases")).toArray();
    require(cases.size() == 72, QStringLiteral("Grammar fixture unexpectedly changed coverage"));
    for (const QJsonValue &caseValue : cases) {
        const QJsonObject testCase = caseValue.toObject();
        const QString name = testCase.value(QStringLiteral("id")).toString();
        const QString source = testCase.value(QStringLiteral("source")).toString();
        const QJsonObject upstreamError = testCase.value(QStringLiteral("reject")).toObject();
        const bool expectedDetector = upstreamError.value(QStringLiteral("class")).toString()
                                      != QStringLiteral("no-diagram");
        const bool expectedParse = testCase.value(QStringLiteral("parseAccept")).toBool();

        const auto preprocessed = preprocessDiagram(source);
        bool detected = false;
        try {
            detected = detectDiagramType(source, preprocessed.config) == QStringLiteral("xychart");
        } catch (const UnknownDiagramError &) {
            detected = false;
        }
        require(detected == expectedDetector, name + QStringLiteral(": detector result mismatch"));
        if (!expectedDetector)
            continue;

        try {
            XYChartData data = XYChartDiagram::parse(preprocessed.code);
            require(expectedParse, name + QStringLiteral(": native parser accepted an upstream rejection"));
            compareData(data, testCase.value(QStringLiteral("expectedDb")).toObject(), name);
        } catch (const XYChartParseError &error) {
            require(!expectedParse,
                    name + QStringLiteral(": native parser rejected an upstream acceptance at %1:%2: %3")
                               .arg(error.line)
                               .arg(error.column)
                               .arg(QString::fromUtf8(error.what())));
            require(error.line >= 1 && error.column >= 1,
                    name + QStringLiteral(": parse error must carry a one-based line and column"));

            const QString phase = upstreamError.value(QStringLiteral("class")).toString();
            if ((phase == QStringLiteral("parser") || phase == QStringLiteral("lexer"))
                && upstreamError.contains(QStringLiteral("line"))) {
                const int expectedLine = upstreamError.value(QStringLiteral("line")).toInt();
                require(error.line == expectedLine,
                        name + QStringLiteral(": native/upstream error line mismatch (%1 vs %2)")
                                   .arg(error.line)
                                   .arg(expectedLine));
            }
        }
    }
}

void checkDirectivePresence()
{
    const XYChartData defaults = XYChartDiagram::parse(QStringLiteral("xychart\nline [1]"));
    require(!defaults.hasOrientationDirective, QStringLiteral("implicit vertical must not mask config orientation"));
    require(!defaults.hasTitleDirective, QStringLiteral("missing title must not mask frontmatter title"));

    const XYChartData explicitVertical =
        XYChartDiagram::parse(QStringLiteral("xychart-beta vertical\ntitle \"   \"\nline [1]"));
    require(explicitVertical.hasOrientationDirective,
            QStringLiteral("explicit vertical directive presence was lost"));
    require(explicitVertical.hasTitleDirective, QStringLiteral("explicit empty title presence was lost"));

    bool rejectedEmptyTitle = false;
    try {
        (void)XYChartDiagram::parse(
            QStringLiteral("xychart-beta\ntitle \"\"\nline [1]"));
    } catch (const XYChartParseError &) {
        rejectedEmptyTitle = true;
    }
    require(rejectedEmptyTitle,
            QStringLiteral("zero-length quoted title must remain a lexer rejection"));

    const XYChartData titleAndRange = XYChartDiagram::parse(QStringLiteral(
        "xychart-beta\nx-axis Time 0 --> 10\ny-axis Value -1 --> 1\nline [1,2]"));
    require(titleAndRange.xAxis.title == QStringLiteral("Time")
                && titleAndRange.xAxis.min == 0.0
                && titleAndRange.xAxis.max == 10.0
                && titleAndRange.yAxis.title == QStringLiteral("Value")
                && titleAndRange.yAxis.min == -1.0
                && titleAndRange.yAxis.max == 1.0,
            QStringLiteral("unquoted axis title plus linear range drifted"));

    const XYChartData sanitized = XYChartDiagram::parse(QStringLiteral(
        "xychart-beta\nx-axis [\"<script>x</script>A\"]\n"
        "line [1 \"<script>x</script>\"]"));
    require(sanitized.xAxis.categories == QStringList({QStringLiteral("A")})
                && !sanitized.plots.front().hasPointLabels,
            QStringLiteral("XYChart DB text sanitization drifted"));

    const XYChartData uppercaseHorizontal =
        XYChartDiagram::parse(QStringLiteral("xychart-beta HORIZONTAL\nline [1]"));
    require(uppercaseHorizontal.hasOrientationDirective
                && uppercaseHorizontal.orientation == XYChartOrientation::Vertical,
            QStringLiteral("uppercase orientation token must preserve the DB's case-sensitive vertical quirk"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        require(argc == 2, QStringLiteral("Usage: MermaidXYChartParserTest <xychart-grammar.json>"));
        runFixture(loadFixture(QString::fromLocal8Bit(argv[1])));
        checkDirectivePresence();
        std::cout << "MermaidXYChartParserTest passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "MermaidXYChartParserTest failed: " << error.what() << '\n';
        return 1;
    }
}
