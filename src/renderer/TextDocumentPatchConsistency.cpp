#include "TextDocumentPatchConsistency.h"

#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>

namespace Muffin {

namespace {

int blockCount(const QTextDocument& document)
{
    int count = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        ++count;
    }
    return count;
}

QTextCharFormat firstCharFormat(const QTextBlock& block)
{
    for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (fragment.isValid() && !fragment.text().isEmpty()) {
            return fragment.charFormat();
        }
    }
    return block.charFormat();
}

QStringList blockFormatDifferences(const QTextBlock& patchedBlock, const QTextBlock& fullBlock)
{
    QStringList differences;
    const QTextBlockFormat patched = patchedBlock.blockFormat();
    const QTextBlockFormat full = fullBlock.blockFormat();
    if (patched.alignment() != full.alignment()) {
        differences.append(QStringLiteral("alignment"));
    }
    if (!qFuzzyCompare(patched.topMargin() + 1.0, full.topMargin() + 1.0)) {
        differences.append(QStringLiteral("topMargin patched=%1 full=%2").arg(patched.topMargin()).arg(full.topMargin()));
    }
    if (!qFuzzyCompare(patched.bottomMargin() + 1.0, full.bottomMargin() + 1.0)) {
        differences.append(QStringLiteral("bottomMargin patched=%1 full=%2").arg(patched.bottomMargin()).arg(full.bottomMargin()));
    }
    if (!qFuzzyCompare(patched.leftMargin() + 1.0, full.leftMargin() + 1.0)) {
        differences.append(QStringLiteral("leftMargin patched=%1 full=%2").arg(patched.leftMargin()).arg(full.leftMargin()));
    }
    if (!qFuzzyCompare(patched.rightMargin() + 1.0, full.rightMargin() + 1.0)) {
        differences.append(QStringLiteral("rightMargin patched=%1 full=%2").arg(patched.rightMargin()).arg(full.rightMargin()));
    }
    return differences;
}

bool sameCharFormat(const QTextBlock& patchedBlock, const QTextBlock& fullBlock)
{
    const QTextCharFormat patched = firstCharFormat(patchedBlock);
    const QTextCharFormat full = firstCharFormat(fullBlock);
    return patched.font().family() == full.font().family()
        && qFuzzyCompare(patched.font().pointSizeF() + 1.0, full.font().pointSizeF() + 1.0)
        && patched.fontWeight() == full.fontWeight()
        && patched.fontItalic() == full.fontItalic();
}

} // namespace

TextDocumentPatchConsistencyResult TextDocumentPatchConsistency::compare(const QTextDocument& patched,
                                                                         const QTextDocument& full)
{
    TextDocumentPatchConsistencyResult result;

    const int patchedBlockCount = blockCount(patched);
    const int fullBlockCount = blockCount(full);
    if (patchedBlockCount != fullBlockCount) {
        result.errors.append(QStringLiteral("Block count mismatch: patched=%1 full=%2.")
                                 .arg(patchedBlockCount)
                                 .arg(fullBlockCount));
        return result;
    }

    QTextBlock patchedBlock = patched.begin();
    QTextBlock fullBlock = full.begin();
    int index = 0;
    while (patchedBlock.isValid() && fullBlock.isValid()) {
        if (patchedBlock.text() != fullBlock.text()) {
            result.errors.append(QStringLiteral("Block %1 text mismatch.").arg(index));
        }
        if ((patchedBlock.textList() != nullptr) != (fullBlock.textList() != nullptr)) {
            result.errors.append(QStringLiteral("Block %1 list state mismatch.").arg(index));
        }
        const QStringList blockFormatDiffs = blockFormatDifferences(patchedBlock, fullBlock);
        if (!blockFormatDiffs.isEmpty()) {
            result.errors.append(QStringLiteral("Block %1 format mismatch: %2.")
                                     .arg(index)
                                     .arg(blockFormatDiffs.join(QStringLiteral(", "))));
        }
        if (!sameCharFormat(patchedBlock, fullBlock)) {
            result.errors.append(QStringLiteral("Block %1 char format mismatch.").arg(index));
        }

        patchedBlock = patchedBlock.next();
        fullBlock = fullBlock.next();
        ++index;
    }

    result.ok = result.errors.isEmpty();
    return result;
}

} // namespace Muffin
