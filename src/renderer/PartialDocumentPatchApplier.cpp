#include "PartialDocumentPatchApplier.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>

namespace Muffin {

namespace {

namespace RangePatchPolicy {

bool supportsKind(RenderSpan::Kind kind)
{
    return kind == RenderSpan::Kind::Paragraph
        || kind == RenderSpan::Kind::Heading
        || kind == RenderSpan::Kind::CodeBlock
        || kind == RenderSpan::Kind::List
        || kind == RenderSpan::Kind::BlockQuote;
}

QString kindName(RenderSpan::Kind kind)
{
    switch (kind) {
    case RenderSpan::Kind::Paragraph:
        return QStringLiteral("Paragraph");
    case RenderSpan::Kind::Heading:
        return QStringLiteral("Heading");
    case RenderSpan::Kind::Text:
        return QStringLiteral("Text");
    case RenderSpan::Kind::SoftBreak:
        return QStringLiteral("SoftBreak");
    case RenderSpan::Kind::LineBreak:
        return QStringLiteral("LineBreak");
    case RenderSpan::Kind::Emphasis:
        return QStringLiteral("Emphasis");
    case RenderSpan::Kind::Strong:
        return QStringLiteral("Strong");
    case RenderSpan::Kind::InlineCode:
        return QStringLiteral("InlineCode");
    case RenderSpan::Kind::Link:
        return QStringLiteral("Link");
    case RenderSpan::Kind::Image:
        return QStringLiteral("Image");
    case RenderSpan::Kind::BlockQuote:
        return QStringLiteral("BlockQuote");
    case RenderSpan::Kind::List:
        return QStringLiteral("List");
    case RenderSpan::Kind::CodeBlock:
        return QStringLiteral("CodeBlock");
    case RenderSpan::Kind::FormulaInline:
        return QStringLiteral("FormulaInline");
    case RenderSpan::Kind::FormulaBlock:
        return QStringLiteral("FormulaBlock");
    case RenderSpan::Kind::Table:
        return QStringLiteral("Table");
    case RenderSpan::Kind::HtmlBlock:
        return QStringLiteral("HtmlBlock");
    case RenderSpan::Kind::ThematicBreak:
        return QStringLiteral("ThematicBreak");
    case RenderSpan::Kind::MarkdownSyntax:
        return QStringLiteral("MarkdownSyntax");
    case RenderSpan::Kind::Unsupported:
        return QStringLiteral("Unsupported");
    }
    return QStringLiteral("Unknown");
}

void copyInsertedFirstBlockFormat(QTextDocument& patchedDocument,
                                  const QTextDocument& partialDocument,
                                  const PartialDocumentPatchStep& step)
{
    const QTextBlock partialBlock = partialDocument.findBlock(step.newRenderedStart);
    QTextBlock patchedBlock = patchedDocument.findBlock(step.oldRenderedStart);
    if (!partialBlock.isValid() || !patchedBlock.isValid()) {
        return;
    }

    QTextCursor cursor(&patchedDocument);
    cursor.setPosition(patchedBlock.position());
    cursor.setBlockFormat(partialBlock.blockFormat());
}

} // namespace RangePatchPolicy

} // namespace

PartialDocumentPatchDryRun PartialDocumentPatchApplier::dryRun(const PartialDocumentPatchPlan& plan,
                                                               int currentRenderedLength)
{
    PartialDocumentPatchDryRun dryRun;

    if (!plan.ok) {
        dryRun.errors.append(QStringLiteral("Patch plan is not valid."));
        dryRun.errors.append(plan.errors);
        return dryRun;
    }
    if (currentRenderedLength < 0) {
        dryRun.errors.append(QStringLiteral("Current rendered document length is invalid."));
        return dryRun;
    }
    if (plan.steps.isEmpty()) {
        dryRun.ok = true;
        return dryRun;
    }
    if (plan.steps.size() != 1) {
        dryRun.errors.append(QStringLiteral("Range patch requires exactly one replacement step."));
        return dryRun;
    }

    const PartialDocumentPatchStep& step = plan.steps.first();
    if (!RangePatchPolicy::supportsKind(step.kind)) {
        dryRun.errors.append(QStringLiteral("Range patch does not support %1.")
                                 .arg(RangePatchPolicy::kindName(step.kind)));
    }
    if (step.nodeId == 0) {
        dryRun.errors.append(QStringLiteral("Partial patch step is missing a node id."));
    }
    if (step.oldRenderedStart < 0 || step.oldRenderedEnd < step.oldRenderedStart) {
        dryRun.errors.append(QStringLiteral("Partial patch step has an invalid old rendered range."));
    }
    if (step.newRenderedStart < 0 || step.newRenderedEnd < step.newRenderedStart) {
        dryRun.errors.append(QStringLiteral("Partial patch step has an invalid new rendered range."));
    }
    if (step.oldRenderedEnd > currentRenderedLength) {
        dryRun.errors.append(QStringLiteral("Partial patch old rendered range exceeds the current document."));
    }
    if (!step.source.isValid()) {
        dryRun.errors.append(QStringLiteral("Partial patch step has an invalid source span."));
    }

    dryRun.ok = dryRun.errors.isEmpty();
    return dryRun;
}

PartialDocumentPatchApplyResult PartialDocumentPatchApplier::applyRangeReplacement(const QTextDocument& currentDocument,
                                                                                   const QTextDocument& partialDocument,
                                                                                   const PartialDocumentPatchPlan& plan)
{
    PartialDocumentPatchApplyResult result;

    PartialDocumentPatchDryRun dryRun = PartialDocumentPatchApplier::dryRun(plan, currentDocument.characterCount());
    if (!dryRun.ok) {
        result.errors = dryRun.errors;
        return result;
    }
    if (plan.steps.isEmpty()) {
        result.document = std::unique_ptr<QTextDocument>(currentDocument.clone());
        result.ok = true;
        return result;
    }

    const PartialDocumentPatchStep& step = plan.steps.first();
    if (step.newRenderedStart < 0 || step.newRenderedEnd < step.newRenderedStart
        || step.newRenderedEnd > partialDocument.characterCount()) {
        result.errors.append(QStringLiteral("Partial document does not contain the new rendered range."));
        return result;
    }

    std::unique_ptr<QTextDocument> patched(currentDocument.clone());

    QTextCursor partialCursor(const_cast<QTextDocument*>(&partialDocument));
    partialCursor.setPosition(step.newRenderedStart);
    partialCursor.setPosition(step.newRenderedEnd, QTextCursor::KeepAnchor);

    QTextCursor cursor(patched.get());
    cursor.setPosition(step.oldRenderedStart);
    cursor.setPosition(step.oldRenderedEnd, QTextCursor::KeepAnchor);
    const QTextDocumentFragment fragment(partialCursor);
    cursor.insertFragment(fragment);
    RangePatchPolicy::copyInsertedFirstBlockFormat(*patched, partialDocument, step);

    result.document = std::move(patched);
    result.ok = true;
    return result;
}

} // namespace Muffin
