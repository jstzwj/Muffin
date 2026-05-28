#include "Document.h"
#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "theme/ThemeStylesheet.h"

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

private:
    Document* m_document;
    QString m_before;
    QString m_after;
    int m_beforeCursorSourceOffset = -1;
    int m_afterCursorSourceOffset = -1;
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
    m_textDocument->setModified(false);

    emit documentRendered();
}

} // namespace Muffin
