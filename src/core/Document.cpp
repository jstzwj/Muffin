#include "Document.h"
#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "theme/ThemeStylesheet.h"

#include <QDateTime>
#include <QUndoCommand>
#include <utility>

namespace Muffin {

namespace Internal {

class MarkdownEditCommand : public QUndoCommand {
public:
    MarkdownEditCommand(Document* document, QString before, QString after,
                        int beforeCursorSourceOffset, int afterCursorSourceOffset, QString label)
        : QUndoCommand(std::move(label))
        , m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_beforeCursorSourceOffset(beforeCursorSourceOffset)
        , m_afterCursorSourceOffset(afterCursorSourceOffset)
    {
    }

    void undo() override { m_document->setMarkdownInternal(m_before, m_beforeCursorSourceOffset); }
    void redo() override { m_document->setMarkdownInternal(m_after, m_afterCursorSourceOffset); }
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* command) override
    {
        const auto* other = dynamic_cast<const MarkdownEditCommand*>(command);
        if (!other || text() != other->text() || m_after != other->m_before) {
            return false;
        }
        if (m_lastEditTime.msecsTo(other->m_lastEditTime) > 1000 ||
            !isMergeableTextEdit(m_before, m_after, other->m_after)) {
            return false;
        }

        m_after = other->m_after;
        m_afterCursorSourceOffset = other->m_afterCursorSourceOffset;
        m_lastEditTime = other->m_lastEditTime;
        return true;
    }

private:
    static bool isMergeableTextEdit(const QString& before, const QString& middle, const QString& after)
    {
        const int firstDelta = qAbs(middle.size() - before.size());
        const int secondDelta = qAbs(after.size() - middle.size());
        return firstDelta <= 1 && secondDelta <= 1;
    }

    Document* m_document;
    QString m_before;
    QString m_after;
    int m_beforeCursorSourceOffset = -1;
    int m_afterCursorSourceOffset = -1;
    QDateTime m_lastEditTime = QDateTime::currentDateTimeUtc();
};

} // namespace Internal

Document::Document(QObject* parent)
    : QObject(parent)
    , m_textDocument(std::make_unique<QTextDocument>())
{
}

bool Document::isModified() const {
    return m_textDocument && m_textDocument->isModified();
}

void Document::setMarkdown(const QString& markdown) {
    m_undoStack.clear();
    setMarkdownInternal(markdown);
    m_undoStack.setClean();
}

void Document::applyMarkdownEdit(const QString& markdown, int cursorSourceOffset, const QString& label)
{
    if (m_markdown == markdown) {
        return;
    }
    m_undoStack.push(new Internal::MarkdownEditCommand(this, m_markdown, markdown, -1, cursorSourceOffset, label));
}

void Document::setMarkdownInternal(const QString& markdown, int cursorSourceOffset)
{
    if (m_markdown == markdown) return;

    m_markdown = markdown;

    CmarkParser parser;
    ParseResult parseResult = parser.parseDocument(m_markdown);
    m_astTree = std::move(parseResult.ast);
    m_mathSpans = std::move(parseResult.mathSpans);
    render();

    emit markdownChanged();
    if (cursorSourceOffset >= 0) {
        emit cursorSourceOffsetRequested(cursorSourceOffset);
    }
}

void Document::setTheme(const Theme& theme) {
    m_theme = theme;
    render();
}

void Document::render() {
    ThemeStylesheet ss(m_theme);
    DocumentRenderer renderer(ss);
    RenderResult result = renderer.render(m_astTree, m_markdown, m_mathSpans);
    m_textDocument = std::move(result.document);
    m_sourceMap = std::move(result.sourceMap);
    m_blocks = std::move(result.blocks);
    m_syntaxTokens = std::move(result.syntaxTokens);
    m_textDocument->setModified(false);

    emit documentRendered();
}

} // namespace Muffin
