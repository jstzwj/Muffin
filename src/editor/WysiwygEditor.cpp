#include "WysiwygEditor.h"
#include "core/MdDocument.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/Theme.h"

#include <cmark-gfm.h>

namespace Md {

WysiwygEditor::WysiwygEditor(QWidget *parent)
    : QTextEdit(parent) {
    setReadOnly(true);
    m_doc = std::make_unique<MdDocument>(this);
    m_renderer = std::make_unique<DocumentRenderer>();

    connect(m_doc.get(), &MdDocument::documentReset,
            this, &WysiwygEditor::onDocumentReset);
}

WysiwygEditor::~WysiwygEditor() = default;

bool WysiwygEditor::loadMarkdown(const QString &markdown) {
    return m_doc->loadFromMarkdown(markdown);
}

void WysiwygEditor::setTheme(const Theme &theme) {
    m_renderer->setTheme(theme);
    if (m_doc->cmarkRoot()) {
        onDocumentReset();
    }
}

void WysiwygEditor::onDocumentReset() {
    cmark_node *cmarkRoot = m_doc->cmarkRoot();
    if (cmarkRoot) {
        m_renderer->renderDocument(cmarkRoot, document());
    }
}

} // namespace Md
