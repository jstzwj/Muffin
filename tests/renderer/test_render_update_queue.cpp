#include "renderer/RenderUpdateQueue.h"

#include <QTest>

using namespace Muffin;

class TestRenderUpdateQueue : public QObject
{
    Q_OBJECT

private slots:
    void drainsSingleRequest()
    {
        RenderUpdateQueue queue;
        queue.enqueue({{1, 2}, {4, 8}, RenderSpan::Kind::Paragraph, QStringLiteral("edit")});

        QCOMPARE(queue.isEmpty(), false);
        QCOMPARE(queue.size(), 1);

        const RenderUpdateBatch batch = queue.drain();
        QCOMPARE(batch.nodeIds, QVector<MarkdownNodeId>({1, 2}));
        QCOMPARE(batch.combinedEditedSource.start, 4);
        QCOMPARE(batch.combinedEditedSource.end, 8);
        QCOMPARE(batch.preferredKind, RenderSpan::Kind::Paragraph);
        QCOMPARE(batch.reasons, QStringList{QStringLiteral("edit")});
        QVERIFY(queue.isEmpty());
    }

    void mergesNodeIdsInFirstSeenOrder()
    {
        RenderUpdateQueue queue;
        queue.enqueue({{3, 1, 3}, {1, 2}, RenderSpan::Kind::Paragraph, QStringLiteral("first")});
        queue.enqueue({{2, 1, 4}, {5, 6}, RenderSpan::Kind::Heading, QStringLiteral("second")});

        const RenderUpdateBatch batch = queue.drain();

        QCOMPARE(batch.nodeIds, QVector<MarkdownNodeId>({3, 1, 2, 4}));
        QCOMPARE(batch.preferredKind, RenderSpan::Kind::Paragraph);
        QCOMPARE(batch.reasons, QStringList({QStringLiteral("first"), QStringLiteral("second")}));
    }

    void expandsEditedSourceSpan()
    {
        RenderUpdateQueue queue;
        queue.enqueue({{1}, {10, 12}, RenderSpan::Kind::Paragraph, {}});
        queue.enqueue({{2}, {4, 6}, RenderSpan::Kind::Paragraph, {}});
        queue.enqueue({{3}, {20, 22}, RenderSpan::Kind::Paragraph, {}});

        const RenderUpdateBatch batch = queue.drain();

        QCOMPARE(batch.combinedEditedSource.start, 4);
        QCOMPARE(batch.combinedEditedSource.end, 22);
    }

    void ignoresEmptyRequests()
    {
        RenderUpdateQueue queue;
        queue.enqueue({{}, {1, 2}, RenderSpan::Kind::Paragraph, QStringLiteral("empty")});
        queue.enqueue({{0, 0}, {3, 4}, RenderSpan::Kind::Heading, QStringLiteral("invalid")});

        QVERIFY(queue.isEmpty());
        const RenderUpdateBatch batch = queue.drain();
        QVERIFY(batch.nodeIds.isEmpty());
        QVERIFY(!batch.combinedEditedSource.isValid());
        QCOMPARE(batch.preferredKind, RenderSpan::Kind::Unsupported);
        QVERIFY(batch.reasons.isEmpty());
    }

    void clearDropsQueuedRequests()
    {
        RenderUpdateQueue queue;
        queue.enqueue({{1}, {1, 2}, RenderSpan::Kind::Paragraph, QStringLiteral("edit")});
        queue.clear();

        QVERIFY(queue.isEmpty());
        QCOMPARE(queue.drain().nodeIds.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestRenderUpdateQueue)
#include "test_render_update_queue.moc"
