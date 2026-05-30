#pragma once

#include "model/MarkdownDocument.h"
#include "parser/BlockReparsePlanner.h"

#include <QString>
#include <QStringList>

namespace Muffin {

struct BlockReparseResult {
    bool attempted = false;
    bool ok = false;
    BlockParseRange range;
    MarkdownDocument localDocument;
    QString localMarkdown;
    QString fullMarkdown;
    QStringList errors;
};

class BlockReparser {
public:
    static BlockReparseResult reparse(const MarkdownDocument& previousDocument,
                                      const BlockParseRange& range,
                                      const QString& fullMarkdown);

private:
    static const MarkdownNode* findLocalBlock(const MarkdownDocument& localDoc,
                                              const MarkdownNode* previousBlock);
    static bool isSupportedTopLevelBlock(MarkdownNodeType type);
};

} // namespace Muffin
