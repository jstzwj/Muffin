#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/MathImageRenderer.h"
#include "theme/Theme.h"
#include "theme/ThemeStylesheet.h"

#include <QApplication>
#include <QTextDocument>
#include <QTest>
#include <QUrl>

using namespace Muffin;

namespace {

RenderResult renderMarkdown(const QString& markdown, ParseResult* parsedOut = nullptr)
{
    CmarkParser parser;
    ParseResult parsed = parser.parseDocument(markdown);
    ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
    DocumentRenderer renderer(stylesheet);
    RenderResult result = renderer.render(parsed.ast, markdown, parsed.mathSpans);
    if (parsedOut) {
        *parsedOut = std::move(parsed);
    }
    return result;
}

int countSpansOfKind(const RenderSourceMap& sourceMap, RenderSpan::Kind kind)
{
    int count = 0;
    for (const RenderSpan& span : sourceMap.spans()) {
        if (span.kind == kind) {
            ++count;
        }
    }
    return count;
}

QImage imageResourceFor(const QTextDocument& document, const MathSpan& span)
{
    const QString resourceName = QStringLiteral("muffin-math:%1:%2").arg(span.source.start).arg(span.source.end);
    return document.resource(QTextDocument::ImageResource, QUrl(resourceName)).value<QImage>();
}

} // namespace

class TestMathRendering : public QObject
{
    Q_OBJECT

private slots:
    void inlineMathRendersAsNonEditableFormulaSpan()
    {
        const QString markdown = QStringLiteral("hello $x+y$ world");
        RenderResult result = renderMarkdown(markdown);

        QVERIFY(result.document);
        QVERIFY(!result.document->toPlainText().contains(QChar('$')));
        bool foundFormula = false;
        for (const RenderSpan& span : result.sourceMap.spans()) {
            if (span.kind == RenderSpan::Kind::FormulaInline) {
                foundFormula = true;
                QVERIFY(!span.editable);
                QCOMPARE(markdown.mid(span.source.start, span.source.end - span.source.start), QStringLiteral("$x+y$"));
            }
        }
        QVERIFY(foundFormula);
    }

    void displayMathRendersAsFormulaBlock()
    {
        const QString markdown = QStringLiteral("$$\nE = mc^2\n$$");
        RenderResult result = renderMarkdown(markdown);

        QVERIFY(result.document);
        QVERIFY(!result.document->toPlainText().contains(QStringLiteral("$$")));
        bool foundFormula = false;
        for (const RenderSpan& span : result.sourceMap.spans()) {
            if (span.kind == RenderSpan::Kind::FormulaBlock) {
                foundFormula = true;
                QVERIFY(span.block);
                QVERIFY(!span.editable);
                QVERIFY(span.renderedEnd > span.renderedStart);
                QCOMPARE(markdown.mid(span.source.start, span.source.end - span.source.start), markdown);
            }
        }
        QVERIFY(foundFormula);
    }

    void invalidFormulaFallsBackToImage()
    {
        const MathSpan span{{0, 10}, {1, 9}, QStringLiteral("\\invalid{"), false};
        MathImageRenderer renderer(Theme::preset(ThemePreset::Github));

        const QImage image = renderer.render(span);

        QVERIFY(!image.isNull());
        QVERIFY(image.width() > 0);
        QVERIFY(image.height() > 0);
    }

    void complexDisplayMathRenders()
    {
        ParseResult parsed;
        const QString markdown = QStringLiteral(
            "$$\n"
            "\\begin{matrix} 1 & 2 \\\\ 3 & 4 \\end{matrix}\n"
            "\\frac{1}{\\sqrt{x+y}}\n"
            "$$");

        RenderResult result = renderMarkdown(markdown, &parsed);

        QVERIFY(result.document);
        QCOMPARE(countSpansOfKind(result.sourceMap, RenderSpan::Kind::FormulaBlock), 1);
        QCOMPARE(parsed.mathSpans.size(), 1);
        const QImage image = imageResourceFor(*result.document, parsed.mathSpans.first());
        QVERIFY(!image.isNull());
        QVERIFY(image.width() > 0);
        QVERIFY(image.height() > 0);
    }

    void unicodeTextAroundInlineMathRenders()
    {
        ParseResult parsed;
        const QString markdown = QStringLiteral("中文前缀 $\\sqrt{x}+\\frac{1}{2}$ 后缀");

        RenderResult result = renderMarkdown(markdown, &parsed);

        QVERIFY(result.document);
        QCOMPARE(countSpansOfKind(result.sourceMap, RenderSpan::Kind::FormulaInline), 1);
        QCOMPARE(parsed.mathSpans.size(), 1);
        const QImage image = imageResourceFor(*result.document, parsed.mathSpans.first());
        QVERIFY(!image.isNull());
        QVERIFY(result.document->toPlainText().contains(QStringLiteral("中文前缀")));
        QVERIFY(result.document->toPlainText().contains(QStringLiteral("后缀")));
    }

    void consecutiveInlineMathRendersSeparately()
    {
        ParseResult parsed;
        const QString markdown = QStringLiteral("$x$ + $y$ + $z$");

        RenderResult result = renderMarkdown(markdown, &parsed);

        QVERIFY(result.document);
        QCOMPARE(parsed.mathSpans.size(), 3);
        QCOMPARE(countSpansOfKind(result.sourceMap, RenderSpan::Kind::FormulaInline), 3);
        for (const MathSpan& span : parsed.mathSpans) {
            const QImage image = imageResourceFor(*result.document, span);
            QVERIFY(!image.isNull());
            QVERIFY(image.width() > 0);
            QVERIFY(image.height() > 0);
        }
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);
    TestMathRendering test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_math_rendering.moc"
