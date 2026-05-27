#include "Document.h"
#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "theme/ThemeStylesheet.h"

namespace Muffin {

Document::Document(QObject* parent)
    : QObject(parent)
    , m_textDocument(std::make_unique<QTextDocument>())
{
}

bool Document::isModified() const {
    return m_textDocument && m_textDocument->isModified();
}

void Document::setMarkdown(const QString& markdown) {
    if (m_markdown == markdown) return;

    m_markdown = markdown;

    CmarkParser parser;
    m_astTree = parser.parse(m_markdown);
    render();

    emit markdownChanged();
}

void Document::setTheme(const Theme& theme) {
    m_theme = theme;
    render();
}

void Document::render() {
    ThemeStylesheet ss(m_theme);
    DocumentRenderer renderer(ss);
    m_textDocument = renderer.render(m_astTree);
    m_textDocument->setModified(false);

    emit documentRendered();
}

} // namespace Muffin
