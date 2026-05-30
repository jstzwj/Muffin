#include "model/MarkdownMathSpanBuilder.h"
#include "model/MarkdownSerializer.h"
#include "model/MarkdownSourceSpanUpdater.h"
#include "model/MarkdownTransform.h"
#include "parser/CmarkParser.h"
#include "parser/SourceCoordinateMapper.h"

#include <QTest>

using namespace Muffin;

namespace {

MarkdownNodeId firstNodeOfType(const MarkdownDocument& document, MarkdownNodeType type)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == type) {
            return node.id;
        }
    }
    return 0;
}

} // namespace

class TestMarkdownMathSpanBuilder : public QObject
{
    Q_OBJECT

private slots:
    void rebuildsInlineFormulaSpanFromSerializedSource()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("a $x$ b"));
        const MarkdownNodeId formulaId = firstNodeOfType(parsed.document, MarkdownNodeType::FormulaInline);
        QVERIFY(formulaId != 0);
        MarkdownDocument transformed = MarkdownTransform::replaceFormulaNode(parsed.document, formulaId, QStringLiteral("$y+1$"));

        MarkdownSerializer serializer;
        MarkdownSerializationResult serialization = serializer.serializeDocumentWithSourceMap(transformed);
        QVector<MathSpan> spans = MarkdownMathSpanBuilder::build(transformed, serialization);

        QCOMPARE(serialization.markdown, QStringLiteral("a $y+1$ b"));
        QCOMPARE(spans.size(), 1);
        QVERIFY(!spans.first().display);
        QCOMPARE(spans.first().tex, QStringLiteral("y+1"));
        QCOMPARE(serialization.markdown.mid(spans.first().source.start,
                                            spans.first().source.end - spans.first().source.start),
                 QStringLiteral("$y+1$"));
        QCOMPARE(serialization.markdown.mid(spans.first().content.start,
                                            spans.first().content.end - spans.first().content.start),
                 QStringLiteral("y+1"));
    }

    void rebuildsBlockFormulaSpanFromSerializedSource()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("$$\nz\n$$"));

        MarkdownSerializer serializer;
        MarkdownSerializationResult serialization = serializer.serializeDocumentWithSourceMap(parsed.document);
        QVector<MathSpan> spans = MarkdownMathSpanBuilder::build(parsed.document, serialization);

        QCOMPARE(spans.size(), 1);
        QVERIFY(spans.first().display);
        QCOMPARE(spans.first().tex, QStringLiteral("z"));
        QCOMPARE(serialization.markdown.mid(spans.first().source.start,
                                            spans.first().source.end - spans.first().source.start),
                 QStringLiteral("$$\nz\n$$"));
        QCOMPARE(serialization.markdown.mid(spans.first().content.start,
                                            spans.first().content.end - spans.first().content.start),
                 QStringLiteral("z"));
    }

    void sourceSpanUpdaterAppliesSerializedFormulaSpans()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("a $x$ b"));
        const MarkdownNodeId formulaId = firstNodeOfType(parsed.document, MarkdownNodeType::FormulaInline);
        QVERIFY(formulaId != 0);
        MarkdownDocument transformed = MarkdownTransform::replaceFormulaNode(parsed.document, formulaId, QStringLiteral("$y+1$"));

        MarkdownSerializer serializer;
        MarkdownSerializationResult serialization = serializer.serializeDocumentWithSourceMap(transformed);
        MarkdownDocument updated = MarkdownSourceSpanUpdater::applySerializedSourceSpans(std::move(transformed), serialization);

        const MarkdownNode* formula = updated.nodeById(formulaId);
        QVERIFY(formula);
        QCOMPARE(updated.source(), QStringLiteral("a $y+1$ b"));
        QCOMPARE(formula->literal, QStringLiteral("y+1"));
        QCOMPARE(updated.source().mid(formula->source.start, formula->source.end - formula->source.start),
                 QStringLiteral("$y+1$"));
        QCOMPARE(updated.source().mid(formula->content.start, formula->content.end - formula->content.start),
                 QStringLiteral("y+1"));
        SourceCoordinateMapper mapper(updated.source());
        QCOMPARE(mapper.spanForRange(formula->sourceRange).start, formula->source.start);
        QCOMPARE(mapper.spanForRange(formula->sourceRange).end, formula->source.end);
    }
};

QTEST_MAIN(TestMarkdownMathSpanBuilder)
#include "test_markdown_math_span_builder.moc"
